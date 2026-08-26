#pragma once

/**
 * System UI message helpers for the e-paper.
 *
 * G10a: Paint_DrawMultilineText. G10b: display_show_msg overloads.
 * G11: showMessageWithLogo wrappers + storedLogoOrDefault.
 *
 * Message-screen declarations stay in display.h for call sites (and
 * lib/wificaptive). Logo wrappers are declared here; bl.cpp includes this
 * header. WifiCaptive keeps a local forward decl for the MSG overload.
 */

#include <api-client/setup.h>
#include <display.h>

void showMessageWithLogo(MSG message_type);
void showMessageWithLogo(MSG message_type, String friendly_id, bool id, const char *fw_version, String message);
void showMessageWithLogo(MSG message_type, const ApiSetupResponse &apiResponse);

/** Logo / loading glyph: 0 = message-screen logo, 1 = loading screen glyph. */
uint8_t *storedLogoOrDefault(int iType);
