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

#include "TranscriptionEngine.h"
#include "third_party/whisper/whisper.h"
#include "mode.h"

#if defined(Q_OS_DARWIN)
#include <TargetConditionals.h>
#include <pthread.h>
#include <pthread/qos.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

#if defined(Q_OS_IOS) && !defined(Q_OS_IOS_APP_EXTENSION)
#include "AudioSessionManager.h"
#include "ios_speech_recognizer.h"
#include "third_party/sherpa-onnx/include/sherpa-onnx/c-api/c-api.h"
#include <QGuiApplication>
#define DS_HAS_APPLE_SPEECH 1
#define DS_HAS_SHERPA_ONNX 1
#elif defined(Q_OS_IOS)
#include "AudioSessionManager.h"
#define DS_HAS_APPLE_SPEECH 0
#define DS_HAS_SHERPA_ONNX 0
#else
#define DS_HAS_APPLE_SPEECH 0
#define DS_HAS_SHERPA_ONNX 0
#endif

#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QHash>
#include <QVariantMap>
#include <QDebug>
#include <chrono>
#include <cmath>
#include <algorithm>

static TranscriptionEngine *s_instance = nullptr;

namespace {

constexpr int kAutoClearDelayMs = 4000;
constexpr qint64 kAppleFinalGraceMs = 1500;
constexpr qint64 kTurnSilenceTimeoutMs = 700;
constexpr size_t kLiveStepSamples = 4800;              // 300 ms of new 16 kHz audio between live passes
constexpr size_t kWhisperMinLiveSamples = 9600;        // 0.6 s before first live pass
constexpr size_t kPauseCommitMinSamples = 48000;       // 3.0 s: commit & slice window on a natural speech pause
constexpr size_t kWhisperSoftWindowSamples = 67200;    // 4.2 s soft window for Whisper
constexpr size_t kWhisperHardWindowSamples = 96000;    // 6.0 s hard window for Whisper
constexpr size_t kWhisperCommitGuardSamples = 14400;   // 0.9 s guard for Whisper
constexpr size_t kParakeetSoftWindowSamples = 57600;   // 3.6 s soft window for Parakeet TDT v3
constexpr size_t kParakeetHardWindowSamples = 88000;   // 5.5 s hard window for Parakeet TDT v3
constexpr size_t kParakeetCommitGuardSamples = 12800;  // 0.8 s guard for Parakeet TDT v3
[[maybe_unused]] constexpr size_t kMaxOverAudioSamples = 16000 * 180;    // whole-over buffer for the end-of-over accurate pass
[[maybe_unused]] constexpr size_t kAccurateChunkMaxSamples = 16000 * 25; // stays inside Whisper's 30 s window
[[maybe_unused]] constexpr size_t kAccurateChunkMinCutSamples = 16000 * 18;

QString wordMatchKey(const QString &w)
{
    QString k;
    k.reserve(w.size());
    for (const QChar c : w) {
        if (c.isLetterOrNumber()) {
            k += c.toLower();
        }
    }
    return k;
}

// LocalAgreement-2 stabilizer: locks words at the start of the active window once confirmed by 2 consecutive passes,
// and prevents a noisy trailing frame or dropped leading segment from erasing or rewriting already-confirmed words on screen.
QString stabilizeWindowTranscript(const QString &normWindow,
                                  QStringList &prevWindowWords,
                                  QStringList &lockedWindowWords,
                                  QString &lastWindowDisplay)
{
    const QStringList curWords = normWindow.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (curWords.isEmpty()) {
        return lastWindowDisplay;
    }

    QStringList mergedWords;

    // 1. Check if `curWords` starts with `lockedWindowWords`
    bool lockedPrefixIntact = true;
    for (int i = 0; i < lockedWindowWords.size(); ++i) {
        if (i >= curWords.size() || wordMatchKey(curWords[i]) != wordMatchKey(lockedWindowWords[i])) {
            lockedPrefixIntact = false;
            break;
        }
    }

    if (lockedPrefixIntact) {
        mergedWords = lockedWindowWords;
        for (int i = static_cast<int>(lockedWindowWords.size()); i < curWords.size(); ++i) {
            mergedWords.push_back(curWords[i]);
        }
    } else {
        // 2. Check if `curWords` is a suffix continuation of `prevWindowWords` (i.e. the model dropped the leading clause)
        int suffixMatchPos = -1;
        if (curWords.size() >= 2 && prevWindowWords.size() >= 2) {
            const QString c0 = wordMatchKey(curWords[0]);
            const QString c1 = wordMatchKey(curWords[1]);
            for (int k = 1; k + 1 < prevWindowWords.size(); ++k) {
                if (wordMatchKey(prevWindowWords[k]) == c0 &&
                    wordMatchKey(prevWindowWords[k + 1]) == c1) {
                    suffixMatchPos = k;
                    break;
                }
            }
        }

        if (suffixMatchPos > 0) {
            mergedWords = prevWindowWords.mid(0, suffixMatchPos);
            mergedWords.append(curWords);
        } else if (!lockedWindowWords.isEmpty()) {
            // 3. Look for the end of `lockedWindowWords` inside `curWords`
            const QString anchorLast = wordMatchKey(lockedWindowWords.last());
            const QString anchorPrev = lockedWindowWords.size() >= 2
                                       ? wordMatchKey(lockedWindowWords[lockedWindowWords.size() - 2])
                                       : QString();
            int anchorIdx = -1;
            for (int i = 0; i < curWords.size(); ++i) {
                if (wordMatchKey(curWords[i]) == anchorLast) {
                    if (anchorPrev.isEmpty() || (i > 0 && wordMatchKey(curWords[i - 1]) == anchorPrev)) {
                        anchorIdx = i;
                        break;
                    }
                }
            }
            if (anchorIdx >= 0) {
                mergedWords = lockedWindowWords;
                for (int i = anchorIdx + 1; i < curWords.size(); ++i) {
                    mergedWords.push_back(curWords[i]);
                }
            } else if (curWords.size() < lockedWindowWords.size()) {
                // Shorter glitch pass: preserve existing display so confirmed text never vanishes
                return lastWindowDisplay;
            } else {
                // Keep locked prefix and use new tail words after it
                mergedWords = lockedWindowWords;
                for (int i = static_cast<int>(lockedWindowWords.size()); i < curWords.size(); ++i) {
                    mergedWords.push_back(curWords[i]);
                }
            }
        } else {
            mergedWords = curWords;
        }
    }

    // 4. Promote newly agreed words into `lockedWindowWords` (LocalAgreement-2 across consecutive passes).
    // Leave at least the last word uncommitted so a word still being spoken ("seventy..." -> "73") can complete.
    const int maxLockable = std::max(0, static_cast<int>(mergedWords.size()) - 1);
    while (lockedWindowWords.size() < maxLockable &&
           lockedWindowWords.size() < prevWindowWords.size()) {
        const int idx = static_cast<int>(lockedWindowWords.size());
        if (wordMatchKey(mergedWords[idx]) == wordMatchKey(prevWindowWords[idx]) &&
            !wordMatchKey(mergedWords[idx]).isEmpty()) {
            lockedWindowWords.push_back(mergedWords[idx]);
        } else {
            break;
        }
    }

    prevWindowWords = mergedWords;
    lastWindowDisplay = mergedWords.join(QLatin1Char(' '));
    return lastWindowDisplay;
}

// Finds the sample index with the lowest 80 ms RMS energy inside [minSample, maxSample] so window cuts happen in silence
size_t findQuietestSampleOffset(const std::vector<float> &audio16k, size_t minSample, size_t maxSample, double *outMinRms = nullptr)
{
    constexpr size_t kFrame = 1280; // 80 ms at 16 kHz
    constexpr size_t kStep  = 320;  // 20 ms hop
    if (audio16k.size() < kFrame || minSample >= maxSample || minSample + kFrame > audio16k.size()) {
        if (outMinRms) *outMinRms = 1.0;
        return std::min(minSample, audio16k.size());
    }
    const size_t endLimit = std::min(maxSample, audio16k.size() - kFrame);
    size_t bestCenter = minSample + kFrame / 2;
    double bestRms = 1e9;

    for (size_t pos = minSample; pos <= endLimit; pos += kStep) {
        double sum = 0.0;
        const float *p = audio16k.data() + pos;
        for (size_t i = 0; i < kFrame; ++i) {
            sum += static_cast<double>(p[i]) * static_cast<double>(p[i]);
        }
        const double rms = std::sqrt(sum / static_cast<double>(kFrame));
        if (rms < bestRms) {
            bestRms = rms;
            bestCenter = pos + kFrame / 2;
        }
    }
    if (outMinRms) *outMinRms = bestRms;
    return bestCenter;
}

qint64 monotonicMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch()).count();
}

// ITU-style amateur callsign: 1-3 character prefix containing a letter, a region digit, 1-4 letter suffix
bool isCallsignShaped(const QString &s)
{
    static const QRegularExpression re(QStringLiteral(R"(^(?=[A-Z0-9]{0,2}[A-Z])[A-Z0-9]{1,3}[0-9][A-Z]{1,4}$)"));
    return re.match(s).hasMatch();
}

QString canonicalCallsign(const QString &raw)
{
    QString c = raw.trimmed().toUpper();
    const qsizetype space = c.indexOf(QLatin1Char(' '));
    if (space > 0) {
        c.truncate(space);
    }
    const qsizetype slash = c.indexOf(QLatin1Char('/'));
    if (slash > 0) {
        c.truncate(slash);
    }
    QString out;
    for (QChar ch : c) {
        if ((ch >= QLatin1Char('A') && ch <= QLatin1Char('Z')) || ch.isDigit()) {
            out += ch;
        }
    }
    return isCallsignShaped(out) ? out : QString();
}

QString natoSpelling(const QString &call)
{
    static const char *const kLetters[26] = {
        "Alpha", "Bravo", "Charlie", "Delta", "Echo", "Foxtrot", "Golf", "Hotel", "India", "Juliett",
        "Kilo", "Lima", "Mike", "November", "Oscar", "Papa", "Quebec", "Romeo", "Sierra", "Tango",
        "Uniform", "Victor", "Whiskey", "Xray", "Yankee", "Zulu"
    };
    static const char *const kDigits[10] = {
        "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine"
    };
    QStringList parts;
    for (QChar c : call) {
        const char ch = c.toLatin1();
        if (ch >= 'A' && ch <= 'Z') {
            parts << QLatin1String(kLetters[ch - 'A']);
        } else if (ch >= '0' && ch <= '9') {
            parts << QLatin1String(kDigits[ch - '0']);
        }
    }
    return parts.join(QLatin1Char(' '));
}

// Letters whose spoken names collide on narrowband radio audio; a substitution outside its group is not a mishearing
int callsignLetterGroup(QChar c)
{
    switch (c.toLatin1()) {
    case 'B': case 'C': case 'D': case 'E': case 'G': case 'P': case 'T': case 'V': case 'Z': return 1;
    case 'M': case 'N': return 2;
    case 'F': case 'S': case 'X': return 3;
    case 'A': case 'H': case 'J': case 'K': return 4;
    case 'I': case 'Y': return 5;
    case 'Q': case 'U': return 6;
    default: return 0;
    }
}

constexpr int kCallsignEditForbidden = 99;

int confusableSubstitutions(const QString &a, const QString &b)
{
    int cost = 0;
    for (qsizetype i = 0; i < a.size(); ++i) {
        if (a[i] == b[i]) {
            continue;
        }
        const int g = callsignLetterGroup(a[i]);
        if (g == 0 || g != callsignLetterGroup(b[i])) {
            return kCallsignEditForbidden;
        }
        ++cost;
    }
    return cost;
}

// Confusable substitutions plus at most one dropped or extra character; a delete+insert pair would otherwise
// turn any letter into any other ("VU3LVU" must never become "VU3LVO")
int callsignEditCost(const QString &a, const QString &b)
{
    if (a.size() == b.size()) {
        return confusableSubstitutions(a, b);
    }
    const QString &longer = a.size() > b.size() ? a : b;
    const QString &shorter = a.size() > b.size() ? b : a;
    if (longer.size() - shorter.size() != 1) {
        return kCallsignEditForbidden;
    }
    int best = kCallsignEditForbidden;
    for (qsizetype i = 0; i < longer.size(); ++i) {
        QString cut = longer;
        cut.remove(i, 1);
        best = std::min(best, 1 + confusableSubstitutions(cut, shorter));
    }
    return best;
}

QString digitsOf(const QString &s)
{
    QString d;
    for (QChar c : s) {
        if (c.isDigit()) {
            d += c;
        }
    }
    return d;
}

// Replaces a near-miss callsign token ("WB2EHC", "B2EHG") with the station the DMR metadata says is on frequency.
// Region digits must match exactly; letters may only drift within their confusion group.
[[maybe_unused]] QString snapToKnownCallsigns(const QString &text, const QStringList &known)
{
    if (text.isEmpty() || known.isEmpty()) {
        return text;
    }
    QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    bool changed = false;
    for (QString &w : words) {
        int start = 0;
        int end = static_cast<int>(w.size());
        while (start < end && !w[start].isLetterOrNumber()) {
            ++start;
        }
        while (end > start && !w[end - 1].isLetterOrNumber()) {
            --end;
        }
        const QString core = w.mid(start, end - start);
        if (core.size() < 3 || core.size() > 8 || known.contains(core)) {
            continue;
        }
        int letters = 0;
        bool plain = true;
        for (QChar c : core) {
            if (c >= QLatin1Char('A') && c <= QLatin1Char('Z')) {
                ++letters;
            } else if (!c.isDigit()) {
                plain = false;
                break;
            }
        }
        const QString coreDigits = digitsOf(core);
        if (!plain || letters < 2 || coreDigits.isEmpty()) {
            continue;
        }
        QString best;
        int bestCost = 99;
        for (const QString &k : known) {
            if (std::abs(k.size() - core.size()) > 1 || digitsOf(k) != coreDigits) {
                continue;
            }
            const int maxCost = (k.size() >= 6) ? 2 : 1;
            const int cost = callsignEditCost(core, k);
            if (cost <= maxCost && cost < bestCost) {
                best = k;
                bestCost = cost;
            }
        }
        if (!best.isEmpty()) {
            w = w.left(start) + best + w.mid(end);
            changed = true;
        }
    }
    return changed ? words.join(QLatin1Char(' ')) : text;
}

const QString &whisperPromptPreamble()
{
    static const QString s = QStringLiteral("Amateur radio QSO.");
    return s;
}

// Whisper conditions its decoder on this text, so the speaker's callsign and its phonetic spelling become the
// most likely reading of what is heard. Only the English preamble is language specific.
[[maybe_unused]] QByteArray buildWhisperPrompt(const QStringList &knownCalls, const QString &lang)
{
    QStringList parts;
    if (lang.isEmpty() || lang == QStringLiteral("auto") || lang == QStringLiteral("en")) {
        parts << whisperPromptPreamble();
    }
    for (int i = 0; i < knownCalls.size() && i < 4; ++i) {
        if (i < 2) {
            parts << QStringLiteral("%1, %2.").arg(knownCalls[i], natoSpelling(knownCalls[i]));
        } else {
            parts << knownCalls[i] + QLatin1Char('.');
        }
    }
    return parts.join(QLatin1Char(' ')).toUtf8();
}

QString stripPromptLeak(const QString &text)
{
    QString s = text;
    s.remove(whisperPromptPreamble(), Qt::CaseInsensitive);
    return s.simplified();
}

QString normalizeModelTier(const QString &type)
{
    const QString t = type.trimmed().toLower();
    if (t == QStringLiteral("turbo")) {
        return QStringLiteral("turbo");
    }
    if (t == QStringLiteral("small")) {
        return QStringLiteral("small");
    }
    if (t == QStringLiteral("base")) {
        return QStringLiteral("base");
    }
    return QStringLiteral("tiny");
}

QString modelSizeLabel(const QString &tier)
{
    if (tier == QStringLiteral("turbo")) {
        return QStringLiteral("~547 MB");
    }
    if (tier == QStringLiteral("small")) {
        return QStringLiteral("~181 MB");
    }
    if (tier == QStringLiteral("base")) {
        return QStringLiteral("~57 MB");
    }
    return QStringLiteral("~31 MB");
}

qint64 minimumModelBytes(const QString &tier)
{
    if (tier == QStringLiteral("turbo")) {
        return 540LL * 1000LL * 1000LL;
    }
    if (tier == QStringLiteral("small")) {
        return 170LL * 1000LL * 1000LL;
    }
    if (tier == QStringLiteral("base")) {
        return 52LL * 1000LL * 1000LL;
    }
    return 28LL * 1000LL * 1000LL;
}

qint64 expectedModelBytes(const QString &tier)
{
    if (tier == QStringLiteral("turbo")) {
        return 574041195LL;
    }
    if (tier == QStringLiteral("small")) {
        return 190085487LL;
    }
    if (tier == QStringLiteral("base")) {
        return 59707625LL;
    }
    return 32152673LL;
}

QString capitalized(const QString &s)
{
    return s.isEmpty() ? s : (s.left(1).toUpper() + s.mid(1));
}

struct DeviceHardwareProfile {
    QString machineId;
    QString marketingName;
    int iphoneMajor { 0 };
    int iphoneMinor { 0 };
    uint64_t ramBytes { 0 };
    bool isLegacyOrA13 { false }; // iPhone 11 series (iPhone12,*) & older (A13 and below, <=4GB RAM, no Metal GPU Family 7)
    bool is4GbMidTier { false };  // 4 GB A14 devices (iPhone 12 / 12 mini)
};

DeviceHardwareProfile queryDeviceHardwareProfile()
{
    DeviceHardwareProfile prof;

    QString machine = qEnvironmentVariable("DROIDSTAR_TEST_DEVICE_ID").trimmed();
#if defined(Q_OS_DARWIN)
    if (machine.isEmpty()) {
        machine = qEnvironmentVariable("SIMULATOR_MODEL_IDENTIFIER").trimmed();
    }
    if (machine.isEmpty()) {
        char buf[128] = {0};
        size_t len = sizeof(buf) - 1;
        if (sysctlbyname("hw.machine", buf, &len, nullptr, 0) == 0 && buf[0] != '\0') {
            machine = QString::fromUtf8(buf).trimmed();
        }
    }
#if !defined(TARGET_OS_SIMULATOR) || !TARGET_OS_SIMULATOR
    {
        uint64_t mem = 0;
        size_t memLen = sizeof(mem);
        if (sysctlbyname("hw.memsize", &mem, &memLen, nullptr, 0) == 0) {
            prof.ramBytes = mem;
        }
    }
#endif
#endif

    prof.machineId = machine;

    static const QHash<QString, QString> kIphoneNames = {
        { QStringLiteral("iPhone10,3"), QStringLiteral("iPhone X") },
        { QStringLiteral("iPhone10,6"), QStringLiteral("iPhone X") },
        { QStringLiteral("iPhone11,2"), QStringLiteral("iPhone XS") },
        { QStringLiteral("iPhone11,4"), QStringLiteral("iPhone XS Max") },
        { QStringLiteral("iPhone11,6"), QStringLiteral("iPhone XS Max") },
        { QStringLiteral("iPhone11,8"), QStringLiteral("iPhone XR") },
        { QStringLiteral("iPhone12,1"), QStringLiteral("iPhone 11") },
        { QStringLiteral("iPhone12,3"), QStringLiteral("iPhone 11 Pro") },
        { QStringLiteral("iPhone12,5"), QStringLiteral("iPhone 11 Pro Max") },
        { QStringLiteral("iPhone12,8"), QStringLiteral("iPhone SE (2nd gen)") },
        { QStringLiteral("iPhone13,1"), QStringLiteral("iPhone 12 mini") },
        { QStringLiteral("iPhone13,2"), QStringLiteral("iPhone 12") },
        { QStringLiteral("iPhone13,3"), QStringLiteral("iPhone 12 Pro") },
        { QStringLiteral("iPhone13,4"), QStringLiteral("iPhone 12 Pro Max") },
        { QStringLiteral("iPhone14,4"), QStringLiteral("iPhone 13 mini") },
        { QStringLiteral("iPhone14,5"), QStringLiteral("iPhone 13") },
        { QStringLiteral("iPhone14,2"), QStringLiteral("iPhone 13 Pro") },
        { QStringLiteral("iPhone14,3"), QStringLiteral("iPhone 13 Pro Max") },
        { QStringLiteral("iPhone14,6"), QStringLiteral("iPhone SE (3rd gen)") },
        { QStringLiteral("iPhone14,7"), QStringLiteral("iPhone 14") },
        { QStringLiteral("iPhone14,8"), QStringLiteral("iPhone 14 Plus") },
        { QStringLiteral("iPhone15,2"), QStringLiteral("iPhone 14 Pro") },
        { QStringLiteral("iPhone15,3"), QStringLiteral("iPhone 14 Pro Max") },
        { QStringLiteral("iPhone15,4"), QStringLiteral("iPhone 15") },
        { QStringLiteral("iPhone15,5"), QStringLiteral("iPhone 15 Plus") },
        { QStringLiteral("iPhone16,1"), QStringLiteral("iPhone 15 Pro") },
        { QStringLiteral("iPhone16,2"), QStringLiteral("iPhone 15 Pro Max") },
        { QStringLiteral("iPhone17,1"), QStringLiteral("iPhone 16 Pro") },
        { QStringLiteral("iPhone17,2"), QStringLiteral("iPhone 16 Pro Max") },
        { QStringLiteral("iPhone17,3"), QStringLiteral("iPhone 16") },
        { QStringLiteral("iPhone17,4"), QStringLiteral("iPhone 16 Plus") },
        { QStringLiteral("iPhone17,5"), QStringLiteral("iPhone 16e") }
    };

    if (machine.startsWith(QStringLiteral("iPhone"))) {
        const QString nums = machine.mid(6);
        const QStringList parts = nums.split(QLatin1Char(','));
        if (!parts.isEmpty()) {
            prof.iphoneMajor = parts[0].toInt();
        }
        if (parts.size() > 1) {
            prof.iphoneMinor = parts[1].toInt();
        }
    }

    if (kIphoneNames.contains(machine)) {
        prof.marketingName = kIphoneNames.value(machine);
    } else {
        const QString simName = qEnvironmentVariable("SIMULATOR_DEVICE_NAME").trimmed();
        if (!simName.isEmpty()) {
            prof.marketingName = simName;
        } else if (!machine.isEmpty()) {
            prof.marketingName = machine;
        } else {
            prof.marketingName = QStringLiteral("this iPhone");
        }
    }

    if (prof.iphoneMajor > 0 && prof.iphoneMajor <= 12) {
        // iPhone 11 / 11 Pro / 11 Pro Max / SE 2 (iPhone12,*) and older (A13 Bionic & earlier)
        prof.isLegacyOrA13 = true;
    } else if (prof.iphoneMajor == 13 && (prof.iphoneMinor == 1 || prof.iphoneMinor == 2)) {
        // iPhone 12 mini / iPhone 12 (4 GB RAM, A14)
        prof.is4GbMidTier = true;
    } else     if (prof.iphoneMajor == 0 && prof.ramBytes > 0 && prof.ramBytes < 3600000000ULL) {
        prof.isLegacyOrA13 = true;
    } else if (prof.iphoneMajor == 0 && prof.ramBytes > 0 && prof.ramBytes < 4300000000ULL) {
        prof.is4GbMidTier = true;
    }

    return prof;
}

const DeviceHardwareProfile &cachedDeviceHardwareProfile()
{
    static const DeviceHardwareProfile s_profile = queryDeviceHardwareProfile();
    return s_profile;
}

// AMBE+2 smears the 1.5-3.4 kHz consonant band, so ASR input gets a 100 Hz high-pass, a +6 dB presence
// peak at 2.2 kHz and a 4-tap Catmull-Rom 8 -> 16 kHz upsampler (linear interpolation loses ~3 dB at 3 kHz).
class DmrSpeechEnhancer
{
public:
    DmrSpeechEnhancer()
    {
        designHighPass(100.0, 0.7071);
        designPresencePeak(2200.0, 0.8, 6.0);
        reset();
    }

    void reset()
    {
        m_hpf.z1 = m_hpf.z2 = 0.0f;
        m_peak.z1 = m_peak.z2 = 0.0f;
        m_h0 = m_h1 = m_h2 = 0.0f;
    }

    void process(int16_t in, std::vector<float> &out16k)
    {
        float x = static_cast<float>(in) * (kPreGain / 32768.0f);
        x = m_hpf.run(x);
        x = m_peak.run(x);
        interpolate(softLimit(x), out16k);
    }

    // Emits the two samples still held in the interpolator history
    void flush(std::vector<float> &out16k)
    {
        interpolate(0.0f, out16k);
        interpolate(0.0f, out16k);
    }

private:
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kFs = 8000.0;
    // The RX tap is already AGC'd to ~0.9 FS; 0.5 keeps presence-boosted peaks below the soft limiter so it never
    // distorts consonants (both models normalize log-mel features, so absolute level does not matter)
    static constexpr float kPreGain = 0.5f;

    struct Biquad {
        float b0 { 1.0f };
        float b1 { 0.0f };
        float b2 { 0.0f };
        float a1 { 0.0f };
        float a2 { 0.0f };
        float z1 { 0.0f };
        float z2 { 0.0f };

        float run(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
    };

    void designHighPass(double f0, double q)
    {
        const double w0 = 2.0 * kPi * f0 / kFs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        m_hpf.b0 = static_cast<float>(((1.0 + cw) / 2.0) / a0);
        m_hpf.b1 = static_cast<float>(-(1.0 + cw) / a0);
        m_hpf.b2 = m_hpf.b0;
        m_hpf.a1 = static_cast<float>((-2.0 * cw) / a0);
        m_hpf.a2 = static_cast<float>((1.0 - alpha) / a0);
    }

    void designPresencePeak(double f0, double q, double gainDb)
    {
        const double A = std::pow(10.0, gainDb / 40.0);
        const double w0 = 2.0 * kPi * f0 / kFs;
        const double cw = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * q);
        const double a0 = 1.0 + alpha / A;
        m_peak.b0 = static_cast<float>((1.0 + alpha * A) / a0);
        m_peak.b1 = static_cast<float>((-2.0 * cw) / a0);
        m_peak.b2 = static_cast<float>((1.0 - alpha * A) / a0);
        m_peak.a1 = m_peak.b1;
        m_peak.a2 = static_cast<float>((1.0 - alpha / A) / a0);
    }

    static float softLimit(float x)
    {
        const float ax = std::fabs(x);
        if (ax <= 0.9f) {
            return x;
        }
        return std::copysign(0.9f + 0.1f * std::tanh((ax - 0.9f) / 0.1f), x);
    }

    void interpolate(float x, std::vector<float> &out16k)
    {
        out16k.push_back(m_h1);
        out16k.push_back((-m_h0 + 9.0f * m_h1 + 9.0f * m_h2 - x) * (1.0f / 16.0f));
        m_h0 = m_h1;
        m_h1 = m_h2;
        m_h2 = x;
    }

    Biquad m_hpf;
    Biquad m_peak;
    float m_h0 { 0.0f };
    float m_h1 { 0.0f };
    float m_h2 { 0.0f };
};

#if DS_HAS_APPLE_SPEECH
void appleSpeechResultTrampoline(int sessionId, const char *text, bool isFinal)
{
    TranscriptionEngine *te = TranscriptionEngine::instance();
    if (!te) {
        return;
    }
    const QString raw = text ? QString::fromUtf8(text) : QString();
    // Run regex/phonetic normalization on the background speech queue so the UI main thread stays free
    const QString norm = TranscriptionEngine::normalizeHamTranscript(raw);
    QMetaObject::invokeMethod(te, [te, sessionId, norm, isFinal]() {
        te->onAppleSpeechPartialResult(sessionId, norm, isFinal);
    }, Qt::QueuedConnection);
}

void appleSpeechStatusTrampoline(bool authorized, bool available, const char *statusMsg)
{
    TranscriptionEngine *te = TranscriptionEngine::instance();
    if (!te) {
        return;
    }
    const QString msg = statusMsg ? QString::fromUtf8(statusMsg) : QString();
    QMetaObject::invokeMethod(te, [te, authorized, available, msg]() {
        te->onAppleSpeechStatusChanged(authorized, available, msg);
    }, Qt::QueuedConnection);
}

void appleSpeechErrorTrampoline(int sessionId, const char *reason)
{
    TranscriptionEngine *te = TranscriptionEngine::instance();
    if (!te) {
        return;
    }
    const QString msg = reason ? QString::fromUtf8(reason) : QString();
    QMetaObject::invokeMethod(te, [te, sessionId, msg]() {
        te->onAppleSpeechSessionFailed(sessionId, msg);
    }, Qt::QueuedConnection);
}
#endif

} // namespace

TranscriptionEngine *TranscriptionEngine::instance()
{
    return s_instance;
}

TranscriptionEngine::TranscriptionEngine(QObject *parent)
    : QObject(parent),
      m_ringBuffer(kRingCapacity, 0)
{
    s_instance = this;
    m_netManager = new QNetworkAccessManager(this);

    m_autoClearTimer = new QTimer(this);
    m_autoClearTimer->setSingleShot(true);
    connect(m_autoClearTimer, &QTimer::timeout, this, [this]() {
        if (!m_activeTurn.load(std::memory_order_relaxed)) {
            bool changed = false;
            {
                QMutexLocker lock(&m_stateMutex);
                if (!m_currentTranscript.isEmpty() || !m_currentCallsign.isEmpty()) {
                    m_currentTranscript.clear();
                    m_currentCallsign.clear();
                    changed = true;
                }
            }
            if (changed) {
                emit transcriptChanged();
            }
        }
    });

    ensureMetalShaderExtracted();
    loadSettings();

#if DS_HAS_APPLE_SPEECH
    ios_speech_init(&appleSpeechResultTrampoline, &appleSpeechStatusTrampoline, &appleSpeechErrorTrampoline);
    m_appleSpeechAuthorized.store(ios_speech_is_authorized(), std::memory_order_relaxed);

    // Picks up Speech Recognition access granted or revoked in iOS Settings while the app was away
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive) {
            return;
        }
        const bool authorized = ios_speech_is_authorized();
        if (authorized != m_appleSpeechAuthorized.load(std::memory_order_relaxed)) {
            onAppleSpeechStatusChanged(authorized, authorized, QString());
        }
    });
#endif

    refreshIdleStatusText();

    m_workerThread = QThread::create([this]() {
        workerLoop();
    });
    m_workerThread->setObjectName(QStringLiteral("LiveTranscriptionWorker"));
    m_workerThread->start();

    if (m_enabled.load(std::memory_order_relaxed)) {
        // Deferred so a first-run Speech Recognition prompt appears over the running UI instead of the splash
        QTimer::singleShot(600, this, [this]() {
            activateSelectedEngine();
        });
    }
}

TranscriptionEngine::~TranscriptionEngine()
{
    if (s_instance == this) {
        s_instance = nullptr;
    }
    cancelDownload();
    m_stopWorker.store(true, std::memory_order_release);
    wakeWorker();
    bool workerStopped = true;
    if (m_workerThread) {
        workerStopped = m_workerThread->wait(8000);
        if (workerStopped) {
            delete m_workerThread;
        }
        m_workerThread = nullptr;
    }
#if DS_HAS_APPLE_SPEECH
    ios_speech_cancel_turn();
#endif
    if (workerStopped) {
        unloadModel();
        unloadParakeet();
    }
}

void TranscriptionEngine::loadSettings()
{
    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
    settings.sync();

    const QString savedEngine = settings.value(QStringLiteral("TRANSCRIPTION_ENGINE"), QStringLiteral("apple")).toString().trimmed().toLower();
    const QString savedLang = settings.value(QStringLiteral("TRANSCRIPTION_LANGUAGE"), QStringLiteral("auto")).toString().trimmed().toLower();
    const QString savedModel = settings.value(QStringLiteral("TRANSCRIPTION_MODEL"), QStringLiteral("base")).toString();
    const QString savedEnabledStr = settings.value(QStringLiteral("TRANSCRIPTION_ENABLED"), QStringLiteral("false")).toString().trimmed().toLower();
    const bool savedEnabled = (savedEnabledStr == QStringLiteral("true") || savedEnabledStr == QStringLiteral("1"));
    const QString savedLogStr = settings.value(QStringLiteral("TRANSCRIPTION_LOG_ENABLED"), QStringLiteral("true")).toString().trimmed().toLower();
    const bool savedLog = (savedLogStr == QStringLiteral("true") || savedLogStr == QStringLiteral("1"));

    {
        QMutexLocker lock(&m_stateMutex);
        if (savedEngine == QStringLiteral("parakeet") && isParakeetSupported() &&
            isModelSupportedOnDevice(QStringLiteral("parakeet"))) {
            m_engineType = QStringLiteral("parakeet");
        } else if (savedEngine == QStringLiteral("whisper") || !isAppleSpeechSupported()) {
            m_engineType = QStringLiteral("whisper");
        } else {
            m_engineType = QStringLiteral("apple");
        }
        m_language = savedLang.isEmpty() ? QStringLiteral("auto") : savedLang;
        QString tier = normalizeModelTier(savedModel);
        if (!isModelSupportedOnDevice(tier)) {
            tier = QStringLiteral("base");
        }
        m_modelType = tier;
    }
    m_enabled.store(savedEnabled, std::memory_order_relaxed);
    m_logTranscripts.store(savedLog, std::memory_order_relaxed);
    qInfo().noquote() << "[Transcription] settings loaded from" << settings.fileName()
                      << "enabled:" << savedEnabled << "engine:" << savedEngine << "model:" << savedModel
                      << "language:" << savedLang << "log:" << savedLog;
}

void TranscriptionEngine::saveSettings() const
{
    QString engine;
    QString lang;
    QString model;
    {
        QMutexLocker lock(&m_stateMutex);
        engine = m_engineType;
        lang = m_language;
        model = m_modelType;
    }
    const bool enabled = m_enabled.load(std::memory_order_relaxed);
    const bool logTurns = m_logTranscripts.load(std::memory_order_relaxed);

    QSettings settings(QSettings::IniFormat, QSettings::UserScope, QStringLiteral("dudetronics"), QStringLiteral("droidstar"));
    settings.setValue(QStringLiteral("TRANSCRIPTION_ENABLED"), enabled ? QStringLiteral("true") : QStringLiteral("false"));
    settings.setValue(QStringLiteral("TRANSCRIPTION_LOG_ENABLED"), logTurns ? QStringLiteral("true") : QStringLiteral("false"));
    settings.setValue(QStringLiteral("TRANSCRIPTION_ENGINE"), engine);
    settings.setValue(QStringLiteral("TRANSCRIPTION_LANGUAGE"), lang);
    settings.setValue(QStringLiteral("TRANSCRIPTION_MODEL"), model);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        qWarning().noquote() << "[Transcription] failed to save settings to" << settings.fileName();
    }
}

void TranscriptionEngine::setLogTranscripts(bool on)
{
    if (m_logTranscripts.exchange(on, std::memory_order_acq_rel) == on) {
        return;
    }
    saveSettings();
    emit logTranscriptsChanged();
}

void TranscriptionEngine::wakeWorker()
{
    QMutexLocker lock(&m_wakeMutex);
    m_wakeCond.wakeOne();
}

bool TranscriptionEngine::isAppleSpeechSupported() const
{
    return DS_HAS_APPLE_SPEECH != 0;
}

bool TranscriptionEngine::isParakeetSupported() const
{
    return DS_HAS_SHERPA_ONNX != 0;
}

QString TranscriptionEngine::deviceModelName() const
{
    return cachedDeviceHardwareProfile().marketingName;
}

bool TranscriptionEngine::isModelSupportedOnDevice(const QString &modelOrEngine) const
{
    return modelUnsupportedReason(modelOrEngine).isEmpty();
}

QString TranscriptionEngine::modelUnsupportedReason(const QString &modelOrEngine) const
{
    const QString key = modelOrEngine.trimmed().toLower();
    if (key.isEmpty() || key == QStringLiteral("apple") || key == QStringLiteral("whisper") ||
        key == QStringLiteral("tiny") || key == QStringLiteral("base")) {
        return QString();
    }

    const DeviceHardwareProfile &prof = cachedDeviceHardwareProfile();
    if (prof.isLegacyOrA13) {
        if (key == QStringLiteral("parakeet")) {
            return QStringLiteral("Parakeet v3 (~640 MB) requires ~1.1 GB RAM and an A14+ chip. Disabled on %1 to prevent app crashes. Use Apple Neural, Tiny, or Base.")
                   .arg(prof.marketingName);
        }
        if (key == QStringLiteral("turbo")) {
            return QStringLiteral("Whisper Turbo (~547 MB) requires ~1 GB RAM and an A14+ Metal GPU. Disabled on %1 to prevent app crashes. Use Apple Neural, Tiny, or Base.")
                   .arg(prof.marketingName);
        }
        if (key == QStringLiteral("small")) {
            return QStringLiteral("Whisper Small (~181 MB) requires an A14+ Metal GPU (iPhone 12+). Disabled on %1 to prevent memory and CPU overload. Use Apple Neural, Tiny, or Base.")
                   .arg(prof.marketingName);
        }
    } else if (prof.is4GbMidTier) {
        if (key == QStringLiteral("parakeet")) {
            return QStringLiteral("Parakeet v3 (~640 MB) requires ~1.1 GB free RAM. Disabled on %1 (4 GB RAM) to prevent iOS memory crashes. Use Apple Neural or Whisper Tiny/Base/Small.")
                   .arg(prof.marketingName);
        }
        if (key == QStringLiteral("turbo")) {
            return QStringLiteral("Whisper Turbo (~547 MB) requires ~1 GB free RAM. Disabled on %1 (4 GB RAM) to prevent iOS memory crashes. Use Apple Neural or Whisper Tiny/Base/Small.")
                   .arg(prof.marketingName);
        }
    }

    return QString();
}

QString TranscriptionEngine::selectedModelKey() const
{
    const QString curEngine = engineType();
    if (curEngine == QStringLiteral("apple")) {
        return QString();
    }
    if (curEngine == QStringLiteral("parakeet")) {
        return QStringLiteral("parakeet");
    }
    return normalizeModelTier(modelType());
}

void TranscriptionEngine::activateSelectedEngine()
{
    const QString curEngine = engineType();
    if (curEngine == QStringLiteral("apple")) {
        // Switching to Apple Neural unloads Whisper/Parakeet from RAM, while any background downloads continue undisturbed
        if (isEnabled()) {
            m_appleStartFailed.store(false, std::memory_order_relaxed);
#if DS_HAS_APPLE_SPEECH
            ios_speech_request_permission();
#endif
        }
    } else if (curEngine == QStringLiteral("parakeet")) {
#if DS_HAS_APPLE_SPEECH
        ios_speech_cancel_turn();
#endif
        if (isEnabled() && !isParakeetDownloaded()) {
            downloadModel(QStringLiteral("parakeet"));
        }
    } else if (isEnabled()) {
#if DS_HAS_APPLE_SPEECH
        ios_speech_cancel_turn();
#endif
        if (!isModelDownloaded(modelType())) {
            downloadModel(modelType());
        }
    }
    m_reloadModelPending.store(true, std::memory_order_release);
    wakeWorker();
    syncDownloadState();
}

void TranscriptionEngine::refreshIdleStatusText()
{
    const QString selKey = selectedModelKey();
    if (!selKey.isEmpty() && m_downloadJobs.contains(selKey)) {
        const ModelDownloadJob *job = m_downloadJobs.value(selKey);
        const int pct = static_cast<int>(std::round(job->progress * 100.0));
        if (m_downloadJobs.size() > 1) {
            updateStatusText(QStringLiteral("Downloading %1 (%2%) • +%3 in parallel…")
                             .arg(job->label)
                             .arg(pct)
                             .arg(m_downloadJobs.size() - 1));
        } else {
            updateStatusText(QStringLiteral("Downloading %1 (%2%)…").arg(job->label).arg(pct));
        }
        return;
    }

    QString bgSuffix;
    if (!m_downloadJobs.isEmpty() && !m_downloadOrder.isEmpty()) {
        const ModelDownloadJob *firstBg = m_downloadJobs.value(m_downloadOrder.first(), nullptr);
        if (firstBg) {
            const int pct = static_cast<int>(std::round(firstBg->progress * 100.0));
            if (m_downloadJobs.size() > 1) {
                bgSuffix = QStringLiteral(" • Downloading %1 (%2%, +%3)")
                           .arg(firstBg->label)
                           .arg(pct)
                           .arg(m_downloadJobs.size() - 1);
            } else {
                bgSuffix = QStringLiteral(" • Downloading %1 (%2%)")
                           .arg(firstBg->label)
                           .arg(pct);
            }
        }
    }

    const bool enabled = m_enabled.load(std::memory_order_relaxed);
    const QString curEngine = engineType();
    const QString tier = modelType();
    const bool whisperLoaded = m_whisperLoaded.load(std::memory_order_relaxed);
    const bool parakeetLoaded = m_parakeetLoaded.load(std::memory_order_relaxed);

    if (curEngine == QStringLiteral("apple")) {
        if (!enabled) {
            updateStatusText(QStringLiteral("Off • Apple Neural (Whisper Disabled)") + bgSuffix);
        } else if (m_appleSpeechAuthorized.load(std::memory_order_relaxed)) {
            if (m_appleStartFailed.load(std::memory_order_relaxed)) {
                QString reason;
                {
                    QMutexLocker lock(&m_stateMutex);
                    reason = m_appleFailureReason;
                }
                const QString head = reason.isEmpty()
                                     ? QStringLiteral("Apple speech unavailable")
                                     : QStringLiteral("Apple speech unavailable (%1)").arg(reason);
                updateStatusText(head + bgSuffix);
            } else {
                updateStatusText(QStringLiteral("Live • Apple Neural Engine") + bgSuffix);
            }
        } else if (m_appleSpeechDenied.load(std::memory_order_relaxed)) {
            updateStatusText(QStringLiteral("Speech access denied • Allow in iOS Settings") + bgSuffix);
        } else {
            updateStatusText(QStringLiteral("Waiting for Speech Recognition permission…") + bgSuffix);
        }
        return;
    }

    if (curEngine == QStringLiteral("parakeet")) {
        const bool downloaded = parakeetLoaded || isParakeetDownloaded();
        if (enabled) {
            if (parakeetLoaded) {
                updateStatusText(QStringLiteral("Parakeet v3 • Ready") + bgSuffix);
            } else if (m_parakeetLoadFailed.load(std::memory_order_relaxed)) {
                updateStatusText(QStringLiteral("Parakeet load failed") + bgSuffix);
            } else if (downloaded) {
                updateStatusText(QStringLiteral("Loading Parakeet v3…") + bgSuffix);
            } else {
                updateStatusText(QStringLiteral("Parakeet v3 required (~640 MB)") + bgSuffix);
            }
        } else {
            updateStatusText((downloaded
                              ? QStringLiteral("Off (Parakeet v3 Ready)")
                              : QStringLiteral("Off (Parakeet v3 ~640 MB)")) + bgSuffix);
        }
        return;
    }

    const QString name = modelDisplayName();
    if (enabled) {
        if (whisperLoaded) {
            updateStatusText(QStringLiteral("Whisper - %1 • Ready").arg(name) + bgSuffix);
        } else if (m_modelLoadFailed.load(std::memory_order_relaxed)) {
            updateStatusText(QStringLiteral("Model load failed • Try another model") + bgSuffix);
        } else if (!resolveLoadableModelPath().isEmpty()) {
            updateStatusText(QStringLiteral("Loading model…") + bgSuffix);
        } else {
            updateStatusText(QStringLiteral("Model required (%1)").arg(modelSizeLabel(tier)) + bgSuffix);
        }
    } else {
        const bool downloaded = whisperLoaded || isModelDownloaded(tier);
        updateStatusText((downloaded
                          ? QStringLiteral("Off (Whisper - %1 Ready)").arg(name)
                          : QStringLiteral("Off (Whisper - %1 %2)").arg(name, modelSizeLabel(tier))) + bgSuffix);
    }
}

void TranscriptionEngine::scheduleAutoClear(int delayMs)
{
    QMetaObject::invokeMethod(this, [this, delayMs]() {
        if (m_autoClearTimer && !m_activeTurn.load(std::memory_order_relaxed)) {
            m_autoClearTimer->start(std::max(500, delayMs));
        }
    }, Qt::QueuedConnection);
}

void TranscriptionEngine::cancelAutoClear()
{
    QMetaObject::invokeMethod(this, [this]() {
        if (m_autoClearTimer && m_autoClearTimer->isActive()) {
            m_autoClearTimer->stop();
        }
    }, Qt::QueuedConnection);
}

QString TranscriptionEngine::modelsDirectoryPath() const
{
    const QString envDir = qEnvironmentVariable("DROIDSTAR_MODELS_DIR").trimmed();
    const QString dirPath = !envDir.isEmpty()
                            ? envDir
                            : (QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + QStringLiteral("/models"));
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(dirPath);
    }
    return dirPath;
}

QString TranscriptionEngine::parakeetDirectoryPath() const
{
    const QString dirPath = modelsDirectoryPath() + QStringLiteral("/parakeet-tdt-0.6b-v3-int8");
    QDir dir(dirPath);
    if (!dir.exists()) {
        dir.mkpath(dirPath);
    }
    return dirPath;
}

bool TranscriptionEngine::isParakeetDownloaded() const
{
    const QString dir = parakeetDirectoryPath();
    const QFileInfo tok(dir + QStringLiteral("/tokens.txt"));
    const QFileInfo joiner(dir + QStringLiteral("/joiner.int8.onnx"));
    const QFileInfo decoder(dir + QStringLiteral("/decoder.int8.onnx"));
    const QFileInfo encoder(dir + QStringLiteral("/encoder.int8.onnx"));
    return tok.isFile() && tok.size() >= 80000LL &&
           joiner.isFile() && joiner.size() >= (6LL * 1000LL * 1000LL) &&
           decoder.isFile() && decoder.size() >= (11LL * 1000LL * 1000LL) &&
           encoder.isFile() && encoder.size() >= (640LL * 1000LL * 1000LL);
}

QString TranscriptionEngine::modelFileName(const QString &type) const
{
    // Always use the multilingual ggml-<tier>-q5_1.bin (or large-v3-turbo-q5_0.bin) model so all 99 languages
    // (English, Spanish, Italian, German, French, Auto-Detect, etc.) work with a single download.
    const QString tier = normalizeModelTier(type.trimmed().isEmpty() ? modelType() : type);
    if (tier == QStringLiteral("turbo")) {
        return QStringLiteral("ggml-large-v3-turbo-q5_0.bin");
    }
    return QStringLiteral("ggml-%1-q5_1.bin").arg(tier);
}

QString TranscriptionEngine::modelFilePath(const QString &type) const
{
    return modelsDirectoryPath() + QLatin1Char('/') + modelFileName(type);
}

QString TranscriptionEngine::modelDownloadUrl(const QString &type) const
{
    return QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/") + modelFileName(type);
}

QString TranscriptionEngine::resolveLoadableModelPath(const QString &type) const
{
    const QString tier = normalizeModelTier(type.trimmed().isEmpty() ? modelType() : type);
    const QString preferred = modelFilePath(tier);
    const QFileInfo preferredInfo(preferred);
    if (preferredInfo.isFile() && preferredInfo.size() >= minimumModelBytes(tier)) {
        return preferred;
    }
    // If the user explicitly selected English ("en") and only has an older .en file on disk, allow loading it
    if (language() == QStringLiteral("en")) {
        const QString enFallback = modelsDirectoryPath() + QStringLiteral("/ggml-%1.en-q5_1.bin").arg(tier);
        const QFileInfo enInfo(enFallback);
        if (enInfo.isFile() && enInfo.size() >= minimumModelBytes(tier)) {
            return enFallback;
        }
    }
    return QString();
}

void TranscriptionEngine::ensureMetalShaderExtracted() const
{
    const QString modelsDir = modelsDirectoryPath();
    const QString dstShader = modelsDir + QStringLiteral("/ggml-metal.metal");
    QFile qrcShader(QStringLiteral(":/DroidStar/third_party/whisper/ggml-metal.metal"));
    if (qrcShader.exists()) {
        QFileInfo dstInfo(dstShader);
        if (!dstInfo.exists() || dstInfo.size() != qrcShader.size()) {
            if (QFile::exists(dstShader)) {
                QFile::remove(dstShader);
            }
            if (qrcShader.open(QIODevice::ReadOnly)) {
                QFile out(dstShader);
                if (out.open(QIODevice::WriteOnly)) {
                    out.write(qrcShader.readAll());
                    out.close();
                }
                qrcShader.close();
            }
        }
        qputenv("GGML_METAL_PATH_RESOURCES", modelsDir.toUtf8());
    }
}

bool TranscriptionEngine::isModelDownloaded(const QString &type) const
{
    return !resolveLoadableModelPath(type).isEmpty();
}

bool TranscriptionEngine::isModelReady() const
{
    const QString curEngine = engineType();
    if (curEngine == QStringLiteral("apple")) {
        return m_appleSpeechAuthorized.load(std::memory_order_relaxed);
    }
    if (curEngine == QStringLiteral("parakeet")) {
        return m_parakeetLoaded.load(std::memory_order_relaxed) || isParakeetDownloaded();
    }
    return m_whisperLoaded.load(std::memory_order_relaxed) || isModelDownloaded(modelType());
}

void TranscriptionEngine::setEnabled(bool on)
{
    const bool prev = m_enabled.exchange(on, std::memory_order_acq_rel);
    saveSettings();
    if (prev == on) {
        refreshIdleStatusText();
        return;
    }

    if (on) {
        activateSelectedEngine();
    } else {
        cancelAutoClear();
        m_turnEndPending.store(true, std::memory_order_release);
        m_activeTurn.store(false, std::memory_order_release);
        {
            QMutexLocker lock(&m_stateMutex);
            m_currentTranscript.clear();
            m_currentCallsign.clear();
        }
        m_reloadModelPending.store(true, std::memory_order_release);
        wakeWorker();
        refreshIdleStatusText();
        emit transcriptChanged();
        emit modelStateChanged();
    }

    emit transcriptionEnabledChanged();
}

QString TranscriptionEngine::engineType() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_engineType;
}

void TranscriptionEngine::setEngineType(const QString &engine)
{
    const QString req = engine.trimmed().toLower();
    if (req == QStringLiteral("parakeet") && !isModelSupportedOnDevice(QStringLiteral("parakeet"))) {
        emit modelDownloadError(modelUnsupportedReason(QStringLiteral("parakeet")));
        return;
    }
    QString norm;
    if (req == QStringLiteral("parakeet") && isParakeetSupported()) {
        norm = QStringLiteral("parakeet");
    } else if (req == QStringLiteral("whisper") || !isAppleSpeechSupported()) {
        norm = QStringLiteral("whisper");
    } else {
        norm = QStringLiteral("apple");
    }
    {
        QMutexLocker lock(&m_stateMutex);
        if (m_engineType == norm) {
            return;
        }
        m_engineType = norm;
    }
    saveSettings();
    emit engineTypeChanged();
    activateSelectedEngine();
}

QString TranscriptionEngine::language() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_language;
}

void TranscriptionEngine::setLanguage(const QString &lang)
{
    const QString norm = lang.trimmed().isEmpty() ? QStringLiteral("auto") : lang.trimmed().toLower();
    {
        QMutexLocker lock(&m_stateMutex);
        if (m_language == norm) {
            return;
        }
        m_language = norm;
    }
    saveSettings();
    emit languageChanged();
    // English/Auto and other languages use different Whisper builds
    activateSelectedEngine();
}

QString TranscriptionEngine::modelType() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_modelType;
}

void TranscriptionEngine::setModelType(const QString &type)
{
    const QString norm = normalizeModelTier(type);
    if (!isModelSupportedOnDevice(norm)) {
        emit modelDownloadError(modelUnsupportedReason(norm));
        return;
    }
    {
        QMutexLocker lock(&m_stateMutex);
        if (m_modelType == norm) {
            return;
        }
        m_modelType = norm;
    }
    saveSettings();
    emit modelTypeChanged();
    activateSelectedEngine();
}

QString TranscriptionEngine::modelDisplayName() const
{
    if (engineType() == QStringLiteral("parakeet")) {
        return QStringLiteral("Parakeet v3");
    }
    const QString tier = normalizeModelTier(modelType());
    return capitalized(tier);
}

QString TranscriptionEngine::statusText() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_statusText;
}

void TranscriptionEngine::updateStatusText(const QString &text)
{
    {
        QMutexLocker lock(&m_stateMutex);
        if (m_statusText == text) {
            return;
        }
        m_statusText = text;
    }
    QMetaObject::invokeMethod(this, [this]() {
        emit modelStateChanged();
    }, Qt::QueuedConnection);
}

QString TranscriptionEngine::currentTranscript() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_currentTranscript;
}

QString TranscriptionEngine::currentCallsign() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_currentCallsign;
}

QVariantList TranscriptionEngine::recentTurns() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_recentTurns;
}

QVariantList TranscriptionEngine::supportedLanguages() const
{
    return QVariantList{
        QVariantMap{{QStringLiteral("code"), QStringLiteral("auto")}, {QStringLiteral("name"), QStringLiteral("Auto-Detect")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("en")},   {QStringLiteral("name"), QStringLiteral("English")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("es")},   {QStringLiteral("name"), QStringLiteral("Spanish (Español)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("de")},   {QStringLiteral("name"), QStringLiteral("German (Deutsch)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("fr")},   {QStringLiteral("name"), QStringLiteral("French (Français)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("ja")},   {QStringLiteral("name"), QStringLiteral("Japanese (日本語)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("it")},   {QStringLiteral("name"), QStringLiteral("Italian (Italiano)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("pt")},   {QStringLiteral("name"), QStringLiteral("Portuguese (Português)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("ru")},   {QStringLiteral("name"), QStringLiteral("Russian (Русский)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("zh")},   {QStringLiteral("name"), QStringLiteral("Chinese (中文)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("hi")},   {QStringLiteral("name"), QStringLiteral("Hindi (हिन्दी)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("ar")},   {QStringLiteral("name"), QStringLiteral("Arabic (العربية)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("nl")},   {QStringLiteral("name"), QStringLiteral("Dutch (Nederlands)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("pl")},   {QStringLiteral("name"), QStringLiteral("Polish (Polski)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("ko")},   {QStringLiteral("name"), QStringLiteral("Korean (한국어)")}},
        QVariantMap{{QStringLiteral("code"), QStringLiteral("tr")},   {QStringLiteral("name"), QStringLiteral("Turkish (Türkçe)")}}
    };
}

void TranscriptionEngine::setUserCallsign(const QString &callsign)
{
    QMutexLocker lock(&m_stateMutex);
    m_userCallsign = callsign.trimmed().toUpper();
}

// Caller must hold m_stateMutex. Ordered by how likely the station is to be spoken in this over.
QStringList TranscriptionEngine::knownCallsignsLocked(const QString &speakerCall) const
{
    QStringList out;
    auto add = [&out](const QString &raw) {
        const QString c = canonicalCallsign(raw);
        if (!c.isEmpty() && !out.contains(c)) {
            out << c;
        }
    };
    add(speakerCall);
    add(m_userCallsign);
    for (const QVariant &v : m_recentTurns) {
        if (out.size() >= 12) {
            break;
        }
        add(v.toMap().value(QStringLiteral("callsign")).toString());
    }
    return out;
}

void TranscriptionEngine::clearTranscript()
{
    cancelAutoClear();
    {
        QMutexLocker lock(&m_stateMutex);
        m_currentTranscript.clear();
        m_currentCallsign.clear();
        m_recentTurns.clear();
    }
    emit transcriptChanged();
    emit recentTurnsChanged();
}

QVariantList TranscriptionEngine::activeDownloads() const
{
    QMutexLocker lock(&m_stateMutex);
    return m_activeDownloadsSnapshot;
}

bool TranscriptionEngine::isModelKeyDownloading(const QString &type) const
{
    const QString req = type.trimmed().toLower();
    const QString key = (req == QStringLiteral("parakeet"))
                        ? QStringLiteral("parakeet")
                        : (req.isEmpty() ? selectedModelKey() : normalizeModelTier(req));
    QMutexLocker lock(&m_stateMutex);
    for (const QVariant &v : m_activeDownloadsSnapshot) {
        if (v.toMap().value(QStringLiteral("key")).toString() == key) {
            return true;
        }
    }
    return false;
}

void TranscriptionEngine::syncDownloadState()
{
    const QString selKey = selectedModelKey();
    const ModelDownloadJob *selJob = (!selKey.isEmpty() && m_downloadJobs.contains(selKey))
                                     ? m_downloadJobs.value(selKey)
                                     : nullptr;
    m_downloading.store(selJob != nullptr, std::memory_order_release);
    m_downloadProgress.store(selJob ? selJob->progress : 0.0, std::memory_order_relaxed);

    QVariantList snapshot;
    for (const QString &k : std::as_const(m_downloadOrder)) {
        if (const ModelDownloadJob *job = m_downloadJobs.value(k, nullptr)) {
            QVariantMap item;
            item[QStringLiteral("key")] = job->key;
            item[QStringLiteral("label")] = job->label;
            item[QStringLiteral("progress")] = job->progress;
            item[QStringLiteral("percent")] = static_cast<int>(std::round(job->progress * 100.0));
            snapshot.push_back(item);
        }
    }
    {
        QMutexLocker lock(&m_stateMutex);
        m_activeDownloadsSnapshot = snapshot;
    }

    refreshIdleStatusText();
    emit downloadProgressChanged();
    emit modelStateChanged();
}

void TranscriptionEngine::downloadModel(const QString &type)
{
    const QString req = type.trimmed().toLower();
    const bool wantParakeet = (req == QStringLiteral("parakeet")) ||
                              (req.isEmpty() && engineType() == QStringLiteral("parakeet"));
    const QString key = wantParakeet ? QStringLiteral("parakeet")
                                     : normalizeModelTier(req.isEmpty() ? modelType() : req);

    if (!isModelSupportedOnDevice(key)) {
        emit modelDownloadError(modelUnsupportedReason(key));
        return;
    }

    // If this model is already downloading, keep it running without resetting
    if (m_downloadJobs.contains(key)) {
        syncDownloadState();
        return;
    }

    auto *job = new ModelDownloadJob();
    job->key = key;

    if (wantParakeet) {
        const QString dir = parakeetDirectoryPath();
        const QString baseUrl = QStringLiteral("https://huggingface.co/csukuangfj/sherpa-onnx-nemo-parakeet-tdt-0.6b-v3-int8/resolve/main/");
        struct FileSpec { const char *name; qint64 minBytes; qint64 expectedBytes; };
        static const FileSpec specs[] = {
            { "tokens.txt",        80000LL,                93939LL },
            { "joiner.int8.onnx",  6LL * 1000LL * 1000LL,  6355277LL },
            { "decoder.int8.onnx", 11LL * 1000LL * 1000LL, 11845275LL },
            { "encoder.int8.onnx", 640LL * 1000LL * 1000LL, 652184281LL }
        };
        for (const auto &sp : specs) {
            const QString fpath = dir + QLatin1Char('/') + QString::fromLatin1(sp.name);
            const QFileInfo fi(fpath);
            job->totalBytes += sp.expectedBytes;
            if (fi.isFile() && fi.size() >= sp.minBytes) {
                job->completedBytes += sp.expectedBytes;
            } else {
                DownloadStep step;
                step.url = baseUrl + QString::fromLatin1(sp.name);
                step.finalPath = fpath;
                step.minBytes = sp.minBytes;
                step.expectedBytes = sp.expectedBytes;
                job->steps.push_back(step);
            }
        }
        job->label = QStringLiteral("Parakeet v3");
    } else {
        const QString finalPath = modelFilePath(key);
        const QFileInfo fi(finalPath);
        const qint64 minB = minimumModelBytes(key);
        const qint64 expB = expectedModelBytes(key);
        job->totalBytes = expB;
        if (fi.isFile() && fi.size() >= minB) {
            job->completedBytes = expB;
        } else {
            DownloadStep step;
            step.url = modelDownloadUrl(key);
            step.finalPath = finalPath;
            step.minBytes = minB;
            step.expectedBytes = expB;
            job->steps.push_back(step);
        }
        job->label = QStringLiteral("Whisper - %1").arg(capitalized(key));
    }

    if (job->steps.isEmpty()) {
        delete job;
        m_reloadModelPending.store(true, std::memory_order_release);
        wakeWorker();
        syncDownloadState();
        return;
    }

    // Account for any existing partial .part file on the first pending step so initial % is accurate
    const QFileInfo firstPartInfo(job->steps.first().finalPath + QStringLiteral(".part"));
    const qint64 existingPartBytes = (firstPartInfo.isFile() && firstPartInfo.size() < job->steps.first().expectedBytes)
                                     ? firstPartInfo.size()
                                     : 0;
    job->progress = job->totalBytes > 0
                    ? std::clamp(static_cast<double>(job->completedBytes + existingPartBytes) / static_cast<double>(job->totalBytes), 0.0, 0.99)
                    : 0.0;

    m_downloadJobs.insert(key, job);
    if (!m_downloadOrder.contains(key)) {
        m_downloadOrder.push_back(key);
    }
    syncDownloadState();
    startNextDownloadStep(job);
}

void TranscriptionEngine::startNextDownloadStep(ModelDownloadJob *job)
{
    if (!job) {
        return;
    }

    if (job->currentStep >= job->steps.size()) {
        const QString finishedKey = job->key;
        m_downloadJobs.remove(finishedKey);
        m_downloadOrder.removeAll(finishedKey);
        delete job;

        m_reloadModelPending.store(true, std::memory_order_release);
        wakeWorker();
        syncDownloadState();
        return;
    }

    const DownloadStep step = job->steps[job->currentStep];
    const QString tempPath = step.finalPath + QStringLiteral(".part");
    const QFileInfo tempInfo(tempPath);

    qint64 resumeOffset = 0;
    if (tempInfo.exists() && tempInfo.isFile()) {
        if (tempInfo.size() > 0 && tempInfo.size() < step.expectedBytes) {
            resumeOffset = tempInfo.size();
        } else {
            QFile::remove(tempPath);
        }
    }

    job->file = new QFile(tempPath, this);
    const QIODevice::OpenMode openMode = (resumeOffset > 0)
                                         ? (QIODevice::WriteOnly | QIODevice::Append)
                                         : QIODevice::WriteOnly;
    if (!job->file->open(openMode)) {
        const QString err = QStringLiteral("Failed to create model file: ") + job->file->errorString();
        delete job->file;
        job->file = nullptr;
        const QString failedKey = job->key;
        m_downloadJobs.remove(failedKey);
        m_downloadOrder.removeAll(failedKey);
        delete job;
        syncDownloadState();
        emit modelDownloadError(err);
        return;
    }

    job->stepResumeOffset = resumeOffset;
    job->checkedRangeStatus = (resumeOffset == 0);

    QNetworkRequest req{QUrl(step.url)};
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("DroidStar/2026"));
    if (resumeOffset > 0) {
        req.setRawHeader("Range", QByteArray("bytes=") + QByteArray::number(resumeOffset) + "-");
    }

    QNetworkReply *reply = m_netManager->get(req);
    job->reply = reply;

    connect(reply, &QNetworkReply::readyRead, this, [job, reply]() {
        if (job->reply != reply || !job->file) {
            return;
        }
        if (!job->checkedRangeStatus) {
            job->checkedRangeStatus = true;
            const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            if (status == 200) {
                // Server ignored Range header and sent the full file from byte 0
                job->file->resize(0);
                job->file->seek(0);
                job->stepResumeOffset = 0;
            }
        }
        job->file->write(reply->readAll());
    });

    connect(reply, &QNetworkReply::downloadProgress, this, [this, job, reply, step](qint64 bytesReceived, qint64 bytesTotal) {
        if (job->reply != reply) {
            return;
        }
        const qint64 stepDone = job->stepResumeOffset + bytesReceived;
        const qint64 stepTotal = (bytesTotal > 0) ? (job->stepResumeOffset + bytesTotal) : step.expectedBytes;
        double prog = 0.0;
        if (job->steps.size() == 1 && stepTotal > 0) {
            prog = std::clamp(static_cast<double>(stepDone) / static_cast<double>(stepTotal), 0.0, 0.999);
        } else if (job->totalBytes > 0) {
            const qint64 tot = std::max<qint64>(job->totalBytes, job->completedBytes + stepTotal);
            prog = std::clamp(static_cast<double>(job->completedBytes + stepDone) / static_cast<double>(tot), 0.0, 0.999);
        }
        job->progress = prog;
        syncDownloadState();
    });

    connect(reply, &QNetworkReply::finished, this, [this, job, reply, step, tempPath]() {
        if (job->reply == reply) {
            job->reply = nullptr;
        }

        if (job->file) {
            if (reply && reply->bytesAvailable() > 0 && reply->error() == QNetworkReply::NoError) {
                if (!job->checkedRangeStatus) {
                    job->checkedRangeStatus = true;
                    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    if (status == 200) {
                        job->file->resize(0);
                        job->file->seek(0);
                        job->stepResumeOffset = 0;
                    }
                }
                job->file->write(reply->readAll());
            }
            job->file->flush();
            job->file->close();
            delete job->file;
            job->file = nullptr;
        }

        const int httpStatus = reply ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() : 0;
        const bool ok = (reply && reply->error() == QNetworkReply::NoError);
        const bool canceled = (reply && reply->error() == QNetworkReply::OperationCanceledError);
        const QString errStr = reply ? reply->errorString() : QStringLiteral("Cancelled");
        if (reply) {
            reply->deleteLater();
        }

        const QFileInfo fi(tempPath);
        if (ok && fi.exists() && fi.size() >= step.minBytes) {
            if (QFile::exists(step.finalPath)) {
                QFile::remove(step.finalPath);
            }
            QFile::rename(tempPath, step.finalPath);
            job->completedBytes += std::max(fi.size(), step.expectedBytes);
            ++job->currentStep;
            startNextDownloadStep(job);
        } else {
            if (httpStatus == 416 || (fi.exists() && fi.size() >= step.expectedBytes)) {
                QFile::remove(tempPath);
            }
            const QString failedKey = job->key;
            const QString failedLabel = job->label;
            m_downloadJobs.remove(failedKey);
            m_downloadOrder.removeAll(failedKey);
            delete job;

            syncDownloadState();
            if (!canceled) {
                emit modelDownloadError(ok
                                        ? QStringLiteral("Downloaded %1 file was incomplete.").arg(failedLabel)
                                        : errStr);
            }
        }
    });
}

void TranscriptionEngine::cancelDownload(const QString &type)
{
    const QString req = type.trimmed().toLower();
    if (req.isEmpty()) {
        const QList<ModelDownloadJob *> jobs = m_downloadJobs.values();
        for (ModelDownloadJob *job : jobs) {
            if (job && job->reply) {
                job->reply->abort();
            }
        }
        return;
    }

    const QString key = (req == QStringLiteral("parakeet")) ? QStringLiteral("parakeet") : normalizeModelTier(req);
    if (ModelDownloadJob *job = m_downloadJobs.value(key, nullptr)) {
        if (job->reply) {
            job->reply->abort();
        }
    }
}

void TranscriptionEngine::pushRxSamples(const int16_t *pcm8k, size_t count)
{
    if (!m_enabled.load(std::memory_order_relaxed) ||
        !m_connected.load(std::memory_order_relaxed) ||
        !m_rxStreamActive.load(std::memory_order_relaxed) ||
        pcm8k == nullptr || count == 0) {
        return;
    }

    uint32_t w = m_ringWritePos.load(std::memory_order_relaxed);
    const uint32_t r = m_ringReadPos.load(std::memory_order_acquire);

    for (size_t i = 0; i < count; ++i) {
        if ((w - r) >= kRingCapacity) {
            break;
        }
        m_ringBuffer[w & kRingMask] = pcm8k[i];
        ++w;
    }

    m_ringWritePos.store(w, std::memory_order_release);
    m_lastSamplePushMs.store(monotonicMs(), std::memory_order_relaxed);

    // Wake roughly every 32 ms of audio (~256 samples at 8 kHz) for low-latency streaming
    if ((w & 0x00FFU) < static_cast<uint32_t>(count)) {
        m_wakeCond.wakeOne();
    }
}

void TranscriptionEngine::markEndOfTransmission()
{
    m_rxStreamActive.store(false, std::memory_order_release);
    if (!m_enabled.load(std::memory_order_relaxed)) {
        return;
    }
    m_turnEndPending.store(true, std::memory_order_release);
    m_wakeCond.wakeOne();
}

void TranscriptionEngine::onStreamStateChanged(uint32_t streamId, uint32_t srcId, const QString &callsign, int streamState)
{
    const QString cleanCall = callsign.trimmed();
    const bool enabled = m_enabled.load(std::memory_order_relaxed);
    bool wake = false;
    bool emitUiUpdate = false;
    bool startAutoClearNow = false;

    {
        QMutexLocker lock(&m_stateMutex);
        const bool isRxActive = (streamState == Mode::STREAM_NEW || streamState == Mode::STREAMING);
        const bool wasRxActive = (m_lastStreamState == Mode::STREAM_NEW || m_lastStreamState == Mode::STREAMING);

        m_connected.store(true, std::memory_order_release);

        if (!enabled) {
            m_rxStreamActive.store(isRxActive, std::memory_order_release);
            if (isRxActive) {
                if (streamId != 0) m_lastStreamId = streamId;
                if (srcId != 0) m_lastSrcId = srcId;
            } else {
                m_lastStreamId = 0;
            }
            m_lastStreamState = streamState;
            return;
        }

        if (isRxActive) {
            m_rxStreamActive.store(true, std::memory_order_release);
            const bool streamChanged = (streamId != 0 && m_lastStreamId != 0 && streamId != m_lastStreamId);
            const bool srcChanged = (srcId != 0 && m_lastSrcId != 0 && srcId != m_lastSrcId);
            const bool callChanged = (!cleanCall.isEmpty() && !m_currentCallsign.isEmpty() && cleanCall != m_currentCallsign);

            if (!wasRxActive || !m_activeTurn.load(std::memory_order_relaxed) || streamChanged || srcChanged || callChanged) {
                const int prevTurnId = m_currentTurnId.fetch_add(1, std::memory_order_acq_rel);
                const bool prevTurnStillPending = wasRxActive || m_turnEndPending.load(std::memory_order_relaxed);
                if (prevTurnStillPending && (streamChanged || srcChanged || callChanged || !wasRxActive)) {
                    // Mid-stream interruption or back-to-back over before worker finalized previous turn:
                    // preserve previous turn's identity and split ring buffer at current write pos
                    if (m_finalizeCallsign.isEmpty()) {
                        m_finalizeCallsign = m_currentCallsign;
                    }
                    if (m_finalizeSrcId == 0) {
                        m_finalizeSrcId = m_currentSrcId.load(std::memory_order_relaxed);
                    }
                    if (m_finalizeTurnId == 0) {
                        m_finalizeTurnId = prevTurnId;
                    }
                    m_newTurnRingPos.store(m_ringWritePos.load(std::memory_order_acquire), std::memory_order_release);
                    m_newTurnPending.store(true, std::memory_order_release);
                } else {
                    // Fresh turn: keep samples already queued for it.
                    m_newTurnRingPos.store(m_ringReadPos.load(std::memory_order_acquire), std::memory_order_release);
                    m_newTurnPending.store(true, std::memory_order_release);
                }

                if (!cleanCall.isEmpty()) {
                    m_pendingCallsign = cleanCall;
                    m_currentCallsign = cleanCall;
                }
                if (srcId != 0) {
                    m_pendingSrcId = srcId;
                    m_currentSrcId.store(srcId, std::memory_order_relaxed);
                }
                m_currentTranscript.clear();
                m_activeTurn.store(true, std::memory_order_release);
                emitUiUpdate = true;
                wake = true;
            } else {
                // Continuing the same active turn: enrich callsign or srcId if they arrived on a subsequent frame
                if (!cleanCall.isEmpty() && m_currentCallsign != cleanCall) {
                    m_currentCallsign = cleanCall;
                    m_pendingCallsign = cleanCall;
                    emitUiUpdate = true;
                }
                if (srcId != 0 && m_currentSrcId.load(std::memory_order_relaxed) == 0) {
                    m_currentSrcId.store(srcId, std::memory_order_relaxed);
                    m_pendingSrcId = srcId;
                }
            }
            if (streamId != 0) m_lastStreamId = streamId;
            if (srcId != 0) m_lastSrcId = srcId;
        } else if (streamState == Mode::STREAM_IDLE ||
                   streamState == Mode::TRANSMITTING ||
                   streamState == Mode::TRANSMITTING_MODEM) {
            // Stream has finished draining playback (or user keyed TX):
            // immediately end the active UI turn so "Listening…" never lingers when Callsign is "—".
            m_rxStreamActive.store(false, std::memory_order_release);
            if (!m_currentCallsign.isEmpty()) {
                m_finalizeCallsign = m_currentCallsign;
            }
            if (m_currentSrcId.load(std::memory_order_relaxed) != 0) {
                m_finalizeSrcId = m_currentSrcId.load(std::memory_order_relaxed);
            }
            m_pendingCallsign.clear();
            m_pendingSrcId = 0;
            const bool wasTurnActive = m_activeTurn.exchange(false, std::memory_order_acq_rel);
            if (m_currentTranscript.isEmpty()) {
                if (!m_currentCallsign.isEmpty()) {
                    m_currentCallsign.clear();
                    emitUiUpdate = true;
                }
            } else {
                startAutoClearNow = true;
            }
            if (wasTurnActive) {
                emitUiUpdate = true;
            }
            m_turnEndPending.store(true, std::memory_order_release);
            m_lastStreamId = 0;
            wake = true;
        } else if (streamState == Mode::STREAM_END || streamState == Mode::STREAM_LOST) {
            // On STREAM_END / STREAM_LOST, m_rxcodecq may still have ~200ms of audio draining via m_rxtimer.
            m_lastStreamId = 0;
        }

        m_lastStreamState = streamState;
    }

    if (emitUiUpdate) {
        if (startAutoClearNow) {
            scheduleAutoClear(kAutoClearDelayMs);
        } else {
            cancelAutoClear();
        }
        QMetaObject::invokeMethod(this, &TranscriptionEngine::transcriptChanged, Qt::QueuedConnection);
    }

    if (wake && enabled) {
        wakeWorker();
    }
}

void TranscriptionEngine::onDisconnected()
{
    cancelAutoClear();
    m_connected.store(false, std::memory_order_release);
    m_rxStreamActive.store(false, std::memory_order_release);
    m_activeTurn.store(false, std::memory_order_release);
    m_currentTurnId.fetch_add(1, std::memory_order_acq_rel);
    m_newTurnPending.store(false, std::memory_order_release);
    m_ringReadPos.store(m_ringWritePos.load(std::memory_order_acquire), std::memory_order_release);
    m_turnEndPending.store(true, std::memory_order_release);

    {
        QMutexLocker lock(&m_stateMutex);
        m_lastStreamId = 0;
        m_lastSrcId = 0;
        m_lastStreamState = Mode::STREAM_IDLE;
        m_currentTranscript.clear();
        m_currentCallsign.clear();
        m_pendingCallsign.clear();
        m_finalizeCallsign.clear();
        m_pendingSrcId = 0;
        m_finalizeSrcId = 0;
        m_finalizeTurnId = 0;
        m_currentSrcId.store(0, std::memory_order_relaxed);
    }

    refreshIdleStatusText();
    QMetaObject::invokeMethod(this, &TranscriptionEngine::transcriptChanged, Qt::QueuedConnection);
    wakeWorker();
}

bool TranscriptionEngine::whisperGpuAllowed() const
{
    if (m_gpuBroken) {
        return false;
    }
#if defined(Q_OS_IOS) && TARGET_OS_SIMULATOR
    // The Simulator's Metal device cannot run the ggml kernels
    return false;
#elif defined(Q_OS_IOS)
    // iOS rejects GPU command buffers from backgrounded apps
    return !isAppInBackground();
#elif defined(Q_OS_MACOS)
    return true;
#else
    return false;
#endif
}

bool TranscriptionEngine::ensureModelLoaded(const QString &modelPath, bool useGpu)
{
    if (m_whisperCtx && m_loadedModelPath == modelPath && m_whisperUsingGpu == useGpu) {
        return true;
    }

    unloadModel();

    if (modelPath.isEmpty() || !QFileInfo::exists(modelPath)) {
        refreshIdleStatusText();
        return false;
    }

    updateStatusText(QStringLiteral("Loading model…"));

    QElapsedTimer loadTimer;
    loadTimer.start();
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = useGpu;
    const QByteArray pathUtf8 = modelPath.toUtf8();
    m_whisperCtx = whisper_init_from_file_with_params(pathUtf8.constData(), cparams);
    if (!m_whisperCtx && useGpu) {
        cparams.use_gpu = false;
        m_whisperCtx = whisper_init_from_file_with_params(pathUtf8.constData(), cparams);
    }
    if (!m_whisperCtx) {
        qWarning().noquote() << "[Transcription] failed to load Whisper model" << modelPath;
        m_failedModelPath = modelPath;
        m_modelLoadFailed.store(true, std::memory_order_relaxed);
        refreshIdleStatusText();
        return false;
    }
    qInfo().noquote() << "[Transcription] Whisper model loaded" << QFileInfo(modelPath).fileName()
                      << (cparams.use_gpu ? "(GPU requested)" : "(CPU)") << "in" << loadTimer.elapsed() << "ms";

    m_loadedModelPath = modelPath;
    m_failedModelPath.clear();
    m_whisperUsingGpu = cparams.use_gpu;
    buildWhisperVocabBiasTable();
    m_modelLoadFailed.store(false, std::memory_order_relaxed);
    m_whisperLoaded.store(true, std::memory_order_release);
    refreshIdleStatusText();
    return true;
}

void TranscriptionEngine::unloadModel()
{
    if (m_whisperCtx) {
        whisper_free(m_whisperCtx);
        m_whisperCtx = nullptr;
    }
    m_whisperUnigramBoosts.clear();
    m_whisperBigramBoosts.clear();
    m_whisperVocabSize = 0;
    m_loadedModelPath.clear();
    m_whisperUsingGpu = false;
    m_whisperLoaded.store(false, std::memory_order_release);
}

void TranscriptionEngine::buildWhisperVocabBiasTable()
{
    m_whisperUnigramBoosts.clear();
    m_whisperBigramBoosts.clear();
    m_whisperVocabSize = 0;
    if (!m_whisperCtx) {
        return;
    }
    m_whisperVocabSize = whisper_n_vocab(m_whisperCtx);

    static const char *const kVocabTerms[] = {
        " Baofeng", " TIDRADIO", " Quansheng", " AnyTone", " Yaesu", " Icom", " Kenwood",
        " Radioddity", " Tytera", " Retevis", " Ailunce", " Alinco", " Wouxun", " BTECH",
        " Elecraft", " FlexRadio", " Xiegu", " MOTOTRBO", " Hytera", " BridgeCom",
        " Nagoya", " ABBREE", " Buddipole", " Buddistick", " Bioenno", " LiFePO4", " Powerpole",
        " Yagi", " dipole", " EFHW", " Hexbeam", " Moxon", " discone", " collinear",
        " balun", " unun", " NanoVNA", " tinySA", " VSWR", " coax",
        " DroidStar", " BrandMeister", " FreeDMR", " Pi-Star", " WPSD", " MMDVM",
        " JumboSpot", " ZUMspot", " openSPOT", " SkyBridge", " ClearNode", " SHARI",
        " DVSwitch", " AllStar", " EchoLink", " WIRES-X", " OpenGD77", " egzumer",
        " D-STAR", " System Fusion", " C4FM", " NXDN", " JS8Call", " WSJT-X", " WSPR",
        " APRS", " Winlink", " SSTV", " RTTY", " PSK31", " CTCSS", " DTMF",
        " talkgroup", " timeslot", " codeplug", " repeater", " local repeater", " transceiver", " vocoder",
        " POTA", " SOTA", " IOTA", " LoTW", " eQSL", " Hamvention"
    };

    whisper_token toks[24];
    for (const char *term : kVocabTerms) {
        const int n = whisper_tokenize(m_whisperCtx, term, toks, 24);
        if (n < 2) {
            continue;
        }
        // 1-token prefix boost only when the first BPE token is >= 5 chars (e.g. " Brand", " Droid", " Repe", " Trans")
        const char *t0Str = whisper_token_to_str(m_whisperCtx, toks[0]);
        if (t0Str && std::strlen(t0Str) >= 5) {
            auto &vec = m_whisperUnigramBoosts[toks[0]];
            bool exists = false;
            for (const auto &p : vec) {
                if (p.first == toks[1]) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                vec.push_back(qMakePair(toks[1], 1.6f));
            }
        }
        // 2-token bigram prefix boost for all multi-token continuations (t_{k-1}, t_k) -> t_{k+1}
        for (int k = 1; k + 1 < n; ++k) {
            const uint64_t key = (static_cast<uint64_t>(static_cast<uint32_t>(toks[k - 1])) << 32) |
                                 static_cast<uint32_t>(toks[k]);
            auto &vec = m_whisperBigramBoosts[key];
            bool exists = false;
            for (const auto &p : vec) {
                if (p.first == toks[k + 1]) {
                    exists = true;
                    break;
                }
            }
            if (!exists) {
                vec.push_back(qMakePair(toks[k + 1], 2.0f));
            }
        }
    }
}

bool TranscriptionEngine::ensureParakeetLoaded()
{
#if DS_HAS_SHERPA_ONNX
    if (m_parakeetCtx) {
        return true;
    }
    if (!isParakeetDownloaded()) {
        refreshIdleStatusText();
        return false;
    }

    updateStatusText(QStringLiteral("Loading Parakeet TDT v3…"));
    QElapsedTimer loadTimer;
    loadTimer.start();

    const QString dir = parakeetDirectoryPath();
    const QByteArray encUtf8 = (dir + QStringLiteral("/encoder.int8.onnx")).toUtf8();
    const QByteArray decUtf8 = (dir + QStringLiteral("/decoder.int8.onnx")).toUtf8();
    const QByteArray joinUtf8 = (dir + QStringLiteral("/joiner.int8.onnx")).toUtf8();
    const QByteArray tokUtf8 = (dir + QStringLiteral("/tokens.txt")).toUtf8();

    SherpaOnnxOfflineRecognizerConfig cfg;
    std::memset(&cfg, 0, sizeof(cfg));
    cfg.feat_config.sample_rate = 16000;
    cfg.feat_config.feature_dim = 80;
    cfg.model_config.transducer.encoder = encUtf8.constData();
    cfg.model_config.transducer.decoder = decUtf8.constData();
    cfg.model_config.transducer.joiner = joinUtf8.constData();
    cfg.model_config.tokens = tokUtf8.constData();
    cfg.model_config.model_type = "nemo_transducer";
    cfg.model_config.provider = "cpu";
#if defined(Q_OS_IOS) && (!defined(TARGET_OS_SIMULATOR) || !TARGET_OS_SIMULATOR)
    cfg.model_config.num_threads = std::clamp(QThread::idealThreadCount() / 2, 2, 4);
#else
    cfg.model_config.num_threads = std::clamp(QThread::idealThreadCount() / 2, 2, 6);
#endif
    cfg.decoding_method = "greedy_search";

    m_parakeetCtx = SherpaOnnxCreateOfflineRecognizer(&cfg);
    if (!m_parakeetCtx) {
        qWarning() << "[Transcription] failed to create Parakeet TDT v3 recognizer";
        m_parakeetLoadFailed.store(true, std::memory_order_relaxed);
        refreshIdleStatusText();
        return false;
    }

    qInfo().noquote() << "[Transcription] Parakeet TDT v3 loaded in" << loadTimer.elapsed() << "ms";
    m_parakeetLoadFailed.store(false, std::memory_order_relaxed);
    m_parakeetLoaded.store(true, std::memory_order_release);
    refreshIdleStatusText();
    return true;
#else
    return false;
#endif
}

void TranscriptionEngine::unloadParakeet()
{
#if DS_HAS_SHERPA_ONNX
    if (m_parakeetCtx) {
        SherpaOnnxDestroyOfflineRecognizer(m_parakeetCtx);
        m_parakeetCtx = nullptr;
    }
#endif
    m_parakeetLoaded.store(false, std::memory_order_release);
}

QString TranscriptionEngine::runParakeetInference(const float *samples16k, size_t sampleCount,
                                                  std::vector<WhisperSegment> *segments)
{
    if (segments) {
        segments->clear();
    }
#if DS_HAS_SHERPA_ONNX
    if (!m_parakeetCtx || samples16k == nullptr || sampleCount < 4800) {
        return QString();
    }

    const SherpaOnnxOfflineStream *stream = SherpaOnnxCreateOfflineStream(m_parakeetCtx);
    if (!stream) {
        return QString();
    }

    SherpaOnnxAcceptWaveformOffline(stream, 16000, samples16k, static_cast<int32_t>(sampleCount));
    SherpaOnnxDecodeOfflineStream(m_parakeetCtx, stream);
    const SherpaOnnxOfflineRecognizerResult *res = SherpaOnnxGetOfflineStreamResult(stream);

    QString out;
    if (res) {
        if (res->text) {
            out = cleanWhisperOutput(QString::fromUtf8(res->text));
        }
        if (segments && res->count > 0 && res->tokens_arr && res->timestamps) {
            QString segAcc;
            float segStartSec = res->timestamps[0];
            for (int32_t i = 0; i < res->count; ++i) {
                const QString tok = res->tokens_arr[i] ? QString::fromUtf8(res->tokens_arr[i]) : QString();
                const float tSec = res->timestamps[i];

                // If a new word starts after >= 1.8s, close the previous segment BEFORE appending `tok`
                // so we never slice a word in half at a subword boundary.
                if (tSec - segStartSec >= 1.8f && tok.startsWith(QLatin1Char(' ')) && !segAcc.trimmed().isEmpty()) {
                    WhisperSegment seg;
                    seg.text = cleanWhisperOutput(segAcc);
                    const float cutSec = std::max(0.0f, tSec - 0.02f);
                    seg.endSample = std::clamp<size_t>(static_cast<size_t>(cutSec * 16000.0f), 0, sampleCount);
                    if (!seg.text.isEmpty()) {
                        segments->push_back(seg);
                    }
                    segAcc = tok;
                    segStartSec = tSec;
                    continue;
                }

                segAcc += tok;
                const bool punctBreak = tok.endsWith(QLatin1Char('.')) ||
                                        tok.endsWith(QLatin1Char('?')) ||
                                        tok.endsWith(QLatin1Char('!')) ||
                                        tok.endsWith(QLatin1Char(','));
                if (punctBreak && !segAcc.trimmed().isEmpty()) {
                    WhisperSegment seg;
                    seg.text = cleanWhisperOutput(segAcc);
                    const float nextSec = (i + 1 < res->count) ? res->timestamps[i + 1] : (tSec + 0.08f);
                    const float cutSec = 0.5f * (tSec + nextSec);
                    seg.endSample = std::clamp<size_t>(static_cast<size_t>(cutSec * 16000.0f), 0, sampleCount);
                    if (!seg.text.isEmpty()) {
                        segments->push_back(seg);
                    }
                    segAcc.clear();
                    if (i + 1 < res->count) {
                        segStartSec = res->timestamps[i + 1];
                    }
                }
            }
            if (!segAcc.trimmed().isEmpty()) {
                WhisperSegment seg;
                seg.text = cleanWhisperOutput(segAcc);
                seg.endSample = sampleCount;
                if (!seg.text.isEmpty()) {
                    segments->push_back(seg);
                }
            }
        }
        SherpaOnnxDestroyOfflineRecognizerResult(res);
    }
    SherpaOnnxDestroyOfflineStream(stream);
    return out.trimmed();
#else
    Q_UNUSED(samples16k);
    Q_UNUSED(sampleCount);
    return QString();
#endif
}

// Greedy decoding without temperature fallback can loop ("CQ, CQ, CQ, CQ, ..." or "the one out of the one out of ...").
// A word may repeat 3 times (a real "CQ CQ CQ"); a multi-word phrase repeating 3+ times is kept once.
QString TranscriptionEngine::collapseRepetitions(const QString &text)
{
    const QStringList words = text.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (words.size() < 4) {
        return text;
    }
    QStringList keys;
    keys.reserve(words.size());
    for (const QString &w : words) {
        QString k;
        for (const QChar c : w) {
            if (c.isLetterOrNumber()) {
                k += c.toLower();
            }
        }
        keys << k;
    }

    QStringList out;
    const int n = static_cast<int>(words.size());
    int i = 0;
    while (i < n) {
        bool collapsed = false;
        for (int len = 1; len <= 10 && i + len * 2 <= n; ++len) {
            int reps = 1;
            while (i + (reps + 1) * len <= n) {
                bool same = true;
                for (int k = 0; k < len; ++k) {
                    const QString &a = keys[i + k];
                    if (a.isEmpty() || keys[i + reps * len + k] != a) {
                        same = false;
                        break;
                    }
                }
                if (!same) {
                    break;
                }
                ++reps;
            }
            const int keep = (len == 1) ? 3 : 1;
            // A 4+ word phrase repeating back-to-back even twice is a decoder loop
            const int minLoop = (len == 1) ? 4 : (len >= 4 ? 2 : 3);
            if (reps >= minLoop) {
                for (int r = 0; r < keep; ++r) {
                    for (int k = 0; k < len; ++k) {
                        out << words[i + r * len + k];
                    }
                }
                i += reps * len;
                // Drop a trailing partial copy of the phrase (or cut-off word) when the decoder hit max_tokens mid-repeat
                const int rem = n - i;
                if (rem > 0 && rem <= len) {
                    bool prefixMatch = true;
                    for (int k = 0; k < rem; ++k) {
                        if (keys[i + k].isEmpty() ||
                            (keys[i + k] != keys[i - len + k] &&
                             !(k + 1 == rem && keys[i - len + k].startsWith(keys[i + k])))) {
                            prefixMatch = false;
                            break;
                        }
                    }
                    if (prefixMatch) {
                        i = n;
                    }
                }
                collapsed = true;
                break;
            }
        }
        if (!collapsed) {
            out << words[i];
            ++i;
        }
    }

    QString result = out.join(QLatin1Char(' '));
    while (result.endsWith(QLatin1Char(','))) {
        result.chop(1);
    }
    return result;
}

QString TranscriptionEngine::cleanWhisperOutput(const QString &raw)
{
    QString s = raw.trimmed();
    if (s.isEmpty()) {
        return QString();
    }

    static const QRegularExpression bracketTagRe(QStringLiteral(R"(\[[^\]]*\]|\([^\)]*\)|\*[^\*]*\*)"));
    s.remove(bracketTagRe);
    // Whisper's YouTube-caption hallucinations, which can also prefix real speech
    static const QRegularExpression captionJunkRe(
        QStringLiteral(R"(\b(?:thanks?(?: you)? (?:so much )?for watching(?: the video)?|please subscribe(?: to (?:my|the) channel)?|subtitles by [^.]*|amara\.org)[.!,]?\s*)"),
        QRegularExpression::CaseInsensitiveOption);
    s.remove(captionJunkRe);
    s = collapseRepetitions(s.simplified());

    static const QStringList hallucinations = {
        QStringLiteral("thank you for watching"),
        QStringLiteral("thanks for watching"),
        QStringLiteral("subtitles by"),
        QStringLiteral("amara.org"),
        QStringLiteral("please subscribe"),
        QStringLiteral("you")
    };
    const QString lower = s.toLower();
    for (const QString &h : hallucinations) {
        if (lower == h || lower == (h + QLatin1Char('.'))) {
            return QString();
        }
    }

    return s;
}

// ─────────────────────────────────────────────────────────────────────────────
// HAM RADIO PHONETIC ALPHABET, NUMBER & JARGON PARSER
// ─────────────────────────────────────────────────────────────────────────────
static QString normalizeHamTranscriptUnlocked(const QString &raw)
{
    QString s = raw.simplified();
    if (s.isEmpty()) {
        return QString();
    }

    struct RegexReplacement {
        QRegularExpression re;
        QString replacement;
    };
    static const std::vector<RegexReplacement> phraseRules = {
        { QRegularExpression(QStringLiteral(R"(\b(?:x|ex)[\s-]+ray\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("xray") },
        { QRegularExpression(QStringLiteral(R"(\bfox[\s-]+trot\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("foxtrot") },
        { QRegularExpression(QStringLiteral(R"(\b(?:see\s+cue|seek\s+you|c[\s\.]+q)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("CQ") },
        { QRegularExpression(QStringLiteral(R"(\bCQCQCQ\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("CQ CQ CQ") },
        { QRegularExpression(QStringLiteral(R"(\bCQCQ\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("CQ CQ") },
        { QRegularExpression(QStringLiteral(R"(\b(?:q[\s\.]*r[\s\.]*z(?:ed|ee)?|cue\s+are\s+z(?:ee|ed))\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRZ") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*s[\s\.]*l\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QSL") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*s[\s\.]*o\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QSO") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*t[\s\.]*h\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QTH") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*m\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRM") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*n\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRN") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*p\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRP") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*o\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRO") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*s[\s\.]*y\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QSY") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*s[\s\.]*b\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QSB") },
        { QRegularExpression(QStringLiteral(R"(\bd[\s\.]*m[\s\.]*r\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("DMR") },
        { QRegularExpression(QStringLiteral(R"(\btalk\s+group\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("TG") },
        { QRegularExpression(QStringLiteral(R"(\bfive[\s-]+(?:by[\s-]+|and[\s-]+)?nine\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("59") },
        { QRegularExpression(QStringLiteral(R"(\b5\s*[-–]\s*9\b)")), QStringLiteral("59") },
        { QRegularExpression(QStringLiteral(R"(\bcinco\s+(?:por\s+|y\s+)?nueve\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("59") },
        { QRegularExpression(QStringLiteral(R"(\bcinque\s+(?:per\s+|e\s+)?nove\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("59") },
        { QRegularExpression(QStringLiteral(R"(\bf(?:ü|ue)nf\s+(?:und\s+)?neun\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("59") },
        { QRegularExpression(QStringLiteral(R"(\bcinq\s+(?:sur\s+|et\s+)?neuf\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("59") },
        { QRegularExpression(QStringLiteral(R"(\b(?:seventy[\s-]+three(?:s|'s)?|seven\s+three(?:s|'s)?|setenta\s+y\s+tres|settanta[\s-]*tr[eé]|dreiundsiebzig|soixante[\s-]+treize|setenta\s+e\s+tr[eê]s)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("73") },
        // Signal reports Whisper writes as a height (5'9") or a decimal (5.9)
        { QRegularExpression(QStringLiteral(R"(\b5\s*['’]\s*9(?:\s*["”])?)")), QStringLiteral("59") },
        { QRegularExpression(QStringLiteral(R"(\b(signal(?:\s+report)?(?:\s+is)?|report(?:\s+is)?|you(?:'re|\s+are))\s+5[.\s]9\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\\1 59") },
        { QRegularExpression(QStringLiteral(R"(\beighty[\s-]+eight\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("88") },
        // Acoustic splits Whisper & Parakeet make on 8 kHz radio audio for NATO phonetics
        { QRegularExpression(QStringLiteral(R"(\bpop[\s-]+up\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("papa") },
        { QRegularExpression(QStringLiteral(R"(\b(?:all|al|l|alpha)\s+for\b(?=\s+(?:alpha|alfa|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet|juliett|kilo|lima|mike|november|oscar|papa|pappa|poppa|pop|quebec|romeo|sierra|tango|uniform|victor|whiskey|whisky|xray|yankee|zulu|number|numeral|digit|figure)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("alpha") },
        { QRegularExpression(QStringLiteral(R"(\b(?:also|out\s+of|as\s+a)\s+(?=(?:bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet|juliett|kilo|lima|mike|november|oscar|papa|pappa|poppa|quebec|romeo|sierra|tango|uniform|victor|whiskey|whisky|xray|yankee|zulu)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("alpha ") },
        { QRegularExpression(QStringLiteral(R"(\ba[ls]sa?[bp]ravo\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("alpha bravo") },
        { QRegularExpression(QStringLiteral(R"(\bwith\s+k(?:ey|i|iwan)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("whiskey") },
        { QRegularExpression(QStringLiteral(R"(\bpapa,?\s+beautiful\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("papa uniform") },
        { QRegularExpression(QStringLiteral(R"(\bpapa,?\s+(?:papa,?\s+)?(?:i\s+)?b(?:y|uy)\s+uniform\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("papa papa uniform") },
        { QRegularExpression(QStringLiteral(R"(\b(?:roll\s+me\s+(?:out|oh|o)|row\s+me\s+(?:oh|o)|roam\s+me\s+(?:oh|o))\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("romeo") },
        { QRegularExpression(QStringLiteral(R"(\b(alpha|alfa|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet|juliett|kilo|lima|mike|november|oscar|papa|quebec|romeo|sierra|tango|uniform|victor|whiskey|whisky|xray|yankee|zulu)\s+in\s+(?:the|a)(?=\s+(?:alpha|alfa|bravo|charlie|delta|echo|foxtrot|golf|hotel|india|juliet|juliett|kilo|lima|mike|november|oscar|papa|quebec|romeo|sierra|tango|uniform|victor|whiskey|whisky|xray|yankee|zulu)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\\1 india") },
        { QRegularExpression(QStringLiteral(R"(\be\s+ar([wknv])\s+([a-z][0-9][a-z]{2,3})\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("here \\1\\2") },
        // Acoustic splits on 8 kHz radio audio for "local repeater" / "repeater" (must run before phoneticMap sees "peter")
        { QRegularExpression(QStringLiteral(R"(\b(?:low[\s-]*(?:col(?:ou?r|lar)|call|cal)|lo[\s-]*col(?:ou?r|lar)|loco)\s+(?:(?:a|er|her|re|the)[\s-]+)?(?:peter|peater|repeater)(s)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("local repeater\\1") },
        { QRegularExpression(QStringLiteral(R"(\blocal\s+(?:(?:a|er|her|re|the)[\s-]+)?(?:peter|peater)(s)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("local repeater\\1") },
        { QRegularExpression(QStringLiteral(R"(\b(dmr|analog|analogue|uhf|vhf|2[\s-]*meter|2m|70[\s-]*cm|70[\s-]*centimeter|linked|club|city|mountain|hilltop|open)\s+(?:re[\s-]*)?(?:peter|peater)(s)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\\1 repeater\\2") },
        { QRegularExpression(QStringLiteral(R"(\b(on\s+the|on\s+a|hit\s+the|hitting\s+the|key(?:ing)?\s+(?:up\s+)?the|into\s+the|through\s+the|via\s+the|from\s+the)\s+(?:re[\s-]*)?(?:peter|peater)(s)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("\\1 repeater\\2") },
        { QRegularExpression(QStringLiteral(R"(\bre[\s-]+(?:peter|peater)(s)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("repeater\\1") }
    };

    for (const auto &rule : phraseRules) {
        s.replace(rule.re, rule.replacement);
    }

    static const QRegularExpression dottedInitialsRe(QStringLiteral(R"(\b([A-Za-z0-9])\.(?=[A-Za-z0-9]\b))"));
    s.replace(dottedInitialsRe, QStringLiteral("\\1 "));
    static const QRegularExpression hyphenChainRe(QStringLiteral(R"(\b([A-Za-z]{1,10})-([A-Za-z0-9]{1,10})(?:-([A-Za-z0-9]{1,10}))?(?:-([A-Za-z0-9]{1,10}))?(?=[.,!?;:]|\s|$))"));
    s.replace(hyphenChainRe, QStringLiteral("\\1 \\2 \\3 \\4"));
    static const QRegularExpression spaceBeforePunctRe(QStringLiteral(R"(\s+([.,!?;:]))"));
    s.replace(spaceBeforePunctRe, QStringLiteral("\\1"));
    s = s.simplified();

    struct PhoneticInfo {
        char ch { 0 };
        bool isPrimary { false };
    };

    static const QHash<QString, PhoneticInfo> phoneticMap = {
        { QStringLiteral("alpha"),      {'A', true} },
        { QStringLiteral("alfa"),       {'A', true} },
        { QStringLiteral("alpa"),       {'A', true} },
        { QStringLiteral("elpha"),      {'A', true} },
        { QStringLiteral("alva"),       {'A', true} },
        { QStringLiteral("america"),    {'A', false} },
        { QStringLiteral("amsterdam"),  {'A', false} },
        { QStringLiteral("able"),       {'A', false} },
        { QStringLiteral("adam"),       {'A', false} },
        { QStringLiteral("bravo"),      {'B', true} },
        { QStringLiteral("brava"),      {'B', true} },
        { QStringLiteral("boston"),     {'B', false} },
        { QStringLiteral("baltimore"),  {'B', false} },
        { QStringLiteral("baker"),      {'B', false} },
        { QStringLiteral("brazil"),     {'B', false} },
        { QStringLiteral("belgium"),    {'B', false} },
        { QStringLiteral("charlie"),    {'C', true} },
        { QStringLiteral("charly"),     {'C', true} },
        { QStringLiteral("charli"),     {'C', true} },
        { QStringLiteral("sharlie"),    {'C', true} },
        { QStringLiteral("ciarli"),     {'C', true} },
        { QStringLiteral("canada"),     {'C', false} },
        { QStringLiteral("california"), {'C', false} },
        { QStringLiteral("columbia"),   {'C', false} },
        { QStringLiteral("delta"),      {'D', true} },
        { QStringLiteral("deltha"),     {'D', true} },
        { QStringLiteral("zelta"),      {'D', true} },
        { QStringLiteral("denmark"),    {'D', false} },
        { QStringLiteral("david"),      {'D', false} },
        { QStringLiteral("dog"),        {'D', false} },
        { QStringLiteral("echo"),       {'E', true} },
        { QStringLiteral("eco"),        {'E', true} },
        { QStringLiteral("eko"),        {'E', true} },
        { QStringLiteral("ecco"),       {'E', true} },
        { QStringLiteral("england"),    {'E', false} },
        { QStringLiteral("edward"),     {'E', false} },
        { QStringLiteral("easy"),       {'E', false} },
        { QStringLiteral("europe"),     {'E', false} },
        { QStringLiteral("foxtrot"),    {'F', true} },
        { QStringLiteral("foxtrod"),    {'F', true} },
        { QStringLiteral("fox"),        {'F', false} },
        { QStringLiteral("florida"),    {'F', false} },
        { QStringLiteral("frank"),      {'F', false} },
        { QStringLiteral("france"),     {'F', false} },
        { QStringLiteral("finland"),    {'F', false} },
        { QStringLiteral("golf"),       {'G', true} },
        { QStringLiteral("gulf"),       {'G', true} },
        { QStringLiteral("golph"),      {'G', true} },
        { QStringLiteral("germany"),    {'G', false} },
        { QStringLiteral("george"),     {'G', false} },
        { QStringLiteral("guatemala"),  {'G', false} },
        { QStringLiteral("geneva"),     {'G', false} },
        { QStringLiteral("hotel"),      {'H', true} },
        { QStringLiteral("hotell"),     {'H', true} },
        { QStringLiteral("honolulu"),   {'H', false} },
        { QStringLiteral("henry"),      {'H', false} },
        { QStringLiteral("hawaii"),     {'H', false} },
        { QStringLiteral("how"),        {'H', false} },
        { QStringLiteral("india"),      {'I', true} },
        { QStringLiteral("indya"),      {'I', true} },
        { QStringLiteral("italy"),      {'I', false} },
        { QStringLiteral("italia"),     {'I', false} },
        { QStringLiteral("ida"),        {'I', false} },
        { QStringLiteral("item"),       {'I', false} },
        { QStringLiteral("juliet"),     {'J', true} },
        { QStringLiteral("juliett"),    {'J', true} },
        { QStringLiteral("juliette"),   {'J', true} },
        { QStringLiteral("julieta"),    {'J', true} },
        { QStringLiteral("giulietta"),  {'J', true} },
        { QStringLiteral("julia"),      {'J', false} },
        { QStringLiteral("japan"),      {'J', false} },
        { QStringLiteral("jack"),       {'J', false} },
        { QStringLiteral("jig"),        {'J', false} },
        { QStringLiteral("john"),       {'J', false} },
        { QStringLiteral("kilo"),       {'K', true} },
        { QStringLiteral("killo"),      {'K', true} },
        { QStringLiteral("keelo"),      {'K', true} },
        { QStringLiteral("chilo"),      {'K', true} },
        { QStringLiteral("quilo"),      {'K', true} },
        { QStringLiteral("kilowatt"),   {'K', true} },
        { QStringLiteral("kentucky"),   {'K', false} },
        { QStringLiteral("king"),       {'K', false} },
        { QStringLiteral("lima"),       {'L', true} },
        { QStringLiteral("leema"),      {'L', true} },
        { QStringLiteral("limma"),      {'L', true} },
        { QStringLiteral("lena"),       {'L', false} },
        { QStringLiteral("london"),     {'L', false} },
        { QStringLiteral("luxembourg"), {'L', false} },
        { QStringLiteral("love"),       {'L', false} },
        { QStringLiteral("lincoln"),    {'L', false} },
        { QStringLiteral("mike"),       {'M', true} },
        { QStringLiteral("myke"),       {'M', true} },
        { QStringLiteral("maik"),       {'M', true} },
        { QStringLiteral("mic"),        {'M', false} },
        { QStringLiteral("mexico"),     {'M', false} },
        { QStringLiteral("montreal"),   {'M', false} },
        { QStringLiteral("mary"),       {'M', false} },
        { QStringLiteral("mother"),     {'M', false} },
        { QStringLiteral("madagascar"), {'M', false} },
        { QStringLiteral("november"),   {'N', true} },
        { QStringLiteral("novemba"),    {'N', true} },
        { QStringLiteral("noviembre"),  {'N', true} },
        { QStringLiteral("novembre"),   {'N', true} },
        { QStringLiteral("norway"),     {'N', false} },
        { QStringLiteral("nancy"),      {'N', false} },
        { QStringLiteral("nectar"),     {'N', false} },
        { QStringLiteral("nan"),        {'N', false} },
        { QStringLiteral("oscar"),      {'O', true} },
        { QStringLiteral("óscar"),      {'O', true} },
        { QStringLiteral("oskar"),      {'O', true} },
        { QStringLiteral("osker"),      {'O', true} },
        { QStringLiteral("ontario"),    {'O', false} },
        { QStringLiteral("ocean"),      {'O', false} },
        { QStringLiteral("oboe"),       {'O', false} },
        { QStringLiteral("oliver"),     {'O', false} },
        { QStringLiteral("papa"),       {'P', true} },
        { QStringLiteral("papá"),       {'P', true} },
        { QStringLiteral("pappa"),      {'P', true} },
        { QStringLiteral("poppa"),      {'P', true} },
        { QStringLiteral("papas"),      {'P', true} },
        { QStringLiteral("pupa"),       {'P', true} },
        { QStringLiteral("pepper"),     {'P', false} },
        { QStringLiteral("pacific"),    {'P', false} },
        { QStringLiteral("portugal"),   {'P', false} },
        { QStringLiteral("peter"),      {'P', false} },
        { QStringLiteral("paris"),      {'P', false} },
        { QStringLiteral("quebec"),     {'Q', true} },
        { QStringLiteral("québec"),     {'Q', true} },
        { QStringLiteral("kebeck"),     {'Q', true} },
        { QStringLiteral("quebeck"),    {'Q', true} },
        { QStringLiteral("kebec"),      {'Q', true} },
        { QStringLiteral("queen"),      {'Q', false} },
        { QStringLiteral("quito"),      {'Q', false} },
        { QStringLiteral("romeo"),      {'R', true} },
        { QStringLiteral("romio"),      {'R', true} },
        { QStringLiteral("romeu"),      {'R', true} },
        { QStringLiteral("radio"),      {'R', false} },
        { QStringLiteral("roger"),      {'R', false} },
        { QStringLiteral("romania"),    {'R', false} },
        { QStringLiteral("robert"),     {'R', false} },
        { QStringLiteral("sierra"),     {'S', true} },
        { QStringLiteral("siera"),      {'S', true} },
        { QStringLiteral("ciara"),      {'S', true} },
        { QStringLiteral("syria"),      {'S', false} },
        { QStringLiteral("sugar"),      {'S', false} },
        { QStringLiteral("santiago"),   {'S', false} },
        { QStringLiteral("spain"),      {'S', false} },
        { QStringLiteral("sweden"),     {'S', false} },
        { QStringLiteral("sam"),        {'S', false} },
        { QStringLiteral("tango"),      {'T', true} },
        { QStringLiteral("tanga"),      {'T', true} },
        { QStringLiteral("tokyo"),      {'T', false} },
        { QStringLiteral("texas"),      {'T', false} },
        { QStringLiteral("tare"),       {'T', false} },
        { QStringLiteral("tom"),        {'T', false} },
        { QStringLiteral("toronto"),    {'T', false} },
        { QStringLiteral("uniform"),    {'U', true} },
        { QStringLiteral("uniforme"),   {'U', true} },
        { QStringLiteral("unifrom"),    {'U', true} },
        { QStringLiteral("unifom"),     {'U', true} },
        { QStringLiteral("unicorn"),    {'U', true} },
        { QStringLiteral("united"),     {'U', false} },
        { QStringLiteral("utah"),       {'U', false} },
        { QStringLiteral("uncle"),      {'U', false} },
        { QStringLiteral("uruguay"),    {'U', false} },
        { QStringLiteral("victor"),     {'V', true} },
        { QStringLiteral("víctor"),     {'V', true} },
        { QStringLiteral("viktor"),     {'V', true} },
        { QStringLiteral("victa"),      {'V', true} },
        { QStringLiteral("vector"),     {'V', true} },
        { QStringLiteral("victoria"),   {'V', false} },
        { QStringLiteral("venezuela"),  {'V', false} },
        { QStringLiteral("whiskey"),    {'W', true} },
        { QStringLiteral("whisky"),     {'W', true} },
        { QStringLiteral("wiskey"),     {'W', true} },
        { QStringLiteral("wisky"),      {'W', true} },
        { QStringLiteral("washington"), {'W', false} },
        { QStringLiteral("william"),    {'W', false} },
        { QStringLiteral("world"),      {'W', false} },
        { QStringLiteral("xray"),       {'X', true} },
        { QStringLiteral("exray"),      {'X', true} },
        { QStringLiteral("xylophone"),  {'X', true} },
        { QStringLiteral("xilophone"),  {'X', true} },
        { QStringLiteral("yankee"),     {'Y', true} },
        { QStringLiteral("yanqui"),     {'Y', true} },
        { QStringLiteral("yanky"),      {'Y', true} },
        { QStringLiteral("yanke"),      {'Y', true} },
        { QStringLiteral("yokohama"),   {'Y', false} },
        { QStringLiteral("yellow"),     {'Y', false} },
        { QStringLiteral("young"),      {'Y', false} },
        { QStringLiteral("zulu"),       {'Z', true} },
        { QStringLiteral("zulú"),       {'Z', true} },
        { QStringLiteral("zula"),       {'Z', true} },
        { QStringLiteral("zoulou"),     {'Z', true} },
        { QStringLiteral("zanzibar"),   {'Z', true} },
        { QStringLiteral("zebra"),      {'Z', true} },
        { QStringLiteral("zed"),        {'Z', true} },
        { QStringLiteral("zee"),        {'Z', true} },
        { QStringLiteral("zeta"),       {'Z', true} },
        { QStringLiteral("zurich"),     {'Z', false} }
    };

    static const QHash<QString, QString> numberWordMap = {
        { QStringLiteral("zero"),       QStringLiteral("0") },
        { QStringLiteral("cero"),       QStringLiteral("0") },
        { QStringLiteral("zéro"),       QStringLiteral("0") },
        { QStringLiteral("null"),       QStringLiteral("0") },
        { QStringLiteral("nadazero"),   QStringLiteral("0") },
        { QStringLiteral("one"),        QStringLiteral("1") },
        { QStringLiteral("uno"),        QStringLiteral("1") },
        { QStringLiteral("eins"),       QStringLiteral("1") },
        { QStringLiteral("unaone"),     QStringLiteral("1") },
        { QStringLiteral("two"),        QStringLiteral("2") },
        { QStringLiteral("dos"),        QStringLiteral("2") },
        { QStringLiteral("due"),        QStringLiteral("2") },
        { QStringLiteral("zwei"),       QStringLiteral("2") },
        { QStringLiteral("zwo"),        QStringLiteral("2") },
        { QStringLiteral("deux"),       QStringLiteral("2") },
        { QStringLiteral("dois"),       QStringLiteral("2") },
        { QStringLiteral("bissotwo"),   QStringLiteral("2") },
        { QStringLiteral("three"),      QStringLiteral("3") },
        { QStringLiteral("tree"),       QStringLiteral("3") },
        { QStringLiteral("tres"),       QStringLiteral("3") },
        { QStringLiteral("três"),       QStringLiteral("3") },
        { QStringLiteral("tre"),        QStringLiteral("3") },
        { QStringLiteral("drei"),       QStringLiteral("3") },
        { QStringLiteral("trois"),      QStringLiteral("3") },
        { QStringLiteral("terrathree"), QStringLiteral("3") },
        { QStringLiteral("four"),       QStringLiteral("4") },
        { QStringLiteral("fower"),      QStringLiteral("4") },
        { QStringLiteral("cuatro"),     QStringLiteral("4") },
        { QStringLiteral("quattro"),    QStringLiteral("4") },
        { QStringLiteral("vier"),       QStringLiteral("4") },
        { QStringLiteral("quatre"),     QStringLiteral("4") },
        { QStringLiteral("kartefour"),  QStringLiteral("4") },
        { QStringLiteral("five"),       QStringLiteral("5") },
        { QStringLiteral("fife"),       QStringLiteral("5") },
        { QStringLiteral("cinco"),      QStringLiteral("5") },
        { QStringLiteral("cinque"),     QStringLiteral("5") },
        { QStringLiteral("fünf"),       QStringLiteral("5") },
        { QStringLiteral("fuenf"),      QStringLiteral("5") },
        { QStringLiteral("cinq"),       QStringLiteral("5") },
        { QStringLiteral("pantafive"),  QStringLiteral("5") },
        { QStringLiteral("six"),        QStringLiteral("6") },
        { QStringLiteral("seis"),       QStringLiteral("6") },
        { QStringLiteral("sei"),        QStringLiteral("6") },
        { QStringLiteral("sechs"),      QStringLiteral("6") },
        { QStringLiteral("soxisix"),    QStringLiteral("6") },
        { QStringLiteral("seven"),      QStringLiteral("7") },
        { QStringLiteral("siete"),      QStringLiteral("7") },
        { QStringLiteral("sette"),      QStringLiteral("7") },
        { QStringLiteral("sieben"),     QStringLiteral("7") },
        { QStringLiteral("sept"),       QStringLiteral("7") },
        { QStringLiteral("sete"),       QStringLiteral("7") },
        { QStringLiteral("setteseven"), QStringLiteral("7") },
        { QStringLiteral("eight"),      QStringLiteral("8") },
        { QStringLiteral("ocho"),       QStringLiteral("8") },
        { QStringLiteral("otto"),       QStringLiteral("8") },
        { QStringLiteral("acht"),       QStringLiteral("8") },
        { QStringLiteral("huit"),       QStringLiteral("8") },
        { QStringLiteral("oito"),       QStringLiteral("8") },
        { QStringLiteral("oktoeight"),  QStringLiteral("8") },
        { QStringLiteral("nine"),       QStringLiteral("9") },
        { QStringLiteral("niner"),      QStringLiteral("9") },
        { QStringLiteral("nueve"),      QStringLiteral("9") },
        { QStringLiteral("nove"),       QStringLiteral("9") },
        { QStringLiteral("neun"),       QStringLiteral("9") },
        { QStringLiteral("neuf"),       QStringLiteral("9") },
        { QStringLiteral("novenine"),   QStringLiteral("9") },
        { QStringLiteral("ten"),        QStringLiteral("10") }
    };

    static const QHash<QString, QString> homophoneNumberMap = {
        { QStringLiteral("oh"),  QStringLiteral("0") },
        { QStringLiteral("won"), QStringLiteral("1") },
        { QStringLiteral("too"), QStringLiteral("2") },
        { QStringLiteral("to"),  QStringLiteral("2") },
        { QStringLiteral("for"), QStringLiteral("4") },
        { QStringLiteral("ate"), QStringLiteral("8") }
    };

    static const QHash<QString, QString> tensWordMap = {
        { QStringLiteral("twenty"),   QStringLiteral("2") },
        { QStringLiteral("thirty"),   QStringLiteral("3") },
        { QStringLiteral("forty"),    QStringLiteral("4") },
        { QStringLiteral("fourty"),   QStringLiteral("4") },
        { QStringLiteral("fifty"),    QStringLiteral("5") },
        { QStringLiteral("sixty"),    QStringLiteral("6") },
        { QStringLiteral("seventy"),  QStringLiteral("7") },
        { QStringLiteral("eighty"),   QStringLiteral("8") },
        { QStringLiteral("ninety"),   QStringLiteral("9") },
        { QStringLiteral("veinte"),   QStringLiteral("2") },
        { QStringLiteral("treinta"),  QStringLiteral("3") },
        { QStringLiteral("cuarenta"), QStringLiteral("4") },
        { QStringLiteral("cincuenta"),QStringLiteral("5") },
        { QStringLiteral("sesenta"),  QStringLiteral("6") },
        { QStringLiteral("setenta"),  QStringLiteral("7") },
        { QStringLiteral("ochenta"),  QStringLiteral("8") },
        { QStringLiteral("noventa"),  QStringLiteral("9") }
    };

    static const QSet<QString> nonCallsignWordSet = {
        // Common 2-4 letter English / ES / DE / FR / IT conversational words + Ham abbreviations
        QStringLiteral("in"), QStringLiteral("on"), QStringLiteral("at"), QStringLiteral("to"),
        QStringLiteral("of"), QStringLiteral("by"), QStringLiteral("is"), QStringLiteral("it"),
        QStringLiteral("or"), QStringLiteral("as"), QStringLiteral("be"), QStringLiteral("we"),
        QStringLiteral("he"), QStringLiteral("me"), QStringLiteral("my"), QStringLiteral("no"),
        QStringLiteral("go"), QStringLiteral("do"), QStringLiteral("up"), QStringLiteral("if"),
        QStringLiteral("an"), QStringLiteral("so"), QStringLiteral("uh"), QStringLiteral("um"),
        QStringLiteral("ah"), QStringLiteral("oh"), QStringLiteral("eh"), QStringLiteral("ok"),
        QStringLiteral("hi"), QStringLiteral("us"), QStringLiteral("am"), QStringLiteral("im"),
        QStringLiteral("el"), QStringLiteral("la"), QStringLiteral("en"), QStringLiteral("un"),
        QStringLiteral("es"), QStringLiteral("mi"), QStringLiteral("tu"), QStringLiteral("su"),
        QStringLiteral("si"), QStringLiteral("ya"), QStringLiteral("da"), QStringLiteral("di"),
        QStringLiteral("il"), QStringLiteral("le"), QStringLiteral("du"), QStringLiteral("au"),
        QStringLiteral("et"), QStringLiteral("ou"), QStringLiteral("zu"), QStringLiteral("ab"),
        QStringLiteral("er"), QStringLiteral("wo"), QStringLiteral("de"), QStringLiteral("cq"),
        QStringLiteral("dx"), QStringLiteral("om"), QStringLiteral("yl"), QStringLiteral("tg"),
        QStringLiteral("id"), QStringLiteral("fm"), QStringLiteral("cw"), QStringLiteral("hf"),
        QStringLiteral("rx"), QStringLiteral("tx"), QStringLiteral("rf"), QStringLiteral("ac"),
        QStringLiteral("dc"), QStringLiteral("db"), QStringLiteral("ht"),
        QStringLiteral("and"), QStringLiteral("the"), QStringLiteral("for"), QStringLiteral("are"),
        QStringLiteral("but"), QStringLiteral("not"), QStringLiteral("you"), QStringLiteral("all"),
        QStringLiteral("any"), QStringLiteral("can"), QStringLiteral("had"), QStringLiteral("her"),
        QStringLiteral("was"), QStringLiteral("one"), QStringLiteral("our"), QStringLiteral("out"),
        QStringLiteral("day"), QStringLiteral("get"), QStringLiteral("has"), QStringLiteral("him"),
        QStringLiteral("his"), QStringLiteral("how"), QStringLiteral("man"), QStringLiteral("new"),
        QStringLiteral("now"), QStringLiteral("old"), QStringLiteral("see"), QStringLiteral("two"),
        QStringLiteral("way"), QStringLiteral("who"), QStringLiteral("boy"), QStringLiteral("did"),
        QStringLiteral("its"), QStringLiteral("let"), QStringLiteral("put"), QStringLiteral("say"),
        QStringLiteral("she"), QStringLiteral("too"), QStringLiteral("use"), QStringLiteral("yes"),
        QStringLiteral("yet"), QStringLiteral("big"), QStringLiteral("end"), QStringLiteral("far"),
        QStringLiteral("few"), QStringLiteral("got"), QStringLiteral("lot"), QStringLiteral("may"),
        QStringLiteral("off"), QStringLiteral("own"), QStringLiteral("ran"), QStringLiteral("run"),
        QStringLiteral("set"), QStringLiteral("sit"), QStringLiteral("ten"), QStringLiteral("top"),
        QStringLiteral("try"), QStringLiteral("war"), QStringLiteral("why"), QStringLiteral("win"),
        QStringLiteral("air"), QStringLiteral("ago"), QStringLiteral("bad"), QStringLiteral("car"),
        QStringLiteral("cut"), QStringLiteral("low"), QStringLiteral("hot"), QStringLiteral("sun"),
        QStringLiteral("sky"), QStringLiteral("sea"), QStringLiteral("rig"), QStringLiteral("ham"),
        QStringLiteral("net"), QStringLiteral("log"), QStringLiteral("app"), QStringLiteral("mic"),
        QStringLiteral("agc"), QStringLiteral("ptt"), QStringLiteral("swr"), QStringLiteral("uhf"),
        QStringLiteral("vhf"), QStringLiteral("dmr"), QStringLiteral("ysf"), QStringLiteral("p25"),
        QStringLiteral("m17"), QStringLiteral("ssb"), QStringLiteral("usb"), QStringLiteral("lsb"),
        QStringLiteral("qsl"), QStringLiteral("qso"), QStringLiteral("qrz"), QStringLiteral("qth"),
        QStringLiteral("qrm"), QStringLiteral("qrn"), QStringLiteral("qrp"), QStringLiteral("qro"),
        QStringLiteral("qsy"), QStringLiteral("qsb"), QStringLiteral("qrt"), QStringLiteral("qrv"),
        QStringLiteral("qrx"), QStringLiteral("rst"), QStringLiteral("qra"), QStringLiteral("qtr"),
        QStringLiteral("con"), QStringLiteral("por"), QStringLiteral("una"), QStringLiteral("uno"),
        QStringLiteral("del"), QStringLiteral("los"), QStringLiteral("las"), QStringLiteral("muy"),
        QStringLiteral("mas"), QStringLiteral("sin"), QStringLiteral("sur"), QStringLiteral("par"),
        QStringLiteral("les"), QStringLiteral("des"), QStringLiteral("est"), QStringLiteral("son"),
        QStringLiteral("une"), QStringLiteral("aux"), QStringLiteral("ich"), QStringLiteral("ein"),
        QStringLiteral("und"), QStringLiteral("der"), QStringLiteral("die"), QStringLiteral("das"),
        QStringLiteral("mit"), QStringLiteral("auf"), QStringLiteral("von"), QStringLiteral("ist"),
        QStringLiteral("wir"), QStringLiteral("sie"), QStringLiteral("aus"), QStringLiteral("bei"),
        QStringLiteral("che"), QStringLiteral("non"), QStringLiteral("per"), QStringLiteral("con"),
        QStringLiteral("mia"), QStringLiteral("mio"), QStringLiteral("sua"), QStringLiteral("suo"),
        QStringLiteral("here"), QStringLiteral("from"), QStringLiteral("that"), QStringLiteral("this"),
        QStringLiteral("with"), QStringLiteral("have"), QStringLiteral("will"), QStringLiteral("your"),
        QStringLiteral("good"), QStringLiteral("copy"), QStringLiteral("over"), QStringLiteral("back"),
        QStringLiteral("into"), QStringLiteral("very"), QStringLiteral("just"), QStringLiteral("like"),
        QStringLiteral("long"), QStringLiteral("make"), QStringLiteral("many"), QStringLiteral("more"),
        QStringLiteral("most"), QStringLiteral("much"), QStringLiteral("name"), QStringLiteral("only"),
        QStringLiteral("some"), QStringLiteral("such"), QStringLiteral("take"), QStringLiteral("than"),
        QStringLiteral("them"), QStringLiteral("then"), QStringLiteral("they"), QStringLiteral("time"),
        QStringLiteral("well"), QStringLiteral("were"), QStringLiteral("what"), QStringLiteral("when"),
        QStringLiteral("work"), QStringLiteral("year"), QStringLiteral("also"), QStringLiteral("call"),
        QStringLiteral("talk"), QStringLiteral("watt"), QStringLiteral("beam"), QStringLiteral("coax"),
        QStringLiteral("band"), QStringLiteral("mode"), QStringLiteral("slot"), QStringLiteral("link"),
        QStringLiteral("node"), QStringLiteral("spot"), QStringLiteral("used"), QStringLiteral("live"),
        QStringLiteral("yeah"), QStringLiteral("okay"), QStringLiteral("fine"), QStringLiteral("loud"),
        QStringLiteral("weak"), QStringLiteral("high"), QStringLiteral("home"), QStringLiteral("city"),
        QStringLiteral("town"), QStringLiteral("area"), QStringLiteral("zone"), QStringLiteral("grid"),
        QStringLiteral("club"), QStringLiteral("test"), QStringLiteral("best"), QStringLiteral("next")
    };

    static const QSet<QString> pronounIVerbs = {
        QStringLiteral("am"), QStringLiteral("was"), QStringLiteral("have"), QStringLiteral("had"),
        QStringLiteral("will"), QStringLiteral("would"), QStringLiteral("can"), QStringLiteral("could"),
        QStringLiteral("used"), QStringLiteral("think"), QStringLiteral("know"), QStringLiteral("see"),
        QStringLiteral("hear"), QStringLiteral("copy"), QStringLiteral("live"), QStringLiteral("lived"),
        QStringLiteral("work"), QStringLiteral("worked"), QStringLiteral("got"), QStringLiteral("just"),
        QStringLiteral("want"), QStringLiteral("need"), QStringLiteral("like"), QStringLiteral("love"),
        QStringLiteral("hope"), QStringLiteral("wish"), QStringLiteral("went"), QStringLiteral("came"),
        QStringLiteral("said"), QStringLiteral("told"), QStringLiteral("run"), QStringLiteral("use"),
        QStringLiteral("usually"), QStringLiteral("always"), QStringLiteral("never"), QStringLiteral("also"),
        QStringLiteral("still"), QStringLiteral("only"), QStringLiteral("really"), QStringLiteral("dont")
    };

    auto isPureDigits = [](const QString &lc) -> bool {
        if (lc.isEmpty() || lc.size() > 4) return false;
        for (QChar c : lc) {
            if (!c.isDigit()) return false;
        }
        return true;
    };

    auto isAsciiAlphaOnly = [](const QString &s) -> bool {
        if (s.isEmpty()) return false;
        for (QChar c : s) {
            const QChar l = c.toLower();
            if (l < QLatin1Char('a') || l > QLatin1Char('z')) return false;
        }
        return true;
    };

    auto isAllAsciiUpper = [](const QString &s) -> bool {
        if (s.isEmpty()) return false;
        for (QChar c : s) {
            if (c < QLatin1Char('A') || c > QLatin1Char('Z')) return false;
        }
        return true;
    };

    auto isSingleAsciiLetter = [](const QString &lc) -> bool {
        return lc.size() == 1 && lc[0] >= QLatin1Char('a') && lc[0] <= QLatin1Char('z');
    };

    auto isNonCallsignAlnumToken = [](const QString &lc) -> bool {
        static const QRegularExpression nonCallRe(
            QStringLiteral(R"(^(?:ft[48]|js8|p25|m17|c4fm|g90|kx[23]|k[1234]|ic\d+|id\d+|ft\d+|ts\d+|tm\d+|th\d+|uv\d+[a-z]*|rt\d+[a-z]*|md\d+[a-z]*|hd[12]|gd\d+[a-z]*|at\d+[a-z]*|rg\d+[a-z]*|pl\d+|so\d+|lmr\d+|3d|4g|5g|wifi\d*|s[1-9]|\d+(?:st|nd|rd|th|s|m|cm|mm|km|hz|khz|mhz|ghz|w|kw|mw|v|mv|a|ma|db|dbi|dbd|ohm))$)"),
            QRegularExpression::CaseInsensitiveOption);
        return nonCallRe.match(lc).hasMatch();
    };

    static const QStringList kPrimaryPhoneticWords = {
        QStringLiteral("november"), QStringLiteral("uniforme"), QStringLiteral("juliette"),
        QStringLiteral("foxtrot"), QStringLiteral("foxtrod"), QStringLiteral("uniform"),
        QStringLiteral("unifrom"), QStringLiteral("unifom"), QStringLiteral("whiskey"),
        QStringLiteral("whisky"), QStringLiteral("wiskey"), QStringLiteral("charlie"),
        QStringLiteral("charly"), QStringLiteral("charli"), QStringLiteral("sharlie"),
        QStringLiteral("juliett"), QStringLiteral("juliet"), QStringLiteral("quebec"),
        QStringLiteral("kebeck"), QStringLiteral("victor"), QStringLiteral("viktor"),
        QStringLiteral("sierra"), QStringLiteral("siera"), QStringLiteral("yankee"),
        QStringLiteral("yanqui"), QStringLiteral("yanky"), QStringLiteral("alpha"),
        QStringLiteral("bravo"), QStringLiteral("brava"), QStringLiteral("delta"),
        QStringLiteral("deltha"), QStringLiteral("hotel"), QStringLiteral("india"),
        QStringLiteral("oscar"), QStringLiteral("oskar"), QStringLiteral("romeo"),
        QStringLiteral("romio"), QStringLiteral("tango"), QStringLiteral("alfa"),
        QStringLiteral("alpa"), QStringLiteral("echo"), QStringLiteral("golf"),
        QStringLiteral("gulf"), QStringLiteral("kilo"), QStringLiteral("lima"),
        QStringLiteral("mike"), QStringLiteral("papa"), QStringLiteral("pappa"),
        QStringLiteral("poppa"), QStringLiteral("xray"), QStringLiteral("zulu")
    };

    struct Token {
        QString raw;
        QString leadPunct;
        QString core;
        QString lowerCore;
        QString trailPunct;
        bool fromGluedOrAlnum { false };
        bool gluedToNext { false };
        bool origHasDigits { false };
        int origGroupId { -1 };
        QString origRaw;
    };

    auto trySegmentGluedToken = [&](const QString &core, QStringList &outParts, bool &outHasDigits) -> bool {
        outParts.clear();
        outHasDigits = false;
        if (core.size() <= 1) return false;
        const QString lc = core.toLower();
        if (phoneticMap.contains(lc) || numberWordMap.contains(lc) || isPureDigits(lc) || isNonCallsignAlnumToken(lc)) {
            return false;
        }
        bool hasAnyDigit = false;
        for (QChar c : core) {
            if (c.isDigit()) {
                hasAnyDigit = true;
            } else {
                const QChar l = c.toLower();
                if (l < QLatin1Char('a') || l > QLatin1Char('z')) {
                    return false;
                }
            }
        }
        outHasDigits = hasAnyDigit;

        auto matchPrimaryPhoneticAt = [&](int pos) -> QString {
            const QString remLower = lc.mid(pos);
            for (const QString &pw : kPrimaryPhoneticWords) {
                if (remLower.startsWith(pw)) {
                    const int afterPos = pos + static_cast<int>(pw.size());
                    if (afterPos == core.size() || core[afterPos].isDigit() || core[afterPos].isUpper()) {
                        return core.mid(pos, pw.size());
                    }
                    // Also allow lowercase phonetic immediately followed by another primary phonetic
                    const QString afterRem = lc.mid(afterPos);
                    for (const QString &pw2 : kPrimaryPhoneticWords) {
                        if (afterRem.startsWith(pw2)) {
                            return core.mid(pos, pw.size());
                        }
                    }
                }
            }
            return QString();
        };

        enum class PartKind { Phonetic, Digits, Letters };
        struct SegPart {
            QString text;
            PartKind kind;
        };
        QVector<SegPart> segs;
        int pos = 0;
        const int len = static_cast<int>(core.size());

        while (pos < len) {
            if (core[pos].isDigit()) {
                int q = pos;
                while (q < len && core[q].isDigit()) ++q;
                const QString dStr = core.mid(pos, q - pos);
                if (dStr.size() > 2) return false;
                segs.push_back({dStr, PartKind::Digits});
                pos = q;
                continue;
            }

            const QString pw = matchPrimaryPhoneticAt(pos);
            if (!pw.isEmpty()) {
                segs.push_back({pw, PartKind::Phonetic});
                pos += pw.size();
                continue;
            }

            int q = pos + 1;
            while (q < len && !core[q].isDigit()) {
                const QString pwAtQ = matchPrimaryPhoneticAt(q);
                if (!pwAtQ.isEmpty()) {
                    const QString prefixLetters = core.mid(pos, q - pos);
                    if (prefixLetters.size() <= 4 && (hasAnyDigit || isAllAsciiUpper(prefixLetters))) {
                        break;
                    }
                }
                ++q;
            }
            const QString lStr = core.mid(pos, q - pos);
            if (lStr.isEmpty() || lStr.size() > 4) return false;
            segs.push_back({lStr, PartKind::Letters});
            pos = q;
        }

        if (segs.size() < 2) return false;

        int numPhon = 0;
        int numDig = 0;
        int numLet = 0;
        bool allLetUpper = true;
        for (const SegPart &sp : segs) {
            if (sp.kind == PartKind::Phonetic) ++numPhon;
            else if (sp.kind == PartKind::Digits) ++numDig;
            else {
                ++numLet;
                if (!isAllAsciiUpper(sp.text)) allLetUpper = false;
            }
        }

        const bool validGluedPhonetic = (numPhon >= 1 && (allLetUpper || numDig >= 1 || numLet == 0));
        const bool validAlnumCall = (numDig >= 1 && (numLet >= 1 || numPhon >= 1) && core.size() >= 2 && core.size() <= 7);
        if (!validGluedPhonetic && !validAlnumCall) {
            return false;
        }

        for (const SegPart &sp : segs) {
            outParts.append(sp.text);
        }
        return true;
    };

    const QStringList rawWords = s.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QVector<Token> tokens;
    tokens.reserve(rawWords.size() * 2);

    int nextGroupId = 1;
    for (const QString &w : rawWords) {
        int start = 0;
        int end = static_cast<int>(w.size());
        while (start < end && !w[start].isLetterOrNumber()) {
            ++start;
        }
        while (end > start && !w[end - 1].isLetterOrNumber()) {
            --end;
        }
        const QString leadPunct = w.left(start);
        const QString core = w.mid(start, end - start);
        const QString trailPunct = w.mid(end);

        QString cleanCore = core;
        if (cleanCore.toLower().endsWith(QStringLiteral("'s"))) {
            cleanCore.chop(2);
        }

        QStringList segParts;
        bool segHasDigits = false;
        if (trySegmentGluedToken(cleanCore, segParts, segHasDigits)) {
            const int gid = nextGroupId++;
            for (int k = 0; k < segParts.size(); ++k) {
                Token sub;
                sub.raw = (k == 0 ? leadPunct : QString()) + segParts[k] + (k + 1 == segParts.size() ? trailPunct : QString());
                sub.leadPunct = (k == 0) ? leadPunct : QString();
                sub.core = segParts[k];
                sub.lowerCore = segParts[k].toLower();
                sub.trailPunct = (k + 1 == segParts.size()) ? trailPunct : QString();
                sub.fromGluedOrAlnum = true;
                sub.gluedToNext = (k + 1 < segParts.size());
                sub.origHasDigits = segHasDigits;
                sub.origGroupId = gid;
                sub.origRaw = w;
                tokens.push_back(sub);
            }
        } else {
            Token tk;
            tk.raw = w;
            tk.leadPunct = leadPunct;
            tk.core = core;
            tk.lowerCore = cleanCore.toLower();
            tk.trailPunct = trailPunct;
            tokens.push_back(tk);
        }
    }

    auto isNumberModifier = [](const QString &lc) -> bool {
        return lc == QStringLiteral("number") ||
               lc == QStringLiteral("numeral") ||
               lc == QStringLiteral("numero") ||
               lc == QStringLiteral("número") ||
               lc == QStringLiteral("numéro") ||
               lc == QStringLiteral("nummer") ||
               lc == QStringLiteral("figure") ||
               lc == QStringLiteral("digit") ||
               lc == QStringLiteral("num") ||
               lc == QStringLiteral("no");
    };

    auto isLetterModifier = [](const QString &lc) -> bool {
        return lc == QStringLiteral("letter") ||
               lc == QStringLiteral("letra") ||
               lc == QStringLiteral("lettera") ||
               lc == QStringLiteral("lettre") ||
               lc == QStringLiteral("buchstabe");
    };

    auto repeatMultiplier = [](const QString &lc) -> int {
        if (lc == QStringLiteral("double") || lc == QStringLiteral("doble") || lc == QStringLiteral("doppia") || lc == QStringLiteral("doppio") || lc == QStringLiteral("doppel")) return 2;
        if (lc == QStringLiteral("triple") || lc == QStringLiteral("tripla") || lc == QStringLiteral("triplo") || lc == QStringLiteral("dreifach")) return 3;
        return 1;
    };

    auto isStrokeWord = [](const QString &lc) -> bool {
        return lc == QStringLiteral("stroke") || lc == QStringLiteral("slash");
    };

    auto isPlainCallsignChunkCandidate = [&](const Token &tk) -> bool {
        if (tk.core.size() < 2 || tk.core.size() > 4) return false;
        if (!isAsciiAlphaOnly(tk.core)) return false;
        if (phoneticMap.contains(tk.lowerCore) || numberWordMap.contains(tk.lowerCore) || tensWordMap.contains(tk.lowerCore)) return false;
        if (tk.fromGluedOrAlnum) return true;
        if (nonCallsignWordSet.contains(tk.lowerCore)) return false;
        return true;
    };

    const int n = static_cast<int>(tokens.size());

    auto isSuffixStarterAt = [&](int pos) -> bool {
        if (pos >= n) return false;
        const Token &tk = tokens[pos];
        if (phoneticMap.contains(tk.lowerCore)) return true;
        if (isLetterModifier(tk.lowerCore)) return true;
        if (isSingleAsciiLetter(tk.lowerCore)) return true;
        if (isPlainCallsignChunkCandidate(tk)) return true;
        return false;
    };

    QStringList outWords;
    int idx = 0;

    while (idx < n) {
        QString seqChars;
        int leadingDigits = 0;
        int prefixLetters = 0;
        int digitsAfterPrefix = 0;
        int suffixLetters = 0;
        bool afterSlash = false;
        int primaryPhoneticCount = 0;
        int totalPhoneticLetters = 0;
        int digitCount = 0;
        bool hadExplicitModifier = false;
        bool hadAlnumChunk = false;
        bool phoneticPrecededNumber = false;
        int j = idx;
        int lastConsumedIdx = -1;

        auto appendCallLetters = [&](const QString &lettersUpper, int primaryCnt) {
            seqChars += lettersUpper;
            const int cnt = static_cast<int>(lettersUpper.size());
            totalPhoneticLetters += cnt;
            primaryPhoneticCount += primaryCnt;
            if (!afterSlash) {
                if (digitsAfterPrefix == 0) {
                    prefixLetters += cnt;
                } else {
                    suffixLetters += cnt;
                }
            }
        };

        auto appendCallDigits = [&](const QString &digitsStr) {
            if (totalPhoneticLetters > 0) {
                phoneticPrecededNumber = true;
            }
            seqChars += digitsStr;
            const int cnt = static_cast<int>(digitsStr.size());
            digitCount += cnt;
            if (!afterSlash) {
                if (prefixLetters == 0) {
                    leadingDigits += cnt;
                } else {
                    digitsAfterPrefix += cnt;
                }
            }
        };

        while (j < n) {
            if (lastConsumedIdx >= idx) {
                const QString &prevTrail = tokens[lastConsumedIdx].trailPunct;
                if (prevTrail.contains(QLatin1Char('.')) ||
                    prevTrail.contains(QLatin1Char('!')) ||
                    prevTrail.contains(QLatin1Char('?')) ||
                    prevTrail.contains(QLatin1Char(';')) ||
                    prevTrail.contains(QLatin1Char(':'))) {
                    break;
                }
            }
            const QString &lc = tokens[j].lowerCore;
            if (lc.isEmpty()) {
                break;
            }

            // If we already completed a full callsign (prefix + digit(s) + >=2 suffix letters) and we are at a space boundary,
            // check if a brand-new second callsign is starting at j (e.g., "VU3LVU VU3LVU").
            if (!afterSlash && digitsAfterPrefix >= 1 && suffixLetters >= 2 && (lastConsumedIdx < idx || !tokens[lastConsumedIdx].gluedToNext)) {
                if (tokens[j].fromGluedOrAlnum && tokens[j].origHasDigits) {
                    break;
                }
                const bool jIsLet = phoneticMap.contains(lc) || isSingleAsciiLetter(lc) || isPlainCallsignChunkCandidate(tokens[j]);
                if (jIsLet && (j + 1) < n) {
                    const QString &n1 = tokens[j + 1].lowerCore;
                    const bool n1IsDig = isPureDigits(n1) || numberWordMap.contains(n1) || tensWordMap.contains(n1);
                    if (n1IsDig && isSuffixStarterAt(j + 2)) {
                        break;
                    }
                    const bool n1IsLet = phoneticMap.contains(n1) || isSingleAsciiLetter(n1);
                    if (n1IsLet && (j + 2) < n) {
                        const QString &n2 = tokens[j + 2].lowerCore;
                        const bool n2IsDig = isPureDigits(n2) || numberWordMap.contains(n2) || tensWordMap.contains(n2);
                        if (n2IsDig && isSuffixStarterAt(j + 3)) {
                            break;
                        }
                    }
                }
            }

            if (isNumberModifier(lc) && (j + 1) < n) {
                const QString &nextLc = tokens[j + 1].lowerCore;
                QString numStr;
                if (isPureDigits(nextLc)) {
                    numStr = nextLc;
                } else if (numberWordMap.contains(nextLc)) {
                    numStr = numberWordMap.value(nextLc);
                } else if (homophoneNumberMap.contains(nextLc)) {
                    numStr = homophoneNumberMap.value(nextLc);
                }
                if (!numStr.isEmpty()) {
                    appendCallDigits(numStr);
                    hadExplicitModifier = true;
                    lastConsumedIdx = j + 1;
                    j += 2;
                    continue;
                }
            }

            if (isLetterModifier(lc) && (j + 1) < n) {
                const QString &nextLc = tokens[j + 1].lowerCore;
                if (phoneticMap.contains(nextLc)) {
                    const PhoneticInfo pi = phoneticMap.value(nextLc);
                    appendCallLetters(QString(QLatin1Char(pi.ch)), 1);
                    hadExplicitModifier = true;
                    lastConsumedIdx = j + 1;
                    j += 2;
                    continue;
                } else if (isSingleAsciiLetter(nextLc)) {
                    appendCallLetters(nextLc.toUpper(), 1);
                    hadExplicitModifier = true;
                    lastConsumedIdx = j + 1;
                    j += 2;
                    continue;
                }
            }

            const int mult = repeatMultiplier(lc);
            if (mult > 1 && (j + 1) < n) {
                const QString &nextLc = tokens[j + 1].lowerCore;
                QString unit;
                bool isLett = false;
                bool isPrim = false;
                if (phoneticMap.contains(nextLc)) {
                    const PhoneticInfo pi = phoneticMap.value(nextLc);
                    unit = QString(QLatin1Char(pi.ch));
                    isLett = true;
                    isPrim = pi.isPrimary;
                } else if (isSingleAsciiLetter(nextLc)) {
                    unit = nextLc.toUpper();
                    isLett = true;
                    isPrim = true;
                } else if (numberWordMap.contains(nextLc)) {
                    unit = numberWordMap.value(nextLc);
                } else if (isPureDigits(nextLc) && nextLc.size() == 1) {
                    unit = nextLc;
                }
                if (!unit.isEmpty()) {
                    QString repeated;
                    for (int m = 0; m < mult; ++m) {
                        repeated += unit;
                    }
                    if (isLett) {
                        appendCallLetters(repeated, isPrim ? 1 : 0);
                    } else {
                        appendCallDigits(repeated);
                    }
                    hadExplicitModifier = true;
                    lastConsumedIdx = j + 1;
                    j += 2;
                    continue;
                }
            }

            if (isStrokeWord(lc) && !seqChars.isEmpty() && (j + 1) < n) {
                const QString &nextLc = tokens[j + 1].lowerCore;
                if (nextLc == QStringLiteral("portable")) {
                    seqChars += QStringLiteral("/P");
                    afterSlash = true;
                    lastConsumedIdx = j + 1;
                    j += 2;
                    continue;
                } else if (nextLc == QStringLiteral("mobile")) {
                    seqChars += QStringLiteral("/M");
                    afterSlash = true;
                    lastConsumedIdx = j + 1;
                    j += 2;
                    continue;
                } else if (nextLc == QStringLiteral("qrp")) {
                    seqChars += QStringLiteral("/QRP");
                    afterSlash = true;
                    lastConsumedIdx = j + 1;
                    j += 2;
                    continue;
                } else if (phoneticMap.contains(nextLc) || numberWordMap.contains(nextLc) || isPureDigits(nextLc) || isSingleAsciiLetter(nextLc)) {
                    seqChars += QLatin1Char('/');
                    afterSlash = true;
                    lastConsumedIdx = j;
                    ++j;
                    continue;
                }
            }

            if (phoneticMap.contains(lc)) {
                const PhoneticInfo pi = phoneticMap.value(lc);
                // Do not start a callsign with a secondary country/city word that ends with a comma ("Bonjour de France, Foxtrot 5...")
                if (!pi.isPrimary && seqChars.isEmpty() && tokens[j].trailPunct.contains(QLatin1Char(','))) {
                    break;
                }
                // Do not append secondary (country/city/common) words after a completed 3-letter callsign suffix
                if (!pi.isPrimary && !tokens[j].fromGluedOrAlnum && digitsAfterPrefix >= 1 && suffixLetters >= 3) {
                    break;
                }
                int primInc = 0;
                if (pi.isPrimary && lc != QStringLiteral("india")) {
                    primInc = 1;
                } else if (lc == QStringLiteral("india") && (seqChars.size() > 1 || ((j + 1) < n && (phoneticMap.contains(tokens[j + 1].lowerCore) || numberWordMap.contains(tokens[j + 1].lowerCore) || tensWordMap.contains(tokens[j + 1].lowerCore) || isPureDigits(tokens[j + 1].lowerCore) || isNumberModifier(tokens[j + 1].lowerCore))))) {
                    primInc = 1;
                }
                if (tokens[j].fromGluedOrAlnum) {
                    hadAlnumChunk = true;
                }
                appendCallLetters(QString(QLatin1Char(pi.ch)), primInc);
                lastConsumedIdx = j;
                ++j;
                continue;
            }

            // Plain multi-letter callsign chunk (e.g., "VU", "LVU", "DR", "WB", "EHG" or decomposed sub-token from "VU3", "VU36", "B2EHG")
            if (isPlainCallsignChunkCandidate(tokens[j])) {
                bool allowChunk = false;
                if (tokens[j].fromGluedOrAlnum) {
                    // If this is the start of a new decomposed alnum token (e.g. "B" in "B2EHG") and we already have prefix letters (e.g. "W" vs "ARW"),
                    // only merge if total prefix letters before the digit will be <= 2 (or <= 3 with leading digit).
                    if (!seqChars.isEmpty() && digitsAfterPrefix == 0 && tokens[j].origHasDigits) {
                        if (prefixLetters + lc.size() <= 2) {
                            allowChunk = true;
                        }
                    } else {
                        allowChunk = true;
                    }
                } else if (seqChars.isEmpty()) {
                    // Prefix chunk at start of callsign (e.g., "VU 3 Lima Victor U", "VU 36 Delta Romeo", "VU 3 LVU", "WB 2 EHG")
                    if (lc.size() <= 3 && (j + 1) < n) {
                        const QString &n1 = tokens[j + 1].lowerCore;
                        if ((isPureDigits(n1) && n1.size() <= 2 && n1 != QStringLiteral("59") && n1 != QStringLiteral("73") && n1 != QStringLiteral("88")) ||
                            (numberWordMap.contains(n1) && numberWordMap.value(n1).size() == 1)) {
                            if (isSuffixStarterAt(j + 2)) {
                                allowChunk = true;
                            } else if ((j + 2) < n && ((isPureDigits(tokens[j + 2].lowerCore) && tokens[j + 2].lowerCore.size() == 1) || numberWordMap.contains(tokens[j + 2].lowerCore)) && isSuffixStarterAt(j + 3)) {
                                allowChunk = true;
                            }
                        } else if (tensWordMap.contains(n1)) {
                            if (isSuffixStarterAt(j + 2) || ((j + 2) < n && numberWordMap.contains(tokens[j + 2].lowerCore) && isSuffixStarterAt(j + 3))) {
                                allowChunk = true;
                            }
                        }
                    }
                } else {
                    // Continuing an active callsign sequence (e.g., "Victor Uniform 3 LVU", "VU36 DR", "VU3 LV Uniform")
                    if (!afterSlash && digitsAfterPrefix >= 1 && (suffixLetters + lc.size()) <= 4) {
                        if (isAllAsciiUpper(tokens[j].core) || (suffixLetters == 0 && (primaryPhoneticCount >= 1 || hadAlnumChunk))) {
                            allowChunk = true;
                        }
                    } else if (!afterSlash && digitsAfterPrefix == 0 && (prefixLetters + lc.size()) <= 3 && isAllAsciiUpper(tokens[j].core) && (j + 1) < n) {
                        const QString &n1 = tokens[j + 1].lowerCore;
                        if (isPureDigits(n1) || numberWordMap.contains(n1)) {
                            allowChunk = true;
                        }
                    }
                }

                if (allowChunk) {
                    hadAlnumChunk = true;
                    appendCallLetters(tokens[j].core.toUpper(), tokens[j].fromGluedOrAlnum ? 0 : 1);
                    lastConsumedIdx = j;
                    ++j;
                    continue;
                }
            }

            if (isSingleAsciiLetter(lc)) {
                // Guard against English pronoun "I" followed by a verb ("I used to live", "I am", "I copy")
                if (!tokens[j].fromGluedOrAlnum && lc == QStringLiteral("i") && (j + 1) < n && pronounIVerbs.contains(tokens[j + 1].lowerCore)) {
                    break;
                }
                // Guard against lowercase indefinite article "a" followed by a normal non-callsign word ("a very good signal")
                if (!tokens[j].fromGluedOrAlnum && tokens[j].core == QStringLiteral("a") && !isSuffixStarterAt(j + 1)) {
                    break;
                }

                const bool isArticleOrPronoun = (lc == QStringLiteral("a") || lc == QStringLiteral("i") ||
                                                 lc == QStringLiteral("e") || lc == QStringLiteral("o") ||
                                                 lc == QStringLiteral("u") || lc == QStringLiteral("y"));
                const bool nextIsNumberMod = ((j + 1) < n && isNumberModifier(tokens[j + 1].lowerCore));
                const bool nextIsPrimaryPhonetic = ((j + 1) < n && phoneticMap.contains(tokens[j + 1].lowerCore) && phoneticMap.value(tokens[j + 1].lowerCore).isPrimary);
                const bool nextIsSingleLetter = ((j + 1) < n && isSingleAsciiLetter(tokens[j + 1].lowerCore) && tokens[j + 1].lowerCore != QStringLiteral("a"));
                const bool nextIsAlnumOrDigit = ((j + 1) < n && (tokens[j + 1].fromGluedOrAlnum || isPureDigits(tokens[j + 1].lowerCore) || numberWordMap.contains(tokens[j + 1].lowerCore)));

                if (tokens[j].fromGluedOrAlnum || !isArticleOrPronoun || !seqChars.isEmpty() || nextIsNumberMod || nextIsPrimaryPhonetic || nextIsSingleLetter || nextIsAlnumOrDigit) {
                    if (tokens[j].fromGluedOrAlnum && !seqChars.isEmpty() && digitsAfterPrefix == 0 && tokens[j].origHasDigits && (prefixLetters + 1) > 2) {
                        break;
                    }
                    if (tokens[j].fromGluedOrAlnum) {
                        hadAlnumChunk = true;
                    }
                    const int primInc = (!isArticleOrPronoun || !seqChars.isEmpty() || nextIsNumberMod || nextIsSingleLetter || nextIsAlnumOrDigit) ? 1 : 0;
                    appendCallLetters(lc.toUpper(), primInc);
                    lastConsumedIdx = j;
                    ++j;
                    continue;
                }
            }

            // Spoken compound two-digit numbers inside callsigns (e.g., "Victor Uniform thirty six Delta Romeo" -> VU36DR)
            if (!seqChars.isEmpty() && prefixLetters >= 1 && prefixLetters <= 3 && digitsAfterPrefix == 0 && suffixLetters == 0 && tensWordMap.contains(lc)) {
                const QString tensDigit = tensWordMap.value(lc);
                if ((j + 1) < n && numberWordMap.contains(tokens[j + 1].lowerCore) && numberWordMap.value(tokens[j + 1].lowerCore).size() == 1 && numberWordMap.value(tokens[j + 1].lowerCore) != QStringLiteral("0")) {
                    const QString twoDigit = tensDigit + numberWordMap.value(tokens[j + 1].lowerCore);
                    if (twoDigit != QStringLiteral("59") && twoDigit != QStringLiteral("73") && twoDigit != QStringLiteral("88") && isSuffixStarterAt(j + 2)) {
                        appendCallDigits(twoDigit);
                        lastConsumedIdx = j + 1;
                        j += 2;
                        continue;
                    }
                } else if (isSuffixStarterAt(j + 1)) {
                    const QString twoDigit = tensDigit + QStringLiteral("0");
                    appendCallDigits(twoDigit);
                    lastConsumedIdx = j;
                    ++j;
                    continue;
                }
            }

            if (numberWordMap.contains(lc) || isPureDigits(lc)) {
                const QString numStr = isPureDigits(lc) ? lc : numberWordMap.value(lc);
                if (!afterSlash && suffixLetters >= 1) {
                    // Already saw prefix + numeral + suffix letters; a new unmodified number is outside this callsign
                    break;
                }
                if (numStr.size() == 1) {
                    if (!afterSlash && prefixLetters > 0 && (digitsAfterPrefix + 1) > 2) {
                        break;
                    }
                    if (seqChars.isEmpty() && !tokens[j].fromGluedOrAlnum) {
                        const bool nextIsCall = ((j + 1) < n && (phoneticMap.contains(tokens[j + 1].lowerCore) || tokens[j + 1].fromGluedOrAlnum));
                        if (!nextIsCall) {
                            break;
                        }
                    }
                    if (tokens[j].fromGluedOrAlnum) {
                        hadAlnumChunk = true;
                    }
                    appendCallDigits(numStr);
                    lastConsumedIdx = j;
                    ++j;
                    continue;
                } else if (numStr.size() == 2) {
                    if (numStr == QStringLiteral("59") || numStr == QStringLiteral("73") || numStr == QStringLiteral("88")) {
                        break;
                    }
                    const bool validTwoDigitInCall =
                        tokens[j].fromGluedOrAlnum ||
                        (prefixLetters >= 1 && prefixLetters <= 3 && digitsAfterPrefix == 0 && suffixLetters == 0 && isSuffixStarterAt(j + 1));
                    if (!validTwoDigitInCall) {
                        break;
                    }
                    if (tokens[j].fromGluedOrAlnum) {
                        hadAlnumChunk = true;
                    }
                    appendCallDigits(numStr);
                    lastConsumedIdx = j;
                    ++j;
                    continue;
                } else {
                    break;
                }
            }

            if (!seqChars.isEmpty() && totalPhoneticLetters > 0 && suffixLetters == 0 && homophoneNumberMap.contains(lc)) {
                if (isSuffixStarterAt(j + 1)) {
                    const QString numStr = homophoneNumberMap.value(lc);
                    appendCallDigits(numStr);
                    lastConsumedIdx = j;
                    ++j;
                    continue;
                }
            }

            break;
        }

        const int totalSymbols = totalPhoneticLetters + digitCount;
        bool acceptSequence = false;

        if (lastConsumedIdx >= idx && !seqChars.isEmpty() && !tokens[lastConsumedIdx].gluedToNext) {
            if (totalSymbols >= 2) {
                const bool validPureSecondaryWithNum =
                    (primaryPhoneticCount == 0 && !hadAlnumChunk && !hadExplicitModifier)
                    ? (totalPhoneticLetters >= 2 && digitCount >= 1 && suffixLetters >= 1)
                    : true;
                if (validPureSecondaryWithNum &&
                    (primaryPhoneticCount >= 1 || hadExplicitModifier || hadAlnumChunk || phoneticPrecededNumber || totalPhoneticLetters >= 3)) {
                    acceptSequence = true;
                }
            } else if (totalSymbols == 1 && hadExplicitModifier) {
                acceptSequence = true;
            }
        }

        if (acceptSequence) {
            QString trail = tokens[lastConsumedIdx].trailPunct;
            if (trail == QStringLiteral(",") && lastConsumedIdx == (n - 1)) {
                trail.clear();
            }
            outWords.append(tokens[idx].leadPunct + seqChars + trail);
            idx = lastConsumedIdx + 1;
        } else {
            if (tokens[idx].fromGluedOrAlnum && tokens[idx].origGroupId > 0) {
                const int gid = tokens[idx].origGroupId;
                outWords.append(tokens[idx].origRaw);
                while (idx < n && tokens[idx].origGroupId == gid) {
                    ++idx;
                }
            } else {
                outWords.append(tokens[idx].raw);
                ++idx;
            }
        }
    }

    QString finalOut = outWords.join(QLatin1Char(' ')).simplified();

    // ─────────────────────────────────────────────────────────────────────────
    // TECHNICAL, HAM BRAND, TRANSCEIVER, ANTENNA, MODE & Q-CODE VOCABULARY
    // Runs across ALL models (Apple Neural, Parakeet TDT v3, and Whisper AI)
    // ─────────────────────────────────────────────────────────────────────────
    static const std::vector<RegexReplacement> techVocabRules = {
        // 1. Transceiver & Radio Brands
        { QRegularExpression(QStringLiteral(R"(\b(?:bao[\s-]*f[aeu]ng|bow[\s-]*f[aeu]ng|bough[\s-]*f[ae]ng|bau[\s-]*f[ae]ng|bio[\s-]*f[ae]ng|bo[\s-]*feng|balfang|pofung)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Baofeng") },
        { QRegularExpression(QStringLiteral(R"(\b(?:tid[\s-]*radio|t[\s\.]*i[\s\.]*d[\s\.]*radio|ted\s+radio)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("TIDRADIO") },
        { QRegularExpression(QStringLiteral(R"(\b(?:quan[\s-]*sh[ea]ng|kw[ao]n[\s-]*sh[ea]ng|quang[\s-]*sh[ea]ng|qiansheng)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Quansheng") },
        { QRegularExpression(QStringLiteral(R"(\b(?:any[\s-]*tone|ani[\s-]*tone)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("AnyTone") },
        { QRegularExpression(QStringLiteral(R"(\b(?:yae[\s-]*su|yay[\s-]*s(?:ue|u)|yeasu|yesu|yay[\s-]*zoo|yaezu)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Yaesu") },
        { QRegularExpression(QStringLiteral(R"(\b(?:i[\s-]*com|eye[\s-]*com)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Icom") },
        { QRegularExpression(QStringLiteral(R"(\bicon(?=\s+(?:ic|id|7300|705|9700|7610|7100|5100|2730|radio|rig|transceiver|handheld|ht)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Icom") },
        { QRegularExpression(QStringLiteral(R"(\bken[\s-]*wood\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Kenwood") },
        { QRegularExpression(QStringLiteral(R"(\b(?:radioddity|radio[\s-]*oddity|radio[\s-]*ditty|radiodity)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Radioddity") },
        { QRegularExpression(QStringLiteral(R"(\b(?:tytera|ty[\s-]*tera|tie[\s-]*tera)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Tytera") },
        { QRegularExpression(QStringLiteral(R"(\bt[\s\.]*y[\s\.]*t(?=\s+(?:md|th|radio|dmr|ht|rig)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("TYT") },
        { QRegularExpression(QStringLiteral(R"(\b(?:retevis|ret[\s-]*a[\s-]*vis|red[\s-]*avis)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Retevis") },
        { QRegularExpression(QStringLiteral(R"(\b(?:ailunce|ai[\s-]*lunce|alliance(?=\s+hd[\s-]*[12]\b))\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Ailunce") },
        { QRegularExpression(QStringLiteral(R"(\b(?:alinco|a[\s-]*linco|alinko|alienco)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Alinco") },
        { QRegularExpression(QStringLiteral(R"(\b(?:wouxun|wou[\s-]*xun|woo[\s-]*shun|wouxon)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Wouxun") },
        { QRegularExpression(QStringLiteral(R"(\b(?:btech|b[\s-]*tech)(?=\s+(?:uv|dmr|gmrs|radio|ht|handheld|amp)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("BTECH") },
        { QRegularExpression(QStringLiteral(R"(\b(?:elecraft|ele[\s-]*craft|ella[\s-]*craft|telecraft(?=\s+(?:k[x1-4]|rig|radio|qrp)\b))\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Elecraft") },
        { QRegularExpression(QStringLiteral(R"(\bflex[\s-]*radio\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("FlexRadio") },
        { QRegularExpression(QStringLiteral(R"(\b(?:xiegu|xie[\s-]*gu|zhegu|she[\s-]*goo(?=\s+(?:g\s*90|x\s*6100|radio|rig)\b))\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Xiegu") },
        { QRegularExpression(QStringLiteral(R"(\b(?:mototrbo|moto[\s-]*turbo)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("MOTOTRBO") },
        { QRegularExpression(QStringLiteral(R"(\b(?:hytera|hi[\s-]*tera|high[\s-]*tera)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Hytera") },
        { QRegularExpression(QStringLiteral(R"(\bbridge[\s-]*com\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("BridgeCom") },
        { QRegularExpression(QStringLiteral(R"(\b(?:abbree|abree)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("ABBREE") },
        { QRegularExpression(QStringLiteral(R"(\b(?:buddipole|buddi[\s-]*pole|buddy[\s-]*pole)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Buddipole") },
        { QRegularExpression(QStringLiteral(R"(\b(?:buddistick|buddi[\s-]*stick|buddy[\s-]*stick)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Buddistick") },
        { QRegularExpression(QStringLiteral(R"(\bwolf\s+river\s+coils?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Wolf River Coil") },
        { QRegularExpression(QStringLiteral(R"(\b(?:bioenno|bio[\s-]*enno)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Bioenno") },
        { QRegularExpression(QStringLiteral(R"(\b(?:lifepo4|life[\s-]*po[\s-]*4|li[\s-]*fe[\s-]*po[\s-]*4)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("LiFePO4") },
        { QRegularExpression(QStringLiteral(R"(\bpower[\s-]*poles?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Powerpole") },

        // 2. Popular Transceiver & Rig Model Numbers
        { QRegularExpression(QStringLiteral(R"(\bt[\s\.]*d[\s-]*h[\s-]*([38])\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("TD-H\\1") },
        { QRegularExpression(QStringLiteral(R"(\bu[\s\.]*v[\s-]*k[\s-]*([56])\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("UV-K\\1") },
        { QRegularExpression(QStringLiteral(R"(\bu[\s\.]*v[\s-]*5[\s-]*r\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("UV-5R") },
        { QRegularExpression(QStringLiteral(R"(\bu[\s\.]*v[\s-]*82\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("UV-82") },
        { QRegularExpression(QStringLiteral(R"(\b(?:bf[\s-]*)?f8[\s-]*hp\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("BF-F8HP") },
        { QRegularExpression(QStringLiteral(R"(\b(?:at[\s-]*)?d[\s-]*([851][67]8)[\s-]*u[\s\.]*v\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("D\\1UV") },
        { QRegularExpression(QStringLiteral(R"(\bic[\s-]*(7300|705|9700|7610|7100|7851|9100|2730)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("IC-\\1") },
        { QRegularExpression(QStringLiteral(R"(\bid[\s-]*(51|52|5100|4100)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("ID-\\1") },
        { QRegularExpression(QStringLiteral(R"(\bft[\s-]*dx[\s-]*(101|10)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("FTDX\\1") },
        { QRegularExpression(QStringLiteral(R"(\bftm[\s-]*(100|200|300|400|500|6000)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("FTM-\\1") },
        { QRegularExpression(QStringLiteral(R"(\bft[\s-]*(60|65|70|817|818|857|891|897|991|710)([dr]{0,2}|a)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("FT-\\1") },
        { QRegularExpression(QStringLiteral(R"(\bft[\s-]*([35])[\s-]*d(?:r)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("FT-\\1D") },
        { QRegularExpression(QStringLiteral(R"(\bth[\s-]*d[\s-]*(72|74|75)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("TH-D\\1") },
        { QRegularExpression(QStringLiteral(R"(\bts[\s-]*(480|590|890|990|2000)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("TS-\\1") },
        { QRegularExpression(QStringLiteral(R"(\bmd[\s-]*uv[\s-]*(380|390)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("MD-UV\\1") },
        { QRegularExpression(QStringLiteral(R"(\bmd[\s-]*(380|390|9600|2017)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("MD-\\1") },
        { QRegularExpression(QStringLiteral(R"(\bopen[\s-]*g[\s\.]*d[\s-]*77\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("OpenGD77") },
        { QRegularExpression(QStringLiteral(R"(\bg[\s\.]*d[\s-]*(77|88)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("GD-\\1") },
        { QRegularExpression(QStringLiteral(R"(\brt[\s-]*3[\s-]*s\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("RT3S") },
        { QRegularExpression(QStringLiteral(R"(\bkx[\s-]*([23])\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("KX\\1") },
        { QRegularExpression(QStringLiteral(R"(\bna[\s-]*771\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("NA-771") },

        // 3. Antennas, Feedlines, Connectors & Test Gear
        { QRegularExpression(QStringLiteral(R"(\b(?:yagi|yaggy|yaggi|ya[\s-]*gi|jagi)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Yagi") },
        { QRegularExpression(QStringLiteral(R"(\byogi(?=\s+(?:antenna|beam|elements?|array|directional)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Yagi") },
        { QRegularExpression(QStringLiteral(R"(\b(?:dipole|die[\s-]*pole|di[\s-]*pole)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("dipole") },
        { QRegularExpression(QStringLiteral(R"(\b(?:end[\s-]*fed\s+half[\s-]*wave|e[\s\.]*f[\s\.]*h[\s\.]*w)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("EFHW") },
        { QRegularExpression(QStringLiteral(R"(\bend[\s-]*fed\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("end-fed") },
        { QRegularExpression(QStringLiteral(R"(\b(?:j[\s-]*pole|jay[\s-]*pole)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("J-pole") },
        { QRegularExpression(QStringLiteral(R"(\bhex[\s-]*beam\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Hexbeam") },
        { QRegularExpression(QStringLiteral(R"(\b(?:moxon|moxen(?=\s+(?:antenna|beam)\b))\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Moxon") },
        { QRegularExpression(QStringLiteral(R"(\bmag[\s-]*loop\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("mag loop") },
        { QRegularExpression(QStringLiteral(R"(\binverted[\s-]*v(?:ee)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("inverted V") },
        { QRegularExpression(QStringLiteral(R"(\b(?:discone|disc[\s-]*cone)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("discone") },
        { QRegularExpression(QStringLiteral(R"(\b(?:collinear|colinear|co[\s-]*linear)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("collinear") },
        { QRegularExpression(QStringLiteral(R"(\b(?:balun|balon)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("balun") },
        { QRegularExpression(QStringLiteral(R"(\b(?:nano[\s-]*vna|nano[\s-]*v[\s\.]*n[\s\.]*a)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("NanoVNA") },
        { QRegularExpression(QStringLiteral(R"(\b(?:tiny[\s-]*sa|tiny[\s-]*s[\s\.]*a)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("tinySA") },
        { QRegularExpression(QStringLiteral(R"(\bv[\s\.]*s[\s\.]*w[\s\.]*r\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("VSWR") },
        { QRegularExpression(QStringLiteral(R"(\b(?:s[\s\.]*w[\s\.]*r|standing\s+wave\s+ratio)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("SWR") },
        { QRegularExpression(QStringLiteral(R"(\b(?:1|one)\s+to\s+(?:1|one)(?=\s+(?:swr|vswr|balun|choke)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("1:1") },
        { QRegularExpression(QStringLiteral(R"(\b(?:49|forty[\s-]*nine)\s+to\s+(?:1|one)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("49:1") },
        { QRegularExpression(QStringLiteral(R"(\b(?:9|nine)\s+to\s+(?:1|one)(?=\s+(?:unun|balun)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("9:1") },
        { QRegularExpression(QStringLiteral(R"(\b(?:4|four)\s+to\s+(?:1|one)(?=\s+(?:unun|balun)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("4:1") },
        { QRegularExpression(QStringLiteral(R"(\bl[\s\.]*m[\s\.]*r[\s-]*(400|240)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("LMR-\\1") },
        { QRegularExpression(QStringLiteral(R"(\br[\s\.]*g[\s-]*(58|213|174)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("RG-\\1") },
        { QRegularExpression(QStringLiteral(R"(\br[\s\.]*g[\s-]*8[\s-]*x\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("RG-8X") },
        { QRegularExpression(QStringLiteral(R"(\bp[\s\.]*l[\s-]*259\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("PL-259") },
        { QRegularExpression(QStringLiteral(R"(\bs[\s\.]*o[\s-]*239\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("SO-239") },

        // 4. Digital Voice, Hotspots, Firmware, Networks & Modes
        { QRegularExpression(QStringLiteral(R"(\bdroid[\s-]*star\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("DroidStar") },
        { QRegularExpression(QStringLiteral(R"(\b(?:brand[\s-]*meister|brand[\s-]*master|brain[\s-]*meister)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("BrandMeister") },
        { QRegularExpression(QStringLiteral(R"(\bt[\s\.]*g[\s\.]*i[\s\.]*f\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("TGIF") },
        { QRegularExpression(QStringLiteral(R"(\bfree[\s-]*dmr\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("FreeDMR") },
        { QRegularExpression(QStringLiteral(R"(\b(?:pi[\s-]*star|pie[\s-]*star|pistar)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Pi-Star") },
        { QRegularExpression(QStringLiteral(R"(\bw[\s\.]*p[\s\.]*s[\s\.]*d\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("WPSD") },
        { QRegularExpression(QStringLiteral(R"(\bm[\s\.]*m[\s\.]*d[\s\.]*v[\s\.]*m\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("MMDVM") },
        { QRegularExpression(QStringLiteral(R"(\bjumbo[\s-]*spot\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("JumboSpot") },
        { QRegularExpression(QStringLiteral(R"(\b(?:zum[\s-]*spot|zoom[\s-]*spot)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("ZUMspot") },
        { QRegularExpression(QStringLiteral(R"(\bopen[\s-]*spot\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("openSPOT") },
        { QRegularExpression(QStringLiteral(R"(\bsky[\s-]*bridge\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("SkyBridge") },
        { QRegularExpression(QStringLiteral(R"(\bclear[\s-]*node\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("ClearNode") },
        { QRegularExpression(QStringLiteral(R"(\b(?:dv[\s-]*switch|d[\s\.]*v[\s\.]*switch)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("DVSwitch") },
        { QRegularExpression(QStringLiteral(R"(\ball[\s-]*star(?=\s+(?:link|node|network)\b))"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("AllStar") },
        { QRegularExpression(QStringLiteral(R"(\becho[\s-]*link\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("EchoLink") },
        { QRegularExpression(QStringLiteral(R"(\bwires[\s-]*x\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("WIRES-X") },
        { QRegularExpression(QStringLiteral(R"(\b(?:code[\s-]*plug|cold[\s-]*plug)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("codeplug") },
        { QRegularExpression(QStringLiteral(R"(\btime[\s-]*slot\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("timeslot") },
        { QRegularExpression(QStringLiteral(R"(\b(?:d[\s-]*star|dee[\s-]*star)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("D-STAR") },
        { QRegularExpression(QStringLiteral(R"(\by[\s\.]*s[\s\.]*f\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("YSF") },
        { QRegularExpression(QStringLiteral(R"(\bsystem\s+fusion\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("System Fusion") },
        { QRegularExpression(QStringLiteral(R"(\bc[\s\.]*(?:4|four)[\s\.]*f[\s\.]*m\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("C4FM") },
        { QRegularExpression(QStringLiteral(R"(\bn[\s\.]*x[\s\.]*d[\s\.]*n\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("NXDN") },
        { QRegularExpression(QStringLiteral(R"(\bp[\s-]*(?:25|twenty[\s-]*five)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("P25") },
        { QRegularExpression(QStringLiteral(R"(\bm[\s-]*(?:17|seventeen)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("M17") },
        { QRegularExpression(QStringLiteral(R"(\bf[\s\.]*t[\s-]*(?:8|eight)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("FT8") },
        { QRegularExpression(QStringLiteral(R"(\bf[\s\.]*t[\s-]*(?:4|four)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("FT4") },
        { QRegularExpression(QStringLiteral(R"(\bj[\s\.]*s[\s-]*8(?:\s*call)?\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("JS8Call") },
        { QRegularExpression(QStringLiteral(R"(\bw[\s\.]*s[\s\.]*j[\s\.]*t[\s-]*x\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("WSJT-X") },
        { QRegularExpression(QStringLiteral(R"(\bw[\s\.]*s[\s\.]*p[\s\.]*r\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("WSPR") },
        { QRegularExpression(QStringLiteral(R"(\ba[\s\.]*p[\s\.]*r[\s\.]*s\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("APRS") },
        { QRegularExpression(QStringLiteral(R"(\bwin[\s-]*link\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Winlink") },
        { QRegularExpression(QStringLiteral(R"(\bs[\s\.]*s[\s\.]*t[\s\.]*v\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("SSTV") },
        { QRegularExpression(QStringLiteral(R"(\br[\s\.]*t[\s\.]*t[\s\.]*y\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("RTTY") },
        { QRegularExpression(QStringLiteral(R"(\bp[\s\.]*s[\s\.]*k[\s-]*31\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("PSK31") },
        { QRegularExpression(QStringLiteral(R"(\bs[\s\.]*s[\s\.]*b\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("SSB") },
        { QRegularExpression(QStringLiteral(R"(\bl[\s\.]*s[\s\.]*b\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("LSB") },
        { QRegularExpression(QStringLiteral(R"(\bv[\s\.]*h[\s\.]*f\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("VHF") },
        { QRegularExpression(QStringLiteral(R"(\bu[\s\.]*h[\s\.]*f\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("UHF") },
        { QRegularExpression(QStringLiteral(R"(\bc[\s\.]*t[\s\.]*c[\s\.]*s[\s\.]*s\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("CTCSS") },
        { QRegularExpression(QStringLiteral(R"(\bd[\s\.]*t[\s\.]*m[\s\.]*f\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("DTMF") },
        { QRegularExpression(QStringLiteral(R"(\bp[\s\.]*t[\s\.]*t\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("PTT") },
        { QRegularExpression(QStringLiteral(R"(\ba[\s\.]*m[\s\.]*b[\s\.]*e\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("AMBE") },

        // 5. Additional Q-Codes & Ham Activities (POTA, SOTA, IOTA, LoTW, eQSL)
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*t\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRT") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*v\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRV") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*x\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRX") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*l\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRL") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*s\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRS") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*r[\s\.]*q\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QRQ") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*s[\s\.]*t\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QST") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*s[\s\.]*p\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QSP") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*s[\s\.]*a\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QSA") },
        { QRegularExpression(QStringLiteral(R"(\bq[\s\.]*t[\s\.]*r\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("QTR") },
        { QRegularExpression(QStringLiteral(R"(\b(?:parks\s+on\s+the\s+air|p[\s\.]*o[\s\.]*t[\s\.]*a)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("POTA") },
        { QRegularExpression(QStringLiteral(R"(\b(?:summits\s+on\s+the\s+air|s[\s\.]*o[\s\.]*t[\s\.]*a)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("SOTA") },
        { QRegularExpression(QStringLiteral(R"(\b(?:islands\s+on\s+the\s+air|i[\s\.]*o[\s\.]*t[\s\.]*a)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("IOTA") },
        { QRegularExpression(QStringLiteral(R"(\b(?:logbook\s+of\s+the\s+world|l[\s\.]*o[\s\.]*t[\s\.]*w)\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("LoTW") },
        { QRegularExpression(QStringLiteral(R"(\be[\s\.]*q[\s\.]*s[\s\.]*l\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("eQSL") },
        { QRegularExpression(QStringLiteral(R"(\bham[\s-]*vention\b)"), QRegularExpression::CaseInsensitiveOption), QStringLiteral("Hamvention") }
    };

    for (const auto &rule : techVocabRules) {
        finalOut.replace(rule.re, rule.replacement);
    }

    static const QRegularExpression singleCodeWithPeriodRe(QStringLiteral(R"(^([A-Z0-9/]{2,12})\.$)"));
    finalOut.replace(singleCodeWithPeriodRe, QStringLiteral("\\1"));
    return finalOut;
}

QString TranscriptionEngine::normalizeHamTranscript(const QString &raw)
{
    // The parser's static regex/hash tables are shared by the Whisper worker and the main thread (Apple partials)
    static QMutex normalizeMutex;
    QMutexLocker lock(&normalizeMutex);
    return normalizeHamTranscriptUnlocked(raw);
}

QString TranscriptionEngine::runWhisperInference(const float *samples16k, size_t sampleCount, const QString &langCode,
                                                 std::vector<WhisperSegment> *segments, bool finalPass, bool accuratePass)
{
    m_lastWhisperFailed = false;
    m_lastWhisperAborted = false;
    if (segments) {
        segments->clear();
    }
    if (!m_whisperCtx || samples16k == nullptr || sampleCount < 4800) {
        return QString();
    }

    std::vector<float> padded;
    const float *inputPtr = samples16k;
    size_t inputCount = sampleCount;
    if (sampleCount < 16800) {
        padded.assign(16800, 0.0f);
        std::copy(samples16k, samples16k + sampleCount, padded.begin());
        inputPtr = padded.data();
        inputCount = padded.size();
    }

    const bool largeEncoder = whisper_model_n_audio_layer(m_whisperCtx) >= 24;
    whisper_full_params wparams = whisper_full_default_params(accuratePass ? WHISPER_SAMPLING_BEAM_SEARCH
                                                                           : WHISPER_SAMPLING_GREEDY);
    wparams.print_realtime   = false;
    wparams.print_progress   = false;
    wparams.print_timestamps = false;
    wparams.print_special    = false;
    wparams.translate        = false;
    wparams.no_context       = true;
    // Always use single_segment + no_timestamps so whisper.cpp never truncates tokens after an internal
    // timestamp token (whisper.cpp:5641) and never runs a second seek re-encode pass (whisper.cpp:5819).
    wparams.no_timestamps    = true;
    wparams.single_segment   = true;
    wparams.suppress_blank   = true;
    wparams.suppress_non_speech_tokens = true;
#if defined(Q_OS_IOS) && (!defined(TARGET_OS_SIMULATOR) || !TARGET_OS_SIMULATOR)
    wparams.n_threads        = std::clamp(QThread::idealThreadCount() / 2, 2, 4);
#else
    wparams.n_threads        = std::clamp(QThread::idealThreadCount() / 2, 2, 8);
#endif

    const int audioFrames = static_cast<int>(inputCount / 320);
    const int audioSeconds = static_cast<int>(sampleCount / 16000);
    if (accuratePass) {
        // End-of-over pass: beam search with temperature fallback over the whole over. Beams x tokens stays well
        // inside the 3 x n_text_ctx self-attention KV cache that whisper_init_state allocates.
        wparams.beam_search.beam_size = m_whisperUsingGpu ? 5 : 3;
        wparams.greedy.best_of   = 2;
        wparams.temperature      = 0.0f;
        wparams.temperature_inc  = 0.2f;
        wparams.max_tokens       = std::clamp(audioSeconds * 8 + 24, 32, 180);
        // Small encoders lose accuracy when the 30 s context is truncated; Turbo tolerates a sized context
        wparams.audio_ctx = largeEncoder ? std::clamp((audioFrames + 128 + 63) & ~63, 512, 1500) : 0;
    } else {
        // No temperature fallback: a re-decode would make a single live pass unbounded
        wparams.temperature      = 0.0f;
        wparams.temperature_inc  = 0.0f;
        wparams.greedy.best_of   = 1;
        // Bounded by speech rate so a repetition loop cannot run on for the whole segment
        wparams.max_tokens       = std::clamp(audioSeconds * 6 + 12, 20, 64);
        // Live drafts keep ~1 s of encoder context past the audio; tighter contexts garble the last words
        wparams.audio_ctx = std::clamp((audioFrames + 48 + 63) & ~63, 128, 512);
    }

    struct AbortCtx {
        TranscriptionEngine *self;
        bool finalPass;
        bool aborted { false };
    } abortCtx { this, finalPass, false };

    wparams.abort_callback_user_data = &abortCtx;
    wparams.abort_callback = [](void *ud) -> bool {
        auto *ctx = static_cast<AbortCtx *>(ud);
        TranscriptionEngine *te = ctx->self;
        if (te->m_stopWorker.load(std::memory_order_relaxed) ||
            !te->m_enabled.load(std::memory_order_relaxed) ||
            !te->m_connected.load(std::memory_order_relaxed) ||
            te->m_reloadModelPending.load(std::memory_order_relaxed)) {
            ctx->aborted = true;
            return true;
        }
        // Live passes yield immediately if a new handle interrupted, or if the over ended while >=300ms of
        // un-drained audio is still waiting in the ring buffer (otherwise let this pass finish and avoid a re-encode).
        if (!ctx->finalPass) {
            if (te->m_newTurnPending.load(std::memory_order_relaxed)) {
                ctx->aborted = true;
                return true;
            }
            if (te->m_turnEndPending.load(std::memory_order_relaxed)) {
                const uint32_t r = te->m_ringReadPos.load(std::memory_order_relaxed);
                const uint32_t w = te->m_ringWritePos.load(std::memory_order_relaxed);
                if ((w - r) >= 2400U) {
                    ctx->aborted = true;
                    return true;
                }
            }
        }
        return false;
    };

    const bool isMultilingualModel = whisper_is_multilingual(m_whisperCtx) != 0;
    const bool autoLang = (langCode.isEmpty() || langCode == QStringLiteral("auto"));
    QByteArray langUtf8;
    if (!isMultilingualModel) {
        langUtf8 = QByteArrayLiteral("en");
    } else if (!autoLang) {
        langUtf8 = langCode.toUtf8();
    } else if (!m_turnDetectedLang.isEmpty()) {
        // Reuse the language already auto-detected on this turn so we skip an extra decoder pass on every 320ms step
        langUtf8 = m_turnDetectedLang;
    } else {
        langUtf8 = QByteArrayLiteral("auto");
    }
    wparams.language = langUtf8.constData();
    wparams.detect_language = false;
    wparams.initial_prompt = m_turnPrompt.isEmpty() ? nullptr : m_turnPrompt.constData();

    // Prefix-conditioned BPE token continuation boosting for multi-token Ham brands & technical vocabulary
    wparams.logits_filter_callback_user_data = this;
    wparams.logits_filter_callback = [](whisper_context *, whisper_state *,
                                        const whisper_token_data *tokens, int n_tokens,
                                        float *logits, void *user_data) {
        auto *self = static_cast<TranscriptionEngine *>(user_data);
        if (!self || !tokens || n_tokens < 1 || !logits || self->m_whisperVocabSize <= 0) {
            return;
        }
        const int32_t lastTok = tokens[n_tokens - 1].id;
        auto uniIt = self->m_whisperUnigramBoosts.constFind(lastTok);
        if (uniIt != self->m_whisperUnigramBoosts.cend()) {
            for (const auto &pair : *uniIt) {
                if (pair.first >= 0 && pair.first < self->m_whisperVocabSize) {
                    logits[pair.first] += pair.second;
                }
            }
        }
        if (n_tokens >= 2) {
            const int32_t prevTok = tokens[n_tokens - 2].id;
            const uint64_t biKey = (static_cast<uint64_t>(static_cast<uint32_t>(prevTok)) << 32) |
                                   static_cast<uint32_t>(lastTok);
            auto biIt = self->m_whisperBigramBoosts.constFind(biKey);
            if (biIt != self->m_whisperBigramBoosts.cend()) {
                for (const auto &pair : *biIt) {
                    if (pair.first >= 0 && pair.first < self->m_whisperVocabSize) {
                        logits[pair.first] += pair.second;
                    }
                }
            }
        }
    };

    int rc = whisper_full(m_whisperCtx, wparams, inputPtr, static_cast<int>(inputCount));

    // Lock in the turn's auto-detected language once >= 1.5s of audio has been analyzed. Accented English on
    // narrowband audio is often scored as another language at low confidence, so an unsure guess falls back to English.
    if (rc == 0 && isMultilingualModel && autoLang && m_turnDetectedLang.isEmpty() && sampleCount >= 24000) {
        const int lid = whisper_full_lang_id(m_whisperCtx);
        const char *lstr = lid >= 0 ? whisper_lang_str(lid) : nullptr;
        if (lstr) {
            QByteArray chosen(lstr);
            const float pDetected = whisper_full_lang_prob(m_whisperCtx, lid);
            const float pEnglish = whisper_full_lang_prob(m_whisperCtx, whisper_lang_id("en"));
            if (chosen != "en" && pDetected >= 0.0f && pDetected < 0.75f && pEnglish >= 0.08f) {
                qInfo().noquote() << "[Transcription] language guess" << chosen << "p =" << pDetected
                                  << "is unsure (en p =" << pEnglish << "); using English";
                chosen = QByteArrayLiteral("en");
                if (accuratePass) {
                    langUtf8 = chosen;
                    wparams.language = langUtf8.constData();
                    rc = whisper_full(m_whisperCtx, wparams, inputPtr, static_cast<int>(inputCount));
                }
            }
            m_turnDetectedLang = chosen;
        }
    }

    if (rc != 0) {
        m_lastWhisperAborted = abortCtx.aborted;
        if (!abortCtx.aborted) {
            qWarning().noquote() << "[Transcription] whisper_full failed with code" << rc
                                 << (m_whisperUsingGpu ? "(GPU)" : "(CPU)");
            m_lastWhisperFailed = true;
        }
        return QString();
    }

    QString result;
    const int nSegments = whisper_full_n_segments(m_whisperCtx);
    for (int i = 0; i < nSegments; ++i) {
        const char *segText = whisper_full_get_segment_text(m_whisperCtx, i);
        const QString cleaned = segText ? cleanWhisperOutput(QString::fromUtf8(segText)) : QString();
        if (segments) {
            const int64_t t1 = whisper_full_get_segment_t1(m_whisperCtx, i); // 10 ms units
            WhisperSegment seg;
            seg.text = cleaned;
            seg.endSample = static_cast<size_t>(std::clamp<int64_t>(t1 * 160, 0, static_cast<int64_t>(sampleCount)));
            segments->push_back(seg);
        }
        if (!cleaned.isEmpty()) {
            if (!result.isEmpty()) {
                result += QLatin1Char(' ');
            }
            result += cleaned;
        }
    }

    return m_turnPrompt.isEmpty() ? result.trimmed() : stripPromptLeak(result);
}

void TranscriptionEngine::publishFinalTurn(int turnId, uint32_t srcId, const QString &callsign, const QString &text)
{
    if (text.trimmed().isEmpty()) {
        return;
    }

    const QString nowStamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    bool updatedCurrentUi = false;
    {
        QMutexLocker lock(&m_stateMutex);
        QVariantMap item;
        item[QStringLiteral("turnId")] = turnId;
        item[QStringLiteral("srcId")] = srcId;
        item[QStringLiteral("callsign")] = callsign;
        item[QStringLiteral("text")] = text;
        item[QStringLiteral("time")] = nowStamp;
        m_recentTurns.prepend(item);
        while (m_recentTurns.size() > 20) {
            m_recentTurns.removeLast();
        }
        if (m_connected.load(std::memory_order_relaxed) &&
            m_currentTurnId.load(std::memory_order_relaxed) == turnId) {
            m_currentTranscript = text;
            if (m_currentCallsign.isEmpty() && !callsign.isEmpty()) {
                m_currentCallsign = callsign;
            }
            updatedCurrentUi = true;
        }
    }

    if (updatedCurrentUi && !m_activeTurn.load(std::memory_order_relaxed)) {
        scheduleAutoClear(kAutoClearDelayMs);
    }

    QMetaObject::invokeMethod(this, [this, turnId, srcId, callsign, text, nowStamp]() {
        emit transcriptChanged();
        emit recentTurnsChanged();
        emit finalTranscriptReady(turnId, srcId, callsign, text, nowStamp);
    }, Qt::QueuedConnection);
}

void TranscriptionEngine::finalizeAppleTurn(int sessionId)
{
    AppleTurnRecord rec;
    {
        QMutexLocker lock(&m_stateMutex);
        auto it = m_appleTurns.find(sessionId);
        if (it == m_appleTurns.end()) {
            return;
        }
        rec = it.value();
        m_appleTurns.erase(it);
    }
    qInfo().noquote() << "[Transcription] turn" << rec.uiTurnId << rec.callsign << "final (Apple):"
                      << (rec.lastText.isEmpty() ? QStringLiteral("<no speech recognized>") : rec.lastText);
    publishFinalTurn(rec.uiTurnId, rec.srcId, rec.callsign, rec.lastText);
}

void TranscriptionEngine::finalizeAppleTurns(bool onlyExpired)
{
    QList<int> due;
    {
        QMutexLocker lock(&m_stateMutex);
        if (m_appleTurns.isEmpty()) {
            return;
        }
        const qint64 now = monotonicMs();
        for (auto it = m_appleTurns.cbegin(); it != m_appleTurns.cend(); ++it) {
            if (!onlyExpired || (it->ended && it->deadlineMs <= now)) {
                due.append(it.key());
            }
        }
    }
    for (int sessionId : due) {
        finalizeAppleTurn(sessionId);
    }
}

void TranscriptionEngine::onAppleSpeechPartialResult(int sessionId, const QString &norm, bool isFinal)
{
    bool uiChanged = false;
    bool finalizeNow = false;
    {
        QMutexLocker lock(&m_stateMutex);
        auto it = m_appleTurns.find(sessionId);
        if (it == m_appleTurns.end()) {
            return;
        }
        if (!norm.isEmpty()) {
            const QString snapped = snapToKnownCallsigns(norm, knownCallsignsLocked(it->callsign));
            it->lastText = snapped;
            if (m_currentTurnId.load(std::memory_order_relaxed) == it->uiTurnId && m_currentTranscript != snapped) {
                m_currentTranscript = snapped;
                uiChanged = true;
            }
        }
        finalizeNow = isFinal && it->ended;
    }
    if (uiChanged) {
        emit transcriptChanged();
    }
    if (finalizeNow) {
        finalizeAppleTurn(sessionId);
    }
}

void TranscriptionEngine::onAppleSpeechStatusChanged(bool authorized, bool available, const QString &statusMsg)
{
    Q_UNUSED(available);
    Q_UNUSED(statusMsg);
    m_appleSpeechAuthorized.store(authorized, std::memory_order_relaxed);
    m_appleSpeechDenied.store(!authorized, std::memory_order_relaxed);
    m_reloadModelPending.store(true, std::memory_order_release);
    wakeWorker();
    refreshIdleStatusText();
    emit modelStateChanged();
}

void TranscriptionEngine::onAppleSpeechSessionFailed(int sessionId, const QString &reason)
{
    qWarning().noquote() << "[Transcription] Apple Speech cannot transcribe:" << reason;
    {
        QMutexLocker lock(&m_stateMutex);
        m_appleFailureReason = reason;
    }
    m_appleFailedAtMs.store(monotonicMs(), std::memory_order_relaxed);
    m_appleStartFailed.store(true, std::memory_order_release);
    m_appleFailedSession.store(sessionId, std::memory_order_release);
    // Loads Whisper (if downloaded) so the rest of this over and later overs still get captions
    m_reloadModelPending.store(true, std::memory_order_release);
    wakeWorker();
    refreshIdleStatusText();
    emit modelStateChanged();
}

void TranscriptionEngine::workerLoop()
{
#if defined(Q_OS_DARWIN)
    // UTILITY QoS confined Whisper to the efficiency cores (4-6x slower per pass)
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INITIATED, 0);
#endif

    enum class TurnEngine { None, Apple, Parakeet, Whisper };

    DmrSpeechEnhancer enhancer;
    std::vector<float> chunk16k;
    chunk16k.reserve(16384);
    std::vector<float> window16k;
    window16k.reserve(kWhisperHardWindowSamples + 32000);
    std::vector<float> overAudio16k;
    overAudio16k.reserve(16000 * 30);
    bool overAudioTruncated = false;
    QStringList turnKnownCalls;
    std::vector<WhisperSegment> segments;
    QString committedText;
    QString lastPublished;
    QStringList prevWindowWords;
    QStringList lockedWindowWords;
    QString lastWindowDisplay;
    size_t samplesAtLastInference = 0;
    bool livePassLogged = false;

    bool turnOpen = false;
    bool turnProvisional = false;
    TurnEngine turnEngine = TurnEngine::None;
    int turnUiId = 0;
    int lastEndedUiId = -1;
    QString turnCallsign;
    uint32_t turnSrcId = 0;
    int appleSessionId = 0;
#if DS_HAS_APPLE_SPEECH
    int appleSessionCounter = 0;
#endif

    QElapsedTimer lastAudioTimer;
    lastAudioTimer.start();
    QElapsedTimer turnTimer;
    turnTimer.start();
    QElapsedTimer modelCheckTimer;
    modelCheckTimer.start();

    auto currentLanguage = [this]() {
        QMutexLocker lock(&m_stateMutex);
        return m_language;
    };

    auto joinRaw = [](const QString &a, const QString &b) -> QString {
        if (a.isEmpty()) return b;
        if (b.isEmpty()) return a;
        return a + QLatin1Char(' ') + b;
    };

    auto refreshTurnContext = [&]() {
        QString lang;
        {
            QMutexLocker lock(&m_stateMutex);
            turnKnownCalls = knownCallsignsLocked(turnCallsign);
            lang = m_language;
        }
        m_turnPrompt = buildWhisperPrompt(turnKnownCalls, lang);
    };

    auto publishLive = [&](const QString &rawFull) {
        const QString norm = snapToKnownCallsigns(normalizeHamTranscript(rawFull), turnKnownCalls);
        if (norm.isEmpty() || norm == lastPublished) {
            return;
        }
        lastPublished = norm;
        bool changed = false;
        {
            QMutexLocker lock(&m_stateMutex);
            if (m_currentTurnId.load(std::memory_order_relaxed) == turnUiId) {
                m_currentTranscript = norm;
                changed = true;
            }
        }
        if (changed) {
            QMetaObject::invokeMethod(this, &TranscriptionEngine::transcriptChanged, Qt::QueuedConnection);
        }
    };

    auto ensureInferenceBackend = [&]() {
        if (m_whisperCtx && m_whisperUsingGpu && !whisperGpuAllowed()) {
            const QString path = m_loadedModelPath;
            ensureModelLoaded(path, false);
        }
    };

    auto resetTurnBuffers = [&]() {
        enhancer.reset();
        chunk16k.clear();
        window16k.clear();
        overAudio16k.clear();
        overAudioTruncated = false;
        segments.clear();
        committedText.clear();
        lastPublished.clear();
        prevWindowWords.clear();
        lockedWindowWords.clear();
        lastWindowDisplay.clear();
        m_turnDetectedLang.clear();
        samplesAtLastInference = 0;
        livePassLogged = false;
    };

    auto routeChunk = [&]() {
        if (chunk16k.empty()) {
            return;
        }
        if (turnEngine == TurnEngine::None) {
            const QString curEng = engineType();
            if (curEng == QStringLiteral("parakeet") && m_parakeetLoaded.load(std::memory_order_relaxed)) {
                turnEngine = TurnEngine::Parakeet;
            } else if (curEng == QStringLiteral("whisper") && m_whisperLoaded.load(std::memory_order_relaxed)) {
                turnEngine = TurnEngine::Whisper;
            }
        }
#if DS_HAS_APPLE_SPEECH
        if (turnEngine == TurnEngine::Apple) {
            ios_speech_append_samples_16k(chunk16k.data(), chunk16k.size());
        }
#endif
        window16k.insert(window16k.end(), chunk16k.begin(), chunk16k.end());
        if (turnEngine != TurnEngine::Apple && !overAudioTruncated) {
            if (overAudio16k.size() + chunk16k.size() <= kMaxOverAudioSamples) {
                overAudio16k.insert(overAudio16k.end(), chunk16k.begin(), chunk16k.end());
            } else {
                overAudioTruncated = true;
            }
        }
        if (turnEngine != TurnEngine::Whisper && turnEngine != TurnEngine::Parakeet &&
            window16k.size() > kWhisperHardWindowSamples) {
            window16k.erase(window16k.begin(),
                            window16k.begin() + static_cast<std::ptrdiff_t>(window16k.size() - kWhisperHardWindowSamples));
        }
        chunk16k.clear();
    };

    auto runWindowInference = [&](std::vector<WhisperSegment> *segs, bool finalPass) -> QString {
        if (window16k.empty()) {
            return QString();
        }
        if (turnEngine == TurnEngine::Parakeet) {
            return runParakeetInference(window16k.data(), window16k.size(), segs);
        }
        ensureInferenceBackend();
        if (!m_whisperCtx) {
            return QString();
        }
        QString text = runWhisperInference(window16k.data(), window16k.size(), currentLanguage(), segs, finalPass);
        if (m_lastWhisperFailed && m_whisperUsingGpu) {
            qWarning() << "[Transcription] GPU inference failed; switching Whisper to CPU";
            m_gpuBroken = true;
            const QString path = m_loadedModelPath;
            if (ensureModelLoaded(path, false)) {
                text = runWhisperInference(window16k.data(), window16k.size(), currentLanguage(), segs, finalPass);
            }
        }
        return text;
    };

    // Re-transcribes the whole over in <=25 s chunks cut at pauses, with beam search and full context. Returns an
    // empty string when the pass cannot run or was interrupted, so the caller keeps the live draft.
    auto transcribeWholeOver = [&]() -> QString {
        if (overAudioTruncated || overAudio16k.size() < 9600) {
            return QString();
        }
        if (turnEngine == TurnEngine::Whisper) {
            ensureInferenceBackend();
            // Turbo's 32-layer encoder on CPU (backgrounded app or Simulator) would take tens of seconds per chunk
            if (!m_whisperCtx || (!m_whisperUsingGpu && whisper_model_n_audio_layer(m_whisperCtx) >= 24)) {
                return QString();
            }
        } else if (turnEngine != TurnEngine::Parakeet || !m_parakeetCtx) {
            return QString();
        }

        QElapsedTimer accurateTimer;
        accurateTimer.start();
        const size_t total = overAudio16k.size();
        QString joined;
        size_t start = 0;
        while (start < total) {
            size_t end = total;
            if (total - start > kAccurateChunkMaxSamples) {
                end = findQuietestSampleOffset(overAudio16k, start + kAccurateChunkMinCutSamples,
                                               start + kAccurateChunkMaxSamples);
                if (end <= start + kAccurateChunkMinCutSamples / 2) {
                    end = start + kAccurateChunkMaxSamples;
                }
            }
            const size_t len = end - start;
            const float *chunk = overAudio16k.data() + start;
            double sum = 0.0;
            for (size_t i = 0; i < len; ++i) {
                sum += static_cast<double>(chunk[i]) * static_cast<double>(chunk[i]);
            }
            if (len >= 8000 && std::sqrt(sum / static_cast<double>(len)) > 0.004) {
                QString raw;
                if (turnEngine == TurnEngine::Parakeet) {
                    raw = runParakeetInference(chunk, len, nullptr);
                } else {
                    raw = runWhisperInference(chunk, len, currentLanguage(), nullptr, true, true);
                    if (m_lastWhisperAborted || m_lastWhisperFailed) {
                        return QString();
                    }
                }
                joined = joinRaw(joined, raw);
            }
            start = end;
        }
        qInfo().noquote() << "[Transcription] turn" << turnUiId << "accurate pass over"
                          << QString::number(total / 16000.0, 'f', 1) << "s took" << accurateTimer.elapsed() << "ms";
        return snapToKnownCallsigns(normalizeHamTranscript(joined), turnKnownCalls);
    };

    auto feedRange = [&](uint32_t fromPos, uint32_t toPos) {
        while (fromPos != toPos) {
            enhancer.process(m_ringBuffer[fromPos & kRingMask], chunk16k);
            ++fromPos;
        }
        m_ringReadPos.store(toPos, std::memory_order_release);
        lastAudioTimer.restart();
        routeChunk();
    };

    auto beginTurn = [&](int uiId, const QString &call, uint32_t src, bool provisional) {
        resetTurnBuffers();
        turnOpen = true;
        turnProvisional = provisional;
        turnUiId = uiId;
        turnCallsign = call;
        turnSrcId = src;
        turnEngine = TurnEngine::None;
        appleSessionId = 0;
        lastAudioTimer.restart();
        turnTimer.restart();
        m_lastSamplePushMs.store(monotonicMs(), std::memory_order_relaxed);
        refreshTurnContext();

        if (engineType() == QStringLiteral("apple")) {
#if DS_HAS_APPLE_SPEECH
            if (m_appleSpeechAuthorized.load(std::memory_order_relaxed)) {
                // The bridge serves one recognition task at a time; overs still awaiting Apple's final result publish now
                finalizeAppleTurns(false);
                const int sessionId = ++appleSessionCounter;
                QString lang;
                QString userCall;
                {
                    QMutexLocker lock(&m_stateMutex);
                    AppleTurnRecord rec;
                    rec.uiTurnId = uiId;
                    rec.srcId = src;
                    rec.callsign = call;
                    m_appleTurns.insert(sessionId, rec);
                    lang = m_language;
                    userCall = m_userCallsign;
                }
                const QByteArray langUtf8 = lang.toUtf8();
                const QByteArray callUtf8 = call.toUtf8();
                const QByteArray userUtf8 = userCall.toUtf8();
                if (ios_speech_start_turn(sessionId, langUtf8.constData(), callUtf8.constData(), userUtf8.constData())) {
                    appleSessionId = sessionId;
                    turnEngine = TurnEngine::Apple;
                    if (m_appleStartFailed.exchange(false, std::memory_order_acq_rel)) {
                        refreshIdleStatusText();
                    }
                    qInfo().noquote() << "[Transcription] turn" << uiId << call << "-> Apple Speech";
                    return;
                }
                {
                    QMutexLocker lock(&m_stateMutex);
                    m_appleTurns.remove(sessionId);
                }
                m_appleFailedAtMs.store(monotonicMs(), std::memory_order_relaxed);
                if (!m_appleStartFailed.exchange(true, std::memory_order_acq_rel)) {
                    refreshIdleStatusText();
                }
            }
#endif
            qInfo().noquote() << "[Transcription] turn" << uiId << call << "-> Apple Speech not ready (Whisper disabled)";
            return;
        }
        if (engineType() == QStringLiteral("parakeet")) {
            if (m_parakeetLoaded.load(std::memory_order_relaxed)) {
                turnEngine = TurnEngine::Parakeet;
            }
            qInfo().noquote() << "[Transcription] turn" << uiId << call << "->"
                              << (turnEngine == TurnEngine::Parakeet ? "Parakeet TDT v3" : "no Parakeet model ready");
            return;
        }
        if (m_whisperLoaded.load(std::memory_order_relaxed)) {
            turnEngine = TurnEngine::Whisper;
        }
        qInfo().noquote() << "[Transcription] turn" << uiId << call << "->"
                          << (turnEngine == TurnEngine::Whisper ? "Whisper" : "no Whisper model ready");
    };

    // Audio reached the worker before the STREAM_NEW event: keep the over intact and adopt the event's identity
    auto relabelTurn = [&](int uiId, const QString &call, uint32_t src) {
        turnUiId = uiId;
        if (!call.isEmpty()) turnCallsign = call;
        if (src != 0) turnSrcId = src;
        turnProvisional = false;
        lastPublished.clear();
        refreshTurnContext();
        if (appleSessionId > 0) {
            bool restored = false;
            {
                QMutexLocker lock(&m_stateMutex);
                auto it = m_appleTurns.find(appleSessionId);
                if (it != m_appleTurns.end()) {
                    it->uiTurnId = uiId;
                    it->callsign = turnCallsign;
                    it->srcId = turnSrcId;
                    if (!it->lastText.isEmpty() && m_currentTurnId.load(std::memory_order_relaxed) == uiId) {
                        m_currentTranscript = it->lastText;
                        restored = true;
                    }
                }
            }
            if (restored) {
                QMetaObject::invokeMethod(this, &TranscriptionEngine::transcriptChanged, Qt::QueuedConnection);
            }
        }
    };

    auto closeTurnState = [&]() {
        resetTurnBuffers();
        turnOpen = false;
        turnProvisional = false;
        turnEngine = TurnEngine::None;
        appleSessionId = 0;
    };

    auto endTurn = [&](int uiId, const QString &call, uint32_t src) {
        if (!turnOpen) {
            return;
        }
        enhancer.flush(chunk16k);
        routeChunk();

        if (turnEngine == TurnEngine::Apple && appleSessionId > 0) {
            {
                QMutexLocker lock(&m_stateMutex);
                auto it = m_appleTurns.find(appleSessionId);
                if (it != m_appleTurns.end()) {
                    it->uiTurnId = uiId;
                    it->callsign = call;
                    it->srcId = src;
                    it->ended = true;
                    it->deadlineMs = monotonicMs() + kAppleFinalGraceMs;
                }
            }
#if DS_HAS_APPLE_SPEECH
            ios_speech_end_turn();
#endif
        } else if (turnEngine == TurnEngine::None) {
            qInfo().noquote() << "[Transcription] turn" << uiId << call << "ended with no engine ready; nothing transcribed";
        } else if (turnEngine == TurnEngine::Parakeet || turnEngine == TurnEngine::Whisper) {
            if (!call.isEmpty() && call != turnCallsign) {
                turnCallsign = call;
                refreshTurnContext();
            }
            const QString accurateText = transcribeWholeOver();
            if (!accurateText.isEmpty()) {
                qInfo().noquote() << "[Transcription] turn" << uiId << call
                                  << (turnEngine == TurnEngine::Parakeet ? "final (Parakeet, accurate):" : "final (Whisper, accurate):")
                                  << accurateText;
                publishFinalTurn(uiId, src, call, accurateText);
                closeTurnState();
                return;
            }

            // Accurate pass unavailable: finish the live draft. Only run a pass if new speech arrived since the
            // last live pass (or no live pass completed yet).
            bool needFinalInference = lastPublished.isEmpty() || samplesAtLastInference == 0;
            if (!needFinalInference && window16k.size() > samplesAtLastInference) {
                const size_t tailSamples = window16k.size() - samplesAtLastInference;
                if (tailSamples >= 2400) {
                    const float *tailPtr = window16k.data() + samplesAtLastInference;
                    double tailSum = 0.0;
                    for (size_t i = 0; i < tailSamples; ++i) {
                        tailSum += static_cast<double>(tailPtr[i]) * static_cast<double>(tailPtr[i]);
                    }
                    const double tailRms = std::sqrt(tailSum / static_cast<double>(tailSamples));
                    if (tailRms > 0.004) {
                        needFinalInference = true;
                    }
                }
            }

            QString finalText = lastPublished;
            if (needFinalInference && window16k.size() >= 4800) {
                const QString rawWin = runWindowInference(nullptr, true);
                const QString normWin = normalizeHamTranscript(rawWin);
                const QString stableWin = stabilizeWindowTranscript(normWin, prevWindowWords, lockedWindowWords, lastWindowDisplay);
                const QString combined = normalizeHamTranscript(joinRaw(committedText, stableWin));
                if (!combined.isEmpty()) {
                    finalText = combined;
                }
            }
            finalText = snapToKnownCallsigns(finalText, turnKnownCalls);
            qInfo().noquote() << "[Transcription] turn" << uiId << call
                              << (turnEngine == TurnEngine::Parakeet ? "final (Parakeet):" : "final (Whisper):")
                              << (finalText.isEmpty() ? QStringLiteral("<no speech recognized>") : finalText);
            publishFinalTurn(uiId, src, call, finalText);
        }

        closeTurnState();
    };

    auto abortTurn = [&]() {
        if (!turnOpen) {
            return;
        }
        if (turnEngine == TurnEngine::Apple && appleSessionId > 0) {
#if DS_HAS_APPLE_SPEECH
            ios_speech_cancel_turn();
#endif
            QMutexLocker lock(&m_stateMutex);
            m_appleTurns.remove(appleSessionId);
        }
        closeTurnState();
    };

    while (!m_stopWorker.load(std::memory_order_acquire)) {
        {
            QMutexLocker lock(&m_wakeMutex);
            if (!m_stopWorker.load(std::memory_order_relaxed) &&
                !m_reloadModelPending.load(std::memory_order_relaxed) &&
                !m_turnEndPending.load(std::memory_order_relaxed) &&
                !m_newTurnPending.load(std::memory_order_relaxed) &&
                m_ringReadPos.load(std::memory_order_relaxed) == m_ringWritePos.load(std::memory_order_acquire)) {
                m_wakeCond.wait(&m_wakeMutex, turnOpen ? 20 : 150);
            }
        }

        if (m_stopWorker.load(std::memory_order_acquire)) {
            break;
        }

        const bool enabled = m_enabled.load(std::memory_order_relaxed);

        // 1. Transcription switched off: drop the open turn and anything still queued
        if (!enabled) {
            abortTurn();
            m_ringReadPos.store(m_ringWritePos.load(std::memory_order_acquire), std::memory_order_release);
            m_newTurnPending.store(false, std::memory_order_relaxed);
            m_turnEndPending.store(false, std::memory_order_relaxed);
            bool uiDirty = m_activeTurn.exchange(false, std::memory_order_acq_rel);
            {
                QMutexLocker lock(&m_stateMutex);
                if (!m_currentTranscript.isEmpty() || !m_currentCallsign.isEmpty() || !m_pendingCallsign.isEmpty()) {
                    m_currentTranscript.clear();
                    m_currentCallsign.clear();
                    m_pendingCallsign.clear();
                    m_pendingSrcId = 0;
                    uiDirty = true;
                }
            }
            if (uiDirty) {
                QMetaObject::invokeMethod(this, &TranscriptionEngine::transcriptChanged, Qt::QueuedConnection);
            }
        }

        // 2. Keep only the selected engine's model resident in memory
        {
            const bool reloadRequested = m_reloadModelPending.exchange(false, std::memory_order_acq_rel);
            if (reloadRequested) {
                m_failedModelPath.clear();
                m_modelLoadFailed.store(false, std::memory_order_relaxed);
                m_parakeetLoadFailed.store(false, std::memory_order_relaxed);
            }
            const QString curEng = engineType();
            const bool wantParakeet = enabled && (curEng == QStringLiteral("parakeet"));
            const bool wantWhisper = enabled && (curEng == QStringLiteral("whisper"));
            const bool parakeetBusy = turnOpen && turnEngine == TurnEngine::Parakeet;
            const bool whisperBusy = turnOpen && turnEngine == TurnEngine::Whisper;

            if (!wantParakeet && m_parakeetCtx && !parakeetBusy) {
                unloadParakeet();
                refreshIdleStatusText();
            }
            if (!wantWhisper && m_whisperCtx && !whisperBusy) {
                unloadModel();
                refreshIdleStatusText();
            }

            if (wantParakeet) {
                if ((reloadRequested || modelCheckTimer.elapsed() > 2000) &&
                    !m_parakeetCtx && !m_parakeetLoadFailed.load(std::memory_order_relaxed) &&
                    isParakeetDownloaded()) {
                    modelCheckTimer.restart();
                    ensureParakeetLoaded();
                }
            } else if (wantWhisper) {
                if (reloadRequested || modelCheckTimer.elapsed() > 2000) {
                    modelCheckTimer.restart();
                    const QString desiredPath = resolveLoadableModelPath();
                    const bool gpu = whisperGpuAllowed();
                    if (!desiredPath.isEmpty() && desiredPath != m_failedModelPath) {
                        const bool pathChanged = (m_loadedModelPath != desiredPath);
                        const bool gpuUpgrade = (m_whisperCtx && !m_whisperUsingGpu && gpu);
                        if (!m_whisperCtx || ((pathChanged || gpuUpgrade) && !whisperBusy)) {
                            ensureModelLoaded(desiredPath, gpu);
                        }
                    } else if (desiredPath.isEmpty() && m_whisperCtx && !whisperBusy) {
                        unloadModel();
                        refreshIdleStatusText();
                    }
                }
            }
            if (reloadRequested) {
                refreshIdleStatusText();
            }
        }

        if (!enabled) {
            finalizeAppleTurns(true);
            continue;
        }

        // Apple Speech gave up on the open over
        const int failedSession = m_appleFailedSession.exchange(0, std::memory_order_acq_rel);
        if (failedSession != 0 && turnOpen && turnEngine == TurnEngine::Apple && failedSession == appleSessionId) {
            AppleTurnRecord rec;
            {
                QMutexLocker lock(&m_stateMutex);
                auto it = m_appleTurns.find(appleSessionId);
                if (it != m_appleTurns.end()) {
                    rec = it.value();
                    m_appleTurns.erase(it);
                }
            }
            appleSessionId = 0;
            turnEngine = TurnEngine::None;
            if (!rec.lastText.isEmpty()) {
                publishFinalTurn(rec.uiTurnId, rec.srcId, rec.callsign, rec.lastText);
            }
            qInfo().noquote() << "[Transcription] turn" << turnUiId << "Apple Speech ended with error";
        }

        // 3. A new handle / new over started
        if (m_newTurnPending.exchange(false, std::memory_order_acq_rel)) {
            const uint32_t splitPos = m_newTurnRingPos.load(std::memory_order_acquire);
            const uint32_t curRead = m_ringReadPos.load(std::memory_order_relaxed);
            if (turnOpen && curRead != splitPos && (splitPos - curRead) < kRingCapacity) {
                feedRange(curRead, splitPos);
            }

            int finalizeId = 0;
            QString finalizeCall;
            uint32_t finalizeSrc = 0;
            QString newCall;
            uint32_t newSrc = 0;
            int newId = 0;
            bool streamAlreadyEnded = false;
            {
                QMutexLocker lock(&m_stateMutex);
                finalizeId = m_finalizeTurnId;
                finalizeCall = m_finalizeCallsign;
                finalizeSrc = m_finalizeSrcId;
                m_finalizeTurnId = 0;
                m_finalizeCallsign.clear();
                m_finalizeSrcId = 0;
                newCall = m_currentCallsign;
                newSrc = m_currentSrcId.load(std::memory_order_relaxed);
                newId = m_currentTurnId.load(std::memory_order_acquire);
                streamAlreadyEnded = (m_lastStreamState == Mode::STREAM_IDLE ||
                                      m_lastStreamState == Mode::TRANSMITTING);
            }
            if (!streamAlreadyEnded) {
                m_turnEndPending.store(false, std::memory_order_relaxed);
            }
            const bool interruption = (finalizeId > 0);

            if (turnOpen && turnProvisional && !interruption) {
                relabelTurn(newId, newCall, newSrc);
            } else {
                if (turnOpen) {
                    const int endId = interruption ? finalizeId : turnUiId;
                    const QString endCall = (interruption && !finalizeCall.isEmpty()) ? finalizeCall : turnCallsign;
                    const uint32_t endSrc = (interruption && finalizeSrc != 0) ? finalizeSrc : turnSrcId;
                    lastEndedUiId = endId;
                    endTurn(endId, endCall, endSrc);
                }
                beginTurn(newId, newCall, newSrc, false);
            }
            m_activeTurn.store(true, std::memory_order_release);
        }

        // 4. Drain incoming 8 kHz RX samples into the current turn
        const uint32_t r = m_ringReadPos.load(std::memory_order_relaxed);
        const uint32_t w = m_ringWritePos.load(std::memory_order_acquire);
        if (r != w) {
            if (!turnOpen) {
                // Never open a new turn from late/trailing ring-buffer samples if RX stream is already idle or disconnected
                if (!m_connected.load(std::memory_order_acquire) ||
                    !m_rxStreamActive.load(std::memory_order_acquire)) {
                    m_ringReadPos.store(w, std::memory_order_release);
                } else {
                    const int activeId = m_currentTurnId.load(std::memory_order_acquire);
                    const bool reuseActiveId = m_activeTurn.load(std::memory_order_acquire) && activeId != lastEndedUiId;
                    int uiId = activeId;
                    QString call;
                    uint32_t src = 0;
                    if (reuseActiveId) {
                        QMutexLocker lock(&m_stateMutex);
                        call = m_currentCallsign;
                        src = m_currentSrcId.load(std::memory_order_relaxed);
                    } else {
                        cancelAutoClear();
                        uiId = m_currentTurnId.fetch_add(1, std::memory_order_acq_rel) + 1;
                        {
                            QMutexLocker lock(&m_stateMutex);
                            if (!m_pendingCallsign.isEmpty()) {
                                m_currentCallsign = m_pendingCallsign;
                            }
                            if (m_pendingSrcId != 0) {
                                m_currentSrcId.store(m_pendingSrcId, std::memory_order_relaxed);
                            }
                            call = m_currentCallsign;
                            src = m_currentSrcId.load(std::memory_order_relaxed);
                            m_currentTranscript.clear();
                        }
                        m_activeTurn.store(true, std::memory_order_release);
                        QMetaObject::invokeMethod(this, &TranscriptionEngine::transcriptChanged, Qt::QueuedConnection);
                    }
                    beginTurn(uiId, call, src, !reuseActiveId);
                    feedRange(r, w);
                }
            } else {
                // No STREAM_NEW followed the early audio, so this is a continuing over rather than the race
                if (turnProvisional && turnTimer.elapsed() > 1500) {
                    turnProvisional = false;
                }
                bool callsignLearned = false;
                {
                    QMutexLocker lock(&m_stateMutex);
                    if (m_currentTurnId.load(std::memory_order_relaxed) == turnUiId) {
                        if (turnCallsign.isEmpty() && !m_currentCallsign.isEmpty()) {
                            turnCallsign = m_currentCallsign;
                            callsignLearned = true;
                        }
                        if (turnSrcId == 0 && m_currentSrcId.load(std::memory_order_relaxed) != 0) {
                            turnSrcId = m_currentSrcId.load(std::memory_order_relaxed);
                        }
                    }
                }
                if (callsignLearned) {
                    refreshTurnContext();
                }
                feedRange(r, w);
            }
        }

        // 5. The over ended (stop_playback(), STREAM_IDLE, or no RX audio for kTurnSilenceTimeoutMs)
        const bool endRequested = m_turnEndPending.exchange(false, std::memory_order_acq_rel);
        const qint64 lastPushMs = m_lastSamplePushMs.load(std::memory_order_relaxed);
        const bool audioTimedOut = turnOpen && lastPushMs > 0 && (monotonicMs() - lastPushMs) > kTurnSilenceTimeoutMs;
        if (endRequested || audioTimedOut) {
            if (turnOpen) {
                const uint32_t r2 = m_ringReadPos.load(std::memory_order_relaxed);
                const uint32_t w2 = m_ringWritePos.load(std::memory_order_acquire);
                if (r2 != w2 && !m_newTurnPending.load(std::memory_order_acquire)) {
                    feedRange(r2, w2);
                }
                {
                    QMutexLocker lock(&m_stateMutex);
                    if (turnCallsign.isEmpty()) {
                        turnCallsign = !m_finalizeCallsign.isEmpty() ? m_finalizeCallsign : m_currentCallsign;
                    }
                    if (turnSrcId == 0) {
                        turnSrcId = m_finalizeSrcId != 0 ? m_finalizeSrcId : m_currentSrcId.load(std::memory_order_relaxed);
                    }
                    m_finalizeCallsign.clear();
                    m_finalizeSrcId = 0;
                }
                lastEndedUiId = turnUiId;
                if (!m_connected.load(std::memory_order_acquire)) {
                    abortTurn();
                } else {
                    endTurn(turnUiId, turnCallsign, turnSrcId);
                }
            }
        }

        // Self-healing idle invariant: whenever no turn is open and no new turn or audio is queued,
        // m_activeTurn MUST be false, and if no caption text was produced, clear the callsign badge immediately.
        if (!turnOpen &&
            !m_newTurnPending.load(std::memory_order_acquire) &&
            m_ringReadPos.load(std::memory_order_relaxed) == m_ringWritePos.load(std::memory_order_acquire) &&
            m_activeTurn.exchange(false, std::memory_order_acq_rel)) {
            bool hasCaptionText = false;
            {
                QMutexLocker lock(&m_stateMutex);
                m_pendingCallsign.clear();
                m_pendingSrcId = 0;
                hasCaptionText = !m_currentTranscript.isEmpty();
                if (!hasCaptionText) {
                    m_currentCallsign.clear();
                }
            }
            refreshIdleStatusText();
            QMetaObject::invokeMethod(this, &TranscriptionEngine::transcriptChanged, Qt::QueuedConnection);
            if (hasCaptionText) {
                scheduleAutoClear(kAutoClearDelayMs);
            }
        }

        // 6. Publish Apple overs whose final result did not arrive within the grace period
        finalizeAppleTurns(true);

        // 7. Parakeet TDT v3 / Whisper: re-transcribe the sliding window as new audio arrives
        const bool isParakeetTurn = (turnEngine == TurnEngine::Parakeet && m_parakeetCtx != nullptr);
        const bool isWhisperTurn = (turnEngine == TurnEngine::Whisper && m_whisperCtx != nullptr);
        const bool localReady = isParakeetTurn || isWhisperTurn;
        const bool isCpuHeavyWhisper = isWhisperTurn && !m_whisperUsingGpu &&
                                       (normalizeModelTier(modelType()) == QStringLiteral("turbo"));
        const size_t minLiveSamples = isCpuHeavyWhisper ? 24000 : kWhisperMinLiveSamples;
        const size_t liveStepSamples = isCpuHeavyWhisper ? 16000 : kLiveStepSamples;
        if (turnOpen && localReady &&
            window16k.size() >= minLiveSamples &&
            (window16k.size() - samplesAtLastInference) >= liveStepSamples) {

            const size_t softWindow = isParakeetTurn ? kParakeetSoftWindowSamples : kWhisperSoftWindowSamples;
            const size_t hardWindow = isParakeetTurn ? kParakeetHardWindowSamples : kWhisperHardWindowSamples;
            const size_t commitGuard = isParakeetTurn ? kParakeetCommitGuardSamples : kWhisperCommitGuardSamples;

            const size_t checkCount = std::min<size_t>(window16k.size(), 16000);
            const float *checkStart = window16k.data() + (window16k.size() - checkCount);
            double rmsSum = 0.0;
            for (size_t i = 0; i < checkCount; ++i) {
                rmsSum += static_cast<double>(checkStart[i]) * static_cast<double>(checkStart[i]);
            }
            const double rms = std::sqrt(rmsSum / static_cast<double>(checkCount));

            // Also measure the last 260 ms (4160 samples) to detect natural speech pauses between clauses
            const size_t tailCheckCount = std::min<size_t>(window16k.size(), 4160);
            const float *tailCheckStart = window16k.data() + (window16k.size() - tailCheckCount);
            double tailRmsSum = 0.0;
            for (size_t i = 0; i < tailCheckCount; ++i) {
                tailRmsSum += static_cast<double>(tailCheckStart[i]) * static_cast<double>(tailCheckStart[i]);
            }
            const double tailRms = std::sqrt(tailRmsSum / static_cast<double>(tailCheckCount));

            if (rms > 0.004) {
                const size_t inferredLen = window16k.size();
                QElapsedTimer passTimer;
                passTimer.start();
                segments.clear();
                const bool needSegments = isParakeetTurn && (inferredLen > softWindow);
                const QString rawWindowText = runWindowInference(needSegments ? &segments : nullptr, false);
                const qint64 passMs = passTimer.elapsed();
                const bool wasAborted = isWhisperTurn && m_lastWhisperAborted;

                if (!wasAborted) {
                    samplesAtLastInference = inferredLen;
                    const QString normWindow = normalizeHamTranscript(rawWindowText);
                    const QString stableWindow = stabilizeWindowTranscript(normWindow,
                                                                           prevWindowWords,
                                                                           lockedWindowWords,
                                                                           lastWindowDisplay);

                    if (!livePassLogged || passMs > 1500) {
                        livePassLogged = true;
                        qInfo().noquote() << "[Transcription] turn" << turnUiId << "live pass over"
                                          << QString::number(inferredLen / 16000.0, 'f', 1) << "s took" << passMs << "ms:"
                                          << (stableWindow.isEmpty() ? QStringLiteral("<empty>") : stableWindow);
                    }
                    if (!m_newTurnPending.load(std::memory_order_acquire)) {
                        publishLive(joinRaw(committedText, stableWindow));
                    }

                    // Slide Strategy A: If the speaker paused for >=260ms after >=3.0s of audio, commit the finished
                    // clause cleanly right inside the silent pause so the active window stays short and fast.
                    if (inferredLen >= kPauseCommitMinSamples && tailRms < 0.010 && !stableWindow.isEmpty()) {
                        committedText = normalizeHamTranscript(joinRaw(committedText, stableWindow));
                        const size_t keepSilence = std::min<size_t>(2400, window16k.size());
                        const size_t drop = window16k.size() - keepSilence;
                        window16k.erase(window16k.begin(), window16k.begin() + static_cast<std::ptrdiff_t>(drop));
                        samplesAtLastInference = window16k.size();
                        prevWindowWords.clear();
                        lockedWindowWords.clear();
                        lastWindowDisplay.clear();
                    }
                    // Slide Strategy B: Parakeet token-timestamp segment commit once window exceeds softWindow (3.6s)
                    else if (isParakeetTurn && inferredLen > softWindow && segments.size() >= 2) {
                        size_t commitEnd = 0;
                        size_t commitCount = 0;
                        for (size_t i = 0; i + 1 < segments.size(); ++i) {
                            const size_t endS = segments[i].endSample;
                            if (endS > commitEnd && endS + commitGuard <= inferredLen) {
                                commitEnd = endS;
                                commitCount = i + 1;
                            }
                        }
                        if (commitEnd > 0) {
                            QString segsText;
                            for (size_t i = 0; i < commitCount; ++i) {
                                segsText = joinRaw(segsText, segments[i].text);
                            }
                            committedText = normalizeHamTranscript(joinRaw(committedText, segsText));
                            const size_t drop = std::min(commitEnd, window16k.size());
                            window16k.erase(window16k.begin(), window16k.begin() + static_cast<std::ptrdiff_t>(drop));
                            samplesAtLastInference = (samplesAtLastInference > drop) ? (samplesAtLastInference - drop) : 0;
                            prevWindowWords.clear();
                            lockedWindowWords.clear();
                            lastWindowDisplay.clear();
                        }
                    }
                    // Slide Strategy C: Continuous non-stop speech past hardWindow (5.5s-6.0s) without a long pause.
                    // Slice at the quietest 80ms energy dip around the middle of the window and commit the locked prefix
                    // so we NEVER dump 100% of the window or lose the ongoing sentence.
                    if (window16k.size() > hardWindow) {
                        const size_t curSize = window16k.size();
                        const size_t searchMin = curSize * 40 / 100;
                        const size_t searchMax = curSize * 68 / 100;
                        const size_t cutSample = findQuietestSampleOffset(window16k, searchMin, searchMax);
                        const double cutRatio = static_cast<double>(cutSample) / static_cast<double>(curSize);

                        const QStringList allWords = lastWindowDisplay.split(QLatin1Char(' '), Qt::SkipEmptyParts);
                        int wordsToCommit = static_cast<int>(std::round(allWords.size() * cutRatio));
                        if (!lockedWindowWords.isEmpty()) {
                            wordsToCommit = std::min<int>(static_cast<int>(lockedWindowWords.size()), std::max(1, wordsToCommit));
                        } else {
                            wordsToCommit = std::clamp<int>(wordsToCommit, 1, std::max(1, static_cast<int>(allWords.size()) - 1));
                        }

                        if (!allWords.isEmpty() && wordsToCommit > 0) {
                            const QString prefixText = allWords.mid(0, wordsToCommit).join(QLatin1Char(' '));
                            committedText = normalizeHamTranscript(joinRaw(committedText, prefixText));
                            const QStringList remainingWords = allWords.mid(wordsToCommit);
                            prevWindowWords = remainingWords;
                            lockedWindowWords = remainingWords.size() > 1
                                                ? remainingWords.mid(0, remainingWords.size() - 1)
                                                : QStringList();
                            lastWindowDisplay = remainingWords.join(QLatin1Char(' '));
                        }

                        const size_t drop = std::min(cutSample, window16k.size());
                        window16k.erase(window16k.begin(), window16k.begin() + static_cast<std::ptrdiff_t>(drop));
                        samplesAtLastInference = (samplesAtLastInference > drop) ? (samplesAtLastInference - drop) : 0;
                    }
                }
            } else {
                samplesAtLastInference = window16k.size();
                if (window16k.size() >= kPauseCommitMinSamples && !lastWindowDisplay.isEmpty()) {
                    committedText = normalizeHamTranscript(joinRaw(committedText, lastWindowDisplay));
                    const size_t keepSilence = std::min<size_t>(2400, window16k.size());
                    const size_t drop = window16k.size() - keepSilence;
                    window16k.erase(window16k.begin(), window16k.begin() + static_cast<std::ptrdiff_t>(drop));
                    samplesAtLastInference = window16k.size();
                    prevWindowWords.clear();
                    lockedWindowWords.clear();
                    lastWindowDisplay.clear();
                } else if (window16k.size() > hardWindow) {
                    // Long carrier without speech: discard silence rather than re-encoding it
                    const size_t drop = window16k.size() - commitGuard;
                    window16k.erase(window16k.begin(), window16k.begin() + static_cast<std::ptrdiff_t>(drop));
                    samplesAtLastInference = window16k.size();
                }
            }
        }
    }

    abortTurn();
}
