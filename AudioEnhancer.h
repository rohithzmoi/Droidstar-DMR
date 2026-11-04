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

#ifndef AUDIOENHANCER_H
#define AUDIOENHANCER_H

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>
#include <QMutex>

class AudioEnhancer
{
public:
    enum NoiseMode : int {
        NoiseOff     = 0,
        NoiseNatural = 1, // Gentle phase-coherent dynamic de-hisser + syllabic noise floor suppressor
        NoiseNeural  = 2  // Deep adaptive vocoder de-hisser + syllabic expander (TX: bundled GTCRN neural denoiser)
    };

    enum Role : int {
        RoleRx = 0,
        RoleTx = 1
    };

    explicit AudioEnhancer(Role role = RoleRx);
    ~AudioEnhancer();

    static void warmUpBundledNeuralModel();
    static bool isNeuralModelReady();

    void reset();

    void setNoiseMode(NoiseMode mode) { m_noiseMode.store(static_cast<int>(mode), std::memory_order_relaxed); }
    NoiseMode noiseMode() const { return static_cast<NoiseMode>(m_noiseMode.load(std::memory_order_relaxed)); }

    void setHdUpscaleEnabled(bool on) { m_hdUpscale.store(on, std::memory_order_relaxed); }
    bool isHdUpscaleEnabled() const { return m_hdUpscale.load(std::memory_order_relaxed); }

    void setMicLevelerEnabled(bool on) { m_micLeveler.store(on, std::memory_order_relaxed); }
    bool isMicLevelerEnabled() const { return m_micLeveler.load(std::memory_order_relaxed); }

    // RX Zero-Latency Light Cleaning & Enhancement (8 kHz post-AGC, post-STT tap): Sub-bass
    void processRxPostAgcFloatInPlace(float *samples8k, size_t count);

    // RX Step 1 (8 kHz, 160 samples): Syllabic RMS AGC + phase-coherent
    void processRx8kInPlace(int16_t *pcm8k, size_t count, bool agcEnabled, float outputVolume = 1.0f);

    // RX Step 2 (8 kHz -> 16 kHz, 160 -> 320 samples):
    // 16-tap half-band polyphase 2x oversampler with exact chronological tap ordering.
    size_t processRxUpscale8kTo16k(const int16_t *pcm8k, size_t count8k, float *out16k, float outputVolume = 1.0f);

    // TX Pipeline (16 kHz -> 8 kHz, 320 -> 160 samples): 16 kHz mic
    void processTx16kFrameTo8k(const float *in16k, int16_t *out8k);

    static float softLimitSample(float x, float knee = 27500.0f, float limit = 32760.0f);

private:
    struct Biquad {
        float b0 { 1.0f }, b1 { 0.0f }, b2 { 0.0f };
        float a1 { 0.0f }, a2 { 0.0f };
        float z1 { 0.0f }, z2 { 0.0f };

        inline float process(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }

        inline void resetState()
        {
            z1 = 0.0f;
            z2 = 0.0f;
        }
    };

    static Biquad makeLowpass(float fs, float fc, float Q = 0.70710678f);
    static Biquad makeHighpass(float fs, float fc, float Q = 0.70710678f);
    static Biquad makeBandpass(float fs, float fc, float Q = 1.0f);
    static Biquad makePeakingEq(float fs, float fc, float Q, float gainDb);

    void initFilters();
    void upsample8kTo16k(const float *in8k, size_t count8k, float *out16k);
    void downsample16kTo8k(const float *in16k, size_t count16k, float *out8k);
    bool runTxNeuralDenoiser16k(float *samples16k, size_t count16k);
    float measureTxNeuralSpeechConfidence16k(const float *samples16k, size_t count16k, float rawRms);

    Role m_role;
    std::atomic<int> m_noiseMode { NoiseNeural };
    std::atomic<bool> m_hdUpscale { true };
    std::atomic<bool> m_micLeveler { false };

    // RX interpolation history (127-tap polyphase, 64 input samples) and 16-tap decimator
    std::array<float, 64> m_upHist{};
    std::array<float, 16> m_downHist{};

    // RX 8 kHz base rumble removal & Syllabic RMS AGC state
    Biquad m_rx8kHpf;
    float m_agcSlowRms { 2500.0f };
    float m_agcNoiseFloor { 180.0f };
    float m_agcMinWinRms { 32767.0f };
    int   m_agcWinCounter { 0 };
    float m_agcGain { 1.0f };
    int m_agcHangCounter { 0 };

    // RX Phase-Coherent Dynamic De-Hisser & Syllabic Downward Expander state (8 kHz)
    Biquad m_rxVoiceBandDetector; // 250 - 1800 Hz voiced formant detector
    Biquad m_rxHissBandDetector;  // 2500 - 3800 Hz unvoiced hiss detector
    float m_rxVoiceEnv { 300.0f };
    float m_rxHissEnv { 80.0f };
    float m_rxDeHissState { 0.0f };
    float m_rxDeHissAlpha { 1.0f };
    float m_rxExpanderGain { 1.0f };

    // RX Studio Broadcast HD Vocal EQ (100% linear, zero IMD/comb distortion)
    Biquad m_rxWarmthEq;    // +2.2 dB @ 230 Hz (restores chest warmth)
    Biquad m_rxAntiBoxEq;   // -2.0 dB @ 540 Hz (removes hollow vocoder boxiness)
    Biquad m_rxPresenceEq;  // +2.5 dB @ 2150 Hz (consonant clarity)
    Biquad m_rxSilkLpf;     // 3450 Hz smooth roll-off above vocoder harmonic band

    // TX 16 kHz Formant & Clarity Conditioner + Syllabic Mic Leveler + Noise Gate
    Biquad m_txHpf;
    Biquad m_txHpf2;
    Biquad m_txVoiceBandDetector;
    Biquad m_txHissBandDetector;
    Biquad m_txWarmthEq;
    Biquad m_txPresenceEq;
    Biquad m_txAntiAliasLpf1;
    Biquad m_txAntiAliasLpf2;
    float m_txVoiceEnv { 300.0f };
    float m_txHissEnv { 80.0f };
    float m_txDeHissAlpha { 1.0f };
    float m_txRmsEnv { 2200.0f };
    float m_txNoiseFloor { 250.0f };
    float m_txMinWinRms { 32767.0f };
    int   m_txWinCounter { 0 };
    int   m_txFrameCount { 0 };
    float m_txGain { 1.0f };
    float m_txExpanderGain { 1.0f };
    float m_txDeHissState { 0.0f };
    int m_txHangCounter { 0 };
    uint32_t m_txNoiseSeed { 0x9E3779B9u }; // comfort-floor LCG state

    // TX GTCRN neural ring buffer (100% wet when active so there
    static constexpr size_t kGtcrnRingCap = 2048;
    static constexpr size_t kGtcrnRingMask = kGtcrnRingCap - 1;
    std::array<float, kGtcrnRingCap> m_gtcrnRing{};
    size_t m_gtcrnWritePos { 0 };
    size_t m_gtcrnReadPos { 0 };
    bool m_gtcrnPrimed { false };
    int m_gtcrnSlowFrames { 0 };
    std::atomic<bool> m_gtcrnThrottled { false };

    // Asynchronous neural VAD sidechain: the capture thread hands the latest 20 ms frame to
    float runTxNeuralConfidenceSync(const float *samples16k, size_t count16k, float rawRms);
    void neuralSidechainWorker();
    void waitForNeuralWorker();
    std::atomic<float> m_neuralConfLatest { -1.0f };
    std::atomic<bool> m_neuralWorkerBusy { false };
    std::atomic<bool> m_neuralShutdown { false };
    QMutex m_neuralJobMutex;
    std::vector<float> m_neuralPendingFrame;
    float m_neuralPendingRms { 0.0f };
    bool m_neuralPendingValid { false };
};

#endif // AUDIOENHANCER_H
