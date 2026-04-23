#include "command_queue.hpp"
#include <cassert>
#include <iostream>

using micmap::driver::CommandQueue;
using micmap::driver::TapCommand;

static_assert(CommandQueue::kMaxDepth == 8, "CommandQueue depth must be 8 per SVR-05");

static void test_empty_queue_returns_nullopt() {
    CommandQueue q;
    assert(!q.try_pop().has_value());
}

static void test_push_then_pop_round_trip() {
    CommandQueue q;
    bool dropped = q.push(TapCommand{});
    assert(!dropped);
    auto c = q.try_pop();
    assert(c.has_value());
    assert(!q.try_pop().has_value());
}

static void test_drop_oldest_on_overflow() {
    CommandQueue q;
    for (int i = 0; i < 8; ++i) {
        bool dropped = q.push(TapCommand{});
        assert(!dropped);
    }
    bool dropped = q.push(TapCommand{});   // 9th -- forces drop-oldest
    assert(dropped);
    // Drain to confirm depth cap held at 8.
    int count = 0;
    while (q.try_pop()) ++count;
    assert(count == 8);
}

int main() {
    test_empty_queue_returns_nullopt();
    test_push_then_pop_round_trip();
    test_drop_oldest_on_overflow();
    std::cout << "test_command_queue PASSED\n";
    return 0;
}
