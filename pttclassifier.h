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

// Pure C++ (no Qt) logic for hardware PTT buttons: known-device signatures, the
// press/release learner, the per-device runtime state machine and the TX arbiter.
// Everything here is deterministic and time-injected so it can be unit tested offline.

#ifndef PTTCLASSIFIER_H
#define PTTCLASSIFIER_H

#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace ptt {

using Bytes = std::vector<uint8_t>;

enum class Kind { Media, Ble, Hid };
enum class Capability { Unknown, Hold, Click };
enum class Behaviour { Auto, Hold, Toggle };      // persisted per device
enum class Effective { Hold, Toggle };
enum class MatchMode { ExactValue, AnyValue, NonZeroPressZeroRelease };
enum class AutoSwitch { Unset, Accepted, Reverted };

// What a device needs at runtime to turn raw notifications into press/release.
struct Mapping {
    std::string channel;                 // "SVC:CHR" for BLE, "key:<code>:<mods>" for HID, "media"
    MatchMode   match = MatchMode::ExactValue;
    Bytes       press;
    Bytes       release;                 // empty => click-only
    Capability  capability = Capability::Unknown;
};

struct RawEvent {
    uint64_t    tMs = 0;
    std::string channel;
    Bytes       value;
};

// ---- string helpers (persistence) ----
std::string toHex(const Bytes &b);
Bytes fromHex(const std::string &hex);
std::string upper(const std::string &s);

const char *kindToString(Kind k);
Kind kindFromString(const std::string &s);
const char *capabilityToString(Capability c);
Capability capabilityFromString(const std::string &s);
const char *behaviourToString(Behaviour b);
Behaviour behaviourFromString(const std::string &s);
const char *matchToString(MatchMode m);
MatchMode matchFromString(const std::string &s);
const char *autoSwitchToString(AutoSwitch a);
AutoSwitch autoSwitchFromString(const std::string &s);

// Click-capable only devices always toggle; explicit Hold/Toggle win; Auto follows the global mode.
Effective resolveBehaviour(Capability cap, Behaviour b, bool globalToggleTx);

// ---- known device signatures ----
struct KnownSignature {
    const char *id;            // persisted as "signature"
    const char *displayName;
    const char *namePrefix;    // advertised name prefix (case-insensitive), may be empty
    const char *service;       // 16-bit or 128-bit UUID string, upper-case, no dashes for 16-bit
    const char *chr;
    Mapping     mapping;
};

const std::vector<KnownSignature> &knownSignatures();
const KnownSignature *findSignatureById(const std::string &id);
// name: advertised/local name; services: advertised service UUIDs (any case); mfg: manufacturer data
const KnownSignature *matchKnownSignature(const std::string &name,
                                          const std::vector<std::string> &services,
                                          const Bytes &mfg);
// After service discovery: match on the actual GATT table ("SVC:CHR" channels).
const KnownSignature *matchKnownSignatureByChannels(const std::string &name,
                                                    const std::vector<std::string> &channels);

// Normalise a UUID string: upper-case, dashes kept for 128-bit, 16-bit forms reduced to 4 hex digits.
std::string normaliseUuid(const std::string &uuid);
std::string makeChannel(const std::string &serviceUuid, const std::string &charUuid);

// ---- learning ----
// Collects notifications on every candidate channel while the user presses the button
// (instruction: "press and hold for about a second, release, then once more").
class Learner {
public:
    struct Result {
        bool        ok = false;
        bool        tentative = false;   // accepted on a single cycle at timeout
        Mapping     mapping;
        int         cyclesSeen = 0;
        std::string reason;              // human readable when !ok
    };

    static constexpr uint64_t kBounceMs = 30;
    static constexpr uint64_t kCycleGapMs = 400;
    static constexpr uint64_t kHoldDwellMs = 250;
    static constexpr uint64_t kClickDwellMaxMs = 150;
    static constexpr size_t   kMaxEventsPerChannel = 40;
    static constexpr size_t   kMaxDistinctValues = 10;
    static constexpr int      kCyclesWanted = 2;

    void start(uint64_t nowMs);
    void feed(const RawEvent &ev);
    bool finished() const;               // best channel explains kCyclesWanted cycles
    Result finish(uint64_t nowMs) const; // on completion or timeout
    bool active() const { return m_active; }
    int bestCycles() const;
    size_t eventCount() const;
    std::string progressText() const;
    std::string lastValueHex() const { return m_lastHex; }

private:
    struct Channel {
        std::vector<std::pair<uint64_t, Bytes>> events;
        bool rejected = false;
    };
    struct Analysis {
        bool ok = false;
        int cycles = 0;
        Capability cap = Capability::Unknown;
        MatchMode match = MatchMode::ExactValue;
        Bytes press, release;
        std::string reason;
    };
    Analysis analyse(const Channel &ch) const;
    Analysis analyseEvents(const std::vector<std::pair<uint64_t, Bytes>> &events) const;
    const std::string *bestChannel(Analysis *out) const;

    bool m_active = false;
    uint64_t m_startMs = 0;
    std::map<std::string, Channel> m_channels;
    std::string m_lastHex;
};

// ---- per-device runtime ----
class DeviceRuntime {
public:
    enum class Action { None, Pressed, Released, Clicked, RekeyPressed };

    static constexpr uint64_t kClickDebounceMs = 250;
    static constexpr uint64_t kDuplicatePressMs = 300;

    Action feed(const RawEvent &ev, const Mapping &m, Effective eff, uint64_t nowMs);
    Action forceRelease();
    bool pressed() const { return m_pressed; }
    void reset() { m_pressed = false; m_lastEdgeMs = 0; m_lastClickMs = 0; }

private:
    bool m_pressed = false;
    uint64_t m_lastEdgeMs = 0;
    uint64_t m_lastClickMs = 0;
};

// ---- TX arbitration across devices ----
class Arbiter {
public:
    bool press(const std::string &id);            // true on 0 -> 1 edge
    bool release(const std::string &id);          // true on 1 -> 0 edge
    std::vector<std::string> releaseAll();        // ids that were pressed
    bool anyPressed() const { return !m_pressed.empty(); }
    bool isPressed(const std::string &id) const { return m_pressed.count(id) != 0; }
    size_t count() const { return m_pressed.size(); }

private:
    std::set<std::string> m_pressed;
};

} // namespace ptt

#endif // PTTCLASSIFIER_H
