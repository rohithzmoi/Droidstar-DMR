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

#ifndef QSOREPLAYENGINE_H
#define QSOREPLAYENGINE_H

// QSO Replay: keeps a compressed on-device copy of every received over (and, when self-logging is
// on, the user's own TX) next to the QSO log. See QsoReplayCapture.h for the audio-thread side.
//
// Threads:
//   main    - this QObject: settings, properties, identity markers from DroidStar::update_data,
//             reconcile against logs.json, bookkeeping of known clip ids (no disk I/O).
//   mode    - only the inline functions in QsoReplayCapture.h.
//   worker  - drains the capture ring, encodes, and does every file operation.
//
// Privacy: clip files live under AppDataLocation/replay (never Documents), are excluded from backup
// and no file path is ever handed to QML; QML only sees clip ids.

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QWaitCondition>
#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

#include "ReplayFileWriter.h"

class QThread;
class LogHandler;

class QsoReplayEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY replayEnabledChanged)
    Q_PROPERTY(bool recordTx READ recordTx WRITE setRecordTx NOTIFY recordTxChanged)
    Q_PROPERTY(bool selfLoggingAllowed READ selfLoggingAllowed WRITE setSelfLoggingAllowed NOTIFY selfLoggingAllowedChanged)
    Q_PROPERTY(int storageBudgetMb READ storageBudgetMb WRITE setStorageBudgetMb NOTIFY storageBudgetMbChanged)
    Q_PROPERTY(int maxOverSeconds READ maxOverSeconds WRITE setMaxOverSeconds NOTIFY maxOverSecondsChanged)
    Q_PROPERTY(qint64 storageUsedBytes READ storageUsedBytes NOTIFY storageChanged)
    Q_PROPERTY(int recordingCount READ recordingCount NOTIFY storageChanged)
    Q_PROPERTY(bool scanComplete READ scanComplete NOTIFY storageChanged)
    Q_PROPERTY(bool recording READ isRecording NOTIFY recordingChanged)
    Q_PROPERTY(QString lastError READ lastError NOTIFY lastErrorChanged)
    Q_PROPERTY(int bytesPerSecond READ bytesPerSecond CONSTANT)

public:
    enum class StreamEvent { New, Ended, Other };
    enum ClipKind { ClipRx = 0, ClipTx = 1 };
    Q_ENUM(ClipKind)

    // loadPersisted=false skips QSettings (unit tests).
    explicit QsoReplayEngine(QObject *parent = nullptr, bool loadPersisted = true);
    ~QsoReplayEngine() override;

    static QsoReplayEngine *instance();

    // main.cpp: wires LogHandler::logSaved/logCleared to reconcile/clearAll.
    void attachLogHandler(LogHandler *handler);
    // Starts the worker thread (startup scan, then drain loop).
    void start();

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool on);
    bool recordTx() const { return m_recordTx; }
    void setRecordTx(bool on);
    bool selfLoggingAllowed() const { return m_selfLoggingAllowed; }
    void setSelfLoggingAllowed(bool on);
    int storageBudgetMb() const { return m_storageBudgetMb.load(std::memory_order_relaxed); }
    void setStorageBudgetMb(int mb);
    int maxOverSeconds() const { return m_maxOverSeconds.load(std::memory_order_relaxed); }
    void setMaxOverSeconds(int seconds);
    qint64 storageUsedBytes() const { return m_usedBytes; }
    int recordingCount() const { return static_cast<int>(m_files.size()); }
    bool scanComplete() const { return m_scanComplete; }
    bool isRecording() const { return m_recording; }
    QString lastError() const { return m_lastError; }
    int bytesPerSecond() const { return m_bytesPerSecond; }

    Q_INVOKABLE bool hasRecording(const QString &id) const { return m_files.contains(id); }
    Q_INVOKABLE void deleteRecording(const QString &id);
    Q_INVOKABLE void deleteRecordings(const QStringList &ids);
    Q_INVOKABLE void clearAll();
    // Deletes clips that no QSO log entry references any more (with a grace window for clips whose
    // log save is still in flight). Also runs automatically after every logs.json save.
    Q_INVOKABLE void reconcile(const QStringList &referencedIds);
    Q_INVOKABLE double hoursForMegabytes(int mb) const;

    // C++ only (never exposed to QML): absolute path of a finished clip, or empty.
    QString pathForId(const QString &id) const;

    // Main-thread hooks (DroidStar).
    void onStreamStateChanged(const QString &mode, quint32 streamId, quint32 srcId,
                              const QString &callsign, const QString &tg, StreamEvent event);
    void onTxStarted(const QString &mode, const QString &myCallsign, const QString &tg);
    // Fallback identity for modes that never report a stream (IAX): the connected mode and host.
    void onConnectionContext(const QString &mode, const QString &refName);
    void onEnteredBackground();
    // Closes any open clip and stops the worker, waiting at most timeoutMs. Safe to call twice.
    void shutdownSync(int timeoutMs = 1500);

    // Test hooks (call before start()).
    void setStorageDirectory(const QString &dir);
    void setWriterFactory(std::function<std::unique_ptr<IReplayFileWriter>()> factory);
    void setFreeSpaceProbe(std::function<qint64()> probe);
    void setTimings(int minClipMs, int idleCloseMs, int reconcileGraceMs);
    QString storageDirectory() const { return m_dir; }

signals:
    // Not "enabledChanged": a QML Connections element has its own `enabled` property, and a handler
    // named onEnabledChanged would collide with it and make the whole component fail to load.
    void replayEnabledChanged();
    void recordTxChanged();
    void selfLoggingAllowedChanged();
    void storageBudgetMbChanged();
    void maxOverSecondsChanged();
    void storageChanged();
    void recordingChanged();
    void lastErrorChanged();
    // A finished clip. kind is ClipKind. QML attaches {id, kind, time, durationMs, bytes, truncated}
    // to the matching QSO log entry or creates one.
    void overRecorded(QString id, int kind, QString mode, uint srcId, QString callsign,
                      QString tgid, QString startTime, int durationMs, qint64 bytes, bool truncated);
    // Clips removed by budget pruning, reconcile or an explicit delete; QML strips the references.
    void recordingsRemoved(QStringList ids);
    void recordingFailed(QString reason);

private:
    struct Identity {
        QString mode;
        quint32 streamId { 0 };
        quint32 srcId { 0 };
        QString callsign;
        QString tg;
        bool bound { false };
        bool sameStream(const Identity &o) const
        {
            return streamId == o.streamId && srcId == o.srcId && callsign == o.callsign;
        }
    };
    struct Marker {
        enum Type { Split, StreamEnd, TxIdentity, Context } type { Split };
        Identity ident;
        quint32 ringPos { 0 };
        qint64 monoMs { 0 };
    };
    struct Boundary {
        bool isEvent { true };
        int evType { 0 };     // QsoReplayCapture::Ev as int
        int kind { 0 };       // ClipKind
        quint32 ringPos { 0 };
        qint64 monoMs { 0 };
        Marker marker;
    };
    struct Command {
        enum Type { ClearAll, Delete, Reconcile, BudgetCheck, Wake } type { Wake };
        QStringList ids;
    };
    struct Clip {
        bool active { false };
        bool fileOpen { false };
        bool discarding { false };
        bool truncated { false };
        int kind { ClipRx };
        QString id;
        QString fileName;
        QString partPath;
        QString finalPath;
        qint64 startWallMs { 0 };
        qint64 startMonoMs { 0 };
        qint64 lastSampleMonoMs { 0 };
        quint64 samples { 0 };
        Identity ident;
    };
    struct FileInfo {
        QString fileName;
        qint64 bytes { 0 };
    };

    void loadSettings();
    void saveSettings() const;
    void applyCaptureFlags();
    void postCommand(Command::Type type, const QStringList &ids = QStringList());
    void postMarker(const Marker &marker);
    void wakeWorker();
    static void wakeThunk(void *ctx);
    void reconcileFromLog();
    QString logFilePath() const;

    // worker thread
    void workerLoop();
    void startupScan();
    void processCommands();
    void pullBoundaries();
    void processAudio();
    void consumeRing(uint32_t count);
    void applyBoundary(const Boundary &b);
    void startClip(int kind, qint64 monoMs, const Identity &ident);
    bool openClipFile();
    void writeClipSamples(const int16_t *pcm, uint32_t count, qint64 monoMs);
    void closeClip();
    void checkIdleClose();
    void enforceBudget();
    void deleteFiles(const QStringList &ids);
    void reconcileFiles(const QSet<QString> &keep);
    QString makeClipId(int kind, qint64 wallMs) const;
    void postRemoved(const QStringList &ids);
    void postError(const QString &reason);
    void postRecordingState(bool on);

    // main-thread state
    QString m_dir;
    bool m_enabled { false };
    bool m_recordTx { false };
    bool m_selfLoggingAllowed { false };
    bool m_persist { true };
    std::atomic<int> m_storageBudgetMb { 100 };
    std::atomic<int> m_maxOverSeconds { 600 };
    std::atomic<bool> m_stop { false };
    bool m_recording { false };
    bool m_scanComplete { false };
    bool m_started { false };
    bool m_shutDown { false };
    qint64 m_usedBytes { 0 };
    QString m_lastError;
    qint64 m_lastErrorMonoMs { 0 };
    int m_bytesPerSecond { 3090 };
    QHash<QString, FileInfo> m_files;
    LogHandler *m_logHandler { nullptr };
    QThread *m_thread { nullptr };
    QString m_ctxMode;
    QString m_ctxRef;

    // cross-thread queues
    QMutex m_queueMutex;
    std::deque<Command> m_commands;
    std::deque<Marker> m_markers;
    QMutex m_wakeMutex;
    QWaitCondition m_wakeCond;

    // worker-owned state
    std::function<std::unique_ptr<IReplayFileWriter>()> m_writerFactory;
    std::function<qint64()> m_freeSpaceProbe;
    std::unique_ptr<IReplayFileWriter> m_writer;
    std::vector<Boundary> m_boundaries;
    std::vector<int16_t> m_chunk;
    Clip m_clip;
    Identity m_pendingRxIdent;
    qint64 m_pendingRxIdentMonoMs { 0 };
    Identity m_txIdent;
    Identity m_context;
    qint64 m_backoffUntilMonoMs { 0 };
    bool m_storageOk { false };
    QHash<QString, FileInfo> m_workerFiles;
    qint64 m_workerUsedBytes { 0 };
    int m_minClipMs { 700 };
    int m_idleCloseMs { 2000 };
    int m_reconcileGraceMs { 90000 };
    int m_bitrateBps { 24000 };
    QString m_extension { QStringLiteral("m4a") };
};

#endif // QSOREPLAYENGINE_H
