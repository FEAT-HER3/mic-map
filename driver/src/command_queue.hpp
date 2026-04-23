#pragma once
#include <deque>
#include <mutex>
#include <optional>

namespace micmap::driver {

// A single tap command. The app posts one of these per detection rising
// edge; the driver expands it into UpdateBooleanComponent(true) followed
// by UpdateBooleanComponent(false) after a short hold, so SteamVR's
// complex_button binding sees a clean single-click.
struct TapCommand {};

class CommandQueue {
public:
    static constexpr size_t kMaxDepth = 8;

    // Producer (HTTP thread). Returns true if queue was full and oldest dropped.
    bool push(TapCommand cmd) {
        std::lock_guard<std::mutex> lk(m_);
        bool dropped = false;
        if (q_.size() >= kMaxDepth) { q_.pop_front(); dropped = true; }
        q_.push_back(cmd);
        return dropped;
    }

    // Consumer (RunFrame). Never blocks.
    std::optional<TapCommand> try_pop() {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return std::nullopt;
        auto c = q_.front();
        q_.pop_front();
        return c;
    }

private:
    std::mutex m_;
    std::deque<TapCommand> q_;
};

} // namespace micmap::driver
