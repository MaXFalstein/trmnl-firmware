#pragma once

/**
 * Deep sleep and wake-source configuration.
 *
 * Extracted from bl.cpp (architecture-modules: sleep / wake orchestration).
 * Call sites in bl_init, error paths, and WifiCaptive keep using goToSleep() /
 * wifiErrorDeepSleep() with unchanged semantics.
 *
 * @see docs/architecture-modules.md
 */

/** Prepare peripherals, enable timer + GPIO wake, enter deep sleep. */
void goToSleep(void);

/** Deep sleep until button only (no timer); used after Wi-Fi retry limit. */
void goToSleepButtonOnly(void);

/** Float/tristate GPIOs for low power (TRMNL X panel/I2C pins). */
void config_gpio_for_lp(void);

/**
 * Wi-Fi connect failure: advance retry schedule, set sleep duration, then
 * goToSleep or goToSleepButtonOnly at the limit. Does not return.
 */
void wifiErrorDeepSleep(void);
