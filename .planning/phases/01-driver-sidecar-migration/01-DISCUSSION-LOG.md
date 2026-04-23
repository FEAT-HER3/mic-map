# Phase 1: Driver Sidecar Migration - Discussion Log

> **Audit trail only.** Do not use as input to planning, research, or execution agents.
> Decisions are captured in CONTEXT.md — this log preserves the alternatives considered.

**Date:** 2026-04-22
**Phase:** 01-driver-sidecar-migration
**Areas discussed:** Reactivation strategy, Click timing & request shape, Driver init failure UX, Test harness & dashboard_manager fate, App-side state machine

---

## Reactivation Strategy

### Q: Which reactivation strategy should the driver implement?

| Option | Description | Selected |
|--------|-------------|----------|
| Event-driven primary, poll fallback | Subscribe to `VREvent_TrackedDeviceDeactivated` to invalidate; also re-check `TrackedDeviceToPropertyContainer` every RunFrame when handle invalid. Defense in depth. | ✓ |
| Event-driven only | Subscribe to deactivation event only. Cleaner code; risk of silent no-op if event missed. | |
| Poll only, no event subscription | Re-check every RunFrame. Most defensive; small per-tick cost. | |

**User's choice:** Event-driven primary, poll fallback.

### Q: When does the spike happen?

| Option | Description | Selected |
|--------|-------------|----------|
| Spike first, then implement | Half-day spike with `hmd_button_test.exe` to characterize Case D before locking strategy. | ✓ |
| Implement defensive (hybrid), then validate | Build the hybrid up front, validate during phase exit UAT. | |
| Defer spike to Phase 5 / docs phase | Ship SUMMARY.md design (event-only), characterize during integration. | |

**User's choice:** Spike first, then implement.

---

## Click Timing & Request Shape

### Q: Where does the press-hold duration live?

| Option | Description | Selected |
|--------|-------------|----------|
| App-supplied per request | HTTP body carries `duration_ms`. App keeps existing 100ms default; tunable from UI/config. | |
| Fixed constant in driver | Driver hardcodes 100ms. Parameterless click. Re-ship driver to change. | |
| Driver default + optional override | Driver default 100ms; HTTP body can override. | |

**User's choice (free text):** "press-hold duration should be dynamic according to how long the user covers the microphone / the noise pattern is detected"

**Notes:** The free-text answer rejected the "click with duration" framing entirely. The user wants press/release semantics that mirror detection state, not a timed pulse. This triggered the follow-up press-model question below.

### Q: Confirming press/release semantics with optional minimum hold?

| Option | Description | Selected |
|--------|-------------|----------|
| Yes, mirror detection state (down/up pair) | Two events: PRESS on Triggered entry, RELEASE on noise stop. | |
| Yes, but enforce a minimum hold | Mirror detection AND guarantee at least N ms of press. Driver releases at `max(detection_duration, min_hold)`. | ✓ |
| No, I meant something else | Open-ended clarification. | |

**User's choice:** Yes, but enforce a minimum hold.

### Q: Given press/release semantics, what's the HTTP shape?

| Option | Description | Selected |
|--------|-------------|----------|
| `POST /button {"state":"down"\|"up"}` | Single endpoint, state in body. Extensible. | ✓ |
| `POST /button/press` and `POST /button/release` | Two endpoints, no body. | |
| `POST /click` (legacy) + `POST /release` (new) | Mixed metaphor for back-compat. | |

**User's choice:** `POST /button {"state":"down"\|"up"}`.

### Q: What's the minimum hold duration?

| Option | Description | Selected |
|--------|-------------|----------|
| 100ms (matches existing) | Same as current `virtual_controller` default. | ✓ |
| 50ms | Shorter, more responsive; needs validation against SteamVR debounce. | |
| Configurable in app config | Expose as `config.json` setting (default 100ms). | |

**User's choice:** 100ms.

### Q: Where does the min-hold timer live?

| Option | Description | Selected |
|--------|-------------|----------|
| Driver-side | Driver records `press_timestamp`, defers `up` until min_hold elapsed. Robust to app lag. | ✓ |
| App-side | App enforces. Crashed app = button stuck down. | |
| Both (defense in depth) | Both enforce. Most robust, slight duplication. | |

**User's choice:** Driver-side.

---

## Driver Init Failure UX

### Q: When the HMD container never becomes available, how should the driver behave?

| Option | Description | Selected |
|--------|-------------|----------|
| Forever-poll silently, log on transitions only | Log "awaiting HMD container" on init; log success when ready. No periodic spam. | ✓ |
| Forever-poll, log a warning every N seconds | Periodic "still awaiting" warnings; pollutes log during dev. | |
| Forever-poll + expose status via `/health` endpoint | Driver adds GET `/health`; app shows banner. Adds HTTP route + UI work. | |

**User's choice:** Forever-poll silently, log on transitions only.

### Q: What about the press/release path when the HMD container isn't ready yet?

| Option | Description | Selected |
|--------|-------------|----------|
| Drop with warning log | Discard with one log line per drop. Aligns with CommandQueue drop-oldest. | ✓ |
| Queue and apply when handle becomes valid | Buffer pending presses; apply when ready. Risk: stale press fires unexpectedly. | |
| Return HTTP 503 to app | Tightest coupling; informative. | |

**User's choice:** Drop with warning log.

---

## Test Harness & dashboard_manager Fate

### Q: How should `hmd_button_test.exe` support the sleep/wake reactivation spike?

| Option | Description | Selected |
|--------|-------------|----------|
| Extend with a press/release loop subcommand | `--loop --interval 5s` for reproducible scripted testing. | |
| Use as-is, manual press triggering | Operator manually triggers between sleep/wake cycles. | ✓ |
| Add explicit `simulate-deactivate` test mode | Synthetic event injection. Reality-divergence risk. | |

**User's choice:** Use as-is, manual press triggering.

### Q: What happens to `dashboard_manager.cpp`?

| Option | Description | Selected |
|--------|-------------|----------|
| Delete entirely | Remove `dashboard_manager.{hpp,cpp}`. Callsites use `driver_client` directly. | ✓ |
| Collapse to no-op pass-through | Keep `IDashboardManager`; `performDashboardAction()` calls `driver_client.press()`. | |
| Delete cpp, keep hpp as deprecated | Transitional `[[deprecated]]` interface for one release. | |

**User's choice:** Delete entirely.

---

## App-Side State Machine

### Q: When should the app emit the 'button up' event?

| Option | Description | Selected |
|--------|-------------|----------|
| After noise drops below threshold for a min release duration | Symmetric with entry; e.g. 80ms. Prevents chatter. | ✓ |
| Immediately when confidence drops below threshold | Tightest tracking; chatter risk. | |
| On Triggered timeout (fixed press duration) | Reverts to fixed press; defeats dynamic model. | |

**User's choice:** Min release duration.

### Q: What replaces the Cooldown state in the new model?

| Option | Description | Selected |
|--------|-------------|----------|
| Post-release cooldown only | After `up` sent, brief cooldown (~200ms) blocks new press. Reuses `cooldown_ms` field. | ✓ |
| No cooldown — rely on release-debounce only | Simpler state machine; risk of double-press on fast cover-uncover-cover. | |
| Both pre-press and post-release cooldowns | Belt and suspenders; less responsive. | |

**User's choice:** Post-release cooldown only.

---

## Wrap-Up Choices

After Area 4, the user opted to discuss app-side state machine changes. After Area 5, the user opted to write CONTEXT.md (declined further area on HTTP back-compat).

## Claude's Discretion

- Exact `CommandQueue` API surface and internal data structure beyond the SVR-05 spec (mutex + bounded deque, depth 8, drop-oldest).
- `IDriverClient` shape: `setButton(state)` vs split `press()`/`release()`.
- Max-hold safety release in driver (defensive against app crash mid-press).
- Exact default values for `min_press_ms`, `min_release_ms`, `cooldown_ms` within the discussed ~80–200ms range.
- Logging verbosity beyond SVR-10's mandate.
- Spike protocol details (cycle count, pass/fail criteria).
- Whether `dashboard_manager` deletion lands as a separate atomic commit or in the same commit as the driver rewrite.

## Deferred Ideas

- `/health` HTTP endpoint (rejected for Phase 1 — silent forever-poll wins on simplicity; revisit in future "driver observability" phase if manual debugging cost grows).
- Configurable min-hold via `config.json` (rejected for Phase 1 in favor of compile-time 100ms; can be promoted later if user-tuning need emerges).
- `hmd_button_test.exe` `--loop` subcommand (not needed for Phase 1 spike; a future regression-test phase may want it).
- Max-hold safety release in driver (Claude's discretion; backlog candidate if planner does not include).

---

*Generated: 2026-04-22*
