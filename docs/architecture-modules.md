# Firmware module architecture (god-module extraction)

`src/bl.cpp` historically held most device behavior (wake, Wi-Fi, setup,
display refresh, sleep, X touchbar). This document tracks incremental
extractions that keep call order and globals semantics stable.

## Goals

1. Make discrete flows reviewable without reading 3k+ lines of `bl.cpp`.
2. Keep behavior stable: same call sites from `bl_init`, same NVS / UI semantics.
3. Leave clear seams for later extractions.

## Module map (this PR: `wifi_session`)

```text
main.cpp
  └─ bl_init()                         src/bl.cpp
        │
        ├─ wifiSessionInit()           src/wifi_session.cpp  (hostname)
        ├─ … display / sensors / battery …
        ├─ wifiSessionConnect()        src/wifi_session.cpp
        │     ├─ X modem prep + scan   (when needed)
        │     ├─ connectWithSavedCredentials()  src/wifi_network.cpp
        │     └─ captive portal OR failure → wifiErrorDeepSleep
        │           └─ blWifiPortalTickX()     bl.cpp (X power-off corners)
        │
        ├─ getDeviceCredentials()      (still in bl / api_setup_session if landed)
        ├─ downloadAndShow()           still in bl.cpp
        └─ goToSleep / X UI            bl.cpp
```

| Module | Responsibility | Not responsible for |
|--------|----------------|---------------------|
| **`wifi_session`** | Boot hostname, X modem prep for portal/5 GHz, STA connect or captive portal | Credential reset policy UI beyond callback, deep-sleep retry schedule (`wifiErrorDeepSleep` stays in bl) |
| **`wifi_network`** | Hostname string, `connectWithSavedCredentials`, reconnect helpers | Captive portal orchestration |
| **`bl`** | Orchestration, X portal tick (IQS323), messages, sleep after Wi-Fi fail | Prefer not to grow portal connect logic here again |

## Public API

### `include/wifi_session.h`

- `wifiSessionInit()` — early in `bl_init` (before any portal / `SF_ADD_WIFI`)
- `wifiSessionConnect()` — after display/filesystem/battery; blocks until connected or sleeps on failure

### Callback into bl (X only)

- `blWifiPortalTickX()` — captive-portal tick for both-corners power-off confirmation

### Helpers still owned by `bl.cpp` (exported)

- `showMessageWithLogo` (WIFI_CONNECT / WIFI_FAILED screens)
- `wifiErrorDeepSleep`, `resetDeviceCredentials`
- `preferences`, and on X: `g_modem`, `iqs323`, `touchbar_tap_mode`

## Call flow (boot)

1. `wifiSessionInit` — captive portal hostname  
2. Hardware / display / battery  
3. `wifiSessionConnect`  
   - X: modem if 5 GHz saved or no credentials; modem scan + portal callbacks if no credentials  
   - STA mode  
   - Saved: `connectWithSavedCredentials` or fail → `wifiErrorDeepSleep`  
   - Unsaved: WIFI_CONNECT screen → `startPortal` (X: tap mode + portal tick) or fail → sleep  
4. Clock sync, API setup / display refresh as before  

## Recommended next extractions (ordered)

| Priority | Extract | From | Notes |
|----------|---------|------|--------|
| 1 | API setup / setup image | `bl.cpp` | `api_setup_session` (separate PR) |
| 2 | ~~Captive portal + Wi-Fi connect~~ | ~~`bl_init`~~ | **Done** → `wifi_session` |
| 3 | Sleep / wake (`goToSleep`, button-only, RTC glue) | `bl.cpp` | `sleep_session.cpp` |
| 4 | `downloadAndShow` + image helpers | `bl.cpp` | `display_session` + `image_pipeline` |
| 5 | X touchbar / IQS323 handlers | `bl.cpp` | Already partly in `lib/trmnl_x` |

## Rules for future PRs

- Prefer **new modules + thin bl_init** over new logic in `bl.cpp`.
- Keep OG and X `#ifdef` paths in the same module until a board HAL exists.
- Build `trmnl` (and `TRMNL_X_dev` when the X PlatformIO family is active) before merge.

## Related docs

- [README.md](../README.md) — compilation entry points
