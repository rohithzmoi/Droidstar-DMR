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

#include "pttclassifier.h"

#include <algorithm>
#include <cctype>

namespace ptt {

// ---------------------------------------------------------------- helpers

std::string toHex(const Bytes &b)
{
    static const char *digits = "0123456789ABCDEF";
    std::string out;
    out.reserve(b.size() * 2);
    for (uint8_t v : b) {
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 0x0F]);
    }
    return out;
}

Bytes fromHex(const std::string &hex)
{
    Bytes out;
    std::string clean;
    for (char c : hex) {
        if (std::isxdigit(static_cast<unsigned char>(c))) clean.push_back(c);
    }
    if (clean.size() % 2) clean.insert(clean.begin(), '0');
    out.reserve(clean.size() / 2);
    for (size_t i = 0; i + 1 < clean.size(); i += 2) {
        auto nib = [](char c) -> uint8_t {
            if (c >= '0' && c <= '9') return static_cast<uint8_t>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<uint8_t>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<uint8_t>(c - 'A' + 10);
            return 0;
        };
        out.push_back(static_cast<uint8_t>((nib(clean[i]) << 4) | nib(clean[i + 1])));
    }
    return out;
}

std::string upper(const std::string &s)
{
    std::string out = s;
    for (char &c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
}

const char *kindToString(Kind k)
{
    switch (k) {
    case Kind::Media: return "media";
    case Kind::Ble: return "ble";
    case Kind::Hid: return "hid";
    }
    return "ble";
}

Kind kindFromString(const std::string &s)
{
    if (s == "media") return Kind::Media;
    if (s == "hid") return Kind::Hid;
    return Kind::Ble;
}

const char *capabilityToString(Capability c)
{
    switch (c) {
    case Capability::Hold: return "hold";
    case Capability::Click: return "click";
    case Capability::Unknown: break;
    }
    return "unknown";
}

Capability capabilityFromString(const std::string &s)
{
    if (s == "hold") return Capability::Hold;
    if (s == "click") return Capability::Click;
    return Capability::Unknown;
}

const char *behaviourToString(Behaviour b)
{
    switch (b) {
    case Behaviour::Hold: return "hold";
    case Behaviour::Toggle: return "toggle";
    case Behaviour::Auto: break;
    }
    return "auto";
}

Behaviour behaviourFromString(const std::string &s)
{
    if (s == "hold") return Behaviour::Hold;
    if (s == "toggle") return Behaviour::Toggle;
    return Behaviour::Auto;
}

const char *matchToString(MatchMode m)
{
    switch (m) {
    case MatchMode::AnyValue: return "any";
    case MatchMode::NonZeroPressZeroRelease: return "nonzero";
    case MatchMode::ExactValue: break;
    }
    return "exact";
}

MatchMode matchFromString(const std::string &s)
{
    if (s == "any") return MatchMode::AnyValue;
    if (s == "nonzero") return MatchMode::NonZeroPressZeroRelease;
    return MatchMode::ExactValue;
}

const char *autoSwitchToString(AutoSwitch a)
{
    switch (a) {
    case AutoSwitch::Accepted: return "accepted";
    case AutoSwitch::Reverted: return "reverted";
    case AutoSwitch::Unset: break;
    }
    return "unset";
}

AutoSwitch autoSwitchFromString(const std::string &s)
{
    if (s == "accepted") return AutoSwitch::Accepted;
    if (s == "reverted") return AutoSwitch::Reverted;
    return AutoSwitch::Unset;
}

Effective resolveBehaviour(Capability cap, Behaviour b, bool globalToggleTx)
{
    if (cap != Capability::Hold) return Effective::Toggle;   // click-only (or unknown) can only toggle
    if (b == Behaviour::Hold) return Effective::Hold;
    if (b == Behaviour::Toggle) return Effective::Toggle;
    return globalToggleTx ? Effective::Toggle : Effective::Hold;
}

std::string normaliseUuid(const std::string &uuid)
{
    std::string u = upper(uuid);
    // CoreBluetooth prints 16-bit UUIDs as "FFE0"; some stacks hand us the full base form.
    static const std::string baseSuffix = "-0000-1000-8000-00805F9B34FB";
    if (u.size() == 36 && u.compare(8, baseSuffix.size(), baseSuffix) == 0 && u.compare(0, 4, "0000") == 0) {
        return u.substr(4, 4);
    }
    return u;
}

std::string makeChannel(const std::string &serviceUuid, const std::string &charUuid)
{
    return normaliseUuid(serviceUuid) + ":" + normaliseUuid(charUuid);
}

// ---------------------------------------------------------------- known signatures

const std::vector<KnownSignature> &knownSignatures()
{
    static const std::vector<KnownSignature> table = [] {
        std::vector<KnownSignature> v;
        {
            // AnyTone BP-02 (ELET-PTT…): notifications "ELET1" on press, "ELET2" on release.
            Mapping m;
            m.channel = "FF01:FF02";
            m.match = MatchMode::ExactValue;
            m.press = {0x45, 0x4C, 0x45, 0x54, 0x31};
            m.release = {0x45, 0x4C, 0x45, 0x54, 0x32};
            m.capability = Capability::Hold;
            v.push_back({"anytone-elet", "AnyTone BP-02", "ELET-PTT", "FF01", "FF02", m});
        }
        {
            // Zello-class buttons (Pryme BT-PTT-Z, Focket PTT-Z01, HM-10 family): 0x01 press, 0x00 release,
            // bitmask variants use 0x04 for a secondary PTT key.
            Mapping m;
            m.channel = "FFE0:FFE1";
            m.match = MatchMode::NonZeroPressZeroRelease;
            m.press = {0x01};
            m.release = {0x00};
            m.capability = Capability::Hold;
            v.push_back({"zello-ffe1", "Zello-class PTT button", "PTT", "FFE0", "FFE1", m});
        }
        return v;
    }();
    return table;
}

const KnownSignature *findSignatureById(const std::string &id)
{
    for (const auto &s : knownSignatures()) {
        if (id == s.id) return &s;
    }
    return nullptr;
}

static bool startsWithCi(const std::string &s, const std::string &prefix)
{
    if (prefix.empty() || s.size() < prefix.size()) return prefix.empty() && !s.empty();
    return upper(s.substr(0, prefix.size())) == upper(prefix);
}

const KnownSignature *matchKnownSignature(const std::string &name,
                                          const std::vector<std::string> &services,
                                          const Bytes &mfg)
{
    std::vector<std::string> norm;
    norm.reserve(services.size());
    for (const auto &s : services) norm.push_back(normaliseUuid(s));

    for (const auto &sig : knownSignatures()) {
        const bool svcHit = std::find(norm.begin(), norm.end(), std::string(sig.service)) != norm.end();
        const bool nameHit = sig.namePrefix[0] != '\0' && startsWithCi(name, sig.namePrefix);
        // AnyTone units carry manufacturer key 0x5045 ("PE") little-endian.
        const bool mfgHit = (std::string(sig.id) == "anytone-elet") && mfg.size() >= 2 && mfg[0] == 0x45 && mfg[1] == 0x50;
        if (svcHit || nameHit || mfgHit) return &sig;
    }
    return nullptr;
}

const KnownSignature *matchKnownSignatureByChannels(const std::string &name,
                                                    const std::vector<std::string> &channels)
{
    std::vector<std::string> norm;
    norm.reserve(channels.size());
    for (const auto &c : channels) norm.push_back(upper(c));
    for (const auto &sig : knownSignatures()) {
        if (std::find(norm.begin(), norm.end(), sig.mapping.channel) != norm.end()) return &sig;
    }
    // Name alone is not enough once we can see the GATT table; the generic learner takes over.
    (void)name;
    return nullptr;
}

// ---------------------------------------------------------------- Learner

void Learner::start(uint64_t nowMs)
{
    m_active = true;
    m_startMs = nowMs;
    m_channels.clear();
    m_lastHex.clear();
}

void Learner::feed(const RawEvent &ev)
{
    if (!m_active) return;
    Channel &ch = m_channels[ev.channel];
    if (ch.rejected) return;
    m_lastHex = toHex(ev.value);
    if (!ch.events.empty()) {
        const auto &last = ch.events.back();
        if (last.second == ev.value && ev.tMs >= last.first && ev.tMs - last.first < kBounceMs) {
            return; // contact bounce / duplicate notification
        }
    }
    ch.events.emplace_back(ev.tMs, ev.value);
    if (ch.events.size() > kMaxEventsPerChannel) {
        ch.rejected = true; // streaming sensor, not a button
        return;
    }
    std::set<Bytes> distinct;
    for (const auto &e : ch.events) {
        distinct.insert(e.second);
        if (distinct.size() > kMaxDistinctValues) {
            ch.rejected = true;
            return;
        }
    }
}

static bool allZero(const Bytes &b)
{
    for (uint8_t v : b) if (v) return false;
    return true;
}

Learner::Analysis Learner::analyse(const Channel &ch) const
{
    Analysis a;
    if (ch.rejected) { a.reason = "too many values"; return a; }
    if (ch.events.empty()) { a.reason = "no data"; return a; }
    return analyseEvents(ch.events);
}

Learner::Analysis Learner::analyseEvents(const std::vector<std::pair<uint64_t, Bytes>> &ev) const
{
    Analysis a;
    if (ev.empty()) { a.reason = "no data"; return a; }

    std::vector<Bytes> distinct;
    for (const auto &e : ev) {
        if (std::find(distinct.begin(), distinct.end(), e.second) == distinct.end()) distinct.push_back(e.second);
    }

    if (distinct.size() == 1) {
        // One value per press: click-only. Count clusters separated by the cycle gap.
        int cycles = 1;
        for (size_t i = 1; i < ev.size(); ++i) {
            if (ev[i].first - ev[i - 1].first > kCycleGapMs) ++cycles;
        }
        a.ok = true;
        a.cycles = cycles;
        a.cap = Capability::Click;
        a.match = MatchMode::ExactValue;
        a.press = distinct[0];
        return a;
    }

    if (distinct.size() == 2) {
        // Two values: press/release pair. If one of them is all-zero it is the release state
        // (a value read right after subscribing is the idle state, not a press).
        Bytes press = distinct[0];
        Bytes release = distinct[1];
        if (allZero(press) && !allZero(release)) std::swap(press, release);
        size_t start = 0;
        while (start < ev.size() && ev[start].second != press) {
            if (ev[start].second != release) { a.reason = "values do not alternate"; return a; }
            ++start;
        }
        if (start >= ev.size()) { a.reason = "waiting for press"; return a; }
        for (size_t i = start; i < ev.size(); ++i) {
            const Bytes &want = ((i - start) % 2 == 0) ? press : release;
            if (ev[i].second != want) { a.reason = "values do not alternate"; return a; }
        }
        std::vector<uint64_t> dwell;
        for (size_t i = start; i + 1 < ev.size(); i += 2) dwell.push_back(ev[i + 1].first - ev[i].first);
        if (dwell.empty()) { a.reason = "waiting for release"; return a; }
        std::vector<uint64_t> sorted = dwell;
        std::sort(sorted.begin(), sorted.end());
        const uint64_t median = sorted[sorted.size() / 2];
        const uint64_t maxDwell = sorted.back();
        a.cycles = static_cast<int>(dwell.size());
        a.press = press;
        if (median >= kHoldDwellMs) {
            a.ok = true;
            a.cap = Capability::Hold;
            a.match = MatchMode::ExactValue;
            a.release = release;
            return a;
        }
        if (maxDwell < kClickDwellMaxMs) {
            // Firmware emits down+up back to back: treat the press value as the click, ignore the other.
            a.ok = true;
            a.cap = Capability::Click;
            a.match = MatchMode::ExactValue;
            return a;
        }
        a.reason = "hold the button longer";
        return a;
    }

    // More than two values. Zello bitmask style first: non-zero (which key) alternating with all-zero,
    // optionally preceded by an idle all-zero read.
    size_t zeroCount = 0;
    for (const auto &d : distinct) if (allZero(d)) ++zeroCount;
    if (zeroCount == 1) {
        size_t start = 0;
        while (start < ev.size() && allZero(ev[start].second)) ++start;
        bool alternates = start < ev.size();
        for (size_t i = start; i < ev.size() && alternates; ++i) {
            const bool wantZero = ((i - start) % 2 == 1);
            if (allZero(ev[i].second) != wantZero) alternates = false;
        }
        if (alternates) {
            int cycles = 0;
            for (size_t i = start; i + 1 < ev.size(); i += 2) ++cycles;
            if (cycles == 0) { a.reason = "waiting for release"; return a; }
            a.ok = true;
            a.cycles = cycles;
            a.cap = Capability::Hold;
            a.match = MatchMode::NonZeroPressZeroRelease;
            a.press = ev[start].second;
            a.release = Bytes(ev[start].second.size(), 0);
            return a;
        }
    }

    // Some buttons report a one-off status value (battery, "hello") right after subscribing:
    // strip leading values that never recur and look at the rest.
    for (size_t strip = 1; strip < ev.size(); ++strip) {
        int occurrences = 0;
        for (const auto &e : ev) if (e.second == ev[strip - 1].second) ++occurrences;
        if (occurrences != 1) break;
        std::vector<std::pair<uint64_t, Bytes>> rest(ev.begin() + static_cast<long>(strip), ev.end());
        Analysis inner = analyseEvents(rest);
        if (inner.ok) return inner;
    }

    a.reason = "unrecognised pattern";
    return a;
}

const std::string *Learner::bestChannel(Analysis *out) const
{
    const std::string *best = nullptr;
    Analysis bestA;
    auto score = [](const std::string &chan, const Analysis &an, size_t events) {
        long s = an.cycles * 1000;
        if (chan == "FF01:FF02" || chan == "FFE0:FFE1") s += 500;
        s -= static_cast<long>(events);
        return s;
    };
    for (const auto &kv : m_channels) {
        Analysis an = analyse(kv.second);
        if (!an.ok || an.cycles < 1) continue;
        if (!best || score(kv.first, an, kv.second.events.size()) > score(*best, bestA, m_channels.at(*best).events.size())) {
            best = &kv.first;
            bestA = an;
        }
    }
    if (out) *out = bestA;
    return best;
}

bool Learner::finished() const
{
    if (!m_active) return false;
    Analysis a;
    const std::string *best = bestChannel(&a);
    return best && a.cycles >= kCyclesWanted;
}

int Learner::bestCycles() const
{
    Analysis a;
    return bestChannel(&a) ? a.cycles : 0;
}

size_t Learner::eventCount() const
{
    size_t n = 0;
    for (const auto &kv : m_channels) n += kv.second.events.size();
    return n;
}

std::string Learner::progressText() const
{
    const int c = bestCycles();
    if (eventCount() == 0) return "Waiting for the button";
    if (c <= 0) return "Data received, keep going";
    if (c < kCyclesWanted) return "Press " + std::to_string(c) + " of " + std::to_string(kCyclesWanted) + " seen, press again";
    return "Done";
}

Learner::Result Learner::finish(uint64_t nowMs) const
{
    (void)nowMs;
    Result r;
    Analysis a;
    const std::string *best = bestChannel(&a);
    if (!best) {
        r.ok = false;
        if (eventCount() == 0) {
            r.reason = "No button data received. This button may use a protocol iOS apps can't read (HID).";
        } else {
            // Surface the most useful per-channel reason.
            std::string why;
            for (const auto &kv : m_channels) {
                Analysis an = analyse(kv.second);
                if (!an.reason.empty()) { why = an.reason; break; }
            }
            r.reason = why.empty() ? "Could not understand the button's signals." : ("Could not understand the button's signals (" + why + ").");
        }
        return r;
    }
    r.ok = true;
    r.cyclesSeen = a.cycles;
    r.tentative = a.cycles < kCyclesWanted;
    r.mapping.channel = *best;
    r.mapping.match = a.match;
    r.mapping.press = a.press;
    r.mapping.release = a.release;
    r.mapping.capability = a.cap;
    return r;
}

// ---------------------------------------------------------------- DeviceRuntime

DeviceRuntime::Action DeviceRuntime::feed(const RawEvent &ev, const Mapping &m, Effective eff, uint64_t nowMs)
{
    if (ev.channel != m.channel) return Action::None;

    bool isPress = false;
    bool isRelease = false;
    switch (m.match) {
    case MatchMode::ExactValue:
        isPress = (ev.value == m.press);
        isRelease = !m.release.empty() && (ev.value == m.release);
        break;
    case MatchMode::AnyValue:
        isPress = true;
        break;
    case MatchMode::NonZeroPressZeroRelease:
        isPress = !allZero(ev.value);
        isRelease = allZero(ev.value);
        break;
    }

    if (m.capability != Capability::Hold || eff == Effective::Toggle) {
        if (!isPress) return Action::None;
        if (m_lastClickMs && nowMs >= m_lastClickMs && nowMs - m_lastClickMs < kClickDebounceMs) return Action::None;
        m_lastClickMs = nowMs;
        return Action::Clicked;
    }

    if (isPress) {
        if (m_pressed) {
            if (nowMs >= m_lastEdgeMs && nowMs - m_lastEdgeMs < kDuplicatePressMs) return Action::None; // duplicate
            m_lastEdgeMs = nowMs;
            return Action::RekeyPressed;   // release was lost
        }
        m_pressed = true;
        m_lastEdgeMs = nowMs;
        return Action::Pressed;
    }
    if (isRelease) {
        if (!m_pressed) return Action::None;
        m_pressed = false;
        m_lastEdgeMs = nowMs;
        return Action::Released;
    }
    return Action::None;
}

DeviceRuntime::Action DeviceRuntime::forceRelease()
{
    if (!m_pressed) return Action::None;
    m_pressed = false;
    return Action::Released;
}

// ---------------------------------------------------------------- Arbiter

bool Arbiter::press(const std::string &id)
{
    const bool wasEmpty = m_pressed.empty();
    m_pressed.insert(id);
    return wasEmpty;
}

bool Arbiter::release(const std::string &id)
{
    if (!m_pressed.erase(id)) return false;
    return m_pressed.empty();
}

std::vector<std::string> Arbiter::releaseAll()
{
    std::vector<std::string> ids(m_pressed.begin(), m_pressed.end());
    m_pressed.clear();
    return ids;
}

} // namespace ptt
