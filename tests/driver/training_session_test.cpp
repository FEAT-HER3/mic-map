/**
 * @file training_session_test.cpp
 * @brief Phase 9 Wave 0 RED scaffold for TrainingSession lifecycle (TRAIN-01..04, TRAIN-06).
 *
 * Convention: plain-main, exit 0 = pass, 1 = fail. Mirrors
 * tests/driver/detection_settings_propagation_test.cpp / tests/test_settings_validator.cpp.
 *
 * RED until Plan 09-01 lands driver/src/training_session.{hpp,cpp}. This translation
 * unit is intentionally fail-to-build at Wave 0 — the compile failure IS
 * the Nyquist gate per .planning/phases/09-training-migration/09-VALIDATION.md.
 *
 * Test cases (per 09-00 PLAN, exercising the public surface documented in
 * 09-PATTERNS.md "driver/src/training_session.{hpp,cpp}"):
 *   1. Construct TrainingSession(48000, 1024); initial state == Collecting; samples_collected() == 0.
 *   2. Feed 100 dummy addSample blocks of 480 floats each; call compute(); state == Ready.
 *   3. recompute(0.5f) while in Ready returns true; preview()->sensitivity ~= 0.5f.
 *   4. Fresh session; tickTimeout(now + 31s); state == Cancelled; last_error()=="training_timed_out_no_samples".
 *   5. validateFinalize(confirm=false, samples=5) returns ValidationError (settings_validator surface; lands 09-01).
 */

#include "training_session.hpp"            // RED hook: lands in Plan 09-01

#include <chrono>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace md = micmap::driver;

#define MM_CHECK(expr) do { if (!(expr)) { \
    std::cerr << "FAIL: " << #expr << " at line " << __LINE__ << "\n"; \
    return 1; } } while(0)

int main() {
    // ----- Case 1: construction places session in Collecting with zero samples.
    {
        md::TrainingSession session(/*sampleRate=*/48000u, /*fftSize=*/1024u);
        MM_CHECK(session.state() == md::SessionState::Collecting);
        MM_CHECK(session.samples_collected() == 0u);
    }

    // ----- Case 2: feed 100 blocks of 480 floats, then compute() => Ready.
    {
        md::TrainingSession session(/*sampleRate=*/48000u, /*fftSize=*/1024u);
        std::vector<float> block(480, 0.1f);
        for (int i = 0; i < 100; ++i) {
            MM_CHECK(session.addSample(block.data(), block.size()));
        }
        auto err = session.compute();
        MM_CHECK(!err.has_value());
        MM_CHECK(session.state() == md::SessionState::Ready);
    }

    // ----- Case 3: recompute(0.5f) refreshes preview sensitivity.
    {
        md::TrainingSession session(/*sampleRate=*/48000u, /*fftSize=*/1024u);
        std::vector<float> block(480, 0.1f);
        for (int i = 0; i < 100; ++i) {
            (void)session.addSample(block.data(), block.size());
        }
        (void)session.compute();
        MM_CHECK(session.recompute(0.5f));
        auto preview = session.preview();
        MM_CHECK(preview.has_value());
        MM_CHECK(preview->sensitivity > 0.49f);
        MM_CHECK(preview->sensitivity < 0.51f);
    }

    // ----- Case 4: 30 s collect-window timeout cancels with the documented error string.
    {
        md::TrainingSession session(/*sampleRate=*/48000u, /*fftSize=*/1024u);
        auto t0 = std::chrono::steady_clock::now();
        bool fired = session.tickTimeout(t0 + std::chrono::seconds(31));
        MM_CHECK(fired);
        MM_CHECK(session.state() == md::SessionState::Cancelled);
        MM_CHECK(session.last_error().has_value());
        MM_CHECK(*session.last_error() == "training_timed_out_no_samples");
    }

    // ----- Case 5: finalize-without-confirm rejection.
    // RED until 09-01 wires validateFinalizePayload alongside the existing
    // settings_validator.hpp surface (mirror of D-15 envelope shape). The
    // assertion below references the validator's expected return type only;
    // the include lives in training_session.hpp's transitive surface.
    {
        // validateFinalize(confirm=false, samples_collected=5) must report a
        // ValidationError with field=="confirm" / reason matching D-13's "must
        // be true to finalize a Ready session" copy.
        md::TrainingSession session(/*sampleRate=*/48000u, /*fftSize=*/1024u);
        std::vector<float> block(480, 0.1f);
        for (int i = 0; i < 5; ++i) {
            (void)session.addSample(block.data(), block.size());
        }
        (void)session.compute();
        auto verr = md::validateFinalizePayload(/*confirm=*/false,
                                                /*samples_collected=*/5);
        MM_CHECK(verr.has_value());
        MM_CHECK(verr->field == "confirm");
    }

    std::cout << "all tests passed\n";
    return 0;
}
