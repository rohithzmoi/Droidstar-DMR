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

#include "QsoReplayEngine.h"
#include "QsoReplayCapture.h"
#include "LogHandler.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QMutexLocker>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QThread>
#include <QtDebug>
#include <algorithm>

namespace {

QsoReplayEngine *s_instance = nullptr;

constexpr int kSampleRate = 8000;
constexpr qint64 kMinFreeBytes = 200LL * 1024 * 1024;
constexpr int kBudgetMinMb = 50;
constexpr int kBudgetMaxMb = 2000;
constexpr int kMaxOverMinSec = 30;
constexpr int kMaxOverMaxSec = 1800;
constexpr qint64 kOpenBackoffMs = 60000;
constexpr qint64 kPendingIdentMaxAgeMs = 5000;
constexpr uint32_t kChunkSamples = 4096;

const QRegularExpression &clipNamePattern()
{
    static const QRegularExpression re(
        QStringLiteral("^(\\d{8}-\\d{6}-\\d{3}_(?:rx|tx)_[0-9a-f]{4})\\.(m4a|wav)$"));
    return re;
}

QString wallTimeString(qint64 wallMs)
{
    return QDateTime::fromMSecsSinceEpoch(wallMs).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

} // namespace

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// main thread
// ─────────────────────────────────────────────────────────────────────────────────────────────────

QsoReplayEngine::QsoReplayEngine(QObject *parent, bool loadPersisted)
    : QObject(parent)
    , m_persist(loadPersisted)
{
    s_instance = this;
    m_dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/replay");
#ifdef Q_OS_IOS
    m_writerFactory = []() { return createAacReplayFileWriter(); };
#else
    m_writerFactory = []() { return createWavReplayFileWriter(); };
#endif
    m_freeSpaceProbe = [this]() { return QStorageInfo(m_dir).bytesAvailable(); };
    if (auto probe = m_writerFactory()) {
        m_bytesPerSecond = probe->nominalBytesPerSecond();
        m_extension = QString::fromLatin1(probe->extension());
    }
    if (m_persist) {
        loadSettings();
    }
    QsoReplayCapture::setWake(&QsoReplayEngine::wakeThunk, this);
    applyCaptureFlags();
}

QsoReplayEngine::~QsoReplayEngine()
{
    shutdownSync(1500);
    QsoReplayCapture::setWake(nullptr, nullptr);
    if (s_instance == this) {
        s_instance = nullptr;
    }
}

QsoReplayEngine *QsoReplayEngine::instance()
{
    return s_instance;
}

void QsoReplayEngine::start()
{
    if (m_started) {
        return;
    }
    m_started = true;
    m_thread = QThread::create([this]() { workerLoop(); });
    m_thread->setObjectName(QStringLiteral("QsoReplayWorker"));
    m_thread->start(QThread::LowestPriority);
}

void QsoReplayEngine::attachLogHandler(LogHandler *handler)
{
    m_logHandler = handler;
    if (!handler) {
        return;
    }
    connect(handler, &LogHandler::logSaved, this, [this](const QString &fileName) {
        if (fileName == QLatin1String("logs.json")) {
            reconcileFromLog();
        }
    });
    connect(handler, &LogHandler::logCleared, this, [this](const QString &fileName) {
        if (fileName == QLatin1String("logs.json")) {
            clearAll();
        }
    });
}

void QsoReplayEngine::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
    settings.sync();
    auto boolOf = [&settings](const char *key, bool def) {
        const QString v = settings.value(QLatin1String(key), def ? QStringLiteral("true") : QStringLiteral("false"))
                              .toString().trimmed().toLower();
        return v == QLatin1String("true") || v == QLatin1String("1");
    };
    // On by default for new installs and updates. A stored value is honoured only when the user
    // actually flipped the toggle (REPLAY_ENABLED_CHOICE); a REPLAY_ENABLED written as a side effect
    // of an earlier build saving other replay settings does not count as a choice.
    const bool userChose = boolOf("REPLAY_ENABLED_CHOICE", false);
    m_enabled = userChose ? boolOf("REPLAY_ENABLED", true) : true;
    m_recordTx = boolOf("REPLAY_RECORD_TX", false);
    m_storageBudgetMb.store(std::clamp(settings.value(QStringLiteral("REPLAY_STORAGE_MB"), 100).toInt(), kBudgetMinMb, kBudgetMaxMb));
    m_maxOverSeconds.store(std::clamp(settings.value(QStringLiteral("REPLAY_MAX_OVER_SEC"), 600).toInt(), kMaxOverMinSec, kMaxOverMaxSec));
    qInfo().noquote() << "[QsoReplay] settings loaded enabled:" << m_enabled << "recordTx:" << m_recordTx
                      << "budgetMb:" << m_storageBudgetMb.load() << "maxOverSec:" << m_maxOverSeconds.load();
}

void QsoReplayEngine::saveSettings() const
{
    if (!m_persist) {
        return;
    }
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
    settings.setValue(QStringLiteral("REPLAY_RECORD_TX"), m_recordTx ? QStringLiteral("true") : QStringLiteral("false"));
    settings.setValue(QStringLiteral("REPLAY_STORAGE_MB"), m_storageBudgetMb.load());
    settings.setValue(QStringLiteral("REPLAY_MAX_OVER_SEC"), m_maxOverSeconds.load());
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qWarning().noquote() << "[QsoReplay] failed to save settings to" << settings.fileName();
    }
}

void QsoReplayEngine::applyCaptureFlags()
{
    QsoReplayCapture::setRecordTx(m_recordTx);
    QsoReplayCapture::setSelfLoggingAllowed(m_selfLoggingAllowed);
    QsoReplayCapture::setEnabled(m_enabled && !m_shutDown);
}

void QsoReplayEngine::setEnabled(bool on)
{
    if (m_enabled == on) {
        return;
    }
    m_enabled = on;
    applyCaptureFlags();
    // Only the QSO Replay toggles call this, so it is always an explicit user choice: persist it
    // together with the marker so later launches and updates keep it.
    if (m_persist) {
        QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
        settings.setValue(QStringLiteral("REPLAY_ENABLED"), on ? QStringLiteral("true") : QStringLiteral("false"));
        settings.setValue(QStringLiteral("REPLAY_ENABLED_CHOICE"), QStringLiteral("true"));
        settings.sync();
        if (settings.status() != QSettings::NoError) {
            qWarning().noquote() << "[QsoReplay] failed to save the QSO Replay choice to" << settings.fileName();
        }
    }
    emit replayEnabledChanged();
    wakeWorker();
}

void QsoReplayEngine::setRecordTx(bool on)
{
    if (m_recordTx == on) {
        return;
    }
    m_recordTx = on;
    applyCaptureFlags();
    saveSettings();
    emit recordTxChanged();
}

void QsoReplayEngine::setSelfLoggingAllowed(bool on)
{
    if (m_selfLoggingAllowed == on) {
        return;
    }
    m_selfLoggingAllowed = on;
    applyCaptureFlags();
    emit selfLoggingAllowedChanged();
}

void QsoReplayEngine::setStorageBudgetMb(int mb)
{
    const int clamped = std::clamp(mb, kBudgetMinMb, kBudgetMaxMb);
    if (m_storageBudgetMb.exchange(clamped) == clamped) {
        return;
    }
    saveSettings();
    emit storageBudgetMbChanged();
    postCommand(Command::BudgetCheck);
}

void QsoReplayEngine::setMaxOverSeconds(int seconds)
{
    const int clamped = std::clamp(seconds, kMaxOverMinSec, kMaxOverMaxSec);
    if (m_maxOverSeconds.exchange(clamped) == clamped) {
        return;
    }
    saveSettings();
    emit maxOverSecondsChanged();
}

void QsoReplayEngine::deleteRecording(const QString &id)
{
    if (!id.isEmpty()) {
        postCommand(Command::Delete, QStringList { id });
    }
}

void QsoReplayEngine::deleteRecordings(const QStringList &ids)
{
    if (!ids.isEmpty()) {
        postCommand(Command::Delete, ids);
    }
}

void QsoReplayEngine::clearAll()
{
    postCommand(Command::ClearAll);
}

void QsoReplayEngine::reconcile(const QStringList &referencedIds)
{
    postCommand(Command::Reconcile, referencedIds);
}

double QsoReplayEngine::hoursForMegabytes(int mb) const
{
    if (m_bytesPerSecond <= 0 || mb <= 0) {
        return 0.0;
    }
    return (static_cast<double>(mb) * 1048576.0) / static_cast<double>(m_bytesPerSecond) / 3600.0;
}

QString QsoReplayEngine::pathForId(const QString &id) const
{
    const auto it = m_files.constFind(id);
    if (it == m_files.constEnd()) {
        return QString();
    }
    return m_dir + QLatin1Char('/') + it->fileName;
}

QString QsoReplayEngine::logFilePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/logs.json");
}

void QsoReplayEngine::reconcileFromLog()
{
    if (!m_logHandler || !m_scanComplete) {
        return;
    }
    const QJsonArray entries = m_logHandler->loadLog(QStringLiteral("logs.json"));
    if (entries.isEmpty()) {
        // An existing, non-empty logs.json that parsed to nothing is a read error, never a reason
        // to delete every clip. A missing or blank file is a genuinely empty log.
        const QFileInfo fi(logFilePath());
        if (fi.exists() && fi.size() > 2) {
            return;
        }
    }
    QStringList ids;
    for (const QJsonValue &v : entries) {
        const QJsonArray clips = v.toObject().value(QStringLiteral("replayClips")).toArray();
        for (const QJsonValue &c : clips) {
            const QString id = c.toObject().value(QStringLiteral("id")).toString();
            if (!id.isEmpty()) {
                ids.append(id);
            }
        }
    }
    reconcile(ids);
}

void QsoReplayEngine::onStreamStateChanged(const QString &mode, quint32 streamId, quint32 srcId,
                                           const QString &callsign, const QString &tg, StreamEvent event)
{
    if (!m_enabled || event == StreamEvent::Other) {
        return;
    }
    Marker m;
    m.type = (event == StreamEvent::New) ? Marker::Split : Marker::StreamEnd;
    m.ident.mode = mode;
    m.ident.streamId = streamId;
    m.ident.srcId = srcId;
    m.ident.callsign = callsign.trimmed();
    m.ident.tg = tg.trimmed();
    m.ident.bound = true;
    postMarker(m);
}

void QsoReplayEngine::onTxStarted(const QString &mode, const QString &myCallsign, const QString &tg)
{
    if (!m_enabled || !m_recordTx) {
        return;
    }
    Marker m;
    m.type = Marker::TxIdentity;
    m.ident.mode = mode;
    m.ident.callsign = myCallsign.trimmed();
    m.ident.tg = tg.trimmed();
    m.ident.bound = true;
    postMarker(m);
}

void QsoReplayEngine::onConnectionContext(const QString &mode, const QString &refName)
{
    if (mode == m_ctxMode && refName == m_ctxRef) {
        return;
    }
    m_ctxMode = mode;
    m_ctxRef = refName;
    Marker m;
    m.type = Marker::Context;
    m.ident.mode = mode;
    m.ident.callsign = refName.trimmed();
    postMarker(m);
}

void QsoReplayEngine::onEnteredBackground()
{
    wakeWorker();
}

void QsoReplayEngine::shutdownSync(int timeoutMs)
{
    if (m_shutDown) {
        return;
    }
    m_shutDown = true;
    QsoReplayCapture::setEnabled(false);
    m_stop.store(true, std::memory_order_release);
    wakeWorker();
    if (m_thread) {
        if (!m_thread->wait(std::max(50, timeoutMs))) {
            qWarning() << "[QsoReplay] worker did not stop within" << timeoutMs << "ms";
        }
        if (m_thread->isFinished()) {
            delete m_thread;
        }
        m_thread = nullptr;
    }
}

void QsoReplayEngine::setStorageDirectory(const QString &dir)
{
    if (!m_started) {
        m_dir = dir;
    }
}

void QsoReplayEngine::setWriterFactory(std::function<std::unique_ptr<IReplayFileWriter>()> factory)
{
    if (m_started || !factory) {
        return;
    }
    m_writerFactory = std::move(factory);
    if (auto probe = m_writerFactory()) {
        m_bytesPerSecond = probe->nominalBytesPerSecond();
        m_extension = QString::fromLatin1(probe->extension());
    }
}

void QsoReplayEngine::setFreeSpaceProbe(std::function<qint64()> probe)
{
    if (!m_started && probe) {
        m_freeSpaceProbe = std::move(probe);
    }
}

void QsoReplayEngine::setTimings(int minClipMs, int idleCloseMs, int reconcileGraceMs)
{
    if (!m_started) {
        m_minClipMs = std::max(0, minClipMs);
        m_idleCloseMs = std::max(50, idleCloseMs);
        m_reconcileGraceMs = std::max(0, reconcileGraceMs);
    }
}

void QsoReplayEngine::postCommand(Command::Type type, const QStringList &ids)
{
    {
        QMutexLocker lock(&m_queueMutex);
        m_commands.push_back(Command { type, ids });
    }
    wakeWorker();
}

void QsoReplayEngine::postMarker(const Marker &marker)
{
    Marker m = marker;
    m.monoMs = QsoReplayCapture::monoNowMs();
    {
        // The ring position is read under the same lock the worker pulls markers with, and the
        // worker snapshots the write position BEFORE pulling. So a marker the worker has not seen
        // yet always lies at or beyond everything the worker is about to consume: audio can never
        // be attributed to the wrong clip because the identity arrived a few milliseconds late.
        QMutexLocker lock(&m_queueMutex);
        m.ringPos = QsoReplayCapture::ringWritePos();
        m_markers.push_back(m);
    }
    wakeWorker();
}

void QsoReplayEngine::wakeWorker()
{
    m_wakeCond.wakeOne();
}

void QsoReplayEngine::wakeThunk(void *ctx)
{
    if (ctx) {
        static_cast<QsoReplayEngine *>(ctx)->m_wakeCond.wakeOne();
    }
}

// ─────────────────────────────────────────────────────────────────────────────────────────────────
// worker thread
// ─────────────────────────────────────────────────────────────────────────────────────────────────

void QsoReplayEngine::workerLoop()
{
    startupScan();
    m_chunk.resize(kChunkSamples);

    while (!m_stop.load(std::memory_order_acquire)) {
        {
            QMutexLocker lock(&m_wakeMutex);
            m_wakeCond.wait(&m_wakeMutex, m_clip.active ? 100UL : 250UL);
        }
        processCommands();
        processAudio();
        checkIdleClose();
    }

    // Shutdown: write out what is already in the ring and finish the open clip.
    processAudio();
    if (m_clip.active) {
        closeClip();
    }
    m_writer.reset();
}

void QsoReplayEngine::startupScan()
{
    QDir dir(m_dir);
    if (!dir.exists() && !QDir().mkpath(m_dir)) {
        m_storageOk = false;
        postError(QStringLiteral("Cannot create the replay folder"));
        return;
    }
    replayPlatformPrepareDirectory(m_dir);
    m_storageOk = QFileInfo(m_dir).isWritable();
    if (!m_storageOk) {
        postError(QStringLiteral("Replay folder is not writable"));
    }

    // A ".part" file is a clip that was being written when the app died: unplayable, remove it.
    const QStringList parts = dir.entryList(QStringList { QStringLiteral("*.part") }, QDir::Files);
    for (const QString &p : parts) {
        QFile::remove(dir.filePath(p));
    }

    m_workerFiles.clear();
    m_workerUsedBytes = 0;
    const QFileInfoList entries = dir.entryInfoList(QDir::Files, QDir::Name);
    for (const QFileInfo &fi : entries) {
        const QRegularExpressionMatch m = clipNamePattern().match(fi.fileName());
        if (!m.hasMatch()) {
            continue;   // never touch files we did not create
        }
        FileInfo info;
        info.fileName = fi.fileName();
        info.bytes = fi.size();
        m_workerFiles.insert(m.captured(1), info);
        m_workerUsedBytes += info.bytes;
    }

    const QHash<QString, FileInfo> snapshot = m_workerFiles;
    const qint64 used = m_workerUsedBytes;
    QMetaObject::invokeMethod(this, [this, snapshot, used]() {
        m_files = snapshot;
        m_usedBytes = used;
        m_scanComplete = true;
        emit storageChanged();
        reconcileFromLog();
    }, Qt::QueuedConnection);

    enforceBudget();
}

void QsoReplayEngine::processCommands()
{
    std::deque<Command> cmds;
    {
        QMutexLocker lock(&m_queueMutex);
        cmds.swap(m_commands);
    }
    for (const Command &c : cmds) {
        switch (c.type) {
        case Command::ClearAll:
            deleteFiles(m_workerFiles.keys());
            break;
        case Command::Delete:
            deleteFiles(c.ids);
            break;
        case Command::Reconcile:
            reconcileFiles(QSet<QString>(c.ids.cbegin(), c.ids.cend()));
            break;
        case Command::BudgetCheck:
            enforceBudget();
            break;
        case Command::Wake:
            break;
        }
    }
}

void QsoReplayEngine::pullBoundaries()
{
    QsoReplayCapture::Event ev;
    while (QsoReplayCapture::popEvent(ev)) {
        Boundary b;
        b.isEvent = true;
        b.evType = static_cast<int>(ev.type);
        b.kind = (ev.kind == QsoReplayCapture::Kind::Tx) ? ClipTx : ClipRx;
        b.ringPos = ev.ringPos;
        b.monoMs = ev.monoMs;
        m_boundaries.push_back(b);
    }
    std::deque<Marker> markers;
    {
        QMutexLocker lock(&m_queueMutex);
        markers.swap(m_markers);
    }
    for (const Marker &m : markers) {
        Boundary b;
        b.isEvent = false;
        b.ringPos = m.ringPos;
        b.monoMs = m.monoMs;
        b.marker = m;
        m_boundaries.push_back(b);
    }
}

void QsoReplayEngine::processAudio()
{
    // Snapshot first, then pull: every boundary posted after this snapshot has a position >= w
    // (see postMarker / QsoReplayCapture::postEvent), so consuming up to w can never run past a
    // boundary this pass has not seen.
    const uint32_t w = QsoReplayCapture::ringWritePos();
    pullBoundaries();
    for (;;) {
        const uint32_t r = QsoReplayCapture::ringReadPos();
        const uint32_t avail = w - r;

        // Earliest boundary by sample position among those inside [r, w]; later ones wait.
        int best = -1;
        uint32_t bestDist = 0;
        for (size_t i = 0; i < m_boundaries.size(); ++i) {
            const uint32_t d = m_boundaries[i].ringPos - r;
            if (d > avail) {
                continue;
            }
            if (best < 0 || d < bestDist) {
                best = static_cast<int>(i);
                bestDist = d;
            }
        }

        const uint32_t n = (best >= 0) ? bestDist : avail;
        if (n > 0) {
            consumeRing(n);
        }
        if (best < 0) {
            break;
        }
        const Boundary b = m_boundaries[static_cast<size_t>(best)];
        m_boundaries.erase(m_boundaries.begin() + best);
        applyBoundary(b);
    }
}

void QsoReplayEngine::consumeRing(uint32_t count)
{
    const qint64 now = QsoReplayCapture::monoNowMs();
    while (count > 0) {
        const uint32_t take = std::min<uint32_t>(count, kChunkSamples);
        const uint32_t got = QsoReplayCapture::peekSamples(m_chunk.data(), take);
        if (got == 0) {
            break;
        }
        if (m_clip.active && !m_clip.discarding) {
            writeClipSamples(m_chunk.data(), got, now);
        }
        QsoReplayCapture::consumeSamples(got);
        count -= got;
    }
}

void QsoReplayEngine::applyBoundary(const Boundary &b)
{
    using QsoReplayCapture::Ev;
    if (b.isEvent) {
        if (b.evType == static_cast<int>(Ev::Begin)) {
            if (m_clip.active) {
                closeClip();
            }
            Identity ident;
            if (b.kind == ClipRx) {
                if (m_pendingRxIdent.bound && (b.monoMs - m_pendingRxIdentMonoMs) <= kPendingIdentMaxAgeMs) {
                    ident = m_pendingRxIdent;
                }
                m_pendingRxIdent = Identity();
            } else {
                ident = m_txIdent;
            }
            startClip(b.kind, b.monoMs, ident);
        } else if (b.evType == static_cast<int>(Ev::End)) {
            if (m_clip.active && m_clip.kind == b.kind) {
                closeClip();
            }
        }
        return;
    }

    const Marker &m = b.marker;
    switch (m.type) {
    case Marker::Split:
        if (m_clip.active && m_clip.kind == ClipRx) {
            if (m_clip.ident.bound && !m_clip.ident.sameStream(m.ident) && m_clip.samples > 0) {
                // A second station keyed up before the sink drained: cut the clip here.
                closeClip();
                startClip(ClipRx, m.monoMs, m.ident);
            } else {
                m_clip.ident = m.ident;
                m_clip.ident.bound = true;
            }
        } else {
            m_pendingRxIdent = m.ident;
            m_pendingRxIdent.bound = true;
            m_pendingRxIdentMonoMs = m.monoMs;
        }
        break;
    case Marker::StreamEnd:
        // The End event from stop_playback() closes the clip once the jitter buffer has drained.
        break;
    case Marker::TxIdentity:
        m_txIdent = m.ident;
        m_txIdent.bound = true;
        if (m_clip.active && m_clip.kind == ClipTx) {
            m_clip.ident = m_txIdent;
        }
        break;
    case Marker::Context:
        m_context = m.ident;
        break;
    }
}

void QsoReplayEngine::startClip(int kind, qint64 monoMs, const Identity &ident)
{
    m_clip = Clip();
    m_clip.active = true;
    m_clip.kind = kind;
    m_clip.startMonoMs = monoMs;
    m_clip.lastSampleMonoMs = QsoReplayCapture::monoNowMs();
    m_clip.startWallMs = QDateTime::currentMSecsSinceEpoch() - (m_clip.lastSampleMonoMs - monoMs);
    m_clip.ident = ident;
    // The file is created lazily on the first samples, so a spurious start_playback() with no
    // audio behind it never touches the disk.
}

QString QsoReplayEngine::makeClipId(int kind, qint64 wallMs) const
{
    const QString stamp = QDateTime::fromMSecsSinceEpoch(wallMs).toString(QStringLiteral("yyyyMMdd-HHmmss-zzz"));
    const QString kindStr = (kind == ClipTx) ? QStringLiteral("tx") : QStringLiteral("rx");
    for (int attempt = 0; attempt < 8; ++attempt) {
        const QString id = QStringLiteral("%1_%2_%3").arg(stamp, kindStr,
                                                           QString::number(QRandomGenerator::global()->bounded(65536), 16).rightJustified(4, QLatin1Char('0')));
        if (!m_workerFiles.contains(id) && !QFile::exists(m_dir + QLatin1Char('/') + id + QLatin1Char('.') + m_extension)) {
            return id;
        }
    }
    return QString();
}

bool QsoReplayEngine::openClipFile()
{
    const qint64 now = QsoReplayCapture::monoNowMs();
    if (!m_storageOk || now < m_backoffUntilMonoMs) {
        return false;
    }
    if (!replayPlatformStorageWritable()) {
        return false;   // before first unlock after a reboot: skip quietly
    }
    const qint64 freeBytes = m_freeSpaceProbe ? m_freeSpaceProbe() : kMinFreeBytes;
    if (freeBytes >= 0 && freeBytes < kMinFreeBytes) {
        postError(QStringLiteral("Low storage"));
        return false;
    }
    const QString id = makeClipId(m_clip.kind, m_clip.startWallMs);
    if (id.isEmpty()) {
        return false;
    }
    m_clip.id = id;
    m_clip.fileName = id + QLatin1Char('.') + m_extension;
    m_clip.finalPath = m_dir + QLatin1Char('/') + m_clip.fileName;
    m_clip.partPath = m_clip.finalPath + QStringLiteral(".part");

    m_writer = m_writerFactory ? m_writerFactory() : nullptr;
    if (!m_writer || !m_writer->open(m_clip.partPath, kSampleRate, 1, m_bitrateBps)) {
        const QString err = m_writer ? m_writer->lastError() : QStringLiteral("no writer");
        m_writer.reset();
        QFile::remove(m_clip.partPath);
        m_backoffUntilMonoMs = now + kOpenBackoffMs;
        postError(QStringLiteral("Cannot start replay: %1").arg(err));
        return false;
    }
    m_clip.fileOpen = true;
    postRecordingState(true);
    return true;
}

void QsoReplayEngine::writeClipSamples(const int16_t *pcm, uint32_t count, qint64 monoMs)
{
    if (!m_clip.fileOpen) {
        if (!openClipFile()) {
            m_clip.discarding = true;
            return;
        }
    }
    if (!m_writer->write(pcm, count)) {
        postError(QStringLiteral("Replay write failed: %1").arg(m_writer->lastError()));
        m_writer->abort();
        m_writer.reset();
        QFile::remove(m_clip.partPath);
        m_clip.fileOpen = false;
        m_clip.discarding = true;
        m_backoffUntilMonoMs = monoMs + kOpenBackoffMs;
        postRecordingState(false);
        return;
    }
    m_clip.samples += count;
    m_clip.lastSampleMonoMs = monoMs;

    const quint64 maxSamples = static_cast<quint64>(m_maxOverSeconds.load(std::memory_order_relaxed)) * kSampleRate;
    if (m_clip.samples >= maxSamples) {
        m_clip.truncated = true;
        closeClip();   // the rest of this over is discarded until the next Begin
    }
}

void QsoReplayEngine::closeClip()
{
    const Clip c = m_clip;
    m_clip = Clip();
    if (!c.fileOpen) {
        return;
    }
    postRecordingState(false);

    const int durationMs = static_cast<int>((c.samples * 1000ULL) / kSampleRate);
    if (durationMs < m_minClipMs) {
        if (m_writer) {
            m_writer->abort();
            m_writer.reset();
        }
        QFile::remove(c.partPath);
        return;
    }

    const bool ok = m_writer && m_writer->close();
    const QString err = m_writer ? m_writer->lastError() : QString();
    m_writer.reset();
    if (!ok) {
        QFile::remove(c.partPath);
        postError(QStringLiteral("Replay finalize failed: %1").arg(err));
        return;
    }
    if (!QFile::rename(c.partPath, c.finalPath)) {
        QFile::remove(c.partPath);
        postError(QStringLiteral("Replay rename failed"));
        return;
    }
    replayPlatformProtectFile(c.finalPath);

    FileInfo info;
    info.fileName = c.fileName;
    info.bytes = QFileInfo(c.finalPath).size();
    m_workerFiles.insert(c.id, info);
    m_workerUsedBytes += info.bytes;

    // Unbound RX identity (IAX never reports a stream): fall back to the connection context.
    Identity ident = c.ident;
    if (!ident.bound && c.kind == ClipRx) {
        ident.mode = m_context.mode;
        ident.callsign = m_context.callsign;
    }
    const QString id = c.id;
    const int kind = c.kind;
    const QString startTime = wallTimeString(c.startWallMs);
    const bool truncated = c.truncated;
    const qint64 bytes = info.bytes;
    QMetaObject::invokeMethod(this, [this, id, info, kind, ident, startTime, durationMs, bytes, truncated]() {
        m_files.insert(id, info);
        m_usedBytes += info.bytes;
        emit storageChanged();
        emit overRecorded(id, kind, ident.mode, ident.srcId, ident.callsign, ident.tg, startTime,
                          durationMs, bytes, truncated);
    }, Qt::QueuedConnection);

    enforceBudget();
}

void QsoReplayEngine::checkIdleClose()
{
    if (!m_clip.active) {
        return;
    }
    const qint64 now = QsoReplayCapture::monoNowMs();
    const qint64 ref = m_clip.fileOpen ? m_clip.lastSampleMonoMs : m_clip.startMonoMs;
    if (now - ref > m_idleCloseMs) {
        closeClip();   // lost End (engine destroyed, interruption): keep what we have
    }
}

void QsoReplayEngine::enforceBudget()
{
    const qint64 budget = static_cast<qint64>(m_storageBudgetMb.load(std::memory_order_relaxed)) * 1024 * 1024;
    if (m_workerUsedBytes <= budget) {
        return;
    }
    QStringList ids = m_workerFiles.keys();
    std::sort(ids.begin(), ids.end());   // names start with the timestamp: oldest first
    QStringList victims;
    qint64 used = m_workerUsedBytes;
    for (const QString &id : ids) {
        if (used <= budget) {
            break;
        }
        used -= m_workerFiles.value(id).bytes;
        victims.append(id);
    }
    deleteFiles(victims);
}

void QsoReplayEngine::deleteFiles(const QStringList &ids)
{
    QStringList removed;
    for (const QString &id : ids) {
        const auto it = m_workerFiles.find(id);
        if (it == m_workerFiles.end()) {
            continue;
        }
        const QString path = m_dir + QLatin1Char('/') + it->fileName;
        if (QFile::exists(path) && !QFile::remove(path)) {
            qWarning() << "[QsoReplay] could not delete" << it->fileName;
            continue;
        }
        m_workerUsedBytes -= it->bytes;
        m_workerFiles.erase(it);
        removed.append(id);
    }
    if (m_workerUsedBytes < 0) {
        m_workerUsedBytes = 0;
    }
    if (!removed.isEmpty()) {
        postRemoved(removed);
    }
}

void QsoReplayEngine::reconcileFiles(const QSet<QString> &keep)
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QStringList orphans;
    for (auto it = m_workerFiles.cbegin(); it != m_workerFiles.cend(); ++it) {
        if (keep.contains(it.key())) {
            continue;
        }
        const QFileInfo fi(m_dir + QLatin1Char('/') + it->fileName);
        const qint64 age = now - fi.lastModified().toMSecsSinceEpoch();
        if (age > m_reconcileGraceMs) {
            orphans.append(it.key());
        }
    }
    deleteFiles(orphans);
}

void QsoReplayEngine::postRemoved(const QStringList &ids)
{
    QMetaObject::invokeMethod(this, [this, ids]() {
        for (const QString &id : ids) {
            const auto it = m_files.find(id);
            if (it != m_files.end()) {
                m_usedBytes -= it->bytes;
                m_files.erase(it);
            }
        }
        if (m_usedBytes < 0) {
            m_usedBytes = 0;
        }
        emit storageChanged();
        emit recordingsRemoved(ids);
    }, Qt::QueuedConnection);
}

void QsoReplayEngine::postError(const QString &reason)
{
    qWarning().noquote() << "[QsoReplay]" << reason;
    QMetaObject::invokeMethod(this, [this, reason]() {
        const qint64 now = QsoReplayCapture::monoNowMs();
        if (reason != m_lastError) {
            m_lastError = reason;
            emit lastErrorChanged();
        }
        // One toast per minute at most; the settings page shows lastError regardless.
        if (now - m_lastErrorMonoMs > 60000) {
            m_lastErrorMonoMs = now;
            emit recordingFailed(reason);
        }
    }, Qt::QueuedConnection);
}

void QsoReplayEngine::postRecordingState(bool on)
{
    QMetaObject::invokeMethod(this, [this, on]() {
        if (m_recording != on) {
            m_recording = on;
            emit recordingChanged();
        }
    }, Qt::QueuedConnection);
}
