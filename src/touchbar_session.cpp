#include <touchbar_session.h>

#ifdef BOARD_TRMNL_X

#include <Arduino.h>
#include <ArduinoLog.h>
#include <Preferences.h>
#include <WifiCaptive.h>
#include <config.h>
#include <display.h>
#include <filesystem.h>
#include <messages.h>
#include <qa.h>
#include <trmnl_log.h>
#include <types.h>

#include "displayed_image.h"
#include "iqs323_task.h"
#include "IQS323.h"

// --- bl.cpp ---
extern Preferences preferences;
extern uint8_t *buffer;
extern unsigned long startup_time;

void showMessageWithLogo(MSG message_type);
void showMessageWithLogo(MSG message_type, String friendly_id, bool id, const char *fw_version, String message);
void goToSleep(void);
void resetDeviceCredentials(void);

// ############################ SLIDER ##################################

IQS323 iqs323;
#define IQS323_I2C_ADDRESS 0x44
// Sensor states
uint16_t slider_position = 65535;
iqs323_gesture_events slider_event = IQS323_GESTURE_NONE;
bool touchbar_tap_mode = true; // false = "slide", true = "tap" (default)
bool otg_message = false;
// Touchbar indicator to redraw after a full-refresh (e.g. logo screen)
static touchbar_side_t pending_indicator_side = TOUCHBAR_LEFT;
static bool pending_indicator_filled = false;
static bool has_pending_indicator = false;

// WiFi reset confirmation constants
#define WIFI_RESET_CONFIRMATION_TIMEOUT_MS 15000
#define WIFI_RESET_POLL_INTERVAL_MS 100

// Static flag to prevent re-entry during WiFi reset confirmation
static bool in_wifi_reset_confirmation = false;
// Static flag to prevent re-entry during power-off confirmation
static bool in_power_off_confirmation = false;
// Cooldown timestamp after cancel to prevent immediate re-trigger
static uint32_t s_power_off_cooldown_until = 0;
// Tracks first detection of both corners held so wakeup_time can be reset once
static bool s_corners_detected = false;

// Read gesture data directly without triggering other handlers
void read_gesture_data_only()
{
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
// Returns true if channel i has been held for HOLD_THRESHOLD_MS since wakeup stub.
// Releases the I2C lock during the wait so the iqs323 task can update the memory map.
static bool tap_mode_is_hold(uint8_t channel_index, time_t hold_threshold_ms = 600)
{
  const uint32_t POLL_INTERVAL_MS = 20;

  // update the memory map to get the latest touch states, but save wakeup stub values for TAPs
  uint8_t saved_status[2] = { iqs323.IQSMemoryMap.SYSTEM_STATUS[0], iqs323.IQSMemoryMap.SYSTEM_STATUS[1] };
  iqs323_task_i2c_lock();
  iqs323.updateInfoFlags(STOP);
  iqs323_task_i2c_unlock();

  while (true) {
    if (millis() - startup_time >= hold_threshold_ms) break;

    iqs323_task_i2c_unlock();
    delay(POLL_INTERVAL_MS);
    iqs323_task_i2c_lock();

    // Only read from chip when it has naturally opened a window (RDY LOW).
    // Forcing I2C on every tick causes the chip to stop responding after ~30+ iterations
    if (iqs323.getRDYStatus()) {
      iqs323.updateInfoFlags(STOP);
    }
    if (!iqs323.channel_touchState((iqs323_channel_e)channel_index)) {
      iqs323.IQSMemoryMap.SYSTEM_STATUS[0] = saved_status[0];
      iqs323.IQSMemoryMap.SYSTEM_STATUS[1] = saved_status[1];
      return false;
    }
  }

  iqs323.updateInfoFlags(STOP);

  if (!iqs323.channel_touchState((iqs323_channel_e)channel_index)) {
    iqs323.IQSMemoryMap.SYSTEM_STATUS[0] = saved_status[0];
    iqs323.IQSMemoryMap.SYSTEM_STATUS[1] = saved_status[1];
  }
  return iqs323.channel_touchState((iqs323_channel_e)channel_index);
}

// Check if user wants to confirm WiFi reset (middle button hold)
bool check_wifi_reset_confirm()
{
  if (slider_event == IQS323_GESTURE_HOLD && iqs323.channel_touchState(IQS323_CH1)) {
    Log_info("WiFi reset confirmed by user - holding middle button");
    return true;
  }
  return false;
}

// Check if user wants to cancel WiFi reset (any tap)
bool check_wifi_reset_cancel()
{
  if (slider_event == IQS323_GESTURE_TAP) {
    Log_info("WiFi reset cancelled by user - tap detected");
    return true;
  }
  return false;
}

static void confirm_wifi_reset()
{
  resetDeviceCredentials();
}

static void confirm_power_off()
{
  clearShipmentStatus();
  ESP.restart();
}

static bool handle_confirmation_flow(bool &in_flag, MSG message, void (*on_confirm)(void))
{
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
    } while ((iqs323.channel_touchState(IQS323_CH0) ||
              iqs323.channel_touchState(IQS323_CH1) ||
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
        Log_info("Confirmation cancelled - outer button in tap mode, status: left=%d right=%d", iqs323.channel_touchState(IQS323_CH0), iqs323.channel_touchState(IQS323_CH2));
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

static void showLastImageAndSleep()
{
  int file_size = 0;
  String curPath = preferences.getString(PREFERENCES_CURRENT_PATH_KEY, "");
  if (!curPath.isEmpty()) {
    uint8_t *buf = display_read_file(curPath.c_str(), &file_size);
    if (buf && file_size > 0) {
      display_show_image(buf, file_size, true);
      free(buf);
      DisplayedImage::remember(curPath.c_str());
    }
  }
  goToSleep();
}

void handle_wifi_reset_confirmation()
{
  Log_info("Entering WiFi reset confirmation mode");
  bool confirmed = handle_confirmation_flow(in_wifi_reset_confirmation, WIFI_RESET_CONFIRM, confirm_wifi_reset);
  
  if (!confirmed) {
    Log_info("WiFi reset cancelled - redrawing last image and sleeping");
    showLastImageAndSleep();
  }
}

void handle_power_off_confirmation()
{
  Log_info("Entering power-off confirmation mode");
  handle_confirmation_flow(in_power_off_confirmation, POWER_OFF_CONFIRM, confirm_power_off);
}

// Check if both left and right corners are being held
bool check_corners_gesture()
{
  // check updated values
  bool left  = iqs323.channel_touchState(IQS323_CH0);
  bool middle = iqs323.channel_touchState(IQS323_CH1);
  bool right = iqs323.channel_touchState(IQS323_CH2);
  Log_info("Hold edges: left=%d middle=%d right=%d tap_mode=%d", left, middle, right, touchbar_tap_mode);

  if (touchbar_tap_mode) {
    bool hold_left  = tap_mode_is_hold(0);
    bool hold_right = tap_mode_is_hold(2);
    Log_info("Hold edges tap mode: hold_left=%d hold_right=%d", hold_left, hold_right);
    return hold_left && hold_right;
  }
  Log_info("Hold edges slider mode: event=%d (HOLD=%d) left=%d right=%d", slider_event, IQS323_GESTURE_HOLD, left, right);
  return slider_event == IQS323_GESTURE_HOLD && left && right;
}

static void show_cached_image_by_offset(int offset) {
  String order = preferences.getString(PREFERENCES_PLAYLIST_ORDER_KEY, "");

  if (order.isEmpty()) {
    String path = (offset > 0)
      ? preferences.getString(PREFERENCES_CURRENT_PATH_KEY, "")
      : preferences.getString(PREFERENCES_LAST_PATH_KEY, "");
    if (path.isEmpty()) { Log_info("No cached image for gesture"); return; }
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

  if (count == 0) { Log_info("No cached images available"); return; }

  String browsePath = preferences.getString(PREFERENCES_BROWSE_PATH_KEY, "");
  if (browsePath.isEmpty()) {
    // Seed from last_path so first RIGHT shows curr_path (forward) and first LEFT shows older (backward).
    // Falls back to curr_path if last_path is absent (e.g. only one image cached).
    String lp = preferences.getString(PREFERENCES_LAST_PATH_KEY, "");
    browsePath = lp.isEmpty() ? preferences.getString(PREFERENCES_CURRENT_PATH_KEY, "") : lp;
  }

  int cur_idx = count - 1;
  for (int i = 0; i < count; i++) {
    if (browsePath == String(images[i])) { cur_idx = i; break; }
  }

  int new_idx = (cur_idx + offset + count) % count;
  Log_info("Playlist browse: %d/%d -> %d (%s)", cur_idx, count, new_idx, images[new_idx]);

  int file_size = 0;
  buffer = display_read_file(images[new_idx], &file_size);
  if (!buffer || file_size == 0) { Log_info("Failed to read %s", images[new_idx]); return; }

  preferences.putString(PREFERENCES_BROWSE_PATH_KEY, String(images[new_idx]));
  display_show_image(buffer, file_size, true);
  DisplayedImage::remember(images[new_idx]);
  goToSleep();
}

void check_channel_states(void)
{
  /* Loop through all the active channels */
  for (uint8_t i = 0; i < 3; i++) {
    if (iqs323.channel_touchState((iqs323_channel_e)(i))) {
      if (touchbar_tap_mode) {
        // Tap mode
        bool hold = tap_mode_is_hold(i, 2000);  // 2 second hold for tap mode actions
        switch (i) {
        case 0:
          if (!hold) {
            display_draw_touchbar_indicator(TOUCHBAR_LEFT, false);
            Log_info("Back button tapped");
            pending_indicator_side = TOUCHBAR_LEFT;
            pending_indicator_filled = false;
            has_pending_indicator = true;
            show_cached_image_by_offset(-1);
          } else {
            display_draw_touchbar_indicator(TOUCHBAR_LEFT, true);
            Log_info("Back button hold");
            pending_indicator_side = TOUCHBAR_LEFT;
            pending_indicator_filled = true;
            has_pending_indicator = true;
            show_cached_image_by_offset(-1);
          }
          break;
        case 1:
          if (hold) {
            display_draw_touchbar_indicator(TOUCHBAR_MIDDLE, true);
            Log_info("Middle button hold");
            pending_indicator_side = TOUCHBAR_MIDDLE;
            pending_indicator_filled = true;
            has_pending_indicator = true;
            // Log_info("Middle button held - OTG toggle");
            // if (otg_state) {
            //   otg_turn_off();
            //   showMessageWithLogo(OTG_TURNED_OFF); otg_state = false;
            // }
            // else {
            //   otg_turn_on();
            //   showMessageWithLogo(OTG_TURNED_ON);
            //   otg_state = true;
            // }
            // delay(1000);
            // showLastImageAndSleep();
          } else {
            display_draw_touchbar_indicator(TOUCHBAR_MIDDLE, false);
            Log_info("Middle button tapped");
            pending_indicator_side = TOUCHBAR_MIDDLE;
            pending_indicator_filled = false;
            has_pending_indicator = true;
          }
          break;
        case 2:
          if (!hold) {
            display_draw_touchbar_indicator(TOUCHBAR_RIGHT, false);
            Log_info("Next button tapped");
            pending_indicator_side = TOUCHBAR_RIGHT;
            pending_indicator_filled = false;
            has_pending_indicator = true;
            show_cached_image_by_offset(+1);
          } else {
            display_draw_touchbar_indicator(TOUCHBAR_RIGHT, true);
            Log_info("Next button hold");
            pending_indicator_side = TOUCHBAR_RIGHT;
            pending_indicator_filled = true;
            has_pending_indicator = true;
            show_cached_image_by_offset(+1);
          }
          break;
        }
      } else {
        // Slide mode
        if ((slider_event == IQS323_GESTURE_TAP || slider_event == IQS323_GESTURE_HOLD)) {
          printf("CH: %d: Touch\n", i);
          switch (i) {
          case 0:
            display_draw_touchbar_indicator(TOUCHBAR_LEFT, slider_event == IQS323_GESTURE_HOLD);
            Log_info("Back button pressed");
            break;
          case 1:
            display_draw_touchbar_indicator(TOUCHBAR_MIDDLE, slider_event == IQS323_GESTURE_HOLD);
            Log_info("Middle button pressed");
            // if (otg_state) {
            //   otg_turn_off();
            //   showMessageWithLogo(OTG_TURNED_OFF); otg_state = false;
            // }
            // else {
            //   otg_turn_on();
            //   showMessageWithLogo(OTG_TURNED_ON);
            //   otg_state = true;
            // }
            // delay(1000);
            // showLastImageAndSleep();
            break;
          case 2:
            display_draw_touchbar_indicator(TOUCHBAR_RIGHT, slider_event == IQS323_GESTURE_HOLD);
            Log_info("Next button pressed");
            break;
          }
        }
      }
    }
  }
}

void read_slider_coordinates(void)
{
  /* read slider coordinates from memory */
  uint16_t buffer = iqs323.sliderCoordinate();

  if(buffer != slider_position)
  {
    slider_position = buffer;
  }
}

/* Function to process Slider gesture events */
void read_gesture_event(void)
{
  /* Read slider bit to check if a slider event occurred */
  bool gesture_event = iqs323.getSliderEvent();
  printf("Gesture event: %d\n", gesture_event);
  if (gesture_event)
  {
    /* returns slider event that occurred (tap, swipe or flick) by reading event bits from MM */
    iqs323_gesture_events gesture_buffer = iqs323.getGestureType();
    printf("Gesture type: %d\n", gesture_buffer);
    if(gesture_buffer != IQS323_GESTURE_NONE)
    {
      slider_event = gesture_buffer;
      switch (slider_event)
      {
        case IQS323_GESTURE_UNKNOWN:
          Log_info("SLIDER: UNKNOWN (something went wrong?)");
          break;
        case IQS323_GESTURE_TAP:
          Log_info("SLIDER: Tap");
          break;
        case IQS323_GESTURE_SWIPE_NEGATIVE:
          Log_info("SLIDER: Swipe <-");
          if (!touchbar_tap_mode) {
            show_cached_image_by_offset(-1);
          }
          break;
        case IQS323_GESTURE_SWIPE_POSITIVE:
          Log_info("SLIDER: Swipe ->");
          if (!touchbar_tap_mode) {
            show_cached_image_by_offset(+1);
          }
          break;
        case IQS323_GESTURE_FLICK_NEGATIVE:
          Log_info("SLIDER: Flick <-");
          break;
        case IQS323_GESTURE_FLICK_POSITIVE:
          Log_info("SLIDER: Flick ->");
          break;
        case IQS323_GESTURE_HOLD:
          Log_info("SLIDER: Hold");
          break;
        case IQS323_GESTURE_NONE:
          Log_info("SLIDER: None");
          break;
      }

      /* Clear event if a finger is removed from slider after the event was processed */
      if (slider_position == 65535)
      {
        slider_event = IQS323_GESTURE_NONE;
      }
    }
  }
}
void process_iqs323_data(void)
{
  /* Read slider coordinates from memory */
  uint16_t buffer = iqs323.sliderCoordinate();

  if(buffer != slider_position)
  {
    slider_position = buffer;
  }

  Log_info("Slider position: %d", slider_position);

  iqs323_task_i2c_lock();

  /* Read gesture event if available */
  read_gesture_event();

  iqs323_task_i2c_unlock();

  if (!in_wifi_reset_confirmation && check_corners_gesture()) {
    handle_wifi_reset_confirmation();
    return;
  }

  iqs323_task_i2c_lock();

  /* Check channel touch states */
  check_channel_states();

  iqs323_task_i2c_unlock();
}
// ############################ SLIDER ################################

void touchbarPrepareCaptivePortal(void)
{
  iqs323_task_i2c_lock();
  iqs323.setGestureConfig(true, STOP);
  iqs323_task_i2c_unlock();
  touchbar_tap_mode = true;
}

void touchbarPortalTick(void)
{
  if (in_power_off_confirmation)
    return;
  if (millis() < s_power_off_cooldown_until)
    return;

  static uint32_t s_corners_start_ms = 0;

  iqs323_task_i2c_lock();
  if (iqs323.getRDYStatus()) {
    iqs323.updateInfoFlags(STOP);
  }
  bool left = iqs323.channel_touchState(IQS323_CH0);
  bool right = iqs323.channel_touchState(IQS323_CH2);
  if (left && right) {
    if (!s_corners_detected) {
      s_corners_start_ms = millis();
      s_corners_detected = true;
    } else if (millis() - s_corners_start_ms >= 600) {
      s_corners_detected = false;
      handle_power_off_confirmation();
      s_power_off_cooldown_until = millis() + 2000;
      iqs323_task_i2c_unlock();
      showMessageWithLogo(WIFI_CONNECT, "", false, Messages::firmware_version().c_str(),
                          WifiCaptivePortal.getAPSSID());
      return;
    }
  } else {
    s_corners_detected = false;
  }
  iqs323_task_i2c_unlock();
}

void touchbarSessionLoadMode(void)
{
  touchbar_tap_mode = preferences.getBool(PREFERENCES_TOUCHBAR_MODE_KEY, true);
  Log_info("Touchbar mode from preferences: %s", touchbar_tap_mode ? "Tap" : "Slide");
}

void touchbarSessionStartTask(bool gpio_wakeup)
{
  if (!iqs323_task_init(NULL)) {
    Log_error("IQS323 Task: Failed to start - rebooting");
    delay(1000);
    ESP.restart();
  }
  if (!iqs323_task_wait_ready(5000)) {
    Log_error("IQS323 Task: Initialization timeout - rebooting");
    delay(1000);
    ESP.restart();
  }
  if (gpio_wakeup) {
    process_iqs323_data();
  }
}

void touchbarNotifyGpioWakeup(bool gpio_wakeup)
{
  if (gpio_wakeup) {
    Log_info("GPIO wakeup detected - using wake stub data");
    iqs323_task_notify_gpio_wakeup(true);
  } else {
    Log_info("Non-GPIO wakeup");
  }
}

void touchbarRedrawPendingIndicator(void)
{
  if (has_pending_indicator) {
    display_draw_touchbar_indicator(pending_indicator_side, pending_indicator_filled);
    has_pending_indicator = false;
  }
}

bool touchbarHasOtgMessage(void)
{
  return otg_message;
}

#endif // BOARD_TRMNL_X
