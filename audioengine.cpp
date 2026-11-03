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
#include <QDebug>
#include <QSettings>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <QtMath>
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
    const float desiredCutoff = 3750.0f;
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

    m_in = new QAudioSource(device, format, this);
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

    if (m_in != nullptr) {
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

void AudioEngine::stop_capture()
{
    m_audioinq.clear();
    m_tx16kCount = 0;
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
            m_rxEnhancer.reset();
            m_preampGain = 1.0f;
            m_aout_gain = 1.0f;
            memset(m_aout_max_buf, 0, sizeof(m_aout_max_buf));
            m_aout_max_buf_p = m_aout_max_buf;
            m_aout_max_buf_idx = 0;
            m_consecutiveWriteFailures = 0;
            m_outdev = m_out->start();
#ifdef Q_OS_IOS
            // Restore PlayAndRecord session after QDarwinAudioSink::open() resets category
            ensureSpeakerRoute();
#endif
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

    if (nrMode > 0 || txEnhanceOn) {
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
                    // Apply nostar/DroidStar mode attenuation (m_inGain = v / m_attenuation) AFTER denoising
                    m_audioinq.enqueue(clampToInt16(static_cast<float>(out8k[k]) * m_inGain));
                }
                m_tx16kCount = 0;
            }
            m_capNextOutPos += step16k;
        }
        m_capNextOutPos = std::max(0.0, m_capNextOutPos - static_cast<double>(frameCount));
    } else {
        // Direct nostar/DroidStar capture resampling + optional linear TX HD Voice EQ
        const double step = (m_srm > 0.0f) ? static_cast<double>(m_srm) : 1.0;
        while (m_capNextOutPos < static_cast<double>(frameCount)) {
            const int idx = std::clamp(static_cast<int>(m_capNextOutPos), 0, frameCount - 1);
            float x = readPunchInCleanedSample(idx) * m_inGain;
            if (hdOn) {
                x = biquadProcess(m_capHighpass, x);
                x = biquadProcess(m_capLowpass, x);
                x = biquadProcess(m_capLowpass2, x);
            }
            m_audioinq.enqueue(clampToInt16(x));
            m_capNextOutPos += step;
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

    // Direct 8 kHz -> device sample rate conversion with zero filtering so RX audio is 100% faithful
    if (m_playbackDeviceRate > 0 && m_playbackDeviceRate != inRate) {
        const double step = static_cast<double>(inRate) / static_cast<double>(m_playbackDeviceRate);
        const int maxFrames = static_cast<int>(std::ceil(static_cast<double>(s) * (static_cast<double>(m_playbackDeviceRate) / inRate))) + 8;

        if (m_playStreamIndex < 0) {
            const float first = static_cast<float>(pcm[0]);
            m_playHist = { first, first, first, first };
            m_playStreamIndex = 0;
            m_playNextOutPos = 0.0;
        }

        if (m_playbackSampleFormat == QAudioFormat::Float) {
            m_playOutFloat.clear();
            m_playOutFloat.reserve(maxFrames * channels);
            for (size_t n = 0; n < s; ++n) {
                const float x = static_cast<float>(pcm[n]);
                m_playHist[0] = m_playHist[1];
                m_playHist[1] = m_playHist[2];
                m_playHist[2] = m_playHist[3];
                m_playHist[3] = x;
                const qint64 streamIndex = m_playStreamIndex + 1;

                while (m_playNextOutPos <= static_cast<double>(streamIndex)) {
                    const float frac = static_cast<float>(m_playNextOutPos - static_cast<double>(streamIndex - 1));
                    const float y = cleanPreampCeiling(hermite4(frac, m_playHist[0], m_playHist[1], m_playHist[2], m_playHist[3]));
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
            for (size_t n = 0; n < s; ++n) {
                const float x = static_cast<float>(pcm[n]);
                m_playHist[0] = m_playHist[1];
                m_playHist[1] = m_playHist[2];
                m_playHist[2] = m_playHist[3];
                m_playHist[3] = x;
                const qint64 streamIndex = m_playStreamIndex + 1;

                while (m_playNextOutPos <= static_cast<double>(streamIndex)) {
                    const float frac = static_cast<float>(m_playNextOutPos - static_cast<double>(streamIndex - 1));
                    const float y = cleanPreampCeiling(hermite4(frac, m_playHist[0], m_playHist[1], m_playHist[2], m_playHist[3]));
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
                qWarning() << "AudioEngine::write detected stalled QAudioSink; reinitializing playback sink";
                m_consecutiveWriteFailures = 0;
#ifdef Q_OS_IOS
                ensureSpeakerRoute();
#endif
                initPlaybackSink();
                start_playback();
            }
        } else {
            m_consecutiveWriteFailures = 0;
        }
    } else if (bytesWritten > 0) {
        m_consecutiveWriteFailures = 0;
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
        max = 0.0f;
        m_audio_out_temp_buf_p = m_audio_out_temp_buf;

        for (size_t i = 0; i < s; i++) {
            aout_abs = fabsf(*m_audio_out_temp_buf_p);
            if (aout_abs > max) {
                max = aout_abs;
            }
            m_audio_out_temp_buf_p++;
        }

        // Only record active voice frames into the 25-frame AGC history so silent lead-in
        // frames at the start of a transmission never ramp AGC gain up to 50x (+34 dB).
        if (max > 80.0f) {
            *m_aout_max_buf_p = max;
            m_aout_max_buf_p++;
            m_aout_max_buf_idx++;

            if (m_aout_max_buf_idx > 24) {
                m_aout_max_buf_idx = 0;
                m_aout_max_buf_p = m_aout_max_buf;
            }
        }

        for (size_t i = 0; i < 25; i++) {
            maxbuf = m_aout_max_buf[i];
            if (maxbuf > max) {
                max = maxbuf;
            }
        }

        if (max > 80.0f) {
            gainfactor = (static_cast<float>(30000) / max);
            if (gainfactor > static_cast<float>(30)) {
                gainfactor = static_cast<float>(30);
            }
        } else {
            gainfactor = 1.0f;
        }

        if (gainfactor < m_aout_gain) {
            m_aout_gain = gainfactor;
            gaindelta = static_cast<float>(0);
        } else {
            if (gainfactor > static_cast<float>(30)) {
                gainfactor = static_cast<float>(30);
            }
            gaindelta = gainfactor - m_aout_gain;
            if (gaindelta > (static_cast<float>(0.05) * m_aout_gain)) {
                gaindelta = (static_cast<float>(0.05) * m_aout_gain);
            }
        }

        gaindelta /= static_cast<float>(s);

        m_audio_out_temp_buf_p = m_audio_out_temp_buf;
        for (size_t i = 0; i < s; i++) {
            *m_audio_out_temp_buf_p = (m_aout_gain + (static_cast<float>(i) * gaindelta)) * (*m_audio_out_temp_buf_p);
            m_audio_out_temp_buf_p++;
        }

        m_aout_gain += (static_cast<float>(s) * gaindelta);
    } else {
        for (size_t i = 0; i < s; i++) {
            m_audio_out_temp_buf[i] *= 7.0f;
        }
    }
    m_audio_out_temp_buf_p = m_audio_out_temp_buf;

    TranscriptionEngine *te = TranscriptionEngine::instance();
    const bool tapForStt = (te != nullptr && te->isEnabled());
    int16_t sttBuf[320];

    // 1. Tap unmodified post-AGC audio for Live Transcription (100% isolated from RX Clarity & NR, Speaker Volume & Pre-Amp)
    if (tapForStt) {
        for (size_t i = 0; i < s; i++) {
            const float normSample = std::clamp(m_audio_out_temp_buf[i], -32760.0f, 32760.0f);
            sttBuf[i] = static_cast<int16_t>(normSample);
        }
        te->pushRxSamples(sttBuf, s);
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
            float sample = m_audio_out_temp_buf[i] * m_volume;
            if (sample > 32760.0f) {
                sample = 32760.0f;
            } else if (sample < -32760.0f) {
                sample = -32760.0f;
            }
            pcm[i] = static_cast<int16_t>(sample);
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
