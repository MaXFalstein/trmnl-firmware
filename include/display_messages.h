#pragma once

/**
 * System UI message screens (Wi-Fi, API errors, firmware, QA, friendly ID, …).
 *
 * Implemented in src/display_messages.cpp. Panel HAL remains in display.cpp /
 * display.h. Call sites may continue to include display.h, which re-exports
 * these declarations.
 *
 * @see docs/architecture-modules.md
 */

#include <Arduino.h>
#ifndef _NO_DEV_CONFIG_
#include "DEV_Config.h"
#endif

// MSG and display_show_msg* are declared in display.h for a single public surface.
// This header documents the UI-message module boundary.
