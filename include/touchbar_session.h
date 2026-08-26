#pragma once

/**
 * TRMNL X touchbar / IQS323 high-level handling.
 *
 * G9a confirmations, G9b playlist browse, G9c process_iqs323_data + bl_init hooks.
 * FreeRTOS IQS task remains in lib/trmnl_x.
 */

#ifdef BOARD_TRMNL_X

#include <stdint.h>

/** Re-entrancy flags / cooldown shared with bl when needed. */
extern bool in_wifi_reset_confirmation;
extern bool in_power_off_confirmation;
extern uint32_t s_power_off_cooldown_until;

void handle_wifi_reset_confirmation(void);
void handle_power_off_confirmation(void);

void show_cached_image_by_offset(int offset);

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

#endif // BOARD_TRMNL_X
