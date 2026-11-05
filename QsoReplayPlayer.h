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

#ifndef QSOREPLAYPLAYER_H
#define QSOREPLAYPLAYER_H

// Plays one QSO Replay clip at a time. Main thread only. QML addresses clips by id; the file path
// is resolved here through QsoReplayEngine::pathForId() and never leaves C++.
//
// Live audio always wins: QML calls pauseForLive() when a station keys up on the connected
// reflector or when the user presses PTT, and playback stays paused until the user taps play.

#include <QObject>
#include <QString>
#include <QTimer>

class QsoReplayEngine;
#ifndef Q_OS_IOS
class QMediaPlayer;
class QAudioOutput;
#endif

class QsoReplayPlayer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ isActive NOTIFY stateChanged)
    Q_PROPERTY(bool playing READ isPlaying NOTIFY stateChanged)
    Q_PROPERTY(int positionMs READ positionMs NOTIFY positionChanged)
    Q_PROPERTY(int durationMs READ durationMs NOTIFY stateChanged)
    Q_PROPERTY(QString currentId READ currentId NOTIFY stateChanged)
    Q_PROPERTY(QString currentLabel READ currentLabel NOTIFY stateChanged)
    Q_PROPERTY(QString pauseReason READ pauseReason NOTIFY stateChanged)
    Q_PROPERTY(qreal volume READ volume WRITE setVolume NOTIFY volumeChanged)

public:
    explicit QsoReplayPlayer(QsoReplayEngine *engine, QObject *parent = nullptr);
    ~QsoReplayPlayer() override;

    bool isActive() const { return !m_currentId.isEmpty(); }
    bool isPlaying() const { return m_playing; }
    int positionMs() const { return m_positionMs; }
    int durationMs() const { return m_durationMs; }
    QString currentId() const { return m_currentId; }
    QString currentLabel() const { return m_currentLabel; }
    QString pauseReason() const { return m_pauseReason; }
    qreal volume() const { return m_volume; }
    void setVolume(qreal v);

    // Loads and starts the clip. Tapping the clip that is already loaded toggles play/pause.
    Q_INVOKABLE bool play(const QString &id, const QString &label = QString());
    Q_INVOKABLE void toggle();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void resume();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void seek(int ms);
    // reason: "live" (a station keyed up) or "tx" (PTT); shown on the player bar.
    Q_INVOKABLE void pauseForLive(const QString &reason);

    // Native backend entry points (already on the main thread).
    void onFinished(bool completedNormally);
    void onInterrupted();

signals:
    void stateChanged();
    void positionChanged();
    void volumeChanged();
    void finished();
    void error(QString reason);

private:
    void setPlaying(bool on);
    void refreshPosition();
    bool backendLoad(const QString &path, int *durationMs);
    bool backendPlay();
    void backendPause();
    void backendStop();
    void backendSeek(int ms);
    int backendPositionMs() const;
    void backendSetVolume(qreal v);

    QsoReplayEngine *m_engine { nullptr };
    QTimer m_tick;
    QString m_currentId;
    QString m_currentLabel;
    QString m_pauseReason;
    bool m_playing { false };
    int m_positionMs { 0 };
    int m_durationMs { 0 };
    qreal m_volume { 1.0 };
#ifndef Q_OS_IOS
    QMediaPlayer *m_media { nullptr };
    QAudioOutput *m_audioOut { nullptr };
#endif
};

#endif // QSOREPLAYPLAYER_H
