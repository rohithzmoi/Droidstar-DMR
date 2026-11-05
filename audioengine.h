/*
    Copyright (C) 2019-2021 Doug McLain
    Modified Copyright (C) 2024-2026 Rohith Namboothiri
 
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
#ifndef AUDIOENGINE_H
#define AUDIOENGINE_H

#include <QObject>
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
#include <QAudio>
#include <QAudioFormat>
#include <QAudioInput>
#else
#include <QAudioDevice>
#include <QAudioSink>
#include <QAudioSource>
#include <QMediaDevices>
#endif
#include <QAudioOutput>
#include <QQueue>
#include <QVector>
#include <array>
#include <cstdint>
#include "AudioEnhancer.h"

#define AUDIO_OUT 1
#define AUDIO_IN  0

class AudioEngine : public QObject
{
    Q_OBJECT
public:
    AudioEngine(QString in, QString out);
    ~AudioEngine();

    // Global lock-free audio enhancement controls (apply immediately across all active Mode threads)
    static void setGlobalNoiseMode(int mode);
    static int globalNoiseMode();
    static void setGlobalHdUpscale(bool enabled);
    static bool globalHdUpscale();
    static void setGlobalTxEnhance(bool enabled);
    static bool globalTxEnhance();
    static void setGlobalVoiceIsolation(bool enabled);
    static bool globalVoiceIsolation();
    static void setGlobalRxClean(bool enabled);
    static bool globalRxClean();
    static void setGlobalSpkPreampDb(float db);
    static float globalSpkPreampDb();

    // Saved user persisted config vs automatic external-mic bypass
    static void loadSavedUserTxSettings(int nrMode, bool hdOn, bool txOn, bool viOn);
    static int savedUserNoiseMode();
    static bool savedUserHdUpscale();
    static bool savedUserTxEnhance();
    static void setExternalMicActive(bool externalActive);
    static bool isExternalMicActive();
    static void setStateChangeCallback(void (*cb)());
    QStringList discover_audio_devices(uint8_t d);
    void init();
    void start_capture();
    void stop_capture();
    void start_playback();
    void stop_playback();
    void write(int16_t *, size_t);
    void set_output_buffer_size(uint32_t b);
    void set_input_buffer_size(uint32_t b);
    void set_output_volume(qreal v);
    void set_input_volume(qreal v);
    void set_agc(bool agc) { m_agc = agc; }
    void flush_capture_for_tx()
    {
        m_audioinq.clear();
        m_tx16kCount = 0;
        m_capPunchInSamples = 0;
        m_capPunchInTotal = static_cast<size_t>(std::max(96, (std::max(8000, m_captureDeviceRate) * 12) / 1000));
        m_txEnhancer.reset();
    }
    bool frame_available() { return (m_audioinq.size() >= 320) ? true : false; }
    uint16_t read(int16_t *, int);
    uint16_t read(int16_t *);
    uint16_t level() { return m_maxlevel; }
    bool m_dataWritten { false };
    void setOutputDevice(const QString &deviceName);
    void setInputDevice(const QString &deviceName);
    QString getFriendlyName(const QString& deviceIdentifier);
    QString mapFriendlyNameToDevice(const QString &friendlyName);

signals:
    void audioDeviceListChanged();
    void audioEngineChanged();

private:
    QString m_outputdevice;
    QString m_inputdevice;
#if QT_VERSION < QT_VERSION_CHECK(6, 3, 0)
    QAudioOutput *m_out { nullptr };
    QAudioInput *m_in { nullptr };
#else
    QAudioSink *m_out { nullptr };
    QAudioSource *m_in { nullptr };
#endif
    QIODevice *m_outdev { nullptr };
    QIODevice *m_indev { nullptr };
    QQueue<int16_t> m_audioinq;
    uint16_t m_maxlevel { 0 };
    bool m_agc { true };
    float m_srm { 1.0f };

    // Audio IO rates & formats
    int m_captureDeviceRate { 8000 };
    int m_captureChannels { 1 };
    QAudioFormat::SampleFormat m_captureSampleFormat { QAudioFormat::Int16 };

    int m_playbackDeviceRate { 8000 };
    int m_playbackChannels { 1 };
    QAudioFormat::SampleFormat m_playbackSampleFormat { QAudioFormat::Int16 };

    uint32_t m_lastPlayRouteGen { 0 };
    uint32_t m_lastCapRouteGen { 0 };
    // Capture liveness: CoreAudio stops calling back after an interruption / media reset while
    // Qt's source still believes it is running, and its stop()/reset()/destructor then wait forever.
    qint64 m_lastCaptureCallbackMs = 0;
    uint32_t m_captureIoGen = 0;           // iOS I/O invalidation generation when capture started
    bool captureSourceLooksDead() const;
    void reapCaptureSource();
    bool m_reinitializingSink { false };
    int m_consecutiveWriteFailures { 0 };
    int m_sinkRebuildCount { 0 };          // back-off for a genuinely dead output
    qint64 m_lastSinkRebuildMs { 0 };

    // Capture resampling state: deviceRate -> 16000 Hz or 8000 Hz (mono)
    qint64 m_capStreamIndex { -1 };
    double m_capNextOutPos { 0.0 };
    float m_capPrevSample { 0.0f };
    std::array<float, 4> m_capHist{};
    std::array<float, 320> m_tx16kBuf{};
    size_t m_tx16kCount { 0 };

    // Playback resampling state: 16000 Hz (or 8000 Hz) -> deviceRate (mono)
    qint64 m_playStreamIndex { -1 };
    double m_playNextOutPos { 0.0 };
    float m_playPrevSample { 0.0f };
    std::array<float, 4> m_playHist{};
    std::array<float, 640> m_rx16kBuf{};
    bool m_lastPlayHdMode { true };

    // Speech RMS AGC state (target -18 dBFS speech level, true-peak governed)
    float m_agcSpeechRms { 5500.0f };  // == kTargetRms so a new stream starts at unity gain
    int m_agcHoldFrames { 0 };

    // 4th-order Linkwitz-Riley (two cascaded 2nd-order Butterworth biquads) filter state
    struct Biquad {
        float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f };
        float a1 { 0.0f }, a2 { 0.0f };
        float z1 { 0.0f }, z2 { 0.0f };
    };
    Biquad m_capAntiAlias1;
    Biquad m_capAntiAlias2;
    Biquad m_capHighpass;
    Biquad m_capHighpass2;
    Biquad m_capLowpass;
    Biquad m_capLowpass2;
    Biquad m_playLowpass;
    Biquad m_playLowpass2;
    size_t m_capPunchInSamples { 0 };
    size_t m_capPunchInTotal { 1 };

    AudioEnhancer m_txEnhancer { AudioEnhancer::RoleTx };
    AudioEnhancer m_rxEnhancer { AudioEnhancer::RoleRx };
    QVector<float> m_playOutFloat;
    QVector<int16_t> m_playOutInt16;

    static Biquad makeLowpassBiquad(float fs, float fc, float Q);
    static Biquad makeHighpassBiquad(float fs, float fc, float Q);
    static float biquadProcess(Biquad &bq, float x);
    static float hermite4(float frac, float ym1, float y0, float y1, float y2);
    void updatePlaybackReconstructionFilter(bool hdActive);

    void initPlaybackSink();
    void initCaptureSource();
    bool matchesSelectedDevice(const QAudioDevice &dev, const QString &selected);

    float m_audio_out_temp_buf[320];
    float *m_audio_out_temp_buf_p;

    float m_aout_max_buf[200];
    float *m_aout_max_buf_p;
    int m_aout_max_buf_idx { 0 };

    float m_aout_gain { 1.0f };
    float m_volume { 1.0f };
    float m_inGain { 1.0f };
    float m_preampGain { 1.0f };

private slots:
    void input_data_received();
    void process_audio(int16_t *pcm, size_t s);
    void handleStateChanged(QAudio::State newState);
};

#endif // AUDIOENGINE_H
