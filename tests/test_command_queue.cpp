#include "command_queue.hpp"
#include <cassert>
#include <iostream>

using micmap::driver::CommandQueue;
using micmap::driver::PressCommand;

static_assert(CommandQueue::kMaxDepth == 8, "CommandQueue depth must be 8 per SVR-05");

static void test_empty_queue_returns_nullopt() {
    CommandQueue q;
    assert(!q.try_pop().has_value());
}

static void test_push_then_pop_round_trip() {
    CommandQueue q;
    bool dropped = q.push({PressCommand::Kind::Down});
    assert(!dropped);
    auto c = q.try_pop();
    assert(c.has_value());
    assert(c->kind == PressCommand::Kind::Down);
    assert(!q.try_pop().has_value());
}

static void test_drop_oldest_on_overflow() {
    CommandQueue q;
    for (int i = 0; i < 8; ++i) {
        bool dropped = q.push({PressCommand::Kind::Down});
        assert(!dropped);
    }
    bool dropped = q.push({PressCommand::Kind::Up});   // 9th
    assert(dropped);
    // Drain — the last surviving entry must be the Up we just pushed.
    std::optional<PressCommand> last;
    while (auto c = q.try_pop()) last = c;
    assert(last.has_value());
    assert(last->kind == PressCommand::Kind::Up);
}

int main() {
    test_empty_queue_returns_nullopt();
    test_push_then_pop_round_trip();
    test_drop_oldest_on_overflow();
    std::cout << "test_command_queue PASSED\n";
    return 0;
}
