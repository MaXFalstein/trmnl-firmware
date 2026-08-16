# God-module extraction plan

This document is the living plan for splitting `src/bl.cpp` and
`src/display.cpp`. **Part A** is the original plan (retained as history).
**Part B** records what was implemented on `extract/*` / `god-mod` branches.
**Part C** recommends the next set of changes—and what to avoid.

Related: [architecture-modules.md](architecture-modules.md) (per-PR module map;
present on extract branches / `god-mod`).

---

# Part A — Original plan (retained)

> Record of the plan as of the five follow-on extractions decision.
> Do not rewrite this section in place; add new status in Part B+.

## Branch strategy for the five follow-on god-module extractions

### Short answer

**Yes — start a new branch (and PR) for each of the five steps.**  
Do **not** pile them onto the existing `god-mod` PR.

Comments on commits inside one PR are a weak substitute: reviewers still get one
growing diff, merge is all-or-nothing, and bisect/revert is painful.

Keep `god-mod` focused on **display_session + image_pipeline only**. Land it
first (or keep it open as base only if you must stack step 1).

### Why not extend the current PR

| Approach | Verdict |
|----------|---------|
| Add all five to `god-mod` PR | **Avoid** — huge review, mixed themes, hard to land partially |
| Same PR, many commits + comments | **Avoid** — GitHub still shows one branch diff; comments do not create independent merge units |
| **One branch + one PR per step** | **Preferred** — matches your branch-from-main model |
| Short stack only if needed | Step 1 can optionally stack on `god-mod` **until** `god-mod` merges; then rebase onto `main` |

### Dependency order

From `docs/architecture-modules.md` (god-mod era):

| # | Extract | Suggested module | Depends on `god-mod`? |
|---|---------|------------------|------------------------|
| 1 | `performApiSetup` + `downloadSetupImage` + `getDeviceCredentials` | `api_setup_session` (or extend `display_session`) | Soft: cleaner if `god-mod` is already on `main` (uses `ImagePipeline` already) |
| 2 | Captive portal + Wi‑Fi connect in `bl_init` | `wifi_session` | No hard dep on 1 |
| 3 | Sleep / wake (`goToSleep`, button-only, RTC glue) | `sleep_session` | No hard dep on 1–2 |
| 4 | X touchbar / IQS323 handlers | move/thin glue into `lib/trmnl_x` + small `touchbar_session` if needed | No hard dep on 1–3 |
| 5 | Split `display.cpp` (panel HAL vs shared UI messages) | e.g. `display_ui` / keep HAL in `display` | Independent of 1–4; can run in parallel after `god-mod` |

**Practical merge order:** land `god-mod` → then 1 → 2 → 3 → 4, with **5 parallel anytime**.  
Do not require a tower of five stacked branches; cut each from **current `main`**
after the previous relevant PR is merged (or from `main` + cherry-pick only if blocked).

### Recommended branch names

| Step | Branch name | PR title (suggested) |
|------|-------------|----------------------|
| 1 | `extract/api-setup-session` | Extract API setup / setup-image flow from bl.cpp |
| 2 | `extract/wifi-session` | Extract captive portal and Wi‑Fi connect from bl_init |
| 3 | `extract/sleep-session` | Extract sleep and wake orchestration from bl.cpp |
| 4 | `extract/touchbar-x` | Move X touchbar / IQS323 handling out of bl.cpp |
| 5 | `extract/display-ui` | Split display.cpp: panel HAL vs system UI messages |

Prefer **`extract/...`** so the series is not confused with the open `god-mod` PR.

### Workflow per step

```bash
git fetch origin
git checkout main
git pull origin main          # after god-mod is merged

git checkout -b extract/api-setup-session
# … implement step 1 only …
git push -u origin extract/api-setup-session
# open PR → base: main
```

Each PR: build `trmnl` + `TRMNL_X_dev` (or wait for CI), update
`docs/architecture-modules.md` module map in that PR.

### Scope discipline (original)

1. **api-setup:** only setup credentials + setup logo download  
2. **wifi-session:** only portal/connect/retry Wi‑Fi  
3. **sleep-session:** only deep/light sleep + wake stub wiring  
4. **touchbar-x:** only IQS323 / confirmation flows / playlist gestures  
5. **display-ui:** only `display.cpp` split — no network changes  

### Original verdict

| Question | Answer |
|----------|--------|
| New branches for each of the five? | **Yes** |
| Keep adding to `god-mod` PR? | **No** |
| Branch names | `extract/api-setup-session`, … `extract/display-ui` |
| Base | Prefer **`main` after `god-mod` merges**; optional short stack of step 1 only on `god-mod` |

---

# Part B — Status (implemented on branches, not necessarily on main)

As of the extract work in this worktree series, **each planned unit has a
dedicated branch and commit**. They were cut from `main` independently (not
stacked), matching Part A. **None of these are assumed merged into `main` until
you merge them**—confirm with CI/PR before treating as done on the default branch.

| Priority | Branch | Module | Typical commit | Relative to main |
|----------|--------|--------|----------------|------------------|
| 0 | `god-mod` | `display_session` + `image_pipeline` + `bl_bridge` | Extract display session and image pipeline | Open / not fully in main |
| 1 | `extract/api-setup-session` | `api_setup_session` | Extract API setup / setup-image | Independent of god-mod (helpers stay in bl) |
| 2 | `extract/wifi-session` | `wifi_session` | Extract captive portal and Wi‑Fi connect | Independent |
| 3 | `extract/sleep-session` | `sleep_session` | Extract sleep and wake entry | Independent |
| 4 | `extract/touchbar-x` | `touchbar_session` | Extract X touchbar / IQS323 handling | Independent |
| 5 | `extract/display-ui` | `display_messages` | Split display panel HAL vs UI messages | Independent |

### Approximate sizes (main vs after local extracts)

| Surface | `main` (approx.) | Notes after extract branches |
|---------|------------------|------------------------------|
| `src/bl.cpp` | ~3800 lines | Still the largest god-module on main; god-mod alone removes ~1.3k; touchbar/api/wifi/sleep each remove hundreds |
| `src/display.cpp` | ~2700 lines | display-ui leaves ~1750 HAL + ~980 `display_messages.cpp` |
| `downloadAndShow` + `handleApiDisplayResponse` | ~1000+ lines in bl | **Largest remaining contiguous domain on main** (already extracted on `god-mod`) |

### Integration note

Independent branches will **overlap** when landed (same regions of `bl.cpp`,
shared helpers, docs). Merge order matters more than inventing new extracts:

1. Prefer landing **`god-mod` first** (biggest bl shrink; defines image helpers).
2. Then **api-setup** (or rebase it onto main after god-mod if you want
   `ImagePipeline` instead of bl-exported helpers).
3. **wifi / sleep / touchbar** in any order after that (touch fewest shared seams).
4. **display-ui** can land anytime; low conflict with bl extracts.

---

# Part C — What else could be extracted?

Sized against **`main`’s** `bl.cpp` / `display.cpp`. After Part B branches land,
line counts drop but the **domain** list is the same.

## C.1 Still makes sense (recommended next set)

Ordered by **review payoff vs risk** (not by original priority numbers).

### 1. Land and integrate existing extract PRs (highest priority)

Not a new module—**integration**. Until `god-mod` and `extract/*` are on
`main`, the tree still has monogod files and duplicate effort.

- Rebase / resolve conflicts once in a controlled order (see Part B).
- One consolidated `docs/architecture-modules.md` on main after merges.
- CI: `trmnl` + `TRMNL_X_dev` (or dual OG/X jobs) on each merge.

### 2. Server refresh path: `display_session` + `image_pipeline` (`god-mod`)

If not already on main: **do this before inventing new bl splits.**

| Extract | From | Why |
|---------|------|-----|
| `downloadAndShow`, `loadApiDisplayInputs`, `handleApiDisplayResponse` | `bl.cpp` | Core product loop; ~1k lines; already designed on `god-mod` |
| `downloadStream`, `writeImageToFile`, path/playlist helpers | `bl.cpp` | Shared by setup + display; god-mod’s `ImagePipeline` |

**Does not make sense to re-implement** from scratch if `god-mod` is open—rebase
and land that PR.

### 3. After god-mod + api/wifi/sleep/touchbar: shrink remaining `bl` domains

| Candidate | Suggested module | Approx. size on main | Why it makes sense | Caveats |
|-----------|------------------|----------------------|--------------------|---------|
| **NTP / clock sync** (`setClock`) | `time_session` / `ntp_sync` | ~90 lines | Clear boundary; used once in `bl_init`; X modem SNTP already branched | Small win alone—batch with logging only if you want fewer PRs |
| **I2C environmental sensors** (`sensor_init` + SCD41/bb_temp globals) | `sensor_session` | ~60–100 lines + globals | Self-contained; only some boards | Keep `#ifdef SENSOR_SDA`; don’t pull battery gauge here |
| **Wake-time button / GPIO policy in `bl_init`** | `wake_session` or extend `sleep_session` | Fragment of `bl_init` | Complements sleep extract; OG button vs X IQS wake differ | Wait until sleep + touchbar are on main to avoid thrash |
| **Special-function double-click handling** (SF_* in `bl_init`) | `special_function_session` or stay in bl | Small switch | Only if SF list grows | Today too small to justify a PR alone |
| **Log submit path** (`submitStoredLogs`, stamp helpers) | extend `app_logger` / `log_session` | ~40–80 lines | Already partially modular (`app_logger`, `stored_logs`) | Needs `preferences`, Wi‑Fi, API key—thin wrapper OK |
| **Battery voltage read used for API stamps** | `power` / keep near `readBatteryVoltage` | Small | `power.cpp` already has USB/charge | Avoid a one-function “module” |

### 4. Further `display.cpp` / HAL splits (after `display_messages` lands)

| Candidate | Suggested home | Why | Why wait / caution |
|-----------|----------------|-----|---------------------|
| **PNG/JPEG/G5 decode + `png_to_epd` / `jpeg_to_epd`** | `display_image_decode.cpp` or keep in `display` | Large, testable without Wi‑Fi | High `#ifdef` density (Spectra6, 4CLR, X FastEPD)—easy to break |
| **X-only board glue in `display.cpp`** (TCA9555 LP pins, OTG, BQ reset, modem bootloader entry, battery count) | `board_trmnl_x.cpp` or expand `lib/trmnl_x` + `power.cpp` | Not “display”; confuses panel HAL reviews | Overlaps sleep (`config_gpio_for_lp`) and modem; do **one** board-HAL PR, not drip |
| **Touchbar indicator drawing** | stays near display or touchbar | Already small | Don’t move without touchbar_session on main |
| **Per-panel driver fork** (bb_epaper file vs FastEPD file) | `display_bb_epaper.cpp` / `display_fastepd.cpp` | Cleaner long-term HAL | **Premature** until message split + image decode settle; doubles `#ifdef` maintenance if done too early |

### 5. Shared-state cleanup (high leverage, low “new feature” risk)

| Work | Why it makes sense | What not to do |
|------|--------------------|----------------|
| **`DisplaySessionContext` (or shrink `bl_bridge`)** | Replaces growing `extern` lists across sessions | Don’t invent a second global bus |
| **Single declaration site for helpers** (`showMessageWithLogo`, sleep, etc.) | Reduces extract friction | Don’t create headers with no impl owners |
| **Playlist order API** | Used by network path and X browse | Belongs with `image_pipeline` / NVS paths once god-mod lands |

---

## C.2 Does **not** make sense (for the next set)

| Idea | Why avoid now |
|------|----------------|
| **More micro-extracts of 20–40 line helpers** (`wait_for_serial`, `log_nvs_usage`, `fixFileName` alone) | Noise PRs; no domain boundary |
| **Forking full `bl_og.cpp` / `bl_x.cpp`** | Architecture rule: keep OG/X `#ifdef` in one module until a real board HAL exists |
| **Re-splitting modules already on `extract/*` before merge** | Duplicate work; fix conflicts once at merge time |
| **Moving `api-client/*` again** | Already modular; not a god-module problem |
| **Absorbing WifiCaptive into firmware modules** | Lives in `lib/wificaptive`; wifi_session should *call* it, not re-home the library |
| **Extracting OTA** | Already `FirmwareUpdateService` |
| **Ethernet / PoE paths** | Out of scope for this product line (`trmnl-fweth` is a different track) |
| **“God module” rename-only refactors** | No behavior or review-surface win |
| **Stacking all remaining work on one branch** | Same failure mode as Part A warned against |
| **Pulling FreeRTOS IQS task out of `lib/trmnl_x` into `src/`** | Wrong direction; touchbar_session correctly sits *above* the task |
| **Deep display partial-refresh policy extract without tests** | Easy to introduce ghosting/regressions; needs device test plan |
| **Extracting `bl_init` as a whole into `bl_init.cpp`** | File move without domain split; reviewers gain nothing |

---

## C.3 Suggested roadmap (next set of changes)

### Phase 0 — Integration (do first)

1. Merge **`god-mod`** → main (or rebase open extracts after it).  
2. Merge **`extract/api-setup-session`**, **`extract/wifi-session`**, **`extract/sleep-session`**, **`extract/touchbar-x`**, **`extract/display-ui`** with conflict resolution, not rewrites.  
3. Stabilize CI on OG + X.

### Phase 1 — Highest-value remaining domain (if god-mod not landed as “done”)

- Ensure **display_session + image_pipeline** are the only owners of refresh I/O.  
- Point api_setup at `ImagePipeline` where possible (optional cleanup PR).

### Phase 2 — Small, clean bl leftovers (optional, separate PRs)

| PR | Scope |
|----|--------|
| `extract/time-sync` | `setClock` only |
| `extract/sensors` | `sensor_init` + sensor globals under `SENSOR_SDA` |
| `extract/wake-handling` | GPIO/button wake policy from `bl_init` (after sleep/touchbar on main) |

Skip Phase 2 until Phase 0 is done—otherwise you fight the same hunks repeatedly.

### Phase 3 — Display HAL maturity (optional)

| PR | Scope |
|----|--------|
| `extract/display-decode` | PNG/JPEG callbacks + `png_to_epd` / `jpeg_to_epd` out of `display.cpp` |
| `extract/board-x-hal` | X TCA/OTG/modem bootloader/battery-count helpers out of `display.cpp` into board/power/trmnl_x |

### Phase 4 — Structure without new files

- Introduce **`DisplaySessionContext`** (or shrink bridge externs).  
- Document call graph in `architecture-modules.md` only after merges (single source of truth on main).

---

## C.4 What “done enough” looks like for bl / display

`bl.cpp` should become **orchestration**:

```text
bl_init:
  hardware / prefs
  wifiSession*
  touchbarSession* (X)
  setClock / sensors (optional modules)
  api setup if needed
  downloadAndShow (display_session)
  OTA / error UI
  goToSleep (sleep_session)

bl_process:
  thin or empty
```

`display.cpp` should become **panel HAL** only:

```text
init / sleep / show_image / decode / board quirks
→ display_messages for status copy
→ optional decode / board-x later
```

You do **not** need to drive `bl.cpp` to zero. A few hundred lines of wiring is healthy.

---

## C.5 Decision summary

| Question | Answer |
|----------|--------|
| What else *could* be extracted? | Time sync, sensors, wake handling, image decode, X board HAL, context/bridge cleanup; plus landing existing branches |
| What makes sense **next**? | **(1)** Merge/integrate god-mod + extract/* **(2)** Ensure display_session/image_pipeline on main **(3)** Only then small bl leftovers or display decode/board-x |
| What does **not** make sense next? | Micro-helpers, OG/X full file forks, redoing open extract work, library rehomes, Ethernet, mega-PR stacks |
| Keep Part A branch-per-PR model? | **Yes** for any new Phase 2/3 work |

---

## Changelog of this document

| Date | Change |
|------|--------|
| (original) | Part A: five extract branches + god-mod separation |
| 2026-07-27 | Part B: status of implemented extract/* and god-mod branches; Part C: further candidates, anti-patterns, phased roadmap |
