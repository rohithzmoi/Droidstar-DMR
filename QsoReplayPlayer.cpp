/*
    Copyright (C) 2026 Rohith Namboothiri

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "QsoReplayPlayer.h"
#include "QsoReplayEngine.h"

#include <QFile>
#include <QtDebug>
#include <algorithm>

#ifdef Q_OS_IOS
#include "ios_replay_player.h"
#else
#include <QAudioOutput>
#include <QMediaPlayer>
#include <QUrl>
#endif

namespace {
QsoReplayPlayer *s_player = nullptr;

#ifdef Q_OS_IOS
// Native callbacks may arrive on any thread: hop to the main thread before touching state.
void replayFinishedThunk(bool completedNormally)
{
    if (QsoReplayPlayer *p = s_player) {
        QMetaObject::invokeMethod(p, [p, completedNormally]() {
            if (s_player == p) {
                p->onFinished(completedNormally);
            }
        }, Qt::QueuedConnection);
    }
}

void replayInterruptedThunk()
{
    if (QsoReplayPlayer *p = s_player) {
        QMetaObject::invokeMethod(p, [p]() {
            if (s_player == p) {
                p->onInterrupted();
            }
        }, Qt::QueuedConnection);
    }
}
#endif
} // namespace

QsoReplayPlayer::QsoReplayPlayer(QsoReplayEngine *engine, QObject *parent)
    : QObject(parent)
    , m_engine(engine)
{
    s_player = this;
    m_tick.setInterval(100);
    connect(&m_tick, &QTimer::timeout, this, &QsoReplayPlayer::refreshPosition);

#ifdef Q_OS_IOS
    ios_replay_player_init(&replayFinishedThunk, &replayInterruptedThunk);
#else
    m_media = new QMediaPlayer(this);
    m_audioOut = new QAudioOutput(this);
    m_media->setAudioOutput(m_audioOut);
    connect(m_media, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus st) {
        if (st == QMediaPlayer::EndOfMedia) {
            onFinished(true);
        }
    });
    connect(m_media, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error, const QString &msg) {
        qWarning() << "[QsoReplay] player error" << msg;
        onFinished(false);
    });
#endif

    if (m_engine) {
        // A clip removed by pruning/clear while it is loaded must not keep playing from a deleted file.
        connect(m_engine, &QsoReplayEngine::recordingsRemoved, this, [this](const QStringList &ids) {
            if (!m_currentId.isEmpty() && ids.contains(m_currentId)) {
                stop();
            }
        });
    }
}

QsoReplayPlayer::~QsoReplayPlayer()
{
    backendStop();
    if (s_player == this) {
        s_player = nullptr;
    }
}

void QsoReplayPlayer::setVolume(qreal v)
{
    const qreal clamped = std::clamp(v, 0.0, 1.0);
    if (qFuzzyCompare(clamped, m_volume)) {
        return;
    }
    m_volume = clamped;
    backendSetVolume(m_volume);
    emit volumeChanged();
}

bool QsoReplayPlayer::play(const QString &id, const QString &label)
{
    if (id.isEmpty() || !m_engine) {
        return false;
    }
    if (id == m_currentId) {
        toggle();
        return true;
    }
    const QString path = m_engine->pathForId(id);
    if (path.isEmpty() || !QFile::exists(path)) {
        // Stale reference (file pruned behind the log entry): drop it so the UI stops offering it.
        if (!path.isEmpty()) {
            m_engine->deleteRecording(id);
        }
        emit error(tr("This replay is no longer available."));
        return false;
    }
    backendStop();
    int durationMs = 0;
    if (!backendLoad(path, &durationMs)) {
        m_currentId.clear();
        m_currentLabel.clear();
        m_pauseReason.clear();
        setPlaying(false);
        emit stateChanged();
        emit error(tr("Could not open this replay."));
        return false;
    }
    m_currentId = id;
    m_currentLabel = label;
    m_pauseReason.clear();
    m_durationMs = durationMs;
    m_positionMs = 0;
    backendSetVolume(m_volume);
    const bool ok = backendPlay();
    setPlaying(ok);
    emit stateChanged();
    emit positionChanged();
    if (!ok) {
        emit error(tr("Could not play this replay."));
    }
    return ok;
}

void QsoReplayPlayer::toggle()
{
    if (m_currentId.isEmpty()) {
        return;
    }
    if (m_playing) {
        pause();
    } else {
        resume();
    }
}

void QsoReplayPlayer::pause()
{
    if (m_currentId.isEmpty() || !m_playing) {
        return;
    }
    backendPause();
    refreshPosition();
    setPlaying(false);
    emit stateChanged();
}

void QsoReplayPlayer::resume()
{
    if (m_currentId.isEmpty() || m_playing) {
        return;
    }
    if (m_durationMs > 0 && m_positionMs >= m_durationMs) {
        backendSeek(0);
        m_positionMs = 0;
    }
    m_pauseReason.clear();
    const bool ok = backendPlay();
    setPlaying(ok);
    emit stateChanged();
    if (!ok) {
        emit error(tr("Could not play this replay."));
    }
}

void QsoReplayPlayer::stop()
{
    const bool wasActive = isActive();
    backendStop();
    m_currentId.clear();
    m_currentLabel.clear();
    m_pauseReason.clear();
    m_positionMs = 0;
    m_durationMs = 0;
    setPlaying(false);
    if (wasActive) {
        emit stateChanged();
        emit positionChanged();
    }
}

void QsoReplayPlayer::seek(int ms)
{
    if (m_currentId.isEmpty()) {
        return;
    }
    const int clamped = std::clamp(ms, 0, std::max(0, m_durationMs));
    backendSeek(clamped);
    m_positionMs = clamped;
    emit positionChanged();
}

void QsoReplayPlayer::pauseForLive(const QString &reason)
{
    if (m_currentId.isEmpty() || !m_playing) {
        return;
    }
    backendPause();
    refreshPosition();
    m_pauseReason = reason;
    setPlaying(false);
    emit stateChanged();
}

void QsoReplayPlayer::onFinished(bool completedNormally)
{
    if (m_currentId.isEmpty()) {
        return;
    }
    if (completedNormally) {
        m_positionMs = m_durationMs;
        setPlaying(false);
        emit positionChanged();
        emit stateChanged();
        emit finished();
    } else {
        stop();
        emit error(tr("Replay playback failed."));
    }
}

void QsoReplayPlayer::onInterrupted()
{
    if (m_currentId.isEmpty()) {
        return;
    }
    refreshPosition();
    m_pauseReason = QStringLiteral("interrupted");
    setPlaying(false);
    emit stateChanged();
}

void QsoReplayPlayer::setPlaying(bool on)
{
    m_playing = on;
    if (on) {
        m_tick.start();
    } else {
        m_tick.stop();
    }
}

void QsoReplayPlayer::refreshPosition()
{
    if (m_currentId.isEmpty()) {
        return;
    }
    const int pos = backendPositionMs();
    if (pos != m_positionMs) {
        m_positionMs = pos;
        emit positionChanged();
    }
}

// ── backends ─────────────────────────────────────────────────────────────────────────────────────

#ifdef Q_OS_IOS

bool QsoReplayPlayer::backendLoad(const QString &path, int *durationMs)
{
    const double seconds = ios_replay_player_load(path.toUtf8().constData());
    if (seconds < 0.0) {
        return false;
    }
    *durationMs = static_cast<int>(seconds * 1000.0 + 0.5);
    return true;
}
bool QsoReplayPlayer::backendPlay() { return ios_replay_player_play(); }
void QsoReplayPlayer::backendPause() { ios_replay_player_pause(); }
void QsoReplayPlayer::backendStop() { ios_replay_player_stop(); }
void QsoReplayPlayer::backendSeek(int ms) { ios_replay_player_seek(ms / 1000.0); }
int QsoReplayPlayer::backendPositionMs() const { return static_cast<int>(ios_replay_player_position() * 1000.0 + 0.5); }
void QsoReplayPlayer::backendSetVolume(qreal v) { ios_replay_player_set_volume(static_cast<float>(v)); }

#else

bool QsoReplayPlayer::backendLoad(const QString &path, int *durationMs)
{
    m_media->setSource(QUrl::fromLocalFile(path));
    *durationMs = static_cast<int>(m_media->duration());
    return true;
}
bool QsoReplayPlayer::backendPlay() { m_media->play(); return true; }
void QsoReplayPlayer::backendPause() { m_media->pause(); }
void QsoReplayPlayer::backendStop() { m_media->stop(); m_media->setSource(QUrl()); }
void QsoReplayPlayer::backendSeek(int ms) { m_media->setPosition(ms); }
int QsoReplayPlayer::backendPositionMs() const
{
    if (m_durationMs == 0 && m_media->duration() > 0) {
        const_cast<QsoReplayPlayer *>(this)->m_durationMs = static_cast<int>(m_media->duration());
    }
    return static_cast<int>(m_media->position());
}
void QsoReplayPlayer::backendSetVolume(qreal v) { m_audioOut->setVolume(static_cast<float>(v)); }

#endif
