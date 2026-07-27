#pragma once

/**
 * TRMNL X touchbar / IQS323 gesture handling.
 *
 * Extracted from bl.cpp (architecture-modules: X touchbar / IQS323).
 * Low-level FreeRTOS task lives in lib/trmnl_x (iqs323_task); this module
 * owns high-level gestures, playlist browse, confirmations, and portal tick.
 *
 * @see docs/architecture-modules.md
 */

#ifdef BOARD_TRMNL_X

#include <IQS323.h>
#include <stdbool.h>

/** Touch controller instance (shared with sleep prep in bl / goToSleep). */
extern IQS323 iqs323;

/** false = slide, true = tap (default). Updated from NVS and /api/display. */
extern bool touchbar_tap_mode;

/** Set when an OTG-related message was shown (logo skip path). */
extern bool otg_message;

/** Process wake-stub / live gestures (playlist, wifi-reset corners). */
void process_iqs323_data(void);

/** Load touchbar_tap_mode from preferences. */
void touchbarSessionLoadMode(void);

/** Start iqs323_task, wait ready; on GPIO wake run process_iqs323_data. */
void touchbarSessionStartTask(bool gpio_wakeup);

/** Notify task whether this wake used GPIO (wake stub data). */
void touchbarNotifyGpioWakeup(bool gpio_wakeup);

/** Enable tap mode + gesture config for captive portal. */
void touchbarPrepareCaptivePortal(void);

/** Portal tick: both corners held → power-off confirmation. */
void touchbarPortalTick(void);

/** Redraw pending touchbar indicator after full refresh (e.g. logo). */
void touchbarRedrawPendingIndicator(void);

/** Whether otg_message is set (logo display gate). */
bool touchbarHasOtgMessage(void);

void handle_wifi_reset_confirmation(void);
void handle_power_off_confirmation(void);

#endif // BOARD_TRMNL_X
