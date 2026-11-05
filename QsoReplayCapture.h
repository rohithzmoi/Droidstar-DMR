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

#ifndef QSOREPLAYCAPTURE_H
#define QSOREPLAYCAPTURE_H

// QSO Replay hot-path capture.
//
// This header is the ONLY thing the live audio path (the Mode thread's 20 ms RX decode timer and
// the 19 ms TX timer) touches. Everything here is inline, allocation-free and lock-free: relaxed /
// acquire-release atomics, a single-producer single-consumer sample ring and a small event ring.
// There are deliberately no Qt types so the file compiles in a plain clang++ unit test and so a
// queued Qt signal (which allocates) can never sneak onto the audio thread.
//
// Producer (mode thread): beginOver() / pushSamples() / endOver().
// Consumer (QsoReplayEngine worker thread): drainSamples() / popEvent().
// Control flags (main thread): setEnabled() / setRecordTx() / setSelfLoggingAllowed().

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace QsoReplayCapture {

enum class Kind : uint8_t { Rx = 0, Tx = 1 };
enum class Ev : uint8_t { None = 0, Begin = 1, End = 2 };

struct Event {
    uint32_t ringPos { 0 };   // sample ring write position when the event was posted
    int64_t monoMs { 0 };     // steady clock, milliseconds
    Ev type { Ev::None };
    Kind kind { Kind::Rx };
};

// 65536 samples = 8.19 s of 8 kHz audio = 128 KB. The worker drains every ~32-100 ms, so the ring
// only fills if the worker is starved for seconds; frames are then dropped and counted.
constexpr uint32_t kRingCapacity = 65536U;
constexpr uint32_t kRingMask = kRingCapacity - 1U;
constexpr uint32_t kEventCapacity = 64U;
constexpr uint32_t kEventMask = kEventCapacity - 1U;
constexpr uint32_t kWakeEverySamples = 256U;   // one wake per 32 ms of audio

typedef void (*WakeFn)(void *ctx);

struct State {
    std::array<int16_t, kRingCapacity> ring {};
    std::atomic<uint32_t> writePos { 0 };
    std::atomic<uint32_t> readPos { 0 };

    struct Slot {
        std::atomic<uint32_t> seq { 0 };   // == claim index + 1 once the slot is fully written
        Event ev;
    };
    std::array<Slot, kEventCapacity> events {};
    std::atomic<uint32_t> evWrite { 0 };
    std::atomic<uint32_t> evRead { 0 };

    std::atomic<bool> enabled { false };
    std::atomic<bool> recordTx { false };
    std::atomic<bool> selfAllowed { false };
    std::atomic<bool> rxOpen { false };
    std::atomic<bool> txOpen { false };

    std::atomic<uint32_t> droppedSamples { 0 };
    std::atomic<uint32_t> droppedEvents { 0 };
    std::atomic<uint32_t> wakeCounter { 0 };

    std::atomic<WakeFn> wakeFn { nullptr };
    std::atomic<void *> wakeCtx { nullptr };
};

inline State &state()
{
    static State s;
    return s;
}

inline int64_t monoNowMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline void wake()
{
    State &s = state();
    WakeFn fn = s.wakeFn.load(std::memory_order_acquire);
    if (fn != nullptr) {
        fn(s.wakeCtx.load(std::memory_order_acquire));
    }
}

// ── control (main thread) ────────────────────────────────────────────────────────────────────────

inline void setWake(WakeFn fn, void *ctx)
{
    State &s = state();
    s.wakeCtx.store(ctx, std::memory_order_release);
    s.wakeFn.store(fn, std::memory_order_release);
}

inline void setEnabled(bool on) { state().enabled.store(on, std::memory_order_release); }
inline bool isEnabled() { return state().enabled.load(std::memory_order_relaxed); }
inline void setRecordTx(bool on) { state().recordTx.store(on, std::memory_order_release); }
inline void setSelfLoggingAllowed(bool on) { state().selfAllowed.store(on, std::memory_order_release); }

// ── producer (mode thread) ───────────────────────────────────────────────────────────────────────

inline bool rxTapEnabled()
{
    const State &s = state();
    return s.enabled.load(std::memory_order_relaxed) && s.rxOpen.load(std::memory_order_relaxed);
}

inline bool txTapEnabled()
{
    const State &s = state();
    return s.enabled.load(std::memory_order_relaxed) && s.txOpen.load(std::memory_order_relaxed) &&
           s.recordTx.load(std::memory_order_relaxed) && s.selfAllowed.load(std::memory_order_relaxed);
}

// Posts an event into the slot ring. Safe for occasional concurrent producers (CAS claim), never
// blocks; a full ring (64 pending, only possible with a dead consumer) drops the event and counts it.
inline void postEvent(Ev type, Kind kind)
{
    State &s = state();
    uint32_t w = s.evWrite.load(std::memory_order_relaxed);
    for (;;) {
        const uint32_t r = s.evRead.load(std::memory_order_acquire);
        if (w - r >= kEventCapacity) {
            s.droppedEvents.fetch_add(1U, std::memory_order_relaxed);
            return;
        }
        if (s.evWrite.compare_exchange_weak(w, w + 1U, std::memory_order_acq_rel, std::memory_order_relaxed)) {
            break;
        }
    }
    State::Slot &slot = s.events[w & kEventMask];
    slot.ev.ringPos = s.writePos.load(std::memory_order_acquire);
    slot.ev.monoMs = monoNowMs();
    slot.ev.type = type;
    slot.ev.kind = kind;
    slot.seq.store(w + 1U, std::memory_order_release);
    wake();
}

// Idempotent: a second Begin of the same kind while it is already open (sink re-init after a route
// change calls start_playback() again) is ignored, so one over stays one clip.
inline void beginOver(Kind kind)
{
    State &s = state();
    std::atomic<bool> &flag = (kind == Kind::Rx) ? s.rxOpen : s.txOpen;
    if (flag.exchange(true, std::memory_order_acq_rel)) {
        return;
    }
    if (s.enabled.load(std::memory_order_relaxed)) {
        postEvent(Ev::Begin, kind);
    }
}

inline void endOver(Kind kind)
{
    State &s = state();
    std::atomic<bool> &flag = (kind == Kind::Rx) ? s.rxOpen : s.txOpen;
    if (!flag.exchange(false, std::memory_order_acq_rel)) {
        return;
    }
    if (s.enabled.load(std::memory_order_relaxed)) {
        postEvent(Ev::End, kind);
    }
}

// Copies n 8 kHz mono int16 samples into the ring. Drops the whole frame when it does not fit.
inline void pushSamples(const int16_t *pcm, size_t n)
{
    if (pcm == nullptr || n == 0 || n > kRingCapacity) {
        return;
    }
    State &s = state();
    const uint32_t w = s.writePos.load(std::memory_order_relaxed);
    const uint32_t r = s.readPos.load(std::memory_order_acquire);
    const uint32_t used = w - r;
    const uint32_t count = static_cast<uint32_t>(n);
    if (kRingCapacity - used < count) {
        s.droppedSamples.fetch_add(count, std::memory_order_relaxed);
        return;
    }
    const uint32_t idx = w & kRingMask;
    const uint32_t first = (kRingCapacity - idx < count) ? (kRingCapacity - idx) : count;
    std::memcpy(&s.ring[idx], pcm, first * sizeof(int16_t));
    if (first < count) {
        std::memcpy(&s.ring[0], pcm + first, (count - first) * sizeof(int16_t));
    }
    s.writePos.store(w + count, std::memory_order_release);

    const uint32_t acc = s.wakeCounter.fetch_add(count, std::memory_order_relaxed) + count;
    if (acc >= kWakeEverySamples) {
        s.wakeCounter.store(0U, std::memory_order_relaxed);
        wake();
    }
}

// ── consumer (worker thread) ─────────────────────────────────────────────────────────────────────

inline bool popEvent(Event &out)
{
    State &s = state();
    const uint32_t r = s.evRead.load(std::memory_order_relaxed);
    State::Slot &slot = s.events[r & kEventMask];
    if (slot.seq.load(std::memory_order_acquire) != r + 1U) {
        return false;   // nothing published yet (or a claimed slot still being written)
    }
    out = slot.ev;
    s.evRead.store(r + 1U, std::memory_order_release);
    return true;
}

inline uint32_t ringWritePos() { return state().writePos.load(std::memory_order_acquire); }
inline uint32_t ringReadPos() { return state().readPos.load(std::memory_order_relaxed); }
inline uint32_t availableSamples() { return ringWritePos() - ringReadPos(); }

// Copies up to maxCount samples starting at the current read position without consuming them.
inline uint32_t peekSamples(int16_t *dst, uint32_t maxCount)
{
    State &s = state();
    const uint32_t r = s.readPos.load(std::memory_order_relaxed);
    const uint32_t w = s.writePos.load(std::memory_order_acquire);
    uint32_t count = w - r;
    if (count > maxCount) {
        count = maxCount;
    }
    if (count == 0) {
        return 0;
    }
    const uint32_t idx = r & kRingMask;
    const uint32_t first = (kRingCapacity - idx < count) ? (kRingCapacity - idx) : count;
    std::memcpy(dst, &s.ring[idx], first * sizeof(int16_t));
    if (first < count) {
        std::memcpy(dst + first, &s.ring[0], (count - first) * sizeof(int16_t));
    }
    return count;
}

inline void consumeSamples(uint32_t count)
{
    State &s = state();
    s.readPos.store(s.readPos.load(std::memory_order_relaxed) + count, std::memory_order_release);
}

inline uint32_t takeDroppedSamples() { return state().droppedSamples.exchange(0U, std::memory_order_relaxed); }
inline uint32_t takeDroppedEvents() { return state().droppedEvents.exchange(0U, std::memory_order_relaxed); }

// Test / shutdown helper: resets positions and flags. Never call while a producer is active.
inline void resetForTests()
{
    State &s = state();
    s.writePos.store(0U);
    s.readPos.store(0U);
    s.evWrite.store(0U);
    s.evRead.store(0U);
    for (auto &slot : s.events) {
        slot.seq.store(0U);
    }
    s.enabled.store(false);
    s.recordTx.store(false);
    s.selfAllowed.store(false);
    s.rxOpen.store(false);
    s.txOpen.store(false);
    s.droppedSamples.store(0U);
    s.droppedEvents.store(0U);
    s.wakeCounter.store(0U);
}

} // namespace QsoReplayCapture

#endif // QSOREPLAYCAPTURE_H
