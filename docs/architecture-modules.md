# Firmware module architecture (god-module extraction)

`src/bl.cpp` and `src/display.cpp` historically held most device behavior.
This document tracks incremental extractions that keep call order and
public APIs stable.

## Goals

1. Make discrete flows reviewable without reading multi-thousand-line files.
2. Keep behavior stable across OG and X product lines.
3. Leave clear seams for later extractions.

## Module map (this PR: display messages split)

```text
callers (bl, qa, main, WifiCaptive)
  │
  ├─ display_show_msg* / Paint_DrawMultilineText
  │     src/display_messages.cpp   ← system UI screens
  │           │
  │           └─ bbep + display_update_epaper()  (panel HAL)
  │
  └─ display_init / display_show_image / png_to_epd / display_sleep
        src/display.cpp            ← panel HAL + image pipeline
```

| Module | Responsibility | Not responsible for |
|--------|----------------|---------------------|
| **`display`** | Panel init, image decode (PNG/JPEG/G5), `display_show_image`, sleep, X board helpers (TCA, OTG, battery detect) | Copy/layout of system status screens |
| **`display_messages`** | `display_show_msg` (all overloads), `display_show_msg_qa`, multiline text paint | Panel driver selection, SPI/I2C, refresh profiles |

## Public API

Unchanged for callers: include `display.h` (contains `MSG` and message APIs).

Implementation boundary:

- `src/display.cpp` — HAL
- `src/display_messages.cpp` — UI message screens
- Shared: global `bbep`, `display_update_epaper` (BB_EPAPER), `display_sleep(ms)`

## Recommended next extractions

| Priority | Extract | Notes |
|----------|---------|--------|
| 1–5 | api_setup / wifi / sleep / touchbar sessions | Separate PRs from main |
| 6 | `downloadAndShow` + image helpers from `bl.cpp` | display_session + image_pipeline |
| 7 | ~~Split `display.cpp` panel vs messages~~ | **Done** → `display_messages` |
| 8 | Further panel split by driver (bb_epaper vs FastEPD) | Optional |

## Rules

- Prefer new modules over growing `bl.cpp` / `display.cpp`.
- Keep OG and X `#ifdef` paths in the same module until a board HAL exists.
- Build `trmnl` (and `TRMNL_X_dev` when the X family is active) before merge.
