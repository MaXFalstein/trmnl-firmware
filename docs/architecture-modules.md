# Firmware module architecture (god-module extraction)

`src/bl.cpp` historically held most device behavior (wake, Wi-Fi, setup,
display refresh, sleep, X touchbar). This document tracks incremental
extractions that keep call order and globals semantics stable.

## Goals

1. Make discrete flows reviewable without reading 3k+ lines of `bl.cpp`.
2. Keep behavior stable: same call sites, same NVS / wake / GPIO semantics.
3. Leave clear seams for later extractions.

## Module map (this PR: `sleep_session`)

```text
main.cpp
  └─ bl_init() / bl_process()          src/bl.cpp
        │
        ├─ … wifi / API / display …
        ├─ goToSleep()                 src/sleep_session.cpp
        │     ├─ submitStoredLogs()    bl.cpp
        │     ├─ WiFi off (non–X-class)
        │     ├─ X: IQS323 prepare + wake stub + panel GPIO LP
        │     ├─ timer + GPIO wake sources
        │     └─ esp_deep_sleep_start()
        ├─ goToSleepButtonOnly()       sleep_session (retry limit)
        └─ wifiErrorDeepSleep()        sleep_session (Wi-Fi fail schedule)
```

| Module | Responsibility | Not responsible for |
|--------|----------------|---------------------|
| **`sleep_session`** | Enter deep sleep, button-only sleep, LP GPIO config, Wi-Fi-fail sleep schedule, X wake-stub registration at sleep | Wake-time button/IQS handling, captive portal, display refresh |
| **`bl`** | Boot orchestration, call goToSleep after work, X UI that decides *when* to sleep | Prefer not to re-implement sleep entry here |
| **`display`** | `display_sleep`, `config_tca95535_pins_for_lp` | Timer/GPIO wake sources |

## Public API

### `include/sleep_session.h`

- `goToSleep()` — used by `bl_init`, error paths, and `WifiCaptive`
- `goToSleepButtonOnly()` — after Wi-Fi retry limit
- `config_gpio_for_lp()` — X pin tristate before deep sleep
- `wifiErrorDeepSleep()` — advance Wi-Fi retry NVS keys then sleep

### Still owned by `bl.cpp` (exported)

- `submitStoredLogs`, `showMessageWithLogo` (WIFI_RETRY_LIMIT)
- `preferences`, `startup_time`, `iPrevWakeTime`
- X: `iqs323`, `touchbar_tap_mode`

### Time

- `getTime()` remains in `bl` / `bl.h` (shared with logging and sleep timestamps)

## Call flow (end of successful wake)

1. Display / API / OTA as before  
2. `display_sleep()` where applicable  
3. `goToSleep()` — logs flush, radio/panel prep, NVS sleep duration, timer + button wake, deep sleep  
4. Next boot: `esp_sleep_get_wakeup_cause()` still read in `bl_init` (wake *handling* stays in bl until a later extract)

## Recommended next extractions (ordered)

| Priority | Extract | From | Notes |
|----------|---------|------|--------|
| 1 | API setup / setup image | `bl.cpp` | `api_setup_session` (separate PR) |
| 2 | Captive portal + Wi-Fi connect | `bl_init` | `wifi_session` (separate PR) |
| 3 | ~~Sleep / wake entry~~ | ~~`bl.cpp`~~ | **Done** → `sleep_session` |
| 4 | `downloadAndShow` + image helpers | `bl.cpp` | `display_session` + `image_pipeline` |
| 5 | X touchbar / IQS323 handlers | `bl.cpp` | Already partly in `lib/trmnl_x` |
| 6 | Wake *handling* in `bl_init` (GPIO vs timer, button read) | `bl.cpp` | Optional follow-on to sleep_session |

## Rules for future PRs

- Prefer **new modules + thin bl_init** over new logic in `bl.cpp`.
- Keep OG and X `#ifdef` paths in the same module until a board HAL exists.
- Build `trmnl` (and `TRMNL_X_dev` when the X PlatformIO family is active) before merge.

## Related docs

- [README.md](../README.md) — compilation entry points
