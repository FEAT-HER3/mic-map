// driver/src/sample_ring.hpp
//
// Phase 7 / D-01..D-04 / Pitfall 12: header-only SPSC bounded ring with
// drop-OLDEST overflow. Single-producer = WASAPI capture callback; single-
// consumer = DetectionRunner thread. Audio thread NEVER blocks (Pitfall 12
// hard rule). Power-of-two slot count for branch-free wrap. Acquire/release
// fences make the ring lock-free for both directions.
//
// SVR-05 / D-22 enforcement: ZERO OpenVR API surface in this translation
// unit. ZERO OpenVR header includes. Lint script
// `cmake/AssertDetectionRunnerNoVrApi.cmake` scans this file on every build.

#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace micmap::driver {

template<size_t kSlots, size_t kFrames>
class SampleRing {
    static_assert((kSlots & (kSlots - 1)) == 0, "kSlots must be power-of-two");
public:
    static constexpr size_t kMask = kSlots - 1;

    /// try_push — Producer (audio cb). Drop-OLDEST on full. Returns true if a
    /// slot was dropped. NEVER blocks (Pitfall 12 hard rule for the audio
    /// thread).
    bool try_push(const float* samples, size_t count) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        const bool full  = (head - tail) == kSlots;
        if (full) {
            // Drop-OLDEST atomicity: producer bumps tail BEFORE writing the
            // new slot. SPSC invariant means consumer never observes tail
            // going backwards (only forwards), and any consumer in flight
            // that read the now-overwritten slot already cleared it with
            // its own tail bump. Pattern from rigtorp/SPSCQueue.
            tail_.fetch_add(1, std::memory_order_release);
            drops_.fetch_add(1, std::memory_order_relaxed);
        }
        auto& slot = slots_[head & kMask];
        const size_t n = (count < kFrames) ? count : kFrames;
        for (size_t i = 0; i < n; ++i) slot[i] = samples[i];
        slot_count_[head & kMask] = n;
        head_.store(head + 1, std::memory_order_release);
        return full;
    }

    /// try_pop — Consumer (detection thread). Returns false if ring is empty;
    /// otherwise fills `out` with the next slot's samples and `out_count` with
    /// the number of valid frames in that slot, then advances tail.
    bool try_pop(std::array<float, kFrames>& out, size_t& out_count) noexcept {
        const size_t tail = tail_.load(std::memory_order_relaxed);
        const size_t head = head_.load(std::memory_order_acquire);
        if (head == tail) return false;
        out = slots_[tail & kMask];
        out_count = slot_count_[tail & kMask];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    /// has_data — Returns true iff head_ != tail_ (cheap two-acquire-load
    /// peek used by the DetectionRunner CV-wait predicate, D-04).
    bool has_data() const noexcept {
        return head_.load(std::memory_order_acquire)
             != tail_.load(std::memory_order_acquire);
    }

    /// drops — Returns the relaxed-load count of dropped slots since
    /// construction (monotonic; D-03 overflow telemetry).
    uint32_t drops() const noexcept { return drops_.load(std::memory_order_relaxed); }

private:
    alignas(64) std::atomic<size_t> head_{0};      // producer-only writer
    alignas(64) std::atomic<size_t> tail_{0};      // consumer writer; producer also bumps on drop
    alignas(64) std::atomic<uint32_t> drops_{0};
    std::array<std::array<float, kFrames>, kSlots> slots_{};
    std::array<size_t, kSlots> slot_count_{};
};

} // namespace micmap::driver
