# Firmware module architecture (god-module extraction)

`src/bl.cpp` historically held most device behavior. This document tracks
incremental extractions that keep call order and globals semantics stable.

## Goals

1. Make discrete flows reviewable without reading 3k+ lines of `bl.cpp`.
2. Keep behavior stable across OG and X product lines.
3. Leave clear seams for later extractions.

## Module map (this PR: `touchbar_session`)

```text
main.cpp
  └─ bl_init()                         src/bl.cpp
        │
        ├─ (X) touchbarSessionLoadMode / StartTask / process_iqs323_data
        │       src/touchbar_session.cpp
        │         ├─ IQS323 gestures, confirmations, playlist browse
        │         └─ lib/trmnl_x iqs323_task (FreeRTOS + RDY)
        │
        ├─ captive portal tick         touchbarPortalTick()
        ├─ downloadAndShow             bl (update_playlist_order stays in bl)
        └─ goToSleep                   bl (uses iqs323 + touchbar_tap_mode)
```

| Module | Responsibility | Not responsible for |
|--------|----------------|---------------------|
| **`touchbar_session`** | X touchbar gestures, wifi-reset / power-off confirm, portal tick, playlist browse by offset | Image download API path, `update_playlist_order` NVS rotation (still bl) |
| **`iqs323_task`** (`lib/trmnl_x`) | FreeRTOS task, init, sleep prep, I2C lock | High-level product gestures |
| **`bl`** | Boot orchestration, display refresh, playlist order updates from network path | Prefer not to re-grow gesture handlers |

## Public API (`include/touchbar_session.h`, `BOARD_TRMNL_X` only)

- `process_iqs323_data()` — wake-stub / live gesture processing
- `touchbarSessionLoadMode()` / `touchbarSessionStartTask(gpio_wakeup)`
- `touchbarNotifyGpioWakeup`, `touchbarPrepareCaptivePortal`, `touchbarPortalTick`
- `touchbarRedrawPendingIndicator`, `touchbarHasOtgMessage`
- Externals: `iqs323`, `touchbar_tap_mode`, `otg_message`

## Recommended next extractions

| Priority | Extract | Notes |
|----------|---------|--------|
| 1–3 | api_setup / wifi_session / sleep_session | Separate PRs from main |
| 4 | `downloadAndShow` + image helpers | display_session + image_pipeline |
| 5 | ~~X touchbar / IQS323 handlers~~ | **Done** → `touchbar_session` |
| 6 | Wake handling in `bl_init` | Optional |

## Rules

- Prefer new modules + thin `bl_init`.
- Keep OG stubs (`iqs323_task_i2c_lock` no-ops) in `bl` for non-X builds.
- Build `trmnl` (and `TRMNL_X_dev` when the X family is active) before merge.
