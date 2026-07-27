#include <api_setup_session.h>

#include <Arduino.h>
#include <ArduinoLog.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WifiCaptive.h>
#include <api-client/request_headers.h>
#include <api-client/setup.h>
#include <api_response_parsing.h>
#include <config.h>
#include <device_id.h>
#include <display.h>
#include <filesystem.h>
#include <http_client.h>
#include <trmnl_log.h>
#include <types.h>

#ifdef BOARD_TRMNL_X
#include <modem.h>
#endif

// --- State and helpers owned by bl.cpp ---
extern Preferences preferences;
extern char filename[1024];
extern char message_buffer[128];
extern bool status;
extern uint8_t *buffer;
extern RTC_DATA_ATTR uint8_t need_to_refresh_display;

uint32_t downloadStream(WiFiClient *stream, int content_size, uint8_t *buffer);
void writeImageToFile(const char *name, uint8_t *in_buffer, size_t size);
void showMessageWithLogo(MSG message_type);
void showMessageWithLogo(MSG message_type, const ApiSetupResponse &apiResponse);
uint8_t *storedLogoOrDefault(int iType);
void goToSleep(void);

#ifdef BOARD_TRMNL_X
extern Modem *g_modem;
#endif

static bool performApiSetup()
{
  // Set up the API inputs
  ApiSetupInputs inputs;
  inputs.baseUrl = preferences.getString(PREFERENCES_API_URL, API_BASE_URL);
  inputs.macAddress = device_mac_address();
  inputs.firmwareVersion = FW_VERSION_STRING;
  inputs.model = String(DEVICE_MODEL);

  Log.info("%s [%d]: [HTTPS] begin /api/setup ...\r\n", __FILE__, __LINE__);
  Log.info("%s [%d]: RSSI: %d\r\n", __FILE__, __LINE__, WiFi.RSSI());
  Log.info("%s [%d]: Device MAC address: %s\r\n", __FILE__, __LINE__, inputs.macAddress.c_str());

  ApiSetupResult result;
  // Call the API client
#ifdef BOARD_TRMNL_X
  if (g_modem && WifiCaptivePortal.getLastCredentials().is5GHz)
  {
    Log_info("API setup via modem (5 GHz path)");
    String reqHeaders = formatHeaders(buildSetupHeaders(inputs));
    auto httpRes = g_modem->httpGet(inputs.baseUrl + "/api/setup", "", 0, reqHeaders);
    if (!httpRes.ok)
    {
      Log_error_submit("[MODEM] /api/setup request failed (%u bytes received)", httpRes.bytesReceived);
      result = {HTTPS_RESPONSE_CODE_INVALID, {}, "Modem httpGet failed"};
    }
    else
    {
      auto apiResp = parseResponse_apiSetup(httpRes.body);
      if (apiResp.outcome == ApiSetupOutcome::DeserializationError)
        result = {HTTPS_JSON_PARSING_ERR, {}, "JSON deserialization error"};
      else
        result = {HTTPS_NO_ERR, apiResp, ""};
    }
  }
  else
#endif // BOARD_TRMNL_X
  {
    result = fetchApiSetup(inputs);
  }
  // Handle connection errors
  if (result.error == HTTPS_UNABLE_TO_CONNECT)
  {
    showMessageWithLogo(WIFI_INTERNAL_ERROR);
    Log_error_submit("[HTTPS] %s", result.error_detail.c_str());
    return false;
  }

  // Handle JSON parsing errors
  if (result.error == HTTPS_JSON_PARSING_ERR)
  {
    Log.error("%s [%d]: JSON deserialization error.\r\n", __FILE__, __LINE__);
    return false;
  }

  // Handle HTTP request errors
  if (result.error != HTTPS_NO_ERR)
  {
    if (WiFi.RSSI() > WIFI_CONNECTION_RSSI)
    {
      showMessageWithLogo(API_SETUP_FAILED);
    }
    else
    {
      showMessageWithLogo(WIFI_WEAK);
    }
    Log_error_submit("[HTTPS] Request failed: %s", result.error_detail.c_str());
    return false;
  }

  // Process the successful response
  auto &apiResponse = result.response;
  uint16_t url_status = apiResponse.status;

  Log.info("%s [%d]: GET... code: %d\r\n", __FILE__, __LINE__, url_status);

  if (url_status == 200)
  {
    status = true;
    Log.info("%s [%d]: status OK.\r\n", __FILE__, __LINE__);

    String api_key = apiResponse.api_key;
    Log.info("%s [%d]: API key - %s\r\n", __FILE__, __LINE__, api_key.c_str());
    size_t res = preferences.putString(PREFERENCES_API_KEY, api_key);
    Log.info("%s [%d]: api key saved in the preferences - %d\r\n", __FILE__, __LINE__, res);

    String friendly_id = apiResponse.friendly_id;
    Log.info("%s [%d]: friendly ID - %s\r\n", __FILE__, __LINE__, friendly_id.c_str());
    res = preferences.putString(PREFERENCES_FRIENDLY_ID, friendly_id);
    Log.info("%s [%d]: friendly ID saved in the preferences - %d\r\n", __FILE__, __LINE__, res);

    String image_url = apiResponse.image_url;
    Log.info("%s [%d]: image_url - %s\r\n", __FILE__, __LINE__, image_url.c_str());
    image_url.toCharArray(filename, image_url.length() + 1);

    String message_str = apiResponse.message;
    Log.info("%s [%d]: message - %s\r\n", __FILE__, __LINE__, message_str.c_str());
    message_str.toCharArray(message_buffer, message_str.length() + 1);

    Log.info("%s [%d]: status - %d\r\n", __FILE__, __LINE__, status);
    return true;
  }
  else if (url_status == 404)
  {
    Log_info("MAC Address is not registered on server");

    showMessageWithLogo(MAC_NOT_REGISTERED, apiResponse);

    preferences.putUInt(PREFERENCES_SLEEP_TIME_KEY, SLEEP_TIME_TO_SLEEP);

    display_sleep();
    goToSleep();
    return false;
  }
  else
  {
    Log.info("%s [%d]: status FAIL.\r\n", __FILE__, __LINE__);
    status = false;
    return false;
  }
}

static void downloadSetupImage()
{
  status = false;
  Log.info("%s [%d]: filename - %s\r\n", __FILE__, __LINE__, filename);

  #ifdef BOARD_TRMNL_X
  if (WifiCaptivePortal.getLastCredentials().is5GHz && g_modem)
  {
    Log_info("Downloading setup image via modem (5 GHz path)");
    auto httpRes = g_modem->httpGet(String(filename), "/logo.bmp");
    if (!httpRes.ok || httpRes.bytesReceived != DISPLAY_BMP_IMAGE_SIZE)
    {
      Log_error_submit("Modem logo download failed: ok=%d bytes=%u expected=%u",
                       httpRes.ok, httpRes.bytesReceived, DISPLAY_BMP_IMAGE_SIZE);
      filesystem_file_delete("/logo.bmp");
    }
    String friendly_id = preferences.getString(PREFERENCES_FRIENDLY_ID, PREFERENCES_FRIENDLY_ID_DEFAULT);
    display_show_msg(storedLogoOrDefault(0), FRIENDLY_ID, friendly_id, true, "", String(message_buffer));
    need_to_refresh_display = 0;
    return;
  }
#endif // BOARD_TRMNL_X

  withHttp(filename, [&](HTTPClient *https, HttpError error) -> bool
           {
    if (error != HttpError::HTTPCLIENT_SUCCESS)
    {
      if (WiFi.RSSI() > WIFI_CONNECTION_RSSI)
      {
        showMessageWithLogo(API_IMAGE_DOWNLOAD_ERROR);
      }
      else
      {
        showMessageWithLogo(WIFI_WEAK);
      }
      Log_error_submit("[HTTPS] Unable to connect");
      return false;
    }

    https->setTimeout(15000);
    https->setConnectTimeout(15000);

    Log.info("%s [%d]: [HTTPS] Request to %s\r\n", __FILE__, __LINE__, filename);
    Log.info("%s [%d]: [HTTPS] GET..\r\n", __FILE__, __LINE__);

    int httpCode = https->GET();

    if(httpCode == HTTP_CODE_PERMANENT_REDIRECT ||httpCode == HTTP_CODE_TEMPORARY_REDIRECT){
              https->end();
              https->begin(https->getLocation());
              Log_info("Redirected to: %s", https->getLocation().c_str());
              https->setTimeout(15000);
              https->setConnectTimeout(15000);
              httpCode = https->GET();
            }

    // httpCode will be negative on error
    if (httpCode <= 0)
    {
      if (WiFi.RSSI() > WIFI_CONNECTION_RSSI)
      {
        showMessageWithLogo(API_IMAGE_DOWNLOAD_ERROR);
      }
      else
      {
        showMessageWithLogo(WIFI_WEAK);
      }
      Log_error_submit("[HTTPS] GET... failed, error: %s", https->errorToString(httpCode).c_str());
      return false;
    }

    // HTTP header has been send and Server response header has been handled
    Log.info("%s [%d]: [HTTPS] GET... code: %d\r\n", __FILE__, __LINE__, httpCode);
    
    // file found at server
    if (httpCode != HTTP_CODE_OK && httpCode != HTTP_CODE_MOVED_PERMANENTLY)
    {
      if (WiFi.RSSI() > WIFI_CONNECTION_RSSI)
      {
        showMessageWithLogo(API_IMAGE_DOWNLOAD_ERROR);
      }
      else
      {
        showMessageWithLogo(WIFI_WEAK);
      }
      Log_error_submit("[HTTPS] GET... failed, error: %s", https->errorToString(httpCode).c_str());
      return false;
    }

    Log.info("%s [%d]: Content size: %d\r\n", __FILE__, __LINE__, https->getSize());

    WiFiClient *stream = https->getStreamPtr();

    uint32_t counter = 0;
#ifdef BOARD_TRMNL_X
    int contentSize = https->getSize();
    buffer = (uint8_t *)malloc(contentSize > 0 ? contentSize : 1);
    if (buffer == nullptr)
    {
      Log_error_submit("Failed to allocate buffer for setup image (%d bytes)", contentSize);
      return false;
    }
    if (stream->available() && contentSize > 0)
    {
      counter = downloadStream(stream, contentSize, buffer);
    }
#else
    // Read and save image data to buffer (BMP or PNG)
    int contentSize = https->getSize();
    buffer = (uint8_t *)malloc(contentSize > 0 ? contentSize : 1);
    if (buffer == nullptr)
    {
      Log_error_submit("Failed to allocate buffer for setup image (%d bytes)", contentSize);
      return false;
    }
    if (stream->available() && contentSize > 0)
    {
      counter = downloadStream(stream, contentSize, buffer);
    }
#endif

    if (counter == DISPLAY_BMP_IMAGE_SIZE)
    {
      Log.info("%s [%d]: Received successfully\r\n", __FILE__, __LINE__);

      writeImageToFile("/logo.bmp", buffer, DEFAULT_IMAGE_SIZE);

      // show the image
      String friendly_id = preferences.getString(PREFERENCES_FRIENDLY_ID, PREFERENCES_FRIENDLY_ID_DEFAULT);
      display_show_msg(storedLogoOrDefault(0), FRIENDLY_ID, friendly_id, true, "", String(message_buffer));
      need_to_refresh_display = 0;
    }
#ifdef BOARD_TRMNL_X 
    else if (counter >= 4 && buffer[0] == 0x89 && buffer[1] == 'P' && buffer[2] == 'N' && buffer[3] == 'G')
    {       
      Log.info("%s [%d]: Received PNG setup logo (%d bytes)\r\n", __FILE__, __LINE__, counter);
      writeImageToFile("/logo.png", buffer, counter);
      free(buffer);
      buffer = nullptr;

      // show the image
      String friendly_id = preferences.getString(PREFERENCES_FRIENDLY_ID, PREFERENCES_FRIENDLY_ID_DEFAULT);
      display_show_msg(storedLogoOrDefault(0), FRIENDLY_ID, friendly_id, true, "", String(message_buffer));
      need_to_refresh_display = 0;
    }
    else
    {
      free(buffer);
      buffer = nullptr;
      Log_error_submit("Setup image: unexpected format or size. Read: %d bytes (expected BMP %d)", counter, DISPLAY_BMP_IMAGE_SIZE);
    }
#else
    else if (counter >= 4 && buffer[0] == 0x89 && buffer[1] == 'P' && buffer[2] == 'N' && buffer[3] == 'G')
    {
      Log.info("%s [%d]: Received PNG setup logo (%d bytes)\r\n", __FILE__, __LINE__, counter);
      writeImageToFile("/logo.png", buffer, counter);
      free(buffer);
      buffer = nullptr;

      String friendly_id = preferences.getString(PREFERENCES_FRIENDLY_ID, PREFERENCES_FRIENDLY_ID_DEFAULT);
      display_show_msg(storedLogoOrDefault(0), FRIENDLY_ID, friendly_id, true, "", String(message_buffer));
      need_to_refresh_display = 0;
    }
    else
    {
      if (buffer) {
        free(buffer);
        buffer = nullptr;
      }
      if (WiFi.RSSI() > WIFI_CONNECTION_RSSI)
      {
        showMessageWithLogo(API_SIZE_ERROR);
      }
      else
      {
        showMessageWithLogo(WIFI_WEAK);
      }
      Log_error_submit("Receiving failed. Read: %d", counter);
    }
#endif // !BOARD_TRMNL_X    
    return true; });
}

void getDeviceCredentials()
{
  bool shouldDownloadImage = performApiSetup();

  Log.info("%s [%d]: status - %d\r\n", __FILE__, __LINE__, status);
  if (shouldDownloadImage)
  {
    downloadSetupImage();
  }
}

