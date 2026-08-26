#include <display_messages.h>
#include <string.h>

#include "Group5.h"

#ifndef BOARD_X_CLASS
#define BB_EPAPER
#include "bb_epaper.h"
extern BBEPAPER bbep;
#else
#include "FastEPD.h"
extern FASTEPD bbep;
#endif

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
