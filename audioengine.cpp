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

#include "audioengine.h"
#include "TranscriptionEngine.h"
#include "QsoReplayCapture.h"
#include <QDebug>
#include <QSettings>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <QtMath>
#include <QElapsedTimer>

static qint64 captureMonotonicMs()
{
    static QElapsedTimer t = [] { QElapsedTimer x; x.start(); return x; }();
    return t.elapsed();
}

#ifdef Q_OS_IOS
#include "AudioSessionManager.h"
#endif

#if defined (Q_OS_MACOS) || defined(Q_OS_IOS)
#define MACHAK 1
#else
#define MACHAK 0
#endif

namespace {
std::atomic<bool> s_enhancerSettingsLoaded { false };
std::atomic<int>  s_savedUserNoiseMode { static_cast<int>(AudioEnhancer::NoiseNeural) };
std::atomic<bool> s_savedUserHdUpscale { true };
std::atomic<bool> s_savedUserTxEnhance { false };
std::atomic<bool> s_externalMicActive { false };
std::atomic<int>  s_globalNoiseMode { static_cast<int>(AudioEnhancer::NoiseNeural) };
std::atomic<bool> s_globalHdUpscale { true };
std::atomic<bool> s_globalTxEnhance { false };
std::atomic<bool> s_globalVoiceIsolation { false };
std::atomic<bool> s_globalRxClean { false };
std::atomic<float> s_globalSpkPreampDb { 6.0f };
std::atomic<void (*)()> s_stateChangeCallback { nullptr };

void notifyTxEnhancerStateChanged()
{
    if (auto cb = s_stateChangeCallback.load(std::memory_order_relaxed)) {
        cb();
    }
}

void ensureEnhancerSettingsLoaded()
{
    if (s_enhancerSettingsLoaded.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    // Load persisted user config for built-in mic (defaults: NR = Neural AI, HD Voice = true, Mic Leveler = false)
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
    const int nrMode = std::clamp(settings.value(QStringLiteral("AUDIO_NR_MODE"), static_cast<int>(AudioEnhancer::NoiseNeural)).toInt(), 0, 2);
    const bool hdOn = settings.value(QStringLiteral("AUDIO_HD_UPSCALE"), true).toBool();
    const bool txOn = settings.value(QStringLiteral("AUDIO_TX_ENHANCE"), false).toBool();
    const bool viOn = settings.value(QStringLiteral("AUDIO_VOICE_ISOLATION"), false).toBool();
    const bool rxCleanOn = settings.value(QStringLiteral("AUDIO_RX_CLEAN"), false).toBool();
    const float preampDb = std::clamp(settings.value(QStringLiteral("AUDIO_SPK_PREAMP_DB"), 6.0f).toFloat(), 0.0f, 24.0f);

    settings.setValue(QStringLiteral("AUDIO_NR_MODE"), nrMode);
    settings.setValue(QStringLiteral("AUDIO_HD_UPSCALE"), hdOn);
    settings.setValue(QStringLiteral("AUDIO_TX_ENHANCE"), txOn);
    settings.setValue(QStringLiteral("AUDIO_VOICE_ISOLATION"), viOn);
    settings.setValue(QStringLiteral("AUDIO_RX_CLEAN"), rxCleanOn);
    settings.setValue(QStringLiteral("AUDIO_SPK_PREAMP_DB"), preampDb);

    s_savedUserNoiseMode.store(nrMode, std::memory_order_relaxed);
    s_savedUserHdUpscale.store(hdOn, std::memory_order_relaxed);
    s_savedUserTxEnhance.store(txOn, std::memory_order_relaxed);
    s_globalVoiceIsolation.store(viOn, std::memory_order_relaxed);
    s_globalRxClean.store(rxCleanOn, std::memory_order_relaxed);
    s_globalSpkPreampDb.store(preampDb, std::memory_order_relaxed);

    const bool extMic = s_externalMicActive.load(std::memory_order_relaxed);
    s_globalNoiseMode.store(extMic ? 0 : nrMode, std::memory_order_relaxed);
    s_globalHdUpscale.store(extMic ? false : hdOn, std::memory_order_relaxed);
    s_globalTxEnhance.store(extMic ? false : txOn, std::memory_order_relaxed);
#ifdef Q_OS_IOS
    setAppleVoiceIsolationEnabled(viOn);
#endif
    AudioEnhancer::warmUpBundledNeuralModel();
}
} // namespace

void AudioEngine::loadSavedUserTxSettings(int nrMode, bool hdOn, bool txOn, bool viOn)
{
    ensureEnhancerSettingsLoaded();
    const int clampedNr = std::clamp(nrMode, 0, 2);
    s_savedUserNoiseMode.store(clampedNr, std::memory_order_relaxed);
    s_savedUserHdUpscale.store(hdOn, std::memory_order_relaxed);
    s_savedUserTxEnhance.store(txOn, std::memory_order_relaxed);
    s_globalVoiceIsolation.store(viOn, std::memory_order_relaxed);

    const bool extMic = s_externalMicActive.load(std::memory_order_relaxed);
    s_globalNoiseMode.store(extMic ? 0 : clampedNr, std::memory_order_relaxed);
    s_globalHdUpscale.store(extMic ? false : hdOn, std::memory_order_relaxed);
    s_globalTxEnhance.store(extMic ? false : txOn, std::memory_order_relaxed);
}

int AudioEngine::savedUserNoiseMode()
{
    ensureEnhancerSettingsLoaded();
    return s_savedUserNoiseMode.load(std::memory_order_relaxed);
}

bool AudioEngine::savedUserHdUpscale()
{
    ensureEnhancerSettingsLoaded();
    return s_savedUserHdUpscale.load(std::memory_order_relaxed);
}

bool AudioEngine::savedUserTxEnhance()
{
    ensureEnhancerSettingsLoaded();
    return s_savedUserTxEnhance.load(std::memory_order_relaxed);
}

void AudioEngine::setExternalMicActive(bool externalActive)
{
    ensureEnhancerSettingsLoaded();
    const bool prev = s_externalMicActive.exchange(externalActive, std::memory_order_acq_rel);
    if (externalActive) {
        // When mic is Bluetooth, wired headset, or external (not built-in), auto-disable all TX enhancements
        s_globalNoiseMode.store(static_cast<int>(AudioEnhancer::NoiseOff), std::memory_order_relaxed);
        s_globalHdUpscale.store(false, std::memory_order_relaxed);
        s_globalTxEnhance.store(false, std::memory_order_relaxed);
    } else {
        // When mic is back to built-in, roll back to the user's saved persisted config state
        s_globalNoiseMode.store(s_savedUserNoiseMode.load(std::memory_order_relaxed), std::memory_order_relaxed);
        s_globalHdUpscale.store(s_savedUserHdUpscale.load(std::memory_order_relaxed), std::memory_order_relaxed);
        s_globalTxEnhance.store(s_savedUserTxEnhance.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    if (prev != externalActive) {
        qInfo() << "[AudioEngine] External/Bluetooth/Wired mic active =" << externalActive
                << "-> Effective TX NR=" << s_globalNoiseMode.load(std::memory_order_relaxed)
                << "HD=" << s_globalHdUpscale.load(std::memory_order_relaxed)
                << "MicLeveler=" << s_globalTxEnhance.load(std::memory_order_relaxed);
        notifyTxEnhancerStateChanged();
    }
}

bool AudioEngine::isExternalMicActive()
{
    ensureEnhancerSettingsLoaded();
    return s_externalMicActive.load(std::memory_order_relaxed);
}

void AudioEngine::setStateChangeCallback(void (*cb)())
{
    s_stateChangeCallback.store(cb, std::memory_order_relaxed);
}

void AudioEngine::setGlobalNoiseMode(int mode)
{
    ensureEnhancerSettingsLoaded();
    const int clamped = std::clamp(mode, 0, 2);
    if (s_externalMicActive.load(std::memory_order_relaxed)) {
        // Keep TX enhancements disabled while on external mic without clobbering saved user config
        s_globalNoiseMode.store(0, std::memory_order_relaxed);
        notifyTxEnhancerStateChanged();
        return;
    }
    s_savedUserNoiseMode.store(clamped, std::memory_order_relaxed);
    s_globalNoiseMode.store(clamped, std::memory_order_relaxed);
}

int AudioEngine::globalNoiseMode()
{
    ensureEnhancerSettingsLoaded();
    if (s_externalMicActive.load(std::memory_order_relaxed)) {
        return 0;
    }
    return s_globalNoiseMode.load(std::memory_order_relaxed);
}

void AudioEngine::setGlobalHdUpscale(bool enabled)
{
    ensureEnhancerSettingsLoaded();
    if (s_externalMicActive.load(std::memory_order_relaxed)) {
        s_globalHdUpscale.store(false, std::memory_order_relaxed);
        notifyTxEnhancerStateChanged();
        return;
    }
    s_savedUserHdUpscale.store(enabled, std::memory_order_relaxed);
    s_globalHdUpscale.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::globalHdUpscale()
{
    ensureEnhancerSettingsLoaded();
    if (s_externalMicActive.load(std::memory_order_relaxed)) {
        return false;
    }
    return s_globalHdUpscale.load(std::memory_order_relaxed);
}

void AudioEngine::setGlobalTxEnhance(bool enabled)
{
    ensureEnhancerSettingsLoaded();
    if (s_externalMicActive.load(std::memory_order_relaxed)) {
        s_globalTxEnhance.store(false, std::memory_order_relaxed);
        notifyTxEnhancerStateChanged();
        return;
    }
    s_savedUserTxEnhance.store(enabled, std::memory_order_relaxed);
    s_globalTxEnhance.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::globalTxEnhance()
{
    ensureEnhancerSettingsLoaded();
    if (s_externalMicActive.load(std::memory_order_relaxed)) {
        return false;
    }
    return s_globalTxEnhance.load(std::memory_order_relaxed);
}

void AudioEngine::setGlobalVoiceIsolation(bool enabled)
{
    ensureEnhancerSettingsLoaded();
    s_globalVoiceIsolation.store(enabled, std::memory_order_relaxed);
#ifdef Q_OS_IOS
    setAppleVoiceIsolationEnabled(enabled);
#endif
}

bool AudioEngine::globalVoiceIsolation()
{
    ensureEnhancerSettingsLoaded();
    return s_globalVoiceIsolation.load(std::memory_order_relaxed);
}

void AudioEngine::setGlobalRxClean(bool enabled)
{
    ensureEnhancerSettingsLoaded();
    s_globalRxClean.store(enabled, std::memory_order_relaxed);
}

bool AudioEngine::globalRxClean()
{
    ensureEnhancerSettingsLoaded();
    return s_globalRxClean.load(std::memory_order_relaxed);
}

void AudioEngine::setGlobalSpkPreampDb(float db)
{
    ensureEnhancerSettingsLoaded();
    const float clamped = std::clamp(db, 0.0f, 24.0f);
    s_globalSpkPreampDb.store(clamped, std::memory_order_relaxed);
}

float AudioEngine::globalSpkPreampDb()
{
    ensureEnhancerSettingsLoaded();
    return s_globalSpkPreampDb.load(std::memory_order_relaxed);
}

AudioEngine::AudioEngine(QString in, QString out) :
    m_outputdevice(out),
    m_inputdevice(in),
    m_out(nullptr),
    m_in(nullptr),
    m_outdev(nullptr),
    m_indev(nullptr),
    m_maxlevel(0),
    m_agc(true),
    m_srm(1.0f),
    m_aout_max_buf_idx(0),
    m_aout_gain(1.0f),
    m_volume(1.0f),
    m_inGain(0.5f)
{
    ensureEnhancerSettingsLoaded();
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;
    memset(m_audio_out_temp_buf, 0, sizeof(m_audio_out_temp_buf));
    memset(m_aout_max_buf, 0, sizeof(m_aout_max_buf));
    m_aout_max_buf_p = m_aout_max_buf;
}

static inline int16_t clampToInt16(float x)
{
    if (!std::isfinite(x)) return 0;
    x = std::clamp(x, -32768.0f, 32767.0f);
    return static_cast<int16_t>(lrintf(x));
}

// Smooth algebraic pre-amp soft-saturation ceiling (linear up to 22000, then C1-continuous
// u / sqrt(1 + u^2) curve asymptotically approaching 32765). Preserves natural harmonic
// roundness without hard flat-topping or frequency filtering.
static inline float cleanPreampCeiling(float x)
{
    if (!std::isfinite(x)) return 0.0f;
    const float ax = std::fabs(x);
    constexpr float kKnee   = 22000.0f;
    constexpr float kMaxOut = 32765.0f;
    if (ax <= kKnee) {
        return x;
    }
    constexpr float kHeadroom = kMaxOut - kKnee; // 10765.0f
    const float u = (ax - kKnee) / kHeadroom;
    return std::copysign(kKnee + kHeadroom * (u / std::sqrt(1.0f + u * u)), x);
}

AudioEngine::Biquad AudioEngine::makeLowpassBiquad(float fs, float fc, float Q)
{
    AudioEngine::Biquad bq;
    if (fs <= 0.0f || fc <= 0.0f || fc >= (fs * 0.499f) || Q <= 0.0f) {
        return bq;
    }

    const float w0 = 2.0f * static_cast<float>(M_PI) * (fc / fs);
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * Q);

    const float b0 = (1.0f - cosw0) * 0.5f;
    const float b1 = (1.0f - cosw0);
    const float b2 = (1.0f - cosw0) * 0.5f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    bq.z1 = 0.0f;
    bq.z2 = 0.0f;
    return bq;
}

AudioEngine::Biquad AudioEngine::makeHighpassBiquad(float fs, float fc, float Q)
{
    AudioEngine::Biquad bq;
    if (fs <= 0.0f || fc <= 0.0f || fc >= (fs * 0.499f) || Q <= 0.0f) {
        return bq;
    }

    const float w0 = 2.0f * static_cast<float>(M_PI) * (fc / fs);
    const float cosw0 = cosf(w0);
    const float sinw0 = sinf(w0);
    const float alpha = sinw0 / (2.0f * Q);

    const float b0 = (1.0f + cosw0) * 0.5f;
    const float b1 = -(1.0f + cosw0);
    const float b2 = (1.0f + cosw0) * 0.5f;
    const float a0 = 1.0f + alpha;
    const float a1 = -2.0f * cosw0;
    const float a2 = 1.0f - alpha;

    bq.b0 = b0 / a0;
    bq.b1 = b1 / a0;
    bq.b2 = b2 / a0;
    bq.a1 = a1 / a0;
    bq.a2 = a2 / a0;
    bq.z1 = 0.0f;
    bq.z2 = 0.0f;
    return bq;
}

float AudioEngine::biquadProcess(AudioEngine::Biquad &bq, float x)
{
    const float y = bq.b0 * x + bq.z1;
    bq.z1 = bq.b1 * x - bq.a1 * y + bq.z2;
    bq.z2 = bq.b2 * x - bq.a2 * y;
    return y;
}

float AudioEngine::hermite4(float frac, float ym1, float y0, float y1, float y2)
{
    const float c0 = y0;
    const float c1 = 0.5f * (y1 - ym1);
    const float c2 = ym1 - 2.5f * y0 + 2.0f * y1 - 0.5f * y2;
    const float c3 = 0.5f * (y2 - ym1) + 1.5f * (y0 - y1);
    return ((c3 * frac + c2) * frac + c1) * frac + c0;
}

void AudioEngine::updatePlaybackReconstructionFilter(bool hdActive)
{
    m_lastPlayHdMode = hdActive;
    const float fs = static_cast<float>(std::max(8000, m_playbackDeviceRate));
    // Only Hermite images around multiples of 16 kHz (>= 12 kHz) remain after the 47-tap half-band,
    // so this stage sits at 6 kHz: it does not touch the 3-3.9 kHz consonant band (a 3.75 kHz LR4 cost
    // -4.4 dB at 3 kHz and -7 dB at 3.4 kHz, which read as "muffled").
    const float desiredCutoff = 6000.0f;
    const float fc = std::min(desiredCutoff, fs * 0.45f);
    const float z1_1 = m_playLowpass.z1;
    const float z2_1 = m_playLowpass.z2;
    const float z1_2 = m_playLowpass2.z1;
    const float z2_2 = m_playLowpass2.z2;
    m_playLowpass  = makeLowpassBiquad(fs, fc, 0.70710678f);
    m_playLowpass2 = makeLowpassBiquad(fs, fc, 0.70710678f);
    m_playLowpass.z1  = z1_1;
    m_playLowpass.z2  = z2_1;
    m_playLowpass2.z1 = z1_2;
    m_playLowpass2.z2 = z2_2;
}

AudioEngine::~AudioEngine()
{
    if (m_in) {
        m_in->disconnect(this);
    }
    if (m_out) {
        m_out->disconnect(this);
    }
    stop_capture();
    stop_playback();
    if (m_in) {
        delete m_in;
        m_in = nullptr;
    }
    if (m_out) {
        delete m_out;
        m_out = nullptr;
    }
    m_indev = nullptr;
    m_outdev = nullptr;
}

void AudioEngine::set_output_buffer_size(uint32_t b)
{
    if (m_out) {
        m_out->setBufferSize(b);
    }
}

void AudioEngine::set_input_buffer_size(uint32_t b)
{
    if (m_in) {
        // Scale buffer size proportionally when capturing at native high sample rate (e.g. 48 kHz vs 8 kHz)
        const uint32_t scaled = (m_captureDeviceRate > 8000)
                                ? std::max<uint32_t>(4096U, b * static_cast<uint32_t>(m_captureDeviceRate / 8000))
                                : std::max<uint32_t>(2048U, b);
        m_in->setBufferSize(scaled);
    }
}

void AudioEngine::set_output_volume(qreal v)
{
    m_volume = std::clamp(static_cast<float>(v), 0.0f, 2.0f);
    if (m_out) {
        m_out->setVolume(1.0);
    }
}

void AudioEngine::set_input_volume(qreal v)
{
    m_inGain = std::clamp(static_cast<float>(v), 0.0f, 2.0f);
    if (m_in) {
        m_in->setVolume(1.0);
    }
}

bool AudioEngine::matchesSelectedDevice(const QAudioDevice &dev, const QString &selected)
{
    if (selected.isEmpty() ||
        selected == "OS Default" ||
        selected == "System Default" ||
        selected == "default" ||
        selected == "defaultInputDevice" ||
        selected == "defaultOutputDevice") {
        return false;
    }
    const QString desc = dev.description();
    const QString idStr = QString::fromUtf8(dev.id());
    if (desc.compare(selected, Qt::CaseInsensitive) == 0 ||
        idStr.compare(selected, Qt::CaseInsensitive) == 0 ||
        getFriendlyName(desc).compare(selected, Qt::CaseInsensitive) == 0) {
        return true;
    }
    if (selected.contains("AirPods", Qt::CaseInsensitive) &&
        (desc.contains("airpods", Qt::CaseInsensitive) || idStr.contains("airpods", Qt::CaseInsensitive))) {
        return true;
    }
    if (selected.contains("Bluetooth", Qt::CaseInsensitive) &&
        (desc.contains("bluetooth", Qt::CaseInsensitive) || idStr.contains("bluetooth", Qt::CaseInsensitive) ||
         desc.contains("airpods", Qt::CaseInsensitive) || desc.contains("headset", Qt::CaseInsensitive))) {
        return true;
    }
    if (desc.contains(selected, Qt::CaseInsensitive)) {
        return true;
    }
    return false;
}

QStringList AudioEngine::discover_audio_devices(uint8_t d)
{
    QStringList list;
    const QList<QAudioDevice> devices = d ? QMediaDevices::audioOutputs() : QMediaDevices::audioInputs();

    for (const QAudioDevice &device : devices) {
        const QString friendly = getFriendlyName(device.description());
        if (!friendly.isEmpty() && !list.contains(friendly)) {
            list.append(friendly);
        }
    }

    return list;
}

void AudioEngine::initPlaybackSink()
{
    if (m_out != nullptr) {
        m_out->disconnect(this);
        m_reinitializingSink = true;
        stop_playback();
        m_reinitializingSink = false;
        delete m_out;
        m_out = nullptr;
        m_outdev = nullptr;
    }

    const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
    if (devices.isEmpty()) {
        qDebug() << "No audio playback hardware found";
        return;
    }

    QAudioDevice device(QMediaDevices::defaultAudioOutput());
    for (const QAudioDevice &candidateDev : devices) {
        if (matchesSelectedDevice(candidateDev, m_outputdevice)) {
            device = candidateDev;
            break;
        }
    }

    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    if (MACHAK || !device.isFormatSupported(format)) {
        QAudioFormat preferred = device.preferredFormat();
#ifdef Q_OS_IOS
        const int activeRate = static_cast<int>(lrint(getActiveAudioSampleRate()));
        if (activeRate >= 8000) {
            preferred.setSampleRate(activeRate);
        }
#endif
        QAudioFormat candidate = preferred;
        // Preserve preferred stereo channel count (2 channels on iOS/macOS) so CoreAudio RemoteIO
        // drives both Left and Right hardware speaker channels at full 0 dBFS scale without applying
        // mono-to-stereo pan-law attenuation (-3 to -6 dB).
        candidate.setChannelCount(std::max(1, preferred.channelCount()));
        candidate.setSampleFormat(QAudioFormat::Int16);
        if (device.isFormatSupported(candidate)) {
            format = candidate;
        } else {
            candidate.setChannelCount(1);
            if (device.isFormatSupported(candidate)) {
                format = candidate;
            } else {
                format = preferred;
            }
        }
    }

    m_playbackDeviceRate = (format.sampleRate() > 0) ? format.sampleRate() : 8000;
    m_playbackChannels = std::max(1, format.channelCount());
    m_playbackSampleFormat = format.sampleFormat();
    m_playStreamIndex = -1;
    m_playNextOutPos = 0.0;
    m_playPrevSample = 0.0f;
    m_playHist.fill(0.0f);
    m_playLowpass = Biquad{};
    m_playLowpass2 = Biquad{};
    updatePlaybackReconstructionFilter(s_globalHdUpscale.load(std::memory_order_relaxed));

    qDebug() << "Playback device:" << device.description()
             << "SR:" << m_playbackDeviceRate
             << "Ch:" << m_playbackChannels
             << "Fmt:" << m_playbackSampleFormat;

    m_out = new QAudioSink(device, format, this);
    // Size buffer for ~120ms at the negotiated rate to prevent underruns without adding latency
    const int bytesPerFrame = m_playbackChannels * ((m_playbackSampleFormat == QAudioFormat::Float) ? 4 : 2);
    const int targetBufBytes = std::clamp((m_playbackDeviceRate * bytesPerFrame * 12) / 100, 4096, 32768);
    m_out->setBufferSize(targetBufBytes);
    m_out->setVolume(1.0);
    m_consecutiveWriteFailures = 0;
    connect(m_out, SIGNAL(stateChanged(QAudio::State)), this, SLOT(handleStateChanged(QAudio::State)));
}

void AudioEngine::initCaptureSource()
{
    if (m_in != nullptr) {
        m_in->disconnect(this);
        stop_capture();
        delete m_in;
        m_in = nullptr;
        m_indev = nullptr;
    }

    const QList<QAudioDevice> devices = QMediaDevices::audioInputs();
    if (devices.isEmpty()) {
        qDebug() << "No audio capture hardware found";
        return;
    }

    QAudioDevice device(QMediaDevices::defaultAudioInput());
    for (const QAudioDevice &candidateDev : devices) {
        if (matchesSelectedDevice(candidateDev, m_inputdevice)) {
            device = candidateDev;
            break;
        }
    }

    const QString devDesc = device.description();
    const QString devId   = QString::fromUtf8(device.id());
    bool isExtMic = m_inputdevice.contains("AirPods", Qt::CaseInsensitive) ||
                    m_inputdevice.contains("Bluetooth", Qt::CaseInsensitive) ||
                    m_inputdevice.contains("External", Qt::CaseInsensitive) ||
                    m_inputdevice.contains("Headset", Qt::CaseInsensitive) ||
                    m_inputdevice.contains("USB", Qt::CaseInsensitive) ||
                    devDesc.contains("AirPods", Qt::CaseInsensitive) ||
                    devDesc.contains("Bluetooth", Qt::CaseInsensitive) ||
                    devDesc.contains("External", Qt::CaseInsensitive) ||
                    devDesc.contains("Headset", Qt::CaseInsensitive) ||
                    devDesc.contains("USB", Qt::CaseInsensitive) ||
                    devId.contains("airpods", Qt::CaseInsensitive) ||
                    devId.contains("bluetooth", Qt::CaseInsensitive) ||
                    devId.contains("external_microphone", Qt::CaseInsensitive) ||
                    devId.contains("headset", Qt::CaseInsensitive) ||
                    devId.contains("usb", Qt::CaseInsensitive);
#ifdef Q_OS_IOS
    if (isIOSExternalMicActive()) {
        isExtMic = true;
    }
#endif
    setExternalMicActive(isExtMic);

    QAudioFormat format;
    format.setSampleRate(8000);
    format.setChannelCount(1);
    format.setSampleFormat(QAudioFormat::Int16);

    int sr = 8000;
    if (MACHAK) {
        sr = device.preferredFormat().sampleRate();
#ifdef Q_OS_IOS
        const int activeRate = static_cast<int>(lrint(getActiveAudioSampleRate()));
        if (activeRate >= 8000) {
            sr = activeRate;
        }
#endif
        if (sr <= 0) sr = 48000;
        m_srm = static_cast<float>(sr) / 8000.0f;
    }
    format.setSampleRate(sr);
    if (!device.isFormatSupported(format)) {
        QAudioFormat candidate = device.preferredFormat();
#ifdef Q_OS_IOS
        const int activeRate = static_cast<int>(lrint(getActiveAudioSampleRate()));
        if (activeRate >= 8000) {
            candidate.setSampleRate(activeRate);
        }
#endif
        candidate.setChannelCount(1);
        candidate.setSampleFormat(QAudioFormat::Int16);
        if (device.isFormatSupported(candidate)) {
            format = candidate;
        } else {
            format = device.preferredFormat();
        }
    }

    // No QObject parent on purpose: Qt hands the source's parent to its internal Darwin object, and a
    // dead source must be detachable without the engine deleting it later. Every path deletes m_in
    // explicitly (destructor and initCaptureSource).
    m_in = new QAudioSource(device, format);
    m_captureDeviceRate = (m_in->format().sampleRate() > 0) ? m_in->format().sampleRate() : format.sampleRate();
    m_captureChannels = std::max(1, m_in->format().channelCount());
    m_captureSampleFormat = m_in->format().sampleFormat();
    m_in->setVolume(1.0);
    m_srm = (m_captureDeviceRate > 0) ? (static_cast<float>(m_captureDeviceRate) / 8000.0f) : 1.0f;

    // Initialize 8 kHz TX HD Voice clarity biquads (85 Hz HPF + +1.2 dB @ 220 Hz warmth + +1.8 dB @ 2000 Hz presence)
    m_capHighpass = makeHighpassBiquad(8000.0f, 85.0f, 0.70710678f);
    {
        // Peaking EQ: +1.2 dB @ 220 Hz, Q = 0.85 (fs = 8000 Hz)
        const float A = std::pow(10.0f, 1.2f / 40.0f);
        const float w0 = 2.0f * static_cast<float>(M_PI) * (220.0f / 8000.0f);
        const float cosw0 = std::cos(w0), sinw0 = std::sin(w0);
        const float alpha = sinw0 / (2.0f * 0.85f);
        const float a0 = 1.0f + alpha / A;
        m_capLowpass = { (1.0f + alpha * A) / a0, (-2.0f * cosw0) / a0, (1.0f - alpha * A) / a0,
                         (-2.0f * cosw0) / a0, (1.0f - alpha / A) / a0, 0.0f, 0.0f };
    }
    {
        // Peaking EQ: +1.8 dB @ 2000 Hz, Q = 0.85 (fs = 8000 Hz)
        const float A = std::pow(10.0f, 1.8f / 40.0f);
        const float w0 = 2.0f * static_cast<float>(M_PI) * (2000.0f / 8000.0f);
        const float cosw0 = std::cos(w0), sinw0 = std::sin(w0);
        const float alpha = sinw0 / (2.0f * 0.85f);
        const float a0 = 1.0f + alpha / A;
        m_capLowpass2 = { (1.0f + alpha * A) / a0, (-2.0f * cosw0) / a0, (1.0f - alpha * A) / a0,
                          (-2.0f * cosw0) / a0, (1.0f - alpha / A) / a0, 0.0f, 0.0f };
    }

    qDebug() << "Capture device:" << device.description()
             << "SR:" << m_captureDeviceRate
             << "Ch:" << m_captureChannels
             << "Fmt:" << m_captureSampleFormat
             << "resample->8k ratio:" << m_srm;
}

void AudioEngine::init()
{
#ifdef Q_OS_IOS
    prepareAudioForCapture();
#endif
    initPlaybackSink();
    initCaptureSource();
#ifdef Q_OS_IOS
    const uint32_t gen = getAudioRouteGeneration();
    m_lastPlayRouteGen = gen;
    m_lastCapRouteGen = gen;
#endif
    emit audioDeviceListChanged();
}

void AudioEngine::start_capture()
{
    m_audioinq.clear();
#ifdef Q_OS_IOS
    prepareAudioForCapture();
    const uint32_t currentGen = getAudioRouteGeneration();
    if (m_in == nullptr || currentGen != m_lastCapRouteGen) {
        initCaptureSource();
        m_lastCapRouteGen = currentGen;
    }
#else
    if (m_in == nullptr) {
        initCaptureSource();
    }
#endif
    // QAudioSource::start() resets first, which blocks forever on a source whose callbacks died.
    if (captureSourceLooksDead()) {
        reapCaptureSource();
        initCaptureSource();
    }

    if (m_in != nullptr) {
        m_lastCaptureCallbackMs = captureMonotonicMs();
#ifdef Q_OS_IOS
        m_captureIoGen = getAudioIOInvalidationGeneration();
#endif
        m_indev = m_in->start();
        if (m_indev != nullptr) {
            m_captureDeviceRate = (m_in->format().sampleRate() > 0) ? m_in->format().sampleRate() : m_captureDeviceRate;
            m_captureChannels = std::max(1, m_in->format().channelCount());
            m_captureSampleFormat = m_in->format().sampleFormat();
            m_srm = (m_captureDeviceRate > 0) ? (static_cast<float>(m_captureDeviceRate) / 8000.0f) : 1.0f;
            m_capHighpass.z1 = m_capHighpass.z2 = 0.0f;
            m_capLowpass.z1  = m_capLowpass.z2  = 0.0f;
            m_capLowpass2.z1 = m_capLowpass2.z2 = 0.0f;
            m_capNextOutPos = 0.0;
            m_capPunchInSamples = 0;
            // 12 ms raised-cosine punch-in ramp at native hardware rate (e.g. 576 samples @ 48 kHz)
            // eliminates PTT hardware startup pop with 0.00 dB effect on passband frequency response.
            m_capPunchInTotal = static_cast<size_t>(std::max(96, (std::max(8000, m_captureDeviceRate) * 12) / 1000));
            m_tx16kCount = 0;
            m_txEnhancer.reset();
            connect(m_indev, SIGNAL(readyRead()), this, SLOT(input_data_received()), Qt::UniqueConnection);
        }
    }
}

bool AudioEngine::captureSourceLooksDead() const
{
    if (m_in == nullptr || m_in->state() == QAudio::StoppedState) {
        return false;
    }
    const qint64 quietMs = captureMonotonicMs() - m_lastCaptureCallbackMs;
#if defined(Q_OS_IOS)
    // iOS stopped or destroyed our units (interruption / media services reset) since capture started.
    if (getAudioIOInvalidationGeneration() != m_captureIoGen && quietMs > 120) {
        return true;
    }
#endif
#if defined(Q_OS_IOS) || defined(Q_OS_MACOS)
    // A live input unit calls back every few ms; two seconds of silence while "running" means the
    // unit was stopped underneath Qt. Long enough that a Bluetooth route renegotiation never trips it.
    return quietMs > 2000;
#else
    // Other Qt audio backends do not block in stop()/reset().
    Q_UNUSED(quietMs);
    return false;
#endif
}

void AudioEngine::reapCaptureSource()
{
    // QDarwinAudioSource::stop(), reset() and its destructor all wait, without a timeout, on a condition
    // that only the (now dead) input callback can signal, so stopping or deleting it here would hang
    // this thread forever. It also owns a QTimer registered on this thread, so it cannot be deleted
    // from any other thread either. The safe outcome is to detach it and leave it alone: one stopped
    // audio unit per interruption, instead of a frozen app.
    if (m_indev != nullptr) {
        m_indev->disconnect(this);
    }
    QAudioSource *dead = m_in;
    m_in = nullptr;
    m_indev = nullptr;
    if (dead == nullptr) {
        return;
    }
    qWarning() << "[AudioEngine] capture source stopped calling back; detaching it instead of stopping it";
    dead->disconnect(this);
    disconnect(dead, nullptr, this, nullptr);
}

void AudioEngine::stop_capture()
{
    m_audioinq.clear();
    m_tx16kCount = 0;
    if (captureSourceLooksDead()) {
        reapCaptureSource();
    }
    if (m_in != nullptr) {
        if (m_indev != nullptr) {
            m_indev->disconnect(this);
            m_indev = nullptr;
        }
        if (m_in->state() != QAudio::StoppedState) {
            m_in->reset();
            if (m_in->state() != QAudio::StoppedState) {
                m_in->stop();
            }
        }
    }
#ifdef Q_OS_IOS
    finishAudioCapture();
#endif
}

void AudioEngine::start_playback()
{
    // QSO Replay: one over starts here. Idempotent, so the sink re-init after a route change
    // (which calls start_playback() again) never splits a clip.
    QsoReplayCapture::beginOver(QsoReplayCapture::Kind::Rx);
#ifdef Q_OS_IOS
    const int activeRate = static_cast<int>(lrint(getActiveAudioSampleRate()));
    const uint32_t currentGen = getAudioRouteGeneration();
    if (m_out == nullptr || currentGen != m_lastPlayRouteGen ||
        (activeRate >= 8000 && std::abs(activeRate - m_playbackDeviceRate) > 100)) {
        initPlaybackSink();
        m_lastPlayRouteGen = currentGen;
    }
#else
    if (m_out == nullptr) {
        initPlaybackSink();
    }
#endif

    if (m_out != nullptr) {
        if (m_outdev == nullptr || m_out->state() == QAudio::StoppedState ||
            m_out->state() == QAudio::SuspendedState || m_out->error() == QAudio::FatalError) {
            if (m_out->state() == QAudio::SuspendedState) {
                m_out->reset();
            }
            m_playStreamIndex = -1;
            m_playNextOutPos = 0.0;
            m_playPrevSample = 0.0f;
            m_playHist.fill(0.0f);
            m_playLowpass.z1 = m_playLowpass.z2 = 0.0f;
            m_playLowpass2.z1 = m_playLowpass2.z2 = 0.0f;
            m_rxEnhancer.reset();
            m_preampGain = 1.0f;
            m_aout_gain = 1.0f;
            m_agcSpeechRms = 5500.0f;
            m_agcHoldFrames = 0;
            memset(m_aout_max_buf, 0, sizeof(m_aout_max_buf));
            m_aout_max_buf_p = m_aout_max_buf;
            m_aout_max_buf_idx = 0;
            m_consecutiveWriteFailures = 0;
            m_outdev = m_out->start();
#ifdef Q_OS_IOS
            // Restore PlayAndRecord session after QDarwinAudioSink::open() resets category
            ensureSpeakerRoute();
#endif
            // Jitter prefill: prime the sink with 60 ms of silence so the hardware never starves
            // when a network packet arrives a frame or two late (each starvation was a 40-90 ms
            // digital-zero hole plus a broadband click in the measured output).
            if (m_outdev != nullptr) {
                const int prefillFrames = std::max(1, (m_playbackDeviceRate * 60) / 1000);
                const int ch = std::max(1, m_playbackChannels);
                if (m_playbackSampleFormat == QAudioFormat::Float) {
                    const QVector<float> zeros(prefillFrames * ch, 0.0f);
                    m_outdev->write(reinterpret_cast<const char *>(zeros.constData()),
                                    static_cast<qsizetype>(zeros.size() * sizeof(float)));
                } else {
                    const QVector<int16_t> zeros(prefillFrames * ch, 0);
                    m_outdev->write(reinterpret_cast<const char *>(zeros.constData()),
                                    static_cast<qsizetype>(zeros.size() * sizeof(int16_t)));
                }
            }
            qDebug() << "Playback started (SR=" << m_playbackDeviceRate << ")";
        }
    }
}

void AudioEngine::stop_playback()
{
    if (!m_reinitializingSink) {
        if (TranscriptionEngine *te = TranscriptionEngine::instance()) {
            te->markEndOfTransmission();
        }
        QsoReplayCapture::endOver(QsoReplayCapture::Kind::Rx);
    }
    if (m_out != nullptr) {
        m_outdev = nullptr;
        if (m_out->state() != QAudio::StoppedState) {
            // In Qt 6.6.1 QDarwinAudioSink:
            // - stop() enters Draining state when Active and blocks on m_drainSemaphore (500ms or infinite hang).
            // - reset() directly from Active resets m_audioBuffer BEFORE AudioOutputUnitStop, racing with renderCallback.
            // Calling suspend() first stops the CoreAudio AudioUnit immediately, and reset() then clears the ring buffer
            // and transitions to StoppedState with shouldDrain=false without ever waiting on m_drainSemaphore.
            m_out->suspend();
            m_out->reset();
        }
        qDebug() << "AudioOut state Stop Playback";
    }
}

void AudioEngine::input_data_received()
{
    if (m_indev == nullptr) {
        return;
    }
    m_lastCaptureCallbackMs = captureMonotonicMs();
    const QByteArray data = m_indev->readAll();
    if (data.isEmpty()) {
        return;
    }

    const int channels = std::max(1, m_captureChannels);
    const int bytesPerSample = (m_captureSampleFormat == QAudioFormat::Float) ? 4
                               : ((m_captureSampleFormat == QAudioFormat::UInt8) ? 1 : 2);
    const int frameBytes = channels * bytesPerSample;
    const int frameCount = static_cast<int>(data.size() / frameBytes);
    if (frameCount <= 0) {
        return;
    }

    const uint8_t *bytes = reinterpret_cast<const uint8_t *>(data.constData());

    // Read raw unattenuated 16-bit microphone sample so Natural & Neural AI noise reduction
    // operate at true 16-bit speech levels BEFORE mode m_attenuation (m_inGain) is applied.
    auto readRawMonoFrame = [&](int frameIdx) -> float {
        const uint8_t *framePtr = bytes + (frameIdx * frameBytes);
        float sum = 0.0f;
        for (int ch = 0; ch < channels; ++ch) {
            const uint8_t *p = framePtr + (ch * bytesPerSample);
            if (m_captureSampleFormat == QAudioFormat::Float) {
                float f = 0.0f;
                memcpy(&f, p, sizeof(float));
                sum += (std::isfinite(f) ? (f * 32767.0f) : 0.0f);
            } else if (m_captureSampleFormat == QAudioFormat::UInt8) {
                sum += (static_cast<int>(*p) - 128) * 256.0f;
            } else {
                const uint16_t u = static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
                sum += static_cast<float>(static_cast<int16_t>(u));
            }
        }
        return (sum / static_cast<float>(channels));
    };

    const int nrMode = s_globalNoiseMode.load(std::memory_order_relaxed);
    const bool hdOn = s_globalHdUpscale.load(std::memory_order_relaxed);
    const bool txEnhanceOn = s_globalTxEnhance.load(std::memory_order_relaxed);

    // Apply 12 ms raised-cosine PTT punch-in envelope at the very start of capture to eliminate
    // hardware ADC startup pop & screen tap click while leaving the frequency response 100% untouched.
    auto readPunchInCleanedSample = [&](int frameIdx) -> float {
        float x = readRawMonoFrame(frameIdx);
        if (m_capPunchInSamples < m_capPunchInTotal) {
            const float prog = static_cast<float>(m_capPunchInSamples) /
                               static_cast<float>(std::max<size_t>(1, m_capPunchInTotal));
            const float env = 0.5f * (1.0f - std::cos(static_cast<float>(M_PI) * std::clamp(prog, 0.0f, 1.0f)));
            ++m_capPunchInSamples;
            x *= env;
        }
        return x;
    };

    // Every capture configuration goes through the same band-limited path: linear interpolation to
    // 16 kHz, then AudioEnhancer::processTx16kFrameTo8k (200 Hz LR4 high-pass, half-band decimation,
    // optional expander / leveler / presence EQ depending on the toggles). With NR and TX enhance off the
    // enhancer is transparent apart from the high-pass and decimator. The former "direct" branch decimated
    // 48 kHz -> 8 kHz by point-sampling, which aliased all 4-24 kHz room noise into the speech band.
    {
        m_txEnhancer.setNoiseMode(static_cast<AudioEnhancer::NoiseMode>(nrMode));
        m_txEnhancer.setHdUpscaleEnabled(hdOn);
        m_txEnhancer.setMicLevelerEnabled(txEnhanceOn);

        const double step16k = (m_captureDeviceRate > 0)
                               ? (static_cast<double>(m_captureDeviceRate) / 16000.0)
                               : 1.0;
        while (m_capNextOutPos < static_cast<double>(frameCount)) {
            const int idx0 = std::clamp(static_cast<int>(m_capNextOutPos), 0, frameCount - 1);
            const int idx1 = std::min(frameCount - 1, idx0 + 1);
            const float frac = static_cast<float>(m_capNextOutPos - static_cast<double>(idx0));
            const float s0 = readPunchInCleanedSample(idx0);
            const float s1 = (idx1 != idx0) ? readRawMonoFrame(idx1) : s0;
            m_tx16kBuf[m_tx16kCount++] = s0 + frac * (s1 - s0);
            if (m_tx16kCount >= 320) {
                std::array<int16_t, 160> out8k{};
                m_txEnhancer.processTx16kFrameTo8k(m_tx16kBuf.data(), out8k.data());
                for (size_t k = 0; k < 160; ++k) {
                    // Apply nostar/DroidStar mode attenuation (m_inGain = v / m_attenuation) AFTER conditioning
                    m_audioinq.enqueue(clampToInt16(static_cast<float>(out8k[k]) * m_inGain));
                }
                m_tx16kCount = 0;
            }
            m_capNextOutPos += step16k;
        }
        m_capNextOutPos = std::max(0.0, m_capNextOutPos - static_cast<double>(frameCount));
    }

    // Cap capture queue to ~0.5 seconds (4000 samples at 8kHz) so TX never builds lag
    // without chopping multi-frame QDarwinAudioSource timer bursts.
    while (m_audioinq.size() > 4000) {
        m_audioinq.dequeue();
    }
}

void AudioEngine::write(int16_t *pcm, size_t s)
{
    m_maxlevel = 0;
    if (pcm == nullptr || s == 0) {
        return;
    }

    // Apply nostar/DroidStar RX AGC + Speaker Volume scaling (zero RX AI or EQ filtering)
    process_audio(pcm, s);

    for (size_t i = 0; i < s; ++i) {
        const uint16_t absVal = static_cast<uint16_t>(std::min<int>(32767, std::abs(static_cast<int>(pcm[i]))));
        if (absVal > m_maxlevel) {
            m_maxlevel = absVal;
        }
    }

#ifdef Q_OS_IOS
    const uint32_t currentGen = getAudioRouteGeneration();
    if (m_out == nullptr || currentGen != m_lastPlayRouteGen || m_outdev == nullptr ||
        m_out->state() == QAudio::StoppedState || m_out->state() == QAudio::SuspendedState ||
        m_out->error() == QAudio::FatalError) {
        start_playback();
    }
#else
    if (m_out == nullptr || m_outdev == nullptr ||
        m_out->state() == QAudio::StoppedState || m_out->state() == QAudio::SuspendedState ||
        m_out->error() == QAudio::FatalError) {
        start_playback();
    }
#endif
    if (m_outdev == nullptr) {
        return;
    }

    const int inRate = 8000;
    const int channels = std::max(1, m_playbackChannels);
    qsizetype bytesWritten = 0;
    qsizetype attemptedBytes = 0;

    // Band-limited playback reconstruction (fixes 4-8 kHz spectral imaging that made RX sound harsh/"digital"):
    //   1. 8 kHz -> 16 kHz with a 127-tap Kaiser polyphase interpolator (flat to 3.7 kHz, -42 dB at 4 kHz,
    //      <= -52 dB above) so even full-band codecs (Codec2, IMBE) leave no 4-4.5 kHz mirror
    //   2. 16 kHz -> device rate with 4-point Hermite (remaining images now sit >= 12 kHz)
    //   3. 4th-order Linkwitz-Riley low-pass at 6 kHz on the device-rate stream for those Hermite images
    // Measured against a clean reference, the previous unfiltered 8 kHz Hermite step left speech images
    // only ~25 dB down between 4 and 8 kHz; this chain pushes everything above 4 kHz below -68 dB for
    // every RX codec while leaving the 3-3.5 kHz consonant band within 1 dB.
    if (m_playbackDeviceRate > 0 && m_playbackDeviceRate != inRate) {
        const size_t n16 = m_rxEnhancer.processRxUpscale8kTo16k(pcm, s, m_rx16kBuf.data(), 1.0f);
        const int midRate = 16000;
        const double step = static_cast<double>(midRate) / static_cast<double>(m_playbackDeviceRate);
        const int maxFrames = static_cast<int>(std::ceil(static_cast<double>(n16) * (static_cast<double>(m_playbackDeviceRate) / midRate))) + 8;

        if (m_playStreamIndex < 0) {
            const float first = (n16 > 0) ? m_rx16kBuf[0] : 0.0f;
            m_playHist = { first, first, first, first };
            m_playStreamIndex = 0;
            m_playNextOutPos = 0.0;
        }

        auto emitDeviceSample = [&](float y) -> float {
            if (m_playbackDeviceRate > midRate) {
                y = biquadProcess(m_playLowpass, y);
                y = biquadProcess(m_playLowpass2, y);
            }
            return cleanPreampCeiling(y);
        };

        if (m_playbackSampleFormat == QAudioFormat::Float) {
            m_playOutFloat.clear();
            m_playOutFloat.reserve(maxFrames * channels);
            for (size_t n = 0; n < n16; ++n) {
                const float x = m_rx16kBuf[n];
                m_playHist[0] = m_playHist[1];
                m_playHist[1] = m_playHist[2];
                m_playHist[2] = m_playHist[3];
                m_playHist[3] = x;
                const qint64 streamIndex = m_playStreamIndex + 1;

                while (m_playNextOutPos <= static_cast<double>(streamIndex)) {
                    const float frac = static_cast<float>(m_playNextOutPos - static_cast<double>(streamIndex - 1));
                    const float y = emitDeviceSample(hermite4(frac, m_playHist[0], m_playHist[1], m_playHist[2], m_playHist[3]));
                    const float norm = std::clamp(y / 32768.0f, -1.0f, 1.0f);
                    for (int ch = 0; ch < channels; ++ch) {
                        m_playOutFloat.push_back(norm);
                    }
                    m_playNextOutPos += step;
                }
                m_playStreamIndex = streamIndex;
            }
            attemptedBytes = static_cast<qsizetype>(m_playOutFloat.size() * sizeof(float));
            bytesWritten = m_outdev->write(reinterpret_cast<const char *>(m_playOutFloat.constData()), attemptedBytes);
        } else {
            m_playOutInt16.clear();
            m_playOutInt16.reserve(maxFrames * channels);
            for (size_t n = 0; n < n16; ++n) {
                const float x = m_rx16kBuf[n];
                m_playHist[0] = m_playHist[1];
                m_playHist[1] = m_playHist[2];
                m_playHist[2] = m_playHist[3];
                m_playHist[3] = x;
                const qint64 streamIndex = m_playStreamIndex + 1;

                while (m_playNextOutPos <= static_cast<double>(streamIndex)) {
                    const float frac = static_cast<float>(m_playNextOutPos - static_cast<double>(streamIndex - 1));
                    const float y = emitDeviceSample(hermite4(frac, m_playHist[0], m_playHist[1], m_playHist[2], m_playHist[3]));
                    const int16_t sample16 = clampToInt16(y);
                    for (int ch = 0; ch < channels; ++ch) {
                        m_playOutInt16.push_back(sample16);
                    }
                    m_playNextOutPos += step;
                }
                m_playStreamIndex = streamIndex;
            }
            attemptedBytes = static_cast<qsizetype>(m_playOutInt16.size() * sizeof(int16_t));
            bytesWritten = m_outdev->write(reinterpret_cast<const char *>(m_playOutInt16.constData()), attemptedBytes);
        }
    } else {
        if (m_playbackSampleFormat == QAudioFormat::Float) {
            m_playOutFloat.clear();
            m_playOutFloat.reserve(static_cast<int>(s) * channels);
            for (size_t n = 0; n < s; ++n) {
                const float norm = std::clamp(static_cast<float>(pcm[n]) / 32768.0f, -1.0f, 1.0f);
                for (int ch = 0; ch < channels; ++ch) {
                    m_playOutFloat.push_back(norm);
                }
            }
            attemptedBytes = static_cast<qsizetype>(m_playOutFloat.size() * sizeof(float));
            bytesWritten = m_outdev->write(reinterpret_cast<const char *>(m_playOutFloat.constData()), attemptedBytes);
        } else {
            m_playOutInt16.clear();
            m_playOutInt16.reserve(static_cast<int>(s) * channels);
            for (size_t n = 0; n < s; ++n) {
                const int16_t s16 = pcm[n];
                for (int ch = 0; ch < channels; ++ch) {
                    m_playOutInt16.push_back(s16);
                }
            }
            attemptedBytes = static_cast<qsizetype>(m_playOutInt16.size() * sizeof(int16_t));
            bytesWritten = m_outdev->write(reinterpret_cast<const char *>(m_playOutInt16.constData()), attemptedBytes);
        }
    }

    if (attemptedBytes > 0 && bytesWritten < attemptedBytes) {
        qDebug() << "AudioEngine::write underrun bytesAttempted=" << attemptedBytes
                 << "bytesWritten=" << bytesWritten
                 << "bytesFree=" << (m_out ? m_out->bytesFree() : 0)
                 << "bufferSize=" << (m_out ? m_out->bufferSize() : 0);
        if (bytesWritten <= 0) {
            if (++m_consecutiveWriteFailures >= 6) {
                m_consecutiveWriteFailures = 0;
                // Exponential back-off: a dead output (media reset, device removed) must not be
                // torn down and rebuilt every 120 ms, which only burns CPU and spams the log.
                const qint64 nowMs = captureMonotonicMs();
                const qint64 holdMs = std::min<qint64>(10000, 1000LL << std::min(m_sinkRebuildCount, 4));
                if (m_sinkRebuildCount == 0 || (nowMs - m_lastSinkRebuildMs) >= holdMs) {
                    qWarning() << "AudioEngine::write detected stalled QAudioSink; reinitializing playback sink";
                    m_lastSinkRebuildMs = nowMs;
                    ++m_sinkRebuildCount;
#ifdef Q_OS_IOS
                    ensureSpeakerRoute();
#endif
                    initPlaybackSink();
                    start_playback();
                }
            }
        } else {
            m_consecutiveWriteFailures = 0;
            m_sinkRebuildCount = 0;
        }
    } else if (bytesWritten > 0) {
        m_consecutiveWriteFailures = 0;
        m_sinkRebuildCount = 0;
    }
}

uint16_t AudioEngine::read(int16_t *pcm, int s)
{
    m_maxlevel = 0;
    if (pcm == nullptr || s <= 0) {
        return 0;
    }

    if (m_audioinq.size() >= s) {
        for (int i = 0; i < s; ++i) {
            pcm[i] = m_audioinq.dequeue();
            const uint16_t absVal = static_cast<uint16_t>(std::min<int>(32767, std::abs(static_cast<int>(pcm[i]))));
            if (absVal > m_maxlevel) {
                m_maxlevel = absVal;
            }
        }
        if (QsoReplayCapture::txTapEnabled()) {
            QsoReplayCapture::pushSamples(pcm, static_cast<size_t>(s));   // own TX, post-processing
        }
        return 1;
    } else if (m_in == nullptr) {
        memset(pcm, 0, sizeof(int16_t) * s);
        return 1;
    } else {
        return 0;
    }
}

uint16_t AudioEngine::read(int16_t *pcm)
{
    if (pcm == nullptr) {
        return 0;
    }
    m_maxlevel = 0;
    const int s = (m_audioinq.size() >= 160) ? 160 : static_cast<int>(m_audioinq.size());

    for (int i = 0; i < s; ++i) {
        pcm[i] = m_audioinq.dequeue();
        const uint16_t absVal = static_cast<uint16_t>(std::min<int>(32767, std::abs(static_cast<int>(pcm[i]))));
        if (absVal > m_maxlevel) {
            m_maxlevel = absVal;
        }
    }

    if (s > 0 && QsoReplayCapture::txTapEnabled()) {
        QsoReplayCapture::pushSamples(pcm, static_cast<size_t>(s));
    }
    return static_cast<uint16_t>(s);
}

void AudioEngine::process_audio(int16_t *pcm, size_t s)
{
    if (s > 320) {
        s = 320;
    }
    float aout_abs, max, gainfactor, gaindelta, maxbuf;

    for (size_t i = 0; i < s; ++i) {
        m_audio_out_temp_buf[i] = static_cast<float>(pcm[i]);
    }

    if (m_agc) {
        // Speech-RMS AGC (replaces the former 30000-peak normalizer that drove the output into
        // continuous clipping): instant true-peak governor at -3 dBFS, gain range -12 dB .. +20 dB,
        // 0.15 dB/frame release so quiet talkers come up smoothly without audible pumping.
        // The tracker follows the louder syllables (fast attack / slow decay), so a nominal
        // 5500 RMS target measures -17.5 dBFS active-speech at the device, i.e. the same loudness
        // as the HamTalk reference recording (-17.2 dBFS) with no samples reaching full scale.
        constexpr float kTargetRms   = 5500.0f;
        constexpr float kPeakCeiling = 23200.0f;  // -3 dBFS
        constexpr float kMaxGain     = 10.0f;     // +20 dB
        constexpr float kMinGain     = 0.25f;     // -12 dB
        // 0.25 dB/frame (12.5 dB/s) release: quiet passages after a pause recover in ~150-250 ms
        // instead of ~400 ms, matching the reference app; still well below rates that pump.
        constexpr float kReleasePerFrame = 1.0292f; // +0.25 dB per 20 ms

        float sumSq = 0.0f;
        max = 0.0f;
        for (size_t i = 0; i < s; i++) {
            const float v = m_audio_out_temp_buf[i];
            sumSq += v * v;
            aout_abs = fabsf(v);
            if (aout_abs > max) {
                max = aout_abs;
            }
        }
        const float frameRms = (s > 0) ? std::sqrt(sumSq / static_cast<float>(s)) : 0.0f;

        // Track the talker's speech level only on frames that actually carry voice. Every RX decoder
        // (AMBE, IMBE, Codec2, u-law) delivers unity-scale PCM, so 80 RMS is about -52 dBFS: well below
        // any speech, above the vocoders' digital-silence floor.
        const bool speechFrame = (frameRms > 80.0f);
        if (speechFrame) {
            // Fast upward tracking (0.25) so hot talkers are caught within a syllable; 0.08 downward
            // (~240 ms) so a quieter passage after a pause recovers in ~80 ms (was 0.06 / ~420 ms)
            // while still not chasing individual syllables (measured gain wobble +0.2 dB std).
            const float a = (frameRms > m_agcSpeechRms) ? 0.25f : 0.08f;
            m_agcSpeechRms += a * (frameRms - m_agcSpeechRms);
            m_agcSpeechRms = std::clamp(m_agcSpeechRms, 50.0f, 30000.0f);
            m_agcHoldFrames = 25;
        } else if (m_agcHoldFrames > 0) {
            --m_agcHoldFrames;
        }

        float desiredGain = std::clamp(kTargetRms / std::max(50.0f, m_agcSpeechRms), kMinGain, kMaxGain);
        if (max > 1.0f) {
            desiredGain = std::min(desiredGain, kPeakCeiling / max);
        }

        const float startGain = m_aout_gain;
        float endGain;
        if (desiredGain < m_aout_gain) {
            endGain = desiredGain;                                   // instant attack on hot frames
        } else if (speechFrame || m_agcHoldFrames > 0) {
            endGain = std::min(desiredGain, m_aout_gain * kReleasePerFrame); // slow release while talking
        } else {
            endGain = m_aout_gain;                                   // freeze gain in pauses (no noise pumping)
        }
        m_aout_gain = std::clamp(endGain, kMinGain, kMaxGain);
        gaindelta = (m_aout_gain - startGain) / static_cast<float>(std::max<size_t>(1, s));

        m_audio_out_temp_buf_p = m_audio_out_temp_buf;
        for (size_t i = 0; i < s; i++) {
            *m_audio_out_temp_buf_p = (startGain + (static_cast<float>(i) * gaindelta)) * (*m_audio_out_temp_buf_p);
            m_audio_out_temp_buf_p++;
        }
    } else {
        for (size_t i = 0; i < s; i++) {
            m_audio_out_temp_buf[i] *= 7.0f;
        }
    }
    (void)gainfactor;
    (void)maxbuf;
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    TranscriptionEngine *te = TranscriptionEngine::instance();
    const bool tapForStt = (te != nullptr && te->isEnabled());
    const bool tapForReplay = QsoReplayCapture::rxTapEnabled();
    int16_t sttBuf[320];

    // 1. Tap unmodified post-AGC audio for Live Transcription and QSO Replay (100% isolated from RX Clarity & NR,
    //    Speaker Volume & Pre-Amp). One int16 conversion feeds both; each consumer is a lock-free ring write.
    if (tapForStt || tapForReplay) {
        for (size_t i = 0; i < s; i++) {
            const float normSample = std::clamp(m_audio_out_temp_buf[i], -32760.0f, 32760.0f);
            sttBuf[i] = static_cast<int16_t>(normSample);
        }
        if (tapForStt) {
            te->pushRxSamples(sttBuf, s);
        }
        if (tapForReplay) {
            QsoReplayCapture::pushSamples(sttBuf, s);
        }
    }

    // 2. Zero-Latency RX Light Audio Cleaning & Enhancement (Dynamic De-Hisser + Soft Noise Floor Expander + Light Clarity EQ)
    if (s_globalRxClean.load(std::memory_order_relaxed)) {
        m_rxEnhancer.processRxPostAgcFloatInPlace(m_audio_out_temp_buf, s);
    }

    // 3. Clean Digital Speaker Pre-Amp (Cloudlifter / Focusrite style: 0 dB to +24 dB pure linear gain)
    //    - Zero frequency filters, zero EQ, zero phase shift, 100% flat frequency response.
    //    - Uses exact 20 ms frame lookahead so quiet/average speech gets the full linear dB boost
    //      while already-hot peaks are governed cleanly without hard square-wave clipping.
    const float preampDb = s_globalSpkPreampDb.load(std::memory_order_relaxed);
    if (preampDb <= 0.05f) {
        m_preampGain = 1.0f;
        for (size_t i = 0; i < s; i++) {
            // Soft true-peak ceiling instead of hard clipping at 32760 (hard clipping generated
            // broadband distortion harmonics that sat 4-8 kHz in the measured output).
            pcm[i] = clampToInt16(cleanPreampCeiling(m_audio_out_temp_buf[i] * m_volume));
        }
        return;
    }

    const float targetPreampGain = std::pow(10.0f, std::clamp(preampDb, 0.0f, 24.0f) / 20.0f);
    float sumSq = 0.0f;
    for (size_t i = 0; i < s; i++) {
        const float v = m_audio_out_temp_buf[i] * m_volume;
        m_audio_out_temp_buf[i] = v;
        sumSq += v * v;
    }

    const float frameRms = (s > 0) ? std::sqrt(sumSq / static_cast<float>(s)) : 0.0f;
    // Allow full linear pre-amp drive across the entire 0..24 dB range (up to 48000 RMS drive into the
    // algebraic soft-knee ceiling) so every step from 0 dB -> 6 dB -> 12 dB -> 18 dB -> 24 dB produces
    // a strong, continuous increase in speaker output without square-wave clipping.
    float desiredPreamp = targetPreampGain;
    if (frameRms > 1.0f) {
        const float maxCleanRmsDrive = std::max(1.0f, 48000.0f / frameRms);
        desiredPreamp = std::min(targetPreampGain, maxCleanRmsDrive);
    }

    const float startG = m_preampGain;
    const float endG = (desiredPreamp < m_preampGain)
                       ? (0.25f * m_preampGain + 0.75f * desiredPreamp)
                       : (0.65f * m_preampGain + 0.35f * desiredPreamp);
    m_preampGain = endG;
    const float stepG = (s > 0) ? ((endG - startG) / static_cast<float>(s)) : 0.0f;

    for (size_t i = 0; i < s; i++) {
        const float g = startG + static_cast<float>(i) * stepG;
        pcm[i] = clampToInt16(cleanPreampCeiling(m_audio_out_temp_buf[i] * g));
    }
}

QString AudioEngine::getFriendlyName(const QString& deviceIdentifier) {
    if (deviceIdentifier == "default" || deviceIdentifier == "OS Default") {
        return "System Default";
    } else if (deviceIdentifier.contains("com.apple.airpods", Qt::CaseInsensitive) ||
               deviceIdentifier.contains("AirPods", Qt::CaseInsensitive)) {
        return "AirPods";
    } else if (deviceIdentifier.contains("Bluetooth", Qt::CaseInsensitive)) {
        return "Bluetooth Device";
    } else if (deviceIdentifier.contains("com.apple.avfoundation.avcapturedevice.built-in_audio")) {
        return "Built-in Microphone";
    } else if (deviceIdentifier.contains("com.apple.avfoundation.avcapturedevice.external_microphone")) {
        return "External Microphone";
    } else {
        return deviceIdentifier;
    }
}

QString AudioEngine::mapFriendlyNameToDevice(const QString &friendlyName) {
    if (friendlyName == "System Default" || friendlyName == "OS Default") {
        return "default";
    } else if (friendlyName == "AirPods") {
        return "AirPods";
    } else if (friendlyName.contains("Bluetooth", Qt::CaseInsensitive)) {
        return "Bluetooth";
    } else {
        return friendlyName;
    }
}

void AudioEngine::setOutputDevice(const QString &deviceName) {
    m_outputdevice = deviceName;
    if (m_out != nullptr) {
        const bool wasActive = (m_outdev != nullptr && m_out->state() != QAudio::StoppedState);
        m_reinitializingSink = true;
        stop_playback();
        m_reinitializingSink = false;
        initPlaybackSink();
        if (wasActive) {
            start_playback();
        }
    }
}

void AudioEngine::setInputDevice(const QString &deviceName) {
    m_inputdevice = deviceName;
    bool isExtMic = m_inputdevice.contains("AirPods", Qt::CaseInsensitive) ||
                    m_inputdevice.contains("Bluetooth", Qt::CaseInsensitive) ||
                    m_inputdevice.contains("External", Qt::CaseInsensitive) ||
                    m_inputdevice.contains("Headset", Qt::CaseInsensitive) ||
                    m_inputdevice.contains("USB", Qt::CaseInsensitive);
#ifdef Q_OS_IOS
    if (isIOSExternalMicActive()) {
        isExtMic = true;
    }
#endif
    setExternalMicActive(isExtMic);
    if (m_in != nullptr) {
        const bool wasActive = (m_indev != nullptr && m_in->state() != QAudio::StoppedState);
        stop_capture();
        initCaptureSource();
        if (wasActive) {
            start_capture();
        }
    }
}

void AudioEngine::handleStateChanged(QAudio::State newState)
{
    switch (newState) {
    case QAudio::ActiveState:
        qDebug() << "AudioOut state active";
        break;

    case QAudio::SuspendedState:
        qDebug() << "AudioOut state suspended";
        break;

    case QAudio::IdleState:
        qDebug() << "AudioOut state idle";
#ifdef Q_OS_IOS
        if (isAppInBackground()) {
            renewBackgroundTask();
        }
#endif
        break;

    case QAudio::StoppedState:
        qDebug() << "AudioOut state stopped";
        break;

    default:
        break;
    }
}
