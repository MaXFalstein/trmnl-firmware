#include <touchbar_session.h>

#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <config.h>
#include <display.h>
#include <filesystem.h>
#include <globals.h>
#include <qa.h>
#include <trmnl_log.h>

#include "IQS323.h"
#include "displayed_image.h"
#include "iqs323_task.h"

void showMessageWithLogo(MSG message_type);
void resetDeviceCredentials(void);
void showLastImageAndSleep(void);
void goToSleep(void);

#define WIFI_RESET_CONFIRMATION_TIMEOUT_MS 15000
#define WIFI_RESET_POLL_INTERVAL_MS        100

bool in_wifi_reset_confirmation = false;
bool in_power_off_confirmation = false;
uint32_t s_power_off_cooldown_until = 0;

// Read gesture data directly without triggering other handlers
static void read_gesture_data_only() {
  // Read slider coordinates
  uint16_t buffer = iqs323.sliderCoordinate();
  if (buffer != slider_position) {
    slider_position = buffer;
  }

  // Read gesture event
  bool gesture_event = iqs323.getSliderEvent();
  if (gesture_event) {
    iqs323_gesture_events gesture_buffer = iqs323.getGestureType();
    if (gesture_buffer != IQS323_GESTURE_NONE) {
      slider_event = gesture_buffer;
    }
  }
}
// Check if user wants to confirm WiFi reset (middle button hold)
static bool check_wifi_reset_confirm() {
  if (slider_event == IQS323_GESTURE_HOLD && iqs323.channel_touchState(IQS323_CH1)) {
    Log_info("WiFi reset confirmed by user - holding middle button");
    return true;
  }
  return false;
}

// Check if user wants to cancel WiFi reset (any tap)
static bool check_wifi_reset_cancel() {
  if (slider_event == IQS323_GESTURE_TAP) {
    Log_info("WiFi reset cancelled by user - tap detected");
    return true;
  }
  return false;
}

static void confirm_wifi_reset() { resetDeviceCredentials(); }

static void confirm_power_off() {
  clearShipmentStatus();
  ESP.restart();
}

static bool handle_confirmation_flow(bool &in_flag, MSG message, void (*on_confirm)(void)) {
  in_flag = true;
  showMessageWithLogo(message);

  // Wait for the triggering hold to be fully released before accepting new input.
  // Without this, lifting fingers from the initial hold could register as a cancel tap.
  {
    const uint32_t RELEASE_TIMEOUT_MS = 2000;
    unsigned long release_start = millis();
    do {
      delay(200);
      iqs323.updateInfoFlags(STOP);
    } while ((iqs323.channel_touchState(IQS323_CH0) || iqs323.channel_touchState(IQS323_CH1) ||
              iqs323.channel_touchState(IQS323_CH2)) &&
             millis() - release_start < RELEASE_TIMEOUT_MS);
    slider_event = IQS323_GESTURE_NONE;
  }

  if (touchbar_tap_mode) {
    const uint32_t HOLD_MS = 600;
    const uint32_t POLL_MS = 20;
    unsigned long start_time = millis();

    while (millis() - start_time < WIFI_RESET_CONFIRMATION_TIMEOUT_MS) {
      delay(POLL_MS);
      if (iqs323.getRDYStatus()) {
        iqs323.updateInfoFlags(STOP);
      }

      if (iqs323.channel_touchState(IQS323_CH0) || iqs323.channel_touchState(IQS323_CH2)) {
        bool left_cancel = iqs323.channel_touchState(IQS323_CH0);
        Log_info("Confirmation cancelled - outer button in tap mode, status: left=%d right=%d",
                 iqs323.channel_touchState(IQS323_CH0), iqs323.channel_touchState(IQS323_CH2));
        display_draw_touchbar_indicator(left_cancel ? TOUCHBAR_LEFT : TOUCHBAR_RIGHT, false);
        in_flag = false;
        return false;
      }

      if (iqs323.channel_touchState(IQS323_CH1)) {
        unsigned long touch_start = millis();
        while (millis() - touch_start < HOLD_MS) {
          delay(POLL_MS);
          if (iqs323.getRDYStatus()) {
            iqs323.updateInfoFlags(STOP);
          }
          if (!iqs323.channel_touchState(IQS323_CH1)) {
            Log_info("Confirmation cancelled - tap on middle button in tap mode");
            display_draw_touchbar_indicator(TOUCHBAR_MIDDLE, false);
            in_flag = false;
            return false;
          }
        }
        display_draw_touchbar_indicator(TOUCHBAR_MIDDLE, true);
        Log_info("Confirmed - holding middle button in tap mode");
        in_flag = false;
        on_confirm();
        return true;
      }
    }

    Log_info("Confirmation timeout - cancelling");
    in_flag = false;
    return false;
  }

  unsigned long start_time = millis();

  while (millis() - start_time < WIFI_RESET_CONFIRMATION_TIMEOUT_MS) {
    delay(WIFI_RESET_POLL_INTERVAL_MS);
    read_gesture_data_only();

    if (check_wifi_reset_confirm()) {
      in_flag = false;
      on_confirm();
      return true;
    }

    if (check_wifi_reset_cancel()) {
      in_flag = false;
      return false;
    }

    if (slider_position == 65535) {
      slider_event = IQS323_GESTURE_NONE;
    }
  }

  Log_info("Confirmation timeout - cancelling");
  in_flag = false;
  return false;
}

void handle_wifi_reset_confirmation() {
  Log_info("Entering WiFi reset confirmation mode");
  bool confirmed = handle_confirmation_flow(in_wifi_reset_confirmation, WIFI_RESET_CONFIRM, confirm_wifi_reset);

  if (!confirmed) {
    Log_info("WiFi reset cancelled - redrawing last image and sleeping");
    showLastImageAndSleep();
  }
}

void handle_power_off_confirmation() {
  Log_info("Entering power-off confirmation mode");

  void show_cached_image_by_offset(int offset) {
    String order = preferences.getString(PREFERENCES_PLAYLIST_ORDER_KEY, "");

    if (order.isEmpty()) {
      String path = (offset > 0) ? preferences.getString(PREFERENCES_CURRENT_PATH_KEY, "")
                                 : preferences.getString(PREFERENCES_LAST_PATH_KEY, "");
      if (path.isEmpty()) {
        Log_info("No cached image for gesture");
        return;
      }
      int file_size = 0;
      buffer = display_read_file(path.c_str(), &file_size);
      if (buffer && file_size > 0) {
        display_show_image(buffer, file_size, true);
        DisplayedImage::remember(path.c_str());
        goToSleep();
      }
      return;
    }

    char images[MAX_CACHED_IMAGES][36];
    int count = 0;
    int start = 0;
    while (start <= (int)order.length() && count < MAX_CACHED_IMAGES) {
      int sep = order.indexOf('|', start);
      String entry = (sep < 0) ? order.substring(start) : order.substring(start, sep);
      if (!entry.isEmpty() && filesystem_file_exists(entry.c_str())) {
        strncpy(images[count], entry.c_str(), 35);
        images[count][35] = '\0';
        count++;
      }
      if (sep < 0) break;
      start = sep + 1;
    }

    if (count == 0) {
      Log_info("No cached images available");
      return;
    }

    String browsePath = preferences.getString(PREFERENCES_BROWSE_PATH_KEY, "");
    if (browsePath.isEmpty()) {
    // Seed from last_path so first RIGHT shows curr_path (forward) and first LEFT shows older (backward).
    // Falls back to curr_path if last_path is absent (e.g. only one image cached).
      String lp = preferences.getString(PREFERENCES_LAST_PATH_KEY, "");
      browsePath = lp.isEmpty() ? preferences.getString(PREFERENCES_CURRENT_PATH_KEY, "") : lp;
    }

    int cur_idx = count - 1;
    for (int i = 0; i < count; i++) {
      if (browsePath == String(images[i])) {
        cur_idx = i;
        break;
      }
    }

    int new_idx = (cur_idx + offset + count) % count;
    Log_info("Playlist browse: %d/%d -> %d (%s)", cur_idx, count, new_idx, images[new_idx]);

    int file_size = 0;
    buffer = display_read_file(images[new_idx], &file_size);
    if (!buffer || file_size == 0) {
      Log_info("Failed to read %s", images[new_idx]);
      return;
    }

    preferences.putString(PREFERENCES_BROWSE_PATH_KEY, String(images[new_idx]));
    display_show_image(buffer, file_size, true);
    DisplayedImage::remember(images[new_idx]);
    goToSleep();
  }

  handle_confirmation_flow(in_power_off_confirmation, POWER_OFF_CONFIRM, confirm_power_off);
}

#endif // BOARD_TRMNL_X
