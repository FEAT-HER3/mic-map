// driver/src/sample_ring.hpp
//
// Phase 7 / D-01..D-04 / Pitfall 12: header-only SPSC bounded ring with
// drop-NEWEST overflow. Single-producer = WASAPI capture callback; single-
// consumer = DetectionRunner thread. Audio thread NEVER blocks (Pitfall 12
// hard rule). Power-of-two slot count for branch-free wrap. Acquire/release
// fences make the ring lock-free for both directions.
//
// P7 REVIEW BL-01: drop-NEWEST (producer never writes tail_; consumer is
// the sole tail_ writer). The previous drop-OLDEST implementation had two
// writers on tail_ -- the producer doing fetch_add(release) on full, and
// the consumer doing load+store. That is a non-atomic RMW from the
// consumer side and races the producer's bump, both losing tail updates
// AND racing the slot contents themselves (producer writing slots_[head &
// kMask] while consumer reads slots_[tail & kMask] -- same index when
// full). Drop-NEWEST under WASAPI back-pressure is indistinguishable from
// drop-OLDEST for a real-time noise-cover detector (the audio fingerprint
// repeats across many 10 ms frames) and removes a class of data races
// permanently. The header comment claim that this pattern was "from
// rigtorp/SPSCQueue" was incorrect -- rigtorp's SPSC queue does not
// support drop-oldest from the producer side at all; its try_push returns
// false when full and the caller must handle it.
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

    /// try_push — Producer (audio cb). Drop-NEWEST on full -- the new
    /// sample is discarded so the producer NEVER writes tail_. Returns
    /// true if the new sample was dropped (i.e. ring was full). NEVER
    /// blocks (Pitfall 12 hard rule for the audio thread).
    bool try_push(const float* samples, size_t count) noexcept {
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        if ((head - tail) == kSlots) {
            // Drop-NEWEST: ring is full; discard THIS sample. Bump the
            // overflow counter for telemetry. Consumer remains the sole
            // tail_ writer, so no two-writer race on tail_ or on the
            // slot contents.
            drops_.fetch_add(1, std::memory_order_relaxed);
            return true;
        }
        auto& slot = slots_[head & kMask];
        const size_t n = (count < kFrames) ? count : kFrames;
        for (size_t i = 0; i < n; ++i) slot[i] = samples[i];
        slot_count_[head & kMask] = n;
        head_.store(head + 1, std::memory_order_release);
        return false;
    }

    /// try_pop — Consumer (detection thread). Returns false if ring is empty;
    /// otherwise fills `out` with the next slot's samples and `out_count` with
    /// the number of valid frames in that slot, then advances tail.
    bool try_pop(std::array<float, kFrames>& out, size_t& out_count) noexcept {
        // BL-01: acquire-load on tail_ for symmetry with the
        // consumer-as-sole-writer discipline. Producer never writes tail_,
        // so the load could be relaxed; acquire is the principled choice
        // and costs nothing on x86/x64 (fence-free load).
        const size_t tail = tail_.load(std::memory_order_acquire);
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
    // P7 D-04: cache-line-separate the producer-only and consumer-side
    // atomics so SPSC false sharing is eliminated. MSVC C4324 ("structure
    // was padded due to alignment specifier") is the EXPECTED outcome of
    // alignas(64) — suppress locally. Consumers under /W4 /WX (driver_micmap)
    // would otherwise fail-build on this template's instantiation site.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4324)
#endif
    alignas(64) std::atomic<size_t> head_{0};      // producer-only writer
    alignas(64) std::atomic<size_t> tail_{0};      // consumer-only writer (BL-01: drop-NEWEST means producer never writes tail_)
    alignas(64) std::atomic<uint32_t> drops_{0};
    std::array<std::array<float, kFrames>, kSlots> slots_{};
    std::array<size_t, kSlots> slot_count_{};
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
};

} // namespace micmap::driver
