#pragma once

/**
 * TRMNL X touchbar / IQS323 high-level handling.
 *
 * Combines confirmation flows (G9a), playlist browse (G9b), and (on this
 * branch) process_iqs323_data / bl_init hooks (G9c).
 */

#ifdef BOARD_TRMNL_X

#include <stdint.h>

/** Re-entrancy flags / cooldown shared with bl portal-tick and gesture code. */
extern bool in_wifi_reset_confirmation;
extern bool in_power_off_confirmation;
extern uint32_t s_power_off_cooldown_until;

void handle_wifi_reset_confirmation(void);
void handle_power_off_confirmation(void);

/**
 * Show a cached playlist image relative to the current browse position.
 * @param offset -1 previous, +1 next (wraps within playlist_order)
 */
void show_cached_image_by_offset(int offset);

#endif // BOARD_TRMNL_X
