# Firmware module architecture (god-module extraction)

`src/bl.cpp` historically held most device behavior (wake, Wi-Fi, setup,
display refresh, sleep, X touchbar). This document tracks incremental
extractions that keep call order and globals semantics stable.

## Goals

1. Make discrete flows reviewable without reading 3k+ lines of `bl.cpp`.
2. Keep behavior stable: same call sites from `bl_init`, same NVS / buffer usage.
3. Leave clear seams for later extractions.

## Module map (this PR: `api_setup_session`)

```text
main.cpp
  └─ bl_init() / bl_process()          src/bl.cpp
        │
        ├─ getDeviceCredentials()      src/api_setup_session.cpp
        │     ├─ performApiSetup()     /api/setup (+ modem 5 GHz on X)
        │     └─ downloadSetupImage()  setup logo → SPIFFS → friendly-ID screen
        │
        ├─ downloadAndShow()           still in bl.cpp (later: display_session)
        ├─ display_*                   src/display.cpp
        └─ goToSleep / sensors / X UI  bl.cpp
```

| Module | Responsibility | Not responsible for |
|--------|----------------|---------------------|
| **`api_setup_session`** | First-time registration: `/api/setup`, store API key / friendly ID, download and show setup logo | Captive portal, deep sleep policy, periodic `/api/display` refresh |
| **`bl`** | Boot, Wi-Fi, orchestration, sleep, messages, X touchbar, image helpers still shared | Prefer not to grow setup logic here again |
| **`display`** | EPD init, `display_show_msg` / `display_show_image`, PNG→EPD | Network |

## Public API

### `include/api_setup_session.h`

- `getDeviceCredentials(void)` — called from `bl_init` when NVS lacks API key / friendly ID.

Internals (file-static in `api_setup_session.cpp`):

- `performApiSetup()` — `fetchApiSetup` or modem HTTP GET on X 5 GHz path
- `downloadSetupImage()` — HTTP(S) or modem logo download, `writeImageToFile`, show friendly ID

### Shared helpers still owned by `bl.cpp` (exported, non-static)

Used by setup and by `downloadAndShow` until a later image-pipeline extract:

- `downloadStream`, `writeImageToFile`
- `showMessageWithLogo(MSG)` and `showMessageWithLogo(MSG, const ApiSetupResponse &)`
- `storedLogoOrDefault`, `goToSleep`

Setup touches these bl globals via `extern`: `preferences`, `filename`,
`message_buffer`, `status`, `buffer`, `need_to_refresh_display`, and on X
`g_modem`.

## Call flow (first boot / missing credentials)

1. `bl_init` — hardware + Wi-Fi
2. If API key or friendly ID missing → `getDeviceCredentials()`
   - `performApiSetup` → NVS credentials + image URL / message
   - On 404 MAC: `MAC_NOT_REGISTERED` screen → sleep
   - On success: `downloadSetupImage` → `/logo.bmp` or `/logo.png` → friendly ID UI
3. Later wakes use normal `/api/display` path (`downloadAndShow` in `bl.cpp`)

## Recommended next extractions (ordered)

| Priority | Extract | From | Notes |
|----------|---------|------|--------|
| 1 | ~~`performApiSetup` + `downloadSetupImage` + `getDeviceCredentials`~~ | ~~`bl.cpp`~~ | **Done** → `api_setup_session` |
| 2 | `downloadAndShow` + response handling + stream/file helpers | `bl.cpp` | `display_session` + `image_pipeline` (see god-mod if landed separately) |
| 3 | Captive portal + Wi-Fi connect block in `bl_init` | `bl.cpp` | `wifi_session.cpp` |
| 4 | Sleep / wake (`goToSleep`, button-only sleep, RTC glue) | `bl.cpp` | `sleep_session.cpp` |
| 5 | X touchbar / IQS323 handlers | `bl.cpp` | Already partly in `lib/trmnl_x` |

## Rules for future PRs

- Prefer **new modules + thin bl_init** over new logic in `bl.cpp`.
- When a function needs many bl globals, either document `extern`s with a short
  comment or introduce a small context struct and pass it.
- Keep OG and X `#ifdef` paths in the same module until a board HAL exists.
- Build `trmnl` (and `TRMNL_X_dev` when the X PlatformIO family is active)
  before merge.

## Related docs

- [README.md](../README.md) — compilation entry points
- [building-og-x.md](building-og-x.md) — OG/BWRY vs X PlatformIO families (if present)
