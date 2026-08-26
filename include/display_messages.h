#pragma once

/**
 * System UI message helpers for the e-paper.
 *
 * G10a: Paint_DrawMultilineText. G10b: display_show_msg overloads.
 * G10c: display_show_msg_qa.
 *
 * Public declarations stay in display.h so call sites (and lib/wificaptive)
 * keep a single include surface. Implementation lives in
 * src/display_messages.cpp; panel HAL remains in display.cpp.
 */

#include <display.h>
