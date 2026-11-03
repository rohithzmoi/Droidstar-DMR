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

#ifndef TRANSCRIPTIONENGINE_H
#define TRANSCRIPTIONENGINE_H

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVector>
#include <QHash>
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QFile>
#include <QTimer>
#include <QSettings>
#include <atomic>
#include <cstdint>
#include <vector>

struct whisper_context;
struct SherpaOnnxOfflineRecognizer;

class TranscriptionEngine : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ isEnabled WRITE setEnabled NOTIFY transcriptionEnabledChanged)
    Q_PROPERTY(bool logTranscripts READ logTranscripts WRITE setLogTranscripts NOTIFY logTranscriptsChanged)
    Q_PROPERTY(QString engineType READ engineType WRITE setEngineType NOTIFY engineTypeChanged)
    Q_PROPERTY(bool appleSpeechSupported READ isAppleSpeechSupported CONSTANT)
    Q_PROPERTY(bool appleSpeechAvailable READ isAppleSpeechAvailable NOTIFY modelStateChanged)
    Q_PROPERTY(bool parakeetSupported READ isParakeetSupported CONSTANT)
    Q_PROPERTY(QString deviceModelName READ deviceModelName CONSTANT)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString modelType READ modelType WRITE setModelType NOTIFY modelTypeChanged)
    Q_PROPERTY(QString modelDisplayName READ modelDisplayName NOTIFY modelStateChanged)
    Q_PROPERTY(bool modelReady READ isModelReady NOTIFY modelStateChanged)
    Q_PROPERTY(bool modelDownloading READ isModelDownloading NOTIFY modelStateChanged)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY downloadProgressChanged)
    Q_PROPERTY(QVariantList activeDownloads READ activeDownloads NOTIFY downloadProgressChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY modelStateChanged)
    Q_PROPERTY(QString currentTranscript READ currentTranscript NOTIFY transcriptChanged)
    Q_PROPERTY(QString currentCallsign READ currentCallsign NOTIFY transcriptChanged)
    Q_PROPERTY(unsigned int currentSrcId READ currentSrcId NOTIFY transcriptChanged)
    Q_PROPERTY(int currentTurnId READ currentTurnId NOTIFY transcriptChanged)
    Q_PROPERTY(bool activeTurn READ isActiveTurn NOTIFY transcriptChanged)
    Q_PROPERTY(QVariantList recentTurns READ recentTurns NOTIFY recentTurnsChanged)
    Q_PROPERTY(QVariantList supportedLanguages READ supportedLanguages CONSTANT)

public:
    explicit TranscriptionEngine(QObject *parent = nullptr);
    ~TranscriptionEngine() override;

    static TranscriptionEngine *instance();

    bool isEnabled() const { return m_enabled.load(std::memory_order_relaxed); }
    void setEnabled(bool on);

    // Whether finished turns are saved into the QSO log (applies to every engine and model)
    bool logTranscripts() const { return m_logTranscripts.load(std::memory_order_relaxed); }
    void setLogTranscripts(bool on);

    // "apple" (Apple Neural Engine), "parakeet" (NVIDIA Parakeet TDT v3 0.6B), or "whisper" (local Whisper AI)
    QString engineType() const;
    void setEngineType(const QString &engine);
    bool isAppleSpeechSupported() const;
    bool isAppleSpeechAvailable() const { return m_appleSpeechAuthorized.load(std::memory_order_relaxed); }
    bool isParakeetSupported() const;
    QString deviceModelName() const;
    Q_INVOKABLE bool isModelSupportedOnDevice(const QString &modelOrEngine) const;
    Q_INVOKABLE QString modelUnsupportedReason(const QString &modelOrEngine) const;

    QString language() const;
    void setLanguage(const QString &lang);

    // Whisper tier: "tiny" (~31 MB), "base" (~57 MB), "small" (~181 MB), or "turbo" (~547 MB Large-v3-Turbo).
    QString modelType() const;
    void setModelType(const QString &type);
    QString modelDisplayName() const;

    bool isModelReady() const;
    bool isModelDownloading() const { return m_downloading.load(std::memory_order_relaxed); }
    double downloadProgress() const { return m_downloadProgress.load(std::memory_order_relaxed); }
    QVariantList activeDownloads() const;
    QString statusText() const;

    QString currentTranscript() const;
    QString currentCallsign() const;
    unsigned int currentSrcId() const { return m_currentSrcId.load(std::memory_order_relaxed); }
    int currentTurnId() const { return m_currentTurnId.load(std::memory_order_relaxed); }
    bool isActiveTurn() const { return m_activeTurn.load(std::memory_order_relaxed); }
    QVariantList recentTurns() const;
    QVariantList supportedLanguages() const;

    Q_INVOKABLE void downloadModel(const QString &type = QString());
    Q_INVOKABLE void cancelDownload(const QString &type = QString());
    Q_INVOKABLE bool isModelKeyDownloading(const QString &type) const;
    Q_INVOKABLE bool isModelDownloaded(const QString &type) const;
    Q_INVOKABLE bool isParakeetDownloaded() const;
    Q_INVOKABLE void clearTranscript();
    Q_INVOKABLE void setUserCallsign(const QString &callsign);
    static QString normalizeHamTranscript(const QString &raw);

    // Apple Speech bridge callbacks, delivered on the main thread
    void onAppleSpeechPartialResult(int sessionId, const QString &rawText, bool isFinal);
    void onAppleSpeechStatusChanged(bool authorized, bool available, const QString &statusMsg);
    void onAppleSpeechSessionFailed(int sessionId, const QString &reason);

    // Zero-latency audio thread hook (called from AudioEngine::write)
    void pushRxSamples(const int16_t *pcm8k, size_t count);

    // Turn boundary hooks (called from AudioEngine::stop_playback and DroidStar::update_data)
    void markEndOfTransmission();
    void onStreamStateChanged(uint32_t streamId, uint32_t srcId, const QString &callsign, int streamState);
    Q_INVOKABLE void onDisconnected();

signals:
    void transcriptionEnabledChanged();
    void logTranscriptsChanged();
    void engineTypeChanged();
    void languageChanged();
    void modelTypeChanged();
    void modelStateChanged();
    void downloadProgressChanged();
    void transcriptChanged();
    void recentTurnsChanged();
    void finalTranscriptReady(int turnId, unsigned int srcId, const QString &callsign, const QString &text, const QString &timestamp);
    void modelDownloadError(const QString &error);

private:
    struct WhisperSegment {
        QString text;
        size_t endSample { 0 };
    };

    struct AppleTurnRecord {
        int uiTurnId { 0 };
        uint32_t srcId { 0 };
        QString callsign;
        QString lastText;
        bool ended { false };
        qint64 deadlineMs { 0 };
    };

    struct DownloadStep {
        QString url;
        QString finalPath;
        qint64 minBytes { 0 };
        qint64 expectedBytes { 0 };
    };

    struct ModelDownloadJob {
        QString key;
        QString label;
        QVector<DownloadStep> steps;
        int currentStep { 0 };
        qint64 completedBytes { 0 };
        qint64 totalBytes { 0 };
        qint64 stepResumeOffset { 0 };
        bool checkedRangeStatus { false };
        double progress { 0.0 };
        QNetworkReply *reply { nullptr };
        QFile *file { nullptr };
    };

    void workerLoop();
    void wakeWorker();
    void activateSelectedEngine();
    bool ensureModelLoaded(const QString &modelPath, bool useGpu);
    void unloadModel();
    void buildWhisperVocabBiasTable();
    bool ensureParakeetLoaded();
    void unloadParakeet();
    bool whisperGpuAllowed() const;
    QString runWhisperInference(const float *samples16k, size_t sampleCount, const QString &langCode,
                                std::vector<WhisperSegment> *segments, bool finalPass, bool accuratePass = false);
    QStringList knownCallsignsLocked(const QString &speakerCall) const;
    QString runParakeetInference(const float *samples16k, size_t sampleCount,
                                 std::vector<WhisperSegment> *segments);
    static QString cleanWhisperOutput(const QString &raw);
    static QString collapseRepetitions(const QString &text);
    QString modelsDirectoryPath() const;
    QString parakeetDirectoryPath() const;
    QString modelFileName(const QString &type) const;
    QString modelFilePath(const QString &type) const;
    QString modelDownloadUrl(const QString &type) const;
    QString resolveLoadableModelPath(const QString &type = QString()) const;
    QString selectedModelKey() const;
    void startNextDownloadStep(ModelDownloadJob *job);
    void syncDownloadState();
    void ensureMetalShaderExtracted() const;
    void updateStatusText(const QString &text);
    void refreshIdleStatusText();
    void loadSettings();
    void saveSettings() const;
    void scheduleAutoClear(int delayMs = 4000);
    void cancelAutoClear();
    void publishFinalTurn(int turnId, uint32_t srcId, const QString &callsign, const QString &text);
    void finalizeAppleTurn(int sessionId);
    void finalizeAppleTurns(bool onlyExpired);

    // Lock-free SPSC ring buffer for 8 kHz mono int16_t RX samples (262144 samples = ~32.7s)
    static constexpr uint32_t kRingCapacity = 262144U;
    static constexpr uint32_t kRingMask = kRingCapacity - 1U;
    std::vector<int16_t> m_ringBuffer;
    std::atomic<uint32_t> m_ringWritePos { 0 };
    std::atomic<uint32_t> m_ringReadPos { 0 };

    std::atomic<bool> m_enabled { false };
    std::atomic<bool> m_logTranscripts { true };
    std::atomic<bool> m_stopWorker { false };
    std::atomic<bool> m_appleSpeechAuthorized { false };
    std::atomic<bool> m_appleSpeechDenied { false };
    std::atomic<bool> m_appleStartFailed { false };
    std::atomic<qint64> m_appleFailedAtMs { 0 };
    std::atomic<int> m_appleFailedSession { 0 };
    std::atomic<bool> m_whisperLoaded { false };
    std::atomic<bool> m_modelLoadFailed { false };
    std::atomic<bool> m_parakeetLoaded { false };
    std::atomic<bool> m_parakeetLoadFailed { false };
    std::atomic<bool> m_downloading { false };
    std::atomic<double> m_downloadProgress { 0.0 };
    std::atomic<bool> m_turnEndPending { false };
    std::atomic<bool> m_newTurnPending { false };
    std::atomic<uint32_t> m_newTurnRingPos { 0 };
    std::atomic<bool> m_reloadModelPending { false };
    std::atomic<bool> m_connected { false };
    std::atomic<bool> m_rxStreamActive { false };
    std::atomic<bool> m_activeTurn { false };
    std::atomic<int> m_currentTurnId { 0 };
    std::atomic<uint32_t> m_currentSrcId { 0 };
    std::atomic<qint64> m_lastSamplePushMs { 0 };

    mutable QMutex m_stateMutex;
    QString m_engineType { QStringLiteral("apple") };
    QString m_language { QStringLiteral("auto") };
    QString m_modelType { QStringLiteral("base") };
    QString m_statusText { QStringLiteral("Off") };
    QString m_appleFailureReason;
    QString m_currentTranscript;
    QString m_currentCallsign;
    QString m_pendingCallsign;
    uint32_t m_pendingSrcId { 0 };
    QString m_finalizeCallsign;
    uint32_t m_finalizeSrcId { 0 };
    int m_finalizeTurnId { 0 };
    QString m_userCallsign;
    uint32_t m_lastStreamId { 0 };
    uint32_t m_lastSrcId { 0 };
    int m_lastStreamState { 4 }; // Mode::STREAM_IDLE
    QVariantList m_recentTurns;
    QHash<int, AppleTurnRecord> m_appleTurns; // keyed by Apple Speech session id
    QTimer *m_autoClearTimer { nullptr };

    // Worker thread and wake condition
    QThread *m_workerThread { nullptr };
    QMutex m_wakeMutex;
    QWaitCondition m_wakeCond;

    // Whisper context (owned & accessed exclusively by worker thread)
    whisper_context *m_whisperCtx { nullptr };
    QString m_loadedModelPath;
    QString m_failedModelPath;
    QByteArray m_turnDetectedLang;
    QByteArray m_turnPrompt; // worker thread only: Whisper initial prompt built from the over's known callsigns
    QHash<int32_t, QVector<QPair<int32_t, float>>> m_whisperUnigramBoosts;
    QHash<uint64_t, QVector<QPair<int32_t, float>>> m_whisperBigramBoosts;
    int32_t m_whisperVocabSize { 0 };
    bool m_whisperUsingGpu { false };
    bool m_gpuBroken { false };
    bool m_lastWhisperFailed { false };
    bool m_lastWhisperAborted { false };

    // Parakeet TDT v3 context via sherpa-onnx (owned & accessed exclusively by worker thread)
    const SherpaOnnxOfflineRecognizer *m_parakeetCtx { nullptr };

    // Model downloader (owned by main thread; supports parallel downloads across models)
    QNetworkAccessManager *m_netManager { nullptr };
    QHash<QString, ModelDownloadJob *> m_downloadJobs;
    QStringList m_downloadOrder;
    QVariantList m_activeDownloadsSnapshot;
};

#endif // TRANSCRIPTIONENGINE_H
