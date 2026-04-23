#pragma once
#include <deque>
#include <mutex>
#include <optional>

namespace micmap::driver {

struct PressCommand {
    enum class Kind { Down, Up };
    Kind kind;
};

class CommandQueue {
public:
    static constexpr size_t kMaxDepth = 8;

    // Producer (HTTP thread). Returns true if queue was full and oldest dropped.
    bool push(PressCommand cmd) {
        std::lock_guard<std::mutex> lk(m_);
        bool dropped = false;
        if (q_.size() >= kMaxDepth) { q_.pop_front(); dropped = true; }
        q_.push_back(cmd);
        return dropped;
    }

    // Consumer (RunFrame). Never blocks.
    std::optional<PressCommand> try_pop() {
        std::lock_guard<std::mutex> lk(m_);
        if (q_.empty()) return std::nullopt;
        auto c = q_.front();
        q_.pop_front();
        return c;
    }

private:
    std::mutex m_;
    std::deque<PressCommand> q_;
};

} // namespace micmap::driver
