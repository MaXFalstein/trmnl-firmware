#include <config.h>
#include <display_messages.h>
#include <globals.h>
#include <inttypes.h>
#include <string.h>
#include <trmnl_log.h>

#include "Group5.h"
#include "Inter_18.h"
#include "messages.h"
#include "nicoclean_8.h"
#include "wifi_connect_qr.h"
#include "wifi_failed_qr.h"

#ifndef BOARD_X_CLASS
#define BB_EPAPER
#include "bb_epaper.h"
extern BBEPAPER bbep;
#else
#include "FastEPD.h"
extern FASTEPD bbep;
#endif

#ifdef BB_EPAPER
// Panel HAL helper (display.cpp)
bool display_update_epaper(int refreshMode, bool wait, bool writePlane = false, uint8_t plane = PLANE_0);
#endif

// Timed light sleep helper used between multi-step message screens
void display_sleep(uint32_t u32Millis);

/**
 * @brief Function to draw multi-line text onto the display
 * @param x_start X coordinate to start drawing
 * @param y_start Y coordinate to start drawing
 * @param message Text message to draw
 * @param max_width Maximum width in pixels for each line
 * @param font_width Width of a single character in pixels
 * @param color_fg Foreground color
 * @param color_bg Background color
 * @param font Font to use
 * @param is_center_aligned If true, center the text; if false, left-align
 * @return none
 */
void Paint_DrawMultilineText(UWORD x_start, UWORD y_start, const char *message, uint16_t max_width, uint16_t font_width,
                             UWORD color_fg, UWORD color_bg, const void *font, bool is_center_aligned) {
  BB_FONT_SMALL *pFont = (BB_FONT_SMALL *)font;
  uint16_t display_width_pixels = max_width;
  int max_chars_per_line = display_width_pixels / font_width;
  const int font_height = pFont->height;
  uint8_t MAX_LINES = 4;

  char lines[MAX_LINES][max_chars_per_line + 1] = {0};
  uint16_t line_count = 0;

  int text_len = strlen(message);
  int current_width = 0;
  int line_index = 0;
  int line_pos = 0;
  int word_start = 0;
  int i = 0;
  char word_buffer[max_chars_per_line + 1] = {0};
  int word_length = 0;

  bbep.setFont(font);
  bbep.setTextColor(color_fg, color_bg);

  bbep.setFont(font);
  bbep.setTextColor(color_fg, color_bg);

  while (i <= text_len && line_index < MAX_LINES) {
    word_length = 0;
    word_start = i;

    // Skip leading spaces
    while (i < text_len && message[i] == ' ') {
      i++;
    }
    word_start = i;

    // Find end of word or end of text
    while (i < text_len && message[i] != ' ') {
      i++;
    }

    word_length = i - word_start;
    if (word_length > max_chars_per_line) {
      word_length = max_chars_per_line; // Truncate if word is too long
    }

    if (word_length > 0) {
      strncpy(word_buffer, message + word_start, word_length);
      word_buffer[word_length] = '\0';
    } else {
      i++;
      continue;
    }

    int word_width = word_length * font_width;

    // Check if adding the word exceeds max_width
    if (current_width + word_width + (current_width > 0 ? font_width : 0) <= display_width_pixels) {
      // Add space before word if not the first word in the line
      if (current_width > 0 && line_pos < max_chars_per_line - 1) {
        lines[line_index][line_pos++] = ' ';
        current_width += font_width;
      }

      // Add word to current line
      if (line_pos + word_length <= max_chars_per_line) {
        strcpy(&lines[line_index][line_pos], word_buffer);
        line_pos += word_length;
        current_width += word_width;
      }
    } else {
      // Current line is full, draw it
      if (line_pos > 0) {
        lines[line_index][line_pos] = '\0'; // Null-terminate the current line
        line_index++;
        line_count++;

        if (line_index >= MAX_LINES) {
          break;
        }

        // Start new line with this word
        strncpy(lines[line_index], word_buffer, word_length);
        line_pos = word_length;
        current_width = word_width;
      } else {
        // Single long word case
        strncpy(lines[line_index], word_buffer, max_chars_per_line);
        lines[line_index][max_chars_per_line] = '\0';
        line_index++;
        line_count++;
        line_pos = 0;
        current_width = 0;
      }
    }

    // Move to next word
    if (message[i] == ' ') {
      i++;
    }
  }

  // Store the last line if any
  if (line_pos > 0 && line_index < MAX_LINES) {
    lines[line_index][line_pos] = '\0';
    line_count++;
  }

  // Draw the lines
  for (int j = 0; j < line_count; j++) {
    uint16_t line_width = strlen(lines[j]) * font_width;
    uint16_t draw_x = x_start;

    if (is_center_aligned) {
      if (line_width < max_width) {
        draw_x = x_start + (max_width - line_width) / 2;
      }
    }
    bbep.setCursor(draw_x, y_start + j * (font_height + 5));
    bbep.print(lines[j]);
  }
}

/**
 * @brief Function to show the image with message on the display
 * @param image_buffer pointer to the uint8_t image buffer
 * @param message_type type of message that will show on the screen
 * @return none
 */
void display_show_msg(uint8_t *image_buffer, MSG message_type, const char *message_text) {
  auto width = display_width();
  auto height = display_height();
  UWORD Imagesize = ((width % 8 == 0) ? (width / 8) : (width / 8 + 1)) * height;
  BB_RECT rect;

  Log_info("display_show_msg start");
  Log_info("maximum_compatibility = %d\n", apiDisplayResult.response.maximum_compatibility);
#ifdef BB_EPAPER
  bbep.allocBuffer(false);
#else
  bbep.setMode(BB_MODE_1BPP); // message screens are 1-bit
#endif
  if (image_buffer && *(uint16_t *)image_buffer == BB_BITMAP_MARKER) {
        // G5 compressed image
    BB_BITMAP *pBBB = (BB_BITMAP *)image_buffer;
    int x = (width - pBBB->width) / 2;
    int y = (height - pBBB->height) / 2; // center it
    if (x > 0 || y > 0) // only clear if the image is smaller than the display
    {
      bbep.fillScreen(BBEP_WHITE);
    }
    bbep.loadG5Image(image_buffer, x, y, BBEP_WHITE, BBEP_BLACK);
  } else {
#ifdef BB_EPAPER
    if (image_buffer) memcpy(bbep.getBuffer(), image_buffer + 62, Imagesize); // uncompressed 1-bpp bitmap
#endif
  }

#ifdef BOARD_X_CLASS
  bbep.setFont(Inter_18);
#else
  bbep.setFont(nicoclean_8);
#endif
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);

  switch (message_type) {
  case OTG_TURNED_ON: {
    const char string1[] = "OTG turned on!";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);
    break;
  }
  case OTG_TURNED_OFF: {
    const char string1[] = "OTG turned off!";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);
    break;
  }
  case MODEM_FLASHING: {
    const char string1[] = "Flashing modem firmware...";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);
    break;
  }
  case MODEM_FLASH_FAILED: {
    const char string1[] = "Failed to flash modem firmware,";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);

    const char string2[] = "device would only operate with 2.4Ghz WiFi.";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 500);
    if (message_text) {
      bbep.println(string2);
      bbep.getStringBox(message_text, &rect);
      bbep.setCursor((bbep.width() - rect.w) / 2, 570);
      bbep.print(message_text);
    } else {
      bbep.print(string2);
    }
    break;
  }
  case READY_TO_SHIP: {
    const char string1[] = "Device is ready to ship!";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);

    const char string2[] = "Unplug the USB-C to enter shipping mode.";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 500);
    bbep.print(string2);
    break;
  }
  case SHIPPING_MODE: {
    const char string1[] = "Welcome to TRMNL.";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);

    const char string2[] = "Attach the dock and a USB-C to get started.";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 500);
    bbep.print(string2);
    break;
  }
  case WIFI_RESET_CONFIRM: {
    const char string1[] = "Are you sure you want to reset WiFi settings?";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);
    const char string2[] = "Hold middle of touch bar to confirm, tap to cancel.";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string2);
    break;
  }

  case POWER_OFF_CONFIRM: {
    const char string1[] = "Turn off device?";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);
    const char string2[] = "Hold middle of touch bar to confirm, tap to cancel.";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string2);
    break;
  }

  case WIFI_CONNECT: {
    const char string1[] = "Connect to TRMNL WiFi";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 430);
    bbep.println(string1);
    const char string2[] = "on your phone or computer";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string2);
  } break;
  case WIFI_FAILED: {
    String string0 = "TRMNL firmware ";
    string0 += Messages::firmware_version();
#ifdef __BB_EPAPER__
    bbep.setCursor(40, 48); // place in upper left corner
#else
    bbep.setCursor(80, 104); // place in upper left corner
#endif
    bbep.println(string0);
    const char string1[] = "Can't establish WiFi connection.";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - (rect.h * 2) - 140);
    bbep.println(string1);
#ifndef BOARD_TRMNL_X
    const char string2[] = "Hold button on the back to reset WiFi, or scan QR Code for help.";
#else
    const char string2[] = "Hold left and right corner of touch bar to reset WiFi, or scan QR Code for help.";
#endif
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.println(string2);
#ifdef __BB_EPAPER__
    bbep.loadG5Image(wifi_failed_qr, bbep.width() - 66 - 40, 40, BBEP_WHITE, BBEP_BLACK);
#else // bigger for X
    bbep.loadG5Image(wifi_failed_qr, bbep.width() - (66 * 2) - 80, 80, BBEP_WHITE, BBEP_BLACK, 2.0f);
#endif
  } break;
  case WIFI_INTERNAL_ERROR: {
    const char string1[] = "WiFi connected, but";
#ifdef __BB_EPAPER__
    int x = 132;
#else
    int x = 0;
#endif
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - 132 - rect.w) / 2, 340);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - (rect.h * 2) - 140);
#endif
    bbep.println(string1);
    const char string2[] = "API connection cannot be";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - x - rect.w) / 2, -1);
    bbep.println(string2);
    const char string3[] = "established. Try to refresh,";
    bbep.getStringBox(string3, &rect);
    bbep.setCursor((bbep.width() - x - rect.w) / 2, -1);
    bbep.println(string3);
    const char string4[] = "or scan QR Code for help.";
    bbep.getStringBox(string4, &rect);
    bbep.setCursor((bbep.width() - x - rect.w) / 2, -1);
    bbep.print(string4);
#ifdef __BB_EPAPER__
    bbep.loadG5Image(wifi_failed_qr, 639, 336, BBEP_WHITE, BBEP_BLACK);
#else // bigger for X
    bbep.loadG5Image(wifi_failed_qr, bbep.width() - (66 * 2) - 80, 80, BBEP_WHITE, BBEP_BLACK, 2.0f);
#endif
  } break;
  case WIFI_WEAK: {
    const char string1[] = "WiFi connected but signal is weak";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - rect.h);
#endif
    bbep.print(string1);
  } break;
  case API_REQUEST_FAILED: {
    const char string1[] = "WiFi connected, request to API failed.";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 340);
    bbep.println(string1);
#ifndef BOARD_TRMNL_X
    const char string2[] = "Short click the button on back,";
#else
    const char string2[] = "Tap the middle of touch bar,";
#endif
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.println(string2);
    const char string3[] = "otherwise check your internet.";
    bbep.getStringBox(string3, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string3);
  } break;
  case API_UNABLE_TO_CONNECT: {
    const char string1[] = "WiFi connected, unable connect to API.";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 340);
    bbep.println(string1);
#ifndef BOARD_TRMNL_X
    const char string2[] = "Short click the button on back,";
#else
    const char string2[] = "Tap the middle of touch bar,";
#endif
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.println(string2);
    const char string3[] = "otherwise check your internet.";
    bbep.getStringBox(string3, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string3);
  } break;
  case API_SETUP_FAILED: {
    const char string1[] = "WiFi connected, /api/setup returned error.";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 340);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - (rect.h * 3));
#endif
    bbep.println(string1);
#ifndef BOARD_TRMNL_X
    const char string2[] = "Short click the button on back,";
#else
    const char string2[] = "Tap the middle of touch bar,";
#endif
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.println(string2);
    const char string3[] = "otherwise check your internet.";
    bbep.getStringBox(string3, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string3);
  } break;
  case API_SIZE_ERROR: {
    const char string1[] = "WiFi connected, TRMNL content malformed.";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - (rect.h * 2));
#endif
    bbep.println(string1);
#ifndef BOARD_TRMNL_X
    const char string2[] = "Wait or reset by holding button on back.";
#else
    const char string2[] = "Wait or reset by holding left and right corner of touch bar.";
#endif
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string2);
  } break;
  case API_FIRMWARE_UPDATE_ERROR: {
    const char string1[] = "WiFi connected, could not get firmware update from api.";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
    bbep.println(string1);
#ifndef BOARD_TRMNL_X
    const char string2[] = "Wait or reset by holding button on back.";
#else
    const char string2[] = "Wait or reset by holding left and right corner of touch bar.";
#endif
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string2);
  } break;
  case WIFI_IMAGE_TIMEOUT: {
    const char string1[] = "Image download timed out; check your network status.";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - rect.h);
#endif
    bbep.print(string1);
  } break;
  case API_IMAGE_DOWNLOAD_ERROR: {
    const char string1[] = "WiFi connected, API could not deliver image to device.";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
    bbep.println(string1);
#ifndef BOARD_TRMNL_X
    const char string2[] = "Wait or reset by holding button on back.";
#else
    const char string2[] = "Wait or reset by holding left and right corner of touch bar.";
#endif
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string2);
  } break;
  case FW_UPDATE: {
    const char string1[] = "Firmware update available! Starting now...";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - rect.h);
#endif
    bbep.print(string1);
  } break;
  case FW_UPDATE_FAILED: {
    const char string1[] = "Firmware update failed. Device will restart...";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - rect.h);
#endif
    bbep.print(string1);
  } break;
  case FW_UPDATE_SUCCESS: {
    const char string1[] = "Firmware update success. Device will restart...";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - rect.h);
#endif
    bbep.print(string1);
  } break;
  case QA_START: {
    const char string1[] = "Starting QA test";
    bbep.getStringBox(string1, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
    bbep.print(string1);
  } break;
  case MSG_TOO_BIG: {
    const char string1[] = "The image file from this URL is too large.";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 360);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - rect.h * 4);
#endif
    bbep.println(string1);
    if (strlen(filename) > 40) {
      filename[40] = 0; // truncate and add elipses
      strcat(filename, "...");
    }
    bbep.getStringBox(filename, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.println(filename);

    const char string2[] = "PNG images can be a maximum of";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.println(string2);
#ifdef __BB_EPAPER__
    String string3 = String(MAX_IMAGE_SIZE) + String(" bytes each and 1 or 2-bpp");
#else
    String string3 = String(MAX_IMAGE_SIZE) + String(" bytes each");
#endif
    bbep.getStringBox(string3.c_str(), &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string3);
  } break;
  case MSG_FORMAT_ERROR: {
    const char string1[] = "The image format is incorrect";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - rect.h);
#endif
    bbep.print(string1);
  } break;
  case TEST: {
    bbep.setCursor(0, 40);
    bbep.println("ABCDEFGHIYABCDEFGHIYABCDEFGHIYABCDEFGHIYABCDEFGHIY");
    bbep.println("abcdefghiyabcdefghiyabcdefghiyabcdefghiyabcdefghiy");
    bbep.println("A B C D E F G H I Y A B C D E F G H I Y A B C D E");
    bbep.println("a b c d e f g h i y a b c d e f g h i y a b c d e");
  } break;
  case FILL_WHITE: {
    Log_info("Display set to white");
#ifdef BOARD_X_CLASS
    if (bbep.getMode() == BB_MODE_4BPP) {
      bbep.fillScreen(15); // in 4-bit mode, color 15 = white
    } else {
      bbep.fillScreen(BBEP_WHITE);
    }
#else
    bbep.fillScreen(BBEP_WHITE);
#endif
  } break;
  case WIFI_RETRY_LIMIT: {
    const char string1[] = "Maximum WiFi retries reached.";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 340);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - (rect.h * 3));
#endif
    bbep.println(string1);
    const char string2[] = "Press button to manually refresh.";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.println(string2);
    const char string3[] = "Hold button to reset WiFi and try another network.";
    bbep.getStringBox(string3, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string3);
  } break;
  case CAPTIVE_WIFI_TIMEOUT: {
    const char string1[] = "Wifi Captive Portal timed out";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 340);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - (rect.h * 2));
#endif
    bbep.println(string1);
#ifdef BOARD_TRMNL_X
    const char string2[] = "Tap touchbar to try again";
#else
    const char string2[] = "Press button to try again";
#endif
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.println(string2);
  } break;
  default:
    break;
  }
#ifdef BB_EPAPER
  if (!display_update_epaper(REFRESH_FULL, true, true, PLANE_0)) {
    Log_error("display_show_msg: e-paper update failed");
    bbep.freeBuffer();
    return;
  }
  bbep.freeBuffer();
#else
  Serial.println("FastEPD full update");
  bbep.fullUpdate(CLEAR_SLOW, true);
#endif
  Log_info("display_show_msg end");
}

/**
 * @brief Function to show the image with message on the display
 * @param image_buffer pointer to the uint8_t image buffer
 * @param message_type type of message that will show on the screen
 * @param friendly_id device friendly ID
 * @param id shows if ID exists
 * @param fw_version version of the firmware
 * @param message additional message
 * @return none
 */
void display_show_msg(uint8_t *image_buffer, MSG message_type, String friendly_id, bool id, const char *fw_version,
                      String message) {
  Log_info("Free heap in display_show_msg - %" PRIu32, ESP.getMaxAllocHeap());
  Log_info("maximum_compatibility = %d\n", apiDisplayResult.response.maximum_compatibility);
#ifdef BB_EPAPER
  bbep.allocBuffer(false);
  Log_info("Free heap after bbep.allocBuffer() - %" PRIu32, ESP.getMaxAllocHeap());
#else
  bbep.setMode(BB_MODE_1BPP); // message screens are 1-bit
#endif

  if (message_type == WIFI_CONNECT) {
    Log_info("Display set to white");
    bbep.fillScreen(BBEP_WHITE);
#ifdef BB_EPAPER
    if (!display_update_epaper(apiDisplayResult.response.maximum_compatibility ? REFRESH_FULL : REFRESH_FAST, true,
                               true, PLANE_0)) {
      Log_error("display_show_msg: WiFi connect update failed");
      bbep.freeBuffer();
      return;
    }
#else
    bbep.fullUpdate();
#endif
    display_sleep(1000);
  }

  auto width = display_width();
  auto height = display_height();
  UWORD Imagesize = ((width % 8 == 0) ? (width / 8) : (width / 8 + 1)) * height;
  BB_RECT rect;

  Log_info("display_show_msg2 start");

    // Load the image into the bb_epaper framebuffer
  if (image_buffer && *(uint16_t *)image_buffer == BB_BITMAP_MARKER) {
        // G5 compressed image
    BB_BITMAP *pBBB = (BB_BITMAP *)image_buffer;
    int x = (width - pBBB->width) / 2;
    int y = (height - pBBB->height) / 2; // center it
    if (x > 0 || y > 0) // only clear if the image is smaller than the display
    {
      bbep.fillScreen(BBEP_WHITE);
    }
    bbep.loadG5Image(image_buffer, x, y, BBEP_WHITE, BBEP_BLACK);
  } else {
#ifdef BB_EPAPER
    if (image_buffer) memcpy(bbep.getBuffer(), image_buffer + 62, Imagesize); // uncompressed 1-bpp bitmap
#endif
  }

#if defined(BOARD_X_CLASS)
  bbep.setFont(Inter_18);
#else
  bbep.setFont(nicoclean_8);
#endif
  bbep.setTextColor(BBEP_BLACK, BBEP_WHITE);
  switch (message_type) {
  case FRIENDLY_ID: {
    Log_info("friendly id case");
    const char string1[] = "Please visit trmnl.com/start";
    bbep.getStringBox(string1, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 400);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 140 - rect.h * 2);
#endif
    bbep.println(string1);

    String string2 = "with Friendly ID ";
    if (id) {
      string2 += friendly_id;
    }
    string2 += " to finish setup";
    bbep.getStringBox(string2, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string2);
  } break;
  case WIFI_CONNECT: {
    Log_info("wifi connect case");

    String string1 = "TRMNL firmware ";
    string1 += fw_version;
    bbep.setCursor(40, 48); // place in upper left corner
    bbep.println(string1);
    String string2 = "Connect your phone or computer to ";
    string2 += (message.length() > 0) ? "\"" + message + "\"" : String("the TRMNL");
    string2 += " Wi-Fi";
    bbep.getStringBox(string2, &rect);
#ifdef __BB_EPAPER__
    bbep.setCursor((bbep.width() - rect.w) / 2, 386);
#else
    bbep.setCursor((bbep.width() - rect.w) / 2, bbep.height() - 100 - rect.h);
#endif
    bbep.println(string2);
    const char string3[] = "or scan the QR code for help";
    bbep.getStringBox(string3, &rect);
    bbep.setCursor((bbep.width() - rect.w) / 2, -1);
    bbep.print(string3);
#ifdef __BB_EPAPER__
    bbep.loadG5Image(wifi_connect_qr, bbep.width() - 40 - 66, 40, BBEP_WHITE, BBEP_BLACK); // 66x66 QR code
#else // bigger for X
    bbep.loadG5Image(wifi_connect_qr, bbep.width() - (66 * 2) - 80, 80, BBEP_WHITE, BBEP_BLACK, 2.0f);
#endif
  } break;
  case MAC_NOT_REGISTERED: {
    UWORD y_start = 340;
    UWORD font_width = 18; // DEBUG
    Paint_DrawMultilineText(0, y_start, message.c_str(), width, font_width, BBEP_BLACK, BBEP_WHITE,
#if defined(BOARD_TRMNL_X) || defined(BOARD_TRMNL_X_EPDIY) || defined(BOARD_TRMNL_X_SENSORIAS3) ||                     \
  defined(BOARD_TRMNL_X_SENSORIAC5) || defined(BOARD_TRMNL_X_LILYGO) || defined(BOARD_TRMNL_X_PAPERS3)
                            Inter_18, true);
#else
                            nicoclean_8, true);
#endif
  } break;
  default:
    break;
  }
  Log_info("Start drawing...");
#ifdef BB_EPAPER
  if (!display_update_epaper(REFRESH_FULL, true, true, PLANE_0)) {
    Log_error("display_show_msg2: e-paper update failed");
    bbep.freeBuffer();
    return;
  }
  bbep.freeBuffer();
#else
  bbep.fullUpdate();
#endif
  Log_info("display_show_msg2 end");
}
