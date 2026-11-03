/*
    Copyright (C) 2024-2026 Rohith Namboothiri

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

#include "AudioEnhancer.h"

#if defined(Q_OS_IOS) && !defined(Q_OS_IOS_APP_EXTENSION)
#include "third_party/sherpa-onnx/include/sherpa-onnx/c-api/c-api.h"
#define DS_ENHANCER_HAS_SHERPA 1
#else
#define DS_ENHANCER_HAS_SHERPA 0
#endif

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QStandardPaths>
#include <QThreadPool>
#include <QDebug>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>

namespace {

// 16-tap maximally-flat Lagrange half-band polyphase FIR coefficients (-77.5 dB THD+N at 1 kHz)
// 2 * (kHalfBand0 + kHalfBand1 + kHalfBand2 + kHalfBand3) == 1.0f
constexpr float kHalfBand0 = -5.0f / 2048.0f;
constexpr float kHalfBand1 =  49.0f / 2048.0f;
constexpr float kHalfBand2 = -245.0f / 2048.0f;
constexpr float kHalfBand3 =  1225.0f / 2048.0f;

#if DS_ENHANCER_HAS_SHERPA
struct BundledGtcrnState {
    QMutex mutex;
    std::atomic<bool> loading { false };
    std::atomic<bool> ready { false };
    std::atomic<bool> failed { false };
    std::atomic<bool> deviceTooSlow { false };
    const SherpaOnnxOnlineSpeechDenoiser *txDenoiser { nullptr };
    QMutex txCallMutex;
    int32_t frameShift { 256 };

    ~BundledGtcrnState()
    {
        if (txDenoiser) {
            SherpaOnnxDestroyOnlineSpeechDenoiser(txDenoiser);
            txDenoiser = nullptr;
        }
    }
};

BundledGtcrnState &gtcrnState()
{
    static BundledGtcrnState s_state;
    return s_state;
}

QString extractBundledGtcrnModel()
{
    const QString baseDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString modelsDir = baseDir + QStringLiteral("/models");
    QDir dir(modelsDir);
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }

    const QString dstPath = modelsDir + QStringLiteral("/gtcrn_simple.onnx");
    QFile qrcFile(QStringLiteral(":/DroidStar/third_party/sherpa-onnx/models/gtcrn_simple.onnx"));
    if (!qrcFile.exists()) {
        const QFileInfo localFile(QStringLiteral("third_party/sherpa-onnx/models/gtcrn_simple.onnx"));
        if (localFile.exists() && localFile.size() > 100000) {
            return localFile.absoluteFilePath();
        }
        return QString();
    }

    const QFileInfo dstInfo(dstPath);
    if (!dstInfo.exists() || dstInfo.size() != qrcFile.size()) {
        const QString tmpPath = dstPath + QStringLiteral(".tmp");
        if (QFile::exists(tmpPath)) {
            QFile::remove(tmpPath);
        }
        if (qrcFile.open(QIODevice::ReadOnly)) {
            QFile out(tmpPath);
            if (out.open(QIODevice::WriteOnly)) {
                out.write(qrcFile.readAll());
                out.flush();
                out.close();
                if (QFile::exists(dstPath)) {
                    QFile::remove(dstPath);
                }
                QFile::rename(tmpPath, dstPath);
            }
            qrcFile.close();
        }
    }

    const QFileInfo finalInfo(dstPath);
    return (finalInfo.exists() && finalInfo.size() > 100000) ? dstPath : QString();
}
#endif

} // namespace

void AudioEnhancer::warmUpBundledNeuralModel()
{
#if DS_ENHANCER_HAS_SHERPA
    BundledGtcrnState &st = gtcrnState();
    if (st.ready.load(std::memory_order_acquire) ||
        st.failed.load(std::memory_order_relaxed) ||
        st.loading.exchange(true, std::memory_order_acq_rel)) {
        return;
    }

    QThreadPool::globalInstance()->start([]() {
        BundledGtcrnState &s = gtcrnState();
        const QString modelPath = extractBundledGtcrnModel();
        if (modelPath.isEmpty()) {
            s.failed.store(true, std::memory_order_relaxed);
            s.loading.store(false, std::memory_order_release);
            return;
        }

        const QByteArray modelUtf8 = modelPath.toUtf8();
        SherpaOnnxOnlineSpeechDenoiserConfig cfg;
        std::memset(&cfg, 0, sizeof(cfg));
        cfg.model.gtcrn.model = modelUtf8.constData();
        cfg.model.num_threads = 1;
        cfg.model.debug = 0;
        cfg.model.provider = "cpu";

        const SherpaOnnxOnlineSpeechDenoiser *tx = SherpaOnnxCreateOnlineSpeechDenoiser(&cfg);
        if (!tx) {
            s.failed.store(true, std::memory_order_relaxed);
            s.loading.store(false, std::memory_order_release);
            return;
        }

        {
            QMutexLocker lock(&s.mutex);
            s.txDenoiser = tx;
            const int32_t shift = SherpaOnnxOnlineSpeechDenoiserGetFrameShiftInSamples(tx);
            s.frameShift = (shift > 0 && shift <= 512) ? shift : 256;
            s.ready.store(true, std::memory_order_release);
        }
        s.loading.store(false, std::memory_order_release);
        qInfo() << "[AudioEnhancer] Bundled GTCRN neural speech denoiser ready (hop=" << s.frameShift << "samples @ 16kHz)";
    });
#endif
}

bool AudioEnhancer::isNeuralModelReady()
{
#if DS_ENHANCER_HAS_SHERPA
    return gtcrnState().ready.load(std::memory_order_acquire);
#else
    return false;
#endif
}

AudioEnhancer::AudioEnhancer(Role role)
    : m_role(role)
{
    initFilters();
    reset();
    if (m_role == RoleTx) {
        warmUpBundledNeuralModel();
    }
}

AudioEnhancer::~AudioEnhancer() = default;

AudioEnhancer::Biquad AudioEnhancer::makeLowpass(float fs, float fc, float Q)
{
    Biquad bq;
    if (fs <= 0.0f || fc <= 0.0f || fc >= (fs * 0.495f) || Q <= 0.0f) {
        return bq;
    }
    const float w0 = 2.0f * static_cast<float>(M_PI) * (fc / fs);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * Q);
    const float a0 = 1.0f + alpha;

    bq.b0 = ((1.0f - cosw0) * 0.5f) / a0;
    bq.b1 = (1.0f - cosw0) / a0;
    bq.b2 = bq.b0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha) / a0;
    return bq;
}

AudioEnhancer::Biquad AudioEnhancer::makeHighpass(float fs, float fc, float Q)
{
    Biquad bq;
    if (fs <= 0.0f || fc <= 0.0f || fc >= (fs * 0.495f) || Q <= 0.0f) {
        return bq;
    }
    const float w0 = 2.0f * static_cast<float>(M_PI) * (fc / fs);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * Q);
    const float a0 = 1.0f + alpha;

    bq.b0 = ((1.0f + cosw0) * 0.5f) / a0;
    bq.b1 = -(1.0f + cosw0) / a0;
    bq.b2 = bq.b0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha) / a0;
    return bq;
}

AudioEnhancer::Biquad AudioEnhancer::makeBandpass(float fs, float fc, float Q)
{
    Biquad bq;
    if (fs <= 0.0f || fc <= 0.0f || fc >= (fs * 0.495f) || Q <= 0.0f) {
        return bq;
    }
    const float w0 = 2.0f * static_cast<float>(M_PI) * (fc / fs);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * Q);
    const float a0 = 1.0f + alpha;

    bq.b0 = alpha / a0;
    bq.b1 = 0.0f;
    bq.b2 = -alpha / a0;
    bq.a1 = (-2.0f * cosw0) / a0;
    bq.a2 = (1.0f - alpha) / a0;
    return bq;
}

AudioEnhancer::Biquad AudioEnhancer::makePeakingEq(float fs, float fc, float Q, float gainDb)
{
    Biquad bq;
    if (fs <= 0.0f || fc <= 0.0f || fc >= (fs * 0.495f) || Q <= 0.0f) {
        return bq;
    }
    const float A = std::pow(10.0f, gainDb / 40.0f);
    const float w0 = 2.0f * static_cast<float>(M_PI) * (fc / fs);
    const float cosw0 = std::cos(w0);
    const float sinw0 = std::sin(w0);
    const float alpha = sinw0 / (2.0f * Q);
    const float a0 = 1.0f + alpha / A;

    bq.b0 = (1.0f + alpha * A) / a0;
    bq.b1 = (-2.0f * cosw0) / a0;
    bq.b2 = (1.0f - alpha * A) / a0;
    bq.a1 = bq.b1;
    bq.a2 = (1.0f - alpha / A) / a0;
    return bq;
}

float AudioEnhancer::softLimitSample(float x, float knee, float limit)
{
    if (!std::isfinite(x)) {
        return 0.0f;
    }
    const float ax = std::fabs(x);
    if (ax <= knee) {
        return x;
    }
    const float headroom = std::max(1.0f, limit - knee);
    const float compressed = knee + headroom * std::tanh((ax - knee) / headroom);
    return std::copysign(compressed, x);
}

void AudioEnhancer::initFilters()
{
    // RX 8 kHz base sub-bass DC/rumble filter (85 Hz Butterworth — preserves male fundamental voice pitch >= 95 Hz)
    m_rx8kHpf = makeHighpass(8000.0f, 85.0f, 0.70710678f);

    // Sidechain energy detectors for RX Dynamic De-Hisser & Soft Syllabic Expander (never subtracted from signal!)
    m_rxVoiceBandDetector = makeBandpass(8000.0f, 750.0f, 0.70f);
    m_rxHissBandDetector  = makeHighpass(8000.0f, 2600.0f, 0.70710678f);

    // RX Vocal Clarity EQ (100% linear biquads, zero low-pass muffling, zero IMD/comb distortion)
    m_rxWarmthEq   = makePeakingEq(8000.0f, 210.0f, 0.80f, 2.2f);   // Vocal chest warmth
    m_rxAntiBoxEq  = makePeakingEq(8000.0f, 520.0f, 1.15f, -3.0f);  // Removes hollow boxy vocoder resonance
    m_rxPresenceEq = makePeakingEq(8000.0f, 2150.0f, 0.85f, 3.6f);  // Crisp consonant & callsign articulation
    m_rxSilkLpf    = makeLowpass(8000.0f, 3450.0f, 0.70710678f);    // Unused in light RX path (kept for ABI compatibility)

    // TX 8 kHz / 16 kHz Formant & Clarity Conditioner:
    // - 85 Hz sub-sonic rumble high-pass filter (removes cooling fan hum & desk thumps while preserving male F0)
    // - Sidechain voice (700 Hz) vs hiss (>2500 Hz) energy detectors for Natural VAD & noise gating
    // - 8 kHz HD Voice clarity contour (+1.2 dB @ 220 Hz chest warmth + +1.8 dB @ 2000 Hz vocal articulation)
    m_txHpf               = makeHighpass(8000.0f, 85.0f, 0.70710678f);
    m_txHpf2              = makeHighpass(16000.0f, 85.0f, 0.70710678f);
    m_txVoiceBandDetector = makeBandpass(8000.0f, 700.0f, 0.65f);
    m_txHissBandDetector  = makeHighpass(8000.0f, 2500.0f, 0.70710678f);
    m_txWarmthEq          = makePeakingEq(8000.0f, 220.0f, 0.85f, 1.2f);
    m_txPresenceEq        = makePeakingEq(8000.0f, 2000.0f, 0.85f, 1.8f);
    m_txAntiAliasLpf1     = makeLowpass(16000.0f, 3750.0f, 0.70710678f);
    m_txAntiAliasLpf2     = makeLowpass(16000.0f, 3750.0f, 0.70710678f);
}

void AudioEnhancer::reset()
{
    m_upHist.fill(0.0f);
    m_downHist.fill(0.0f);

    m_rx8kHpf.resetState();
    m_agcSlowRms = 2500.0f;
    m_agcNoiseFloor = 550.0f;
    m_agcMinWinRms = 32767.0f;
    m_agcWinCounter = 0;
    m_agcGain = 1.0f;
    m_agcHangCounter = 0;

    m_rxVoiceBandDetector.resetState();
    m_rxHissBandDetector.resetState();
    m_rxVoiceEnv = 300.0f;
    m_rxHissEnv = 80.0f;
    m_rxDeHissState = 0.0f;
    m_rxDeHissAlpha = 1.0f;
    m_rxExpanderGain = 1.0f;

    m_rxWarmthEq.resetState();
    m_rxAntiBoxEq.resetState();
    m_rxPresenceEq.resetState();
    m_rxSilkLpf.resetState();

    m_txHpf.resetState();
    m_txHpf2.resetState();
    m_txVoiceBandDetector.resetState();
    m_txHissBandDetector.resetState();
    m_txWarmthEq.resetState();
    m_txPresenceEq.resetState();
    m_txAntiAliasLpf1.resetState();
    m_txAntiAliasLpf2.resetState();
    m_txVoiceEnv = 300.0f;
    m_txHissEnv = 80.0f;
    m_txDeHissAlpha = 1.0f;
    m_txRmsEnv = 2200.0f;
    m_txNoiseFloor = 250.0f;
    m_txMinWinRms = 32767.0f;
    m_txWinCounter = 0;
    m_txFrameCount = 0;
    m_txGain = 1.0f;
    m_txExpanderGain = 0.0f;
    m_txDeHissState = 0.0f;
    m_txHangCounter = 0;

    m_gtcrnRing.fill(0.0f);
    m_gtcrnWritePos = 0;
    m_gtcrnReadPos = 0;
    m_gtcrnPrimed = false;
    m_gtcrnSlowFrames = 0;
    m_gtcrnThrottled = false;

#if DS_ENHANCER_HAS_SHERPA
    BundledGtcrnState &st = gtcrnState();
    if (m_role == RoleTx && st.ready.load(std::memory_order_acquire) && st.txDenoiser) {
        QMutexLocker lock(&st.txCallMutex);
        SherpaOnnxOnlineSpeechDenoiserReset(st.txDenoiser);
    }
#endif
}

void AudioEnhancer::upsample8kTo16k(const float *in8k, size_t count8k, float *out16k)
{
    for (size_t i = 0; i < count8k; ++i) {
        // Shift 8-sample history: m_upHist[0] is x[n], m_upHist[3] is x[n-3], m_upHist[4] is x[n-4]
        for (size_t k = 7; k > 0; --k) {
            m_upHist[k] = m_upHist[k - 1];
        }
        m_upHist[0] = in8k[i];

        // Polyphase branch 0 (earlier in time, t = n - 3.5): midpoint between m_upHist[4] (n-4) and m_upHist[3] (n-3)
        const float midpoint = kHalfBand0 * (m_upHist[0] + m_upHist[7])
                             + kHalfBand1 * (m_upHist[1] + m_upHist[6])
                             + kHalfBand2 * (m_upHist[2] + m_upHist[5])
                             + kHalfBand3 * (m_upHist[3] + m_upHist[4]);
        out16k[2 * i] = midpoint;

        // Polyphase branch 1 (later in time, t = n - 3.0): exact integer delay tap m_upHist[3]
        out16k[2 * i + 1] = m_upHist[3];
    }
}

void AudioEnhancer::downsample16kTo8k(const float *in16k, size_t count16k, float *out8k)
{
    const size_t pairs = count16k / 2;
    for (size_t i = 0; i < pairs; ++i) {
        for (size_t k = 15; k >= 2; --k) {
            m_downHist[k] = m_downHist[k - 2];
        }
        m_downHist[1] = in16k[2 * i];
        m_downHist[0] = in16k[2 * i + 1];

        const float center = m_downHist[7];
        const float side = kHalfBand0 * (m_downHist[0] + m_downHist[14])
                         + kHalfBand1 * (m_downHist[2] + m_downHist[12])
                         + kHalfBand2 * (m_downHist[4] + m_downHist[10])
                         + kHalfBand3 * (m_downHist[6] + m_downHist[8]);
        out8k[i] = 0.5f * (center + side);
    }
}

void AudioEnhancer::processRxPostAgcFloatInPlace(float *samples8k, size_t count)
{
    if (!samples8k || count == 0) {
        return;
    }

    const size_t n = std::min<size_t>(count, 320);
    std::array<float, 320> work8k{};

    float sumSq = 0.0f;
    float framePeak = 0.0f;
    float voiceSq = 0.0f;
    float hissSq = 0.0f;

    // 1. 85 Hz sub-bass rumble/hum HPF + sidechain voiced vs. unvoiced hiss energy measurement
    for (size_t i = 0; i < n; ++i) {
        const float xIn = std::isfinite(samples8k[i]) ? samples8k[i] : 0.0f;
        const float xHp = m_rx8kHpf.process(xIn);
        work8k[i] = xHp;

        const float vBand = m_rxVoiceBandDetector.process(xHp);
        const float hBand = m_rxHissBandDetector.process(xHp);

        sumSq   += xHp * xHp;
        voiceSq += vBand * vBand;
        hissSq  += hBand * hBand;

        const float ax = std::fabs(xHp);
        if (ax > framePeak) {
            framePeak = ax;
        }
    }

    if (framePeak < 8.0f) {
        m_rxDeHissState = 0.0f;
        return;
    }

    const float invN = 1.0f / static_cast<float>(n);
    const float frameRms = std::sqrt(sumSq * invN);
    const float voiceRms = std::sqrt(voiceSq * invN);
    const float hissRms  = std::sqrt(hissSq * invN);

    m_rxVoiceEnv += ((voiceRms > m_rxVoiceEnv) ? 0.35f : 0.10f) * (voiceRms - m_rxVoiceEnv);
    m_rxHissEnv  += ((hissRms  > m_rxHissEnv)  ? 0.35f : 0.12f) * (hissRms  - m_rxHissEnv);

    const float vhRatio = m_rxVoiceEnv / (m_rxHissEnv + 25.0f);

    // Noise-like frame: weak voiced-formant dominance. Only such frames may teach the noise floor, so a long
    // continuous over with no pauses can never drag the floor up into the speech level.
    const bool noiseLike = (vhRatio < 1.25f);

    // 2. Adaptive post-AGC noise floor tracker (rolling 600 ms minimum-statistics window of noise-like frames)
    if (noiseLike && frameRms > 25.0f && frameRms < m_agcMinWinRms) {
        m_agcMinWinRms = frameRms;
    }
    if (++m_agcWinCounter >= 30) {
        if (m_agcMinWinRms < 2800.0f) {
            m_agcNoiseFloor = 0.45f * m_agcNoiseFloor + 0.55f * (m_agcMinWinRms * 1.08f);
        }
        m_agcMinWinRms = 32767.0f;
        m_agcWinCounter = 0;
    }

    if (frameRms > 25.0f && frameRms < m_agcNoiseFloor) {
        m_agcNoiseFloor = 0.70f * m_agcNoiseFloor + 0.30f * frameRms;
    } else if (frameRms < 2200.0f && vhRatio < 1.15f) {
        m_agcNoiseFloor = 0.85f * m_agcNoiseFloor + 0.15f * frameRms;
    } else if (frameRms < m_agcNoiseFloor * 1.65f && noiseLike) {
        m_agcNoiseFloor = 0.94f * m_agcNoiseFloor + 0.06f * frameRms;
    }

    // Hard ceiling relative to the tracked speech level: the floor can never exceed 40% of the talker's
    // running speech RMS, so the expander thresholds always stay well below the voice.
    const float floorCeiling = std::clamp(m_agcSlowRms * 0.40f, 300.0f, 2000.0f);
    m_agcNoiseFloor = std::clamp(m_agcNoiseFloor, 120.0f, floorCeiling);

    // 3. Syllabic speech activity detector with 140 ms (7-frame) tail hang
    const float openThresh  = std::max(550.0f, m_agcNoiseFloor * 1.85f);
    const float closeThresh = std::max(380.0f, m_agcNoiseFloor * 1.35f);

    bool speechActive = false;
    if ((frameRms >= openThresh && vhRatio >= 0.85f) ||
        frameRms >= openThresh * 1.45f ||
        (framePeak >= openThresh * 2.80f && vhRatio >= 0.90f) ||
        (frameRms >= 1200.0f && vhRatio >= 1.60f) ||   // clearly voiced content at a modest level
        frameRms >= 3200.0f) {                          // absolute fail-safe: post-AGC level this high is always speech
        speechActive = true;
        m_agcHangCounter = 7;
    } else if (m_agcHangCounter > 0) {
        --m_agcHangCounter;
        speechActive = (frameRms >= closeThresh);
    }

    // Running speech level (used only to bound the noise floor above)
    if (speechActive) {
        m_agcSlowRms += ((frameRms > m_agcSlowRms) ? 0.15f : 0.04f) * (frameRms - m_agcSlowRms);
        m_agcSlowRms = std::clamp(m_agcSlowRms, 800.0f, 24000.0f);
    }

    // 4. Syllabic Downward Expander (-13 dB = 0.22x floor between words; still never a hard gate)
    constexpr float kMinExpFloor = 0.22f;
    float targetExp = 1.0f;
    if (!speechActive) {
        const float floorCeil = m_agcNoiseFloor * 1.12f;
        const float ratio = std::clamp((frameRms - floorCeil) / std::max(100.0f, openThresh - floorCeil), 0.0f, 1.0f);
        targetExp = kMinExpFloor + (1.0f - kMinExpFloor) * (ratio * ratio);
    }

    // Attack: frame stats are known up-front, so a detected word onset opens fully within the first 4 ms
    // (32 samples) of the frame instead of ramping across all 20 ms. Release: ~80 ms to avoid pumping.
    const float startExp = m_rxExpanderGain;
    float endExp;
    size_t attackLen = n;
    if (speechActive && targetExp > m_rxExpanderGain) {
        endExp = targetExp;
        attackLen = std::min<size_t>(32, n);
    } else if (targetExp > m_rxExpanderGain) {
        endExp = 0.20f * m_rxExpanderGain + 0.80f * targetExp;
    } else {
        endExp = 0.72f * m_rxExpanderGain + 0.28f * targetExp;
    }
    m_rxExpanderGain = endExp;
    const float stepExp = (endExp - startExp) / static_cast<float>(attackLen);

    // 5. Phase-Coherent Dynamic High-Shelf De-Hisser (> 2.0 kHz)
    //    Uses exact identity y[n] = alpha * x[n] + (1 - alpha) * lp[n]:
    //    - During clear speech & sibilant consonants (alpha -> 0.96): 0 dB flat, 100% open top end.
    //    - During unvoiced vocoder quantization hiss / noisy tails (alpha -> 0.24): smooth -10 dB high-shelf.
    const float snrNorm = std::clamp((frameRms - m_agcNoiseFloor * 1.15f) / std::max(450.0f, m_agcNoiseFloor * 2.4f), 0.0f, 1.0f);
    const float vhNorm  = std::clamp((vhRatio - 0.65f) / 1.65f, 0.0f, 1.0f);
    const float clarityFactor = speechActive ? std::max(vhNorm, snrNorm * 0.85f) : (vhNorm * 0.25f);
    const float minAlpha = speechActive ? 0.50f : 0.24f;
    constexpr float kMaxAlpha = 0.96f;
    const float targetAlpha = std::clamp(minAlpha + (kMaxAlpha - minAlpha) * clarityFactor, minAlpha, kMaxAlpha);

    const float startAlpha = m_rxDeHissAlpha;
    const float endAlpha = (targetAlpha > m_rxDeHissAlpha)
                           ? (0.35f * m_rxDeHissAlpha + 0.65f * targetAlpha)
                           : (0.72f * m_rxDeHissAlpha + 0.28f * targetAlpha);
    m_rxDeHissAlpha = endAlpha;
    const float stepAlpha = (endAlpha - startAlpha) * invN;

    // 6. Sample-by-sample zero-latency processing: Dynamic De-Hisser + Light Vocal Clarity EQ + Soft Expander
    for (size_t i = 0; i < n; ++i) {
        const float x = work8k[i];
        m_rxDeHissState += 0.36f * (x - m_rxDeHissState);

        const float alpha = startAlpha + static_cast<float>(i) * stepAlpha;
        float y = alpha * x + (1.0f - alpha) * m_rxDeHissState;

        // RX vocal clarity contour (+2.2 dB @ 210 Hz, -3.0 dB @ 520 Hz, +3.6 dB @ 2150 Hz)
        y = m_rxWarmthEq.process(y);
        y = m_rxAntiBoxEq.process(y);
        y = m_rxPresenceEq.process(y);

        const float gExp = (i < attackLen) ? (startExp + static_cast<float>(i) * stepExp) : endExp;
        samples8k[i] = y * gExp;
    }

    // 7. Long-over robustness: flush denormals in every recursive state (prevents CPU spikes after long quiet
    //    tails) and recover instantly if any state ever becomes non-finite (output falls back to clean pass-through).
    auto flush = [](float &v) {
        if (!std::isfinite(v)) { v = 0.0f; return false; }
        if (std::fabs(v) < 1e-12f) { v = 0.0f; }
        return true;
    };
    bool healthy = true;
    for (Biquad *bq : { &m_rx8kHpf, &m_rxVoiceBandDetector, &m_rxHissBandDetector,
                        &m_rxWarmthEq, &m_rxAntiBoxEq, &m_rxPresenceEq }) {
        healthy &= flush(bq->z1);
        healthy &= flush(bq->z2);
    }
    healthy &= flush(m_rxDeHissState);
    if (!healthy || !std::isfinite(m_rxVoiceEnv) || !std::isfinite(m_rxHissEnv) ||
        !std::isfinite(m_agcNoiseFloor) || !std::isfinite(m_agcSlowRms) ||
        !std::isfinite(m_rxExpanderGain) || !std::isfinite(m_rxDeHissAlpha)) {
        m_rxVoiceEnv = 300.0f;
        m_rxHissEnv = 80.0f;
        m_agcNoiseFloor = 550.0f;
        m_agcSlowRms = 2500.0f;
        m_agcMinWinRms = 32767.0f;
        m_agcWinCounter = 0;
        m_agcHangCounter = 0;
        m_rxExpanderGain = 1.0f;
        m_rxDeHissAlpha = 1.0f;
        m_rxDeHissState = 0.0f;
        for (size_t i = 0; i < n; ++i) {
            if (!std::isfinite(samples8k[i])) {
                samples8k[i] = 0.0f;
            }
        }
    }
}

void AudioEnhancer::processRx8kInPlace(int16_t *pcm8k, size_t count, bool agcEnabled, float outputVolume)
{
    if (!pcm8k || count == 0) {
        return;
    }

    // RX is kept 100% true-to-source (no RX noise reduction, no RX EQ filters, no RX gating)
    // so operators hear the exact unaltered signal transmitted by others and on Parrot Echo Tests.
    if (!agcEnabled && std::fabs(outputVolume - 1.0f) < 1e-4f) {
        return;
    }

    const size_t n = std::min<size_t>(count, 320);
    float sumSq = 0.0f;
    float framePeak = 0.0f;

    for (size_t i = 0; i < n; ++i) {
        const float x = static_cast<float>(pcm8k[i]);
        sumSq += x * x;
        const float ax = std::fabs(x);
        if (ax > framePeak) {
            framePeak = ax;
        }
    }

    const float frameRms = std::sqrt(sumSq / static_cast<float>(n));

    float startGain = 1.0f;
    float endGain = 1.0f;
    if (agcEnabled) {
        if (frameRms < m_agcMinWinRms) {
            m_agcMinWinRms = frameRms;
        }
        if (++m_agcWinCounter >= 35) {
            if (m_agcMinWinRms < 1800.0f) {
                m_agcNoiseFloor = 0.50f * m_agcNoiseFloor + 0.50f * (m_agcMinWinRms * 1.10f);
            }
            m_agcMinWinRms = frameRms;
            m_agcWinCounter = 0;
        }

        if (frameRms < m_agcNoiseFloor) {
            m_agcNoiseFloor = 0.80f * m_agcNoiseFloor + 0.20f * frameRms;
        } else if (frameRms < m_agcNoiseFloor * 1.85f) {
            m_agcNoiseFloor = 0.97f * m_agcNoiseFloor + 0.03f * frameRms;
        }
        m_agcNoiseFloor = std::clamp(m_agcNoiseFloor, 60.0f, 1600.0f);

        const float openThresh = std::max(380.0f, m_agcNoiseFloor * 2.15f);
        const float closeThresh = std::max(240.0f, m_agcNoiseFloor * 1.50f);

        bool speechActive = false;
        if (frameRms >= openThresh || framePeak >= openThresh * 2.6f) {
            speechActive = true;
            m_agcHangCounter = 8;
        } else if (m_agcHangCounter > 0) {
            --m_agcHangCounter;
            speechActive = (frameRms >= closeThresh);
        }

        if (speechActive) {
            const float rmsAlpha = (frameRms > m_agcSlowRms) ? 0.20f : 0.06f;
            m_agcSlowRms += rmsAlpha * (frameRms - m_agcSlowRms);
            m_agcSlowRms = std::clamp(m_agcSlowRms, 400.0f, 24000.0f);
        }

        constexpr float kTargetRms  = 9500.0f;
        constexpr float kTargetPeak = 28500.0f;
        constexpr float kMaxBoost   = 4.5f;

        float desiredGain = m_agcGain;
        if (speechActive) {
            const float rmsGain = kTargetRms / std::max(350.0f, m_agcSlowRms);
            const float peakGain = (framePeak > 1.0f) ? (kTargetPeak / framePeak) : kMaxBoost;
            desiredGain = std::clamp(std::min(rmsGain, peakGain), 0.25f, kMaxBoost);
        } else {
            if (framePeak > kTargetPeak) {
                desiredGain = kTargetPeak / framePeak;
            } else {
                desiredGain = 0.96f * m_agcGain + 0.04f * std::min(m_agcGain, 1.35f);
            }
        }

        startGain = m_agcGain;
        if (desiredGain < m_agcGain) {
            endGain = 0.25f * m_agcGain + 0.75f * desiredGain;
        } else {
            endGain = 0.92f * m_agcGain + 0.08f * desiredGain;
        }
        m_agcGain = endGain;
    }

    const float totalStart = startGain * outputVolume;
    const float totalEnd   = endGain * outputVolume;
    const float gainStep   = (totalEnd - totalStart) / static_cast<float>(n);

    for (size_t i = 0; i < n; ++i) {
        const float g = totalStart + static_cast<float>(i) * gainStep;
        pcm8k[i] = static_cast<int16_t>(lrintf(softLimitSample(static_cast<float>(pcm8k[i]) * g)));
    }
}

size_t AudioEnhancer::processRxUpscale8kTo16k(const int16_t *pcm8k, size_t count8k, float *out16k, float outputVolume)
{
    if (!pcm8k || !out16k || count8k == 0) {
        return 0;
    }

    const size_t n8 = std::min<size_t>(count8k, 320);
    std::array<float, 320> in8kFloat{};
    for (size_t i = 0; i < n8; ++i) {
        in8kFloat[i] = static_cast<float>(pcm8k[i]) * outputVolume;
    }

    const size_t n16 = n8 * 2;
    upsample8kTo16k(in8kFloat.data(), n8, out16k);

    for (size_t i = 0; i < n16; ++i) {
        out16k[i] = softLimitSample(out16k[i]);
    }
    return n16;
}

bool AudioEnhancer::runTxNeuralDenoiser16k(float *samples16k, size_t count16k)
{
#if DS_ENHANCER_HAS_SHERPA
    if (m_role != RoleTx || m_gtcrnThrottled || count16k == 0) {
        return false;
    }
    BundledGtcrnState &st = gtcrnState();
    if (!st.ready.load(std::memory_order_acquire) || !st.txDenoiser) {
        return false;
    }

    std::array<float, 640> normIn{};
    const size_t n = std::min<size_t>(count16k, normIn.size());
    for (size_t i = 0; i < n; ++i) {
        normIn[i] = std::clamp(samples16k[i] * (1.0f / 32768.0f), -1.0f, 1.0f);
    }

    if (!m_gtcrnPrimed) {
        m_gtcrnWritePos = 0;
        m_gtcrnReadPos = 0;
        // Pre-warm GTCRN internal STFT window with 256 zero samples and prime ring with 320 samples
        // so the 320-vs-256 hop cycle never underflows on any frame.
        std::array<float, 256> zeroWarm{};
        {
            QMutexLocker lock(&st.txCallMutex);
            const SherpaOnnxDenoisedAudio *warmOut =
                SherpaOnnxOnlineSpeechDenoiserRun(st.txDenoiser, zeroWarm.data(), 256, 16000);
            if (warmOut) {
                SherpaOnnxDestroyDenoisedAudio(warmOut);
            }
        }
        for (size_t i = 0; i < 320; ++i) {
            m_gtcrnRing[m_gtcrnWritePos & kGtcrnRingMask] = 0.0f;
            ++m_gtcrnWritePos;
        }
        m_gtcrnPrimed = true;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const SherpaOnnxDenoisedAudio *denoised = nullptr;
    {
        QMutexLocker lock(&st.txCallMutex);
        denoised = SherpaOnnxOnlineSpeechDenoiserRun(st.txDenoiser, normIn.data(), static_cast<int32_t>(n), 16000);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    if (elapsedUs > 16000) {
        if (++m_gtcrnSlowFrames >= 5) {
            m_gtcrnThrottled = true;
        }
    } else {
        m_gtcrnSlowFrames = 0;
    }

    if (denoised) {
        if (denoised->samples && denoised->n > 0) {
            for (int32_t i = 0; i < denoised->n; ++i) {
                if ((m_gtcrnWritePos - m_gtcrnReadPos) < kGtcrnRingCap) {
                    m_gtcrnRing[m_gtcrnWritePos & kGtcrnRingMask] = denoised->samples[i] * 32768.0f;
                    ++m_gtcrnWritePos;
                }
            }
        }
        SherpaOnnxDestroyDenoisedAudio(denoised);
    }

    if ((m_gtcrnWritePos - m_gtcrnReadPos) < n) {
        return false;
    }

    // 100% phase-coherent output (never mix delayed clean with undelayed dry!)
    for (size_t i = 0; i < n; ++i) {
        samples16k[i] = m_gtcrnRing[m_gtcrnReadPos & kGtcrnRingMask];
        ++m_gtcrnReadPos;
    }
    return true;
#else
    (void)samples16k;
    (void)count16k;
    return false;
#endif
}

float AudioEnhancer::measureTxNeuralSpeechConfidence16k(const float *samples16k, size_t count16k, float rawRms)
{
#if DS_ENHANCER_HAS_SHERPA
    if (m_role != RoleTx || m_gtcrnThrottled || count16k == 0) {
        return -1.0f;
    }
    BundledGtcrnState &st = gtcrnState();
    if (!st.ready.load(std::memory_order_acquire) || !st.txDenoiser) {
        return -1.0f;
    }

    std::array<float, 640> normIn{};
    const size_t n = std::min<size_t>(count16k, normIn.size());
    for (size_t i = 0; i < n; ++i) {
        normIn[i] = std::clamp(samples16k[i] * (1.0f / 32768.0f), -1.0f, 1.0f);
    }

    if (!m_gtcrnPrimed) {
        std::array<float, 256> zeroWarm{};
        {
            QMutexLocker lock(&st.txCallMutex);
            const SherpaOnnxDenoisedAudio *warmOut =
                SherpaOnnxOnlineSpeechDenoiserRun(st.txDenoiser, zeroWarm.data(), 256, 16000);
            if (warmOut) {
                SherpaOnnxDestroyDenoisedAudio(warmOut);
            }
        }
        m_gtcrnPrimed = true;
    }

    const auto t0 = std::chrono::steady_clock::now();
    const SherpaOnnxDenoisedAudio *denoised = nullptr;
    {
        QMutexLocker lock(&st.txCallMutex);
        denoised = SherpaOnnxOnlineSpeechDenoiserRun(st.txDenoiser, normIn.data(), static_cast<int32_t>(n), 16000);
    }
    const auto t1 = std::chrono::steady_clock::now();
    const auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    if (elapsedUs > 16000) {
        if (++m_gtcrnSlowFrames >= 5) {
            m_gtcrnThrottled = true;
        }
    } else {
        m_gtcrnSlowFrames = 0;
    }

    float denRms = 0.0f;
    bool valid = false;
    if (denoised) {
        if (denoised->samples && denoised->n > 0) {
            double sq = 0.0;
            for (int32_t i = 0; i < denoised->n; ++i) {
                const double v = static_cast<double>(denoised->samples[i]) * 32768.0;
                sq += v * v;
            }
            denRms = static_cast<float>(std::sqrt(sq / static_cast<double>(denoised->n)));
            valid = true;
        }
        SherpaOnnxDestroyDenoisedAudio(denoised);
    }

    if (!valid) {
        return -1.0f;
    }
    // Return neural speech-to-total ratio in [0.0 .. 1.0], scaled to zero when denoised output is below mic floor
    if (denRms < 95.0f) {
        return 0.0f;
    }
    return std::clamp(denRms / std::max(120.0f, rawRms), 0.0f, 1.0f);
#else
    (void)samples16k;
    (void)count16k;
    (void)rawRms;
    return -1.0f;
#endif
}

void AudioEnhancer::processTx16kFrameTo8k(const float *in16k, int16_t *out8k)
{
    if (!in16k || !out8k) {
        return;
    }

    const NoiseMode mode = noiseMode();
    const bool hdOn = isHdUpscaleEnabled();
    const bool levelerOn = isMicLevelerEnabled();

    // 1. Form clean 8 kHz frame via 2-sample anti-alias average (preserving 100% linear phase for IMBE/AMBE!)
    //    Never replace the waveform with STFT-warped neural output or steep low-pass filters, because
    //    imbe_vocoder's v_uv_det() requires intact time-domain harmonic phase and full 3.8 kHz upper-formant
    //    energy to classify speech as Voiced (Vl=1) rather than Unvoiced white noise (Vl=0).
    std::array<float, 320> hp16k{};
    float raw16kSq = 0.0f;
    for (size_t i = 0; i < 320; ++i) {
        const float s = m_txHpf2.process(in16k[i]);
        hp16k[i] = s;
        raw16kSq += s * s;
    }
    const float raw16kRms = std::sqrt(raw16kSq / 320.0f);

    std::array<float, 160> work8k{};
    float sumSq = 0.0f;
    float peak = 0.0f;
    float voiceSq = 0.0f;
    float hissSq = 0.0f;

    for (size_t i = 0; i < 160; ++i) {
        float x = 0.5f * (in16k[2 * i] + in16k[2 * i + 1]);
        x = m_txHpf.process(x);

        const float vBand = m_txVoiceBandDetector.process(x);
        const float hBand = m_txHissBandDetector.process(x);
        const float vAbs  = std::fabs(vBand);
        const float hAbs  = std::fabs(hBand);
        m_txVoiceEnv += ((vAbs > m_txVoiceEnv) ? 0.12f : 0.025f) * (vAbs - m_txVoiceEnv);
        m_txHissEnv  += ((hAbs > m_txHissEnv)  ? 0.12f : 0.025f) * (hAbs - m_txHissEnv);
        voiceSq += vBand * vBand;
        hissSq  += hBand * hBand;

        if (hdOn) {
            // Phase-clean 8 kHz HD Voice contour (+1.2 dB @ 220 Hz warmth + +1.8 dB @ 2000 Hz articulation)
            x = m_txWarmthEq.process(x);
            x = m_txPresenceEq.process(x);
        }

        work8k[i] = x;
        sumSq += x * x;
        const float ax = std::fabs(x);
        if (ax > peak) {
            peak = ax;
        }
    }

    const float rms      = std::sqrt(sumSq / 160.0f);
    const float voiceRms = std::sqrt(voiceSq / 160.0f);
    const float hissRms  = std::sqrt(hissSq / 160.0f);
    const float vhRatio  = voiceRms / (hissRms + 10.0f);
    const float crest    = peak / (rms + 1.0f);

    // 2. Non-deadlocking Adaptive Noise Floor Tracker (tracks ambient room/fan noise up to 3500 RMS)
    ++m_txFrameCount;
    if (rms < m_txMinWinRms) {
        m_txMinWinRms = rms;
    }
    if (++m_txWinCounter >= 30) {
        if (m_txMinWinRms < 3500.0f) {
            m_txNoiseFloor = 0.40f * m_txNoiseFloor + 0.60f * (m_txMinWinRms * 1.12f);
        }
        m_txMinWinRms = rms;
        m_txWinCounter = 0;
    }

    if (m_txFrameCount <= 3 && rms < 3000.0f && crest < 3.6f) {
        m_txNoiseFloor = (m_txFrameCount == 1) ? rms : (0.50f * m_txNoiseFloor + 0.50f * rms);
    } else if (rms < m_txNoiseFloor) {
        m_txNoiseFloor = 0.75f * m_txNoiseFloor + 0.25f * rms;
    } else if (vhRatio < 1.35f && crest < 3.2f && rms < 3500.0f) {
        m_txNoiseFloor = 0.88f * m_txNoiseFloor + 0.12f * rms;
    } else if (rms < m_txNoiseFloor * 1.85f) {
        m_txNoiseFloor = 0.95f * m_txNoiseFloor + 0.05f * rms;
    }
    m_txNoiseFloor = std::clamp(m_txNoiseFloor, 60.0f, 3200.0f);

    // 3. Neural AI (GTCRN sidechain) & Natural VAD Decision
    float neuralConf = -1.0f;
    if (mode == NoiseNeural) {
        neuralConf = measureTxNeuralSpeechConfidence16k(hp16k.data(), 320, raw16kRms);
    }

    const float gateOpen  = std::max(280.0f, m_txNoiseFloor * 2.15f);
    const float gateClose = std::max(190.0f, m_txNoiseFloor * 1.50f);

    bool voiceActive = false;
    if (neuralConf >= 0.0f) {
        // Neural AI mode: require both GTCRN neural speech confidence and acoustic voice-band energy
        if (neuralConf >= 0.38f && rms >= std::max(220.0f, m_txNoiseFloor * 1.65f) && vhRatio >= 1.10f) {
            voiceActive = true;
            m_txHangCounter = 5; // 100 ms syllable tail hang time
        } else if (m_txHangCounter > 0) {
            --m_txHangCounter;
            voiceActive = (neuralConf >= 0.24f && rms >= gateClose);
            if (!voiceActive) {
                m_txHangCounter = 0;
            }
        }
    } else {
        // Natural mode (or fallback if GTCRN is still warming up)
        if (rms >= gateOpen && vhRatio >= 1.25f) {
            voiceActive = true;
            m_txHangCounter = 5;
        } else if (m_txHangCounter > 0) {
            --m_txHangCounter;
            voiceActive = (rms >= gateClose && vhRatio >= 1.05f);
            if (!voiceActive) {
                m_txHangCounter = 0;
            }
        }
    }

    // Smooth raised-cosine gate attack/release (4 ms attack on word onset so initial consonant is never
    // clipped or stepped; smooth 20 ms release into non-speech pauses to prevent click at gate close).
    float targetExp = 1.0f;
    if (!voiceActive && (mode != NoiseOff || levelerOn)) {
        targetExp = 0.0f;
    }
    const float startExp = m_txExpanderGain;
    const float endExp   = (targetExp >= 0.5f) ? 1.0f : 0.0f;
    m_txExpanderGain = endExp;

    if (startExp <= 0.001f && endExp <= 0.001f) {
        m_txAntiAliasLpf1.z1 = 0.0f;
        m_txAntiAliasLpf1.z2 = 0.0f;
        m_txAntiAliasLpf2.z1 = 0.0f;
        m_txAntiAliasLpf2.z2 = 0.0f;
        std::memset(out8k, 0, 160 * sizeof(int16_t));
        return;
    }

    float desiredGain = 1.0f;
    if (levelerOn && voiceActive) {
        m_txRmsEnv += ((rms > m_txRmsEnv) ? 0.20f : 0.06f) * (rms - m_txRmsEnv);
        m_txRmsEnv = std::clamp(m_txRmsEnv, 600.0f, 16000.0f);
        constexpr float kTxTargetRms = 3200.0f;
        constexpr float kTxMaxPeak   = 18000.0f;
        const float snrMaxBoost = std::clamp(rms / std::max(200.0f, m_txNoiseFloor * 1.8f), 1.0f, 1.6f);
        const float gRms = std::clamp(kTxTargetRms / m_txRmsEnv, 0.60f, snrMaxBoost);
        const float gPeak = (peak > 1.0f) ? (kTxMaxPeak / peak) : snrMaxBoost;
        desiredGain = std::min(gRms, gPeak);
    }

    const float startGain = m_txGain;
    const float endGain = levelerOn
                          ? ((desiredGain < m_txGain) ? (0.30f * m_txGain + 0.70f * desiredGain)
                                                      : (0.88f * m_txGain + 0.12f * desiredGain))
                          : 1.0f;
    m_txGain = endGain;

    const size_t expRampLen = (endExp > startExp) ? 32U : 160U;
    const float stepGain = (endGain - startGain) / 160.0f;

    for (size_t i = 0; i < 160; ++i) {
        float gExp = endExp;
        if (startExp != endExp && i < expRampLen) {
            const float prog = static_cast<float>(i) / static_cast<float>(expRampLen);
            const float rc = 0.5f * (1.0f - std::cos(static_cast<float>(M_PI) * prog));
            gExp = startExp + (endExp - startExp) * rc;
        }
        const float gLvl = startGain + static_cast<float>(i) * stepGain;
        const float y = work8k[i] * (gLvl * gExp);
        out8k[i] = static_cast<int16_t>(lrintf(softLimitSample(y, 26000.0f, 32000.0f)));
    }
}
