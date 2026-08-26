#pragma once

/**
 * System UI message helpers for the e-paper (word-wrap painter, then
 * display_show_msg* screens in later G10 slices).
 *
 * Public declarations stay in display.h so call sites (and lib/wificaptive)
 * keep a single include surface. Implementation lives in
 * src/display_messages.cpp; panel HAL remains in display.cpp.
 */

#include <display.h>
