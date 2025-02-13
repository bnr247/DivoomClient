// ------VERSIONS------
// Arduinojson v6.21.5
// ESP32 board v2.05
//---------------------

#include "configs.h"

#include <ArduinoHttpClient.h>
#include <AsyncTCP_SSL.h>
#include <DivoomClient.h>

#include <ColorConverterLib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <FastED.h>

#define DIVOOM_PER_PAGE 5

#define PIN_LEDS 5
#define LED_RES_X 16  // Number of pixels wide of each INDIVIDUAL panel module.
#define LED_RES_Y 16  // Number of pixels tall of each INDIVIDUAL panel module.
#define NUM_ROWS 1    // Number of rows of chained INDIVIDUAL PANELS
#define NUM_COLS 1    // Number of INDIVIDUAL PANELS per ROW

#define LED_BRIGHTNESS 32

const uint8_t MATRIX_WIDTH = LED_RES_X * NUM_COLS;
const uint8_t MATRIX_HEIGHT = LED_RES_Y * NUM_ROWS;
const uint16_t NUM_LEDS = (MATRIX_WIDTH * MATRIX_HEIGHT);
const uint16_t xybuff = (LED_RES_X * LED_RES_Y);

CRGB leds[NUM_LEDS];

WiFiClient wifi;

DivoomClient divoom_client(wifi, DIVOOM_EMAIL, DIVOOM_MD5_PASSWORD);

byte* tmp_frames_data;
byte rendering_frames_data[DIVOOM_ALL_FRAMES_SIZE];

int32_t center_x, center_y, start_x, start_y;

int scroll_position;

bool next_gif_ready = false;
bool request_next_gif = false;

DivoomFileInfoLite divoom_files_list[DIVOOM_PER_PAGE];
uint8_t divoom_files_list_count = 0;
uint8_t divoom_current_page = 1;
uint8_t divoom_current_gif_index = -1;
DivoomPixelBeanHeader divoom_pixel_bean_header;

unsigned long watchdog_millis;

uint16_t XY(uint8_t x, uint8_t y) {
  if (x >= MATRIX_WIDTH || x < 0 || y >= MATRIX_HEIGHT || y < 0) return -1;
  uint16_t i;
  uint8_t yi = y / LED_RES_Y;
  y %= LED_RES_Y;
  if (yi & 0x01) {
    if (x & 0x01) {
      uint8_t reverseY = (LED_RES_Y - 1) - y;
      i = (((LED_RES_X * NUM_COLS) - x - 1) * LED_RES_Y) + reverseY;
    } else {
      i = (((LED_RES_X * NUM_COLS) - x - 1) * LED_RES_Y) + y;
    }
  } else {
    if (x & 0x01) {
      uint8_t reverseY = (LED_RES_Y - 1) - y;
      i = (x * LED_RES_Y) + reverseY;
    } else {
      i = (x * LED_RES_Y) + y;
    }
  }
  i += xybuff * yi * NUM_COLS;
  return i;
}

void OnParseSuccess(DivoomPixelBeanHeader header) {
  Serial.println("Success");
  divoom_pixel_bean_header = header;
  next_gif_ready = true;
}

void OnParseError(int8_t error) {
  Serial.print("ERROR: ");
  Serial.println(error);
  next_gif_ready = false;
  request_next_gif = true;
}

void renderFrames(void* param) {
  while (!next_gif_ready) {
    delay(1000);
  }

  uint8_t local_total_frames;
  int local_speed;

  uint8_t frame;
  uint32_t i;
  uint8_t x, y;

  uint8_t grid_x, grid_y;
  uint8_t draw_x, draw_y;

  uint8_t red, green, blue;
  double hue, saturation, lighting, value;
  uint16_t pixel_color;

  int max_times;
  int times;

  uint32_t start_pos;
  uint32_t real_i;

  while (true) {
    local_total_frames = divoom_pixel_bean_header.total_frames;
    local_speed = min(100, max(50, (int)divoom_pixel_bean_header.speed));

    memcpy(rendering_frames_data, tmp_frames_data, local_total_frames * DIVOOM_FRAME_SIZE);

    frame = 0;
    i = 0;

    max_times = (int)(7000 / local_speed / local_total_frames);
    max_times = max(3, max_times);
    times = 1;

    request_next_gif = true;
    while (true) {
      ++frame;
      if (frame > local_total_frames) {
        ++times;
        if (times >= max_times and next_gif_ready) {
          break;
        }
        frame = 1;
      }

      start_pos = (frame - 1) * DIVOOM_FRAME_SIZE;

      i = 0;
      x = 0;
      y = 0;

      grid_x = 0;
      grid_y = 0;

      while (i <= DIVOOM_FRAME_SIZE - 3) {
        real_i = start_pos + i;
        red = rendering_frames_data[real_i];
        green = rendering_frames_data[real_i + 1];
        blue = rendering_frames_data[real_i + 2];

        draw_x = start_x + (grid_x * 16) + x * 1;
        draw_y = start_y + (grid_y * 16) + y * 1;

        leds[XY(draw_x, draw_y)] = applyGamma_video(CRGB(red, green, blue), 2.6, 2.2, 2.5);

        i += 3;
        ++x;

        if ((i / 3) % 16 == 0) {
          x = 0;
          ++y;
        }
      }

      vTaskDelay(local_speed);
    }
  }
}

void requestNextGif() {
  watchdog_millis = millis();
  next_gif_ready = false;

  divoom_current_page = random(1, 500);
  ++divoom_current_gif_index;

  if (divoom_files_list_count == 0 || divoom_current_gif_index >= divoom_files_list_count) {
    int category_id = random(0, 35);
    Serial.print("Category: ");
    Serial.println(category_id);
    Serial.print("Page: ");
    Serial.println(divoom_current_page);
    divoom_client.GetCategoryFileList(divoom_files_list, &divoom_files_list_count, category_id, divoom_current_page, DIVOOM_PER_PAGE);
    divoom_current_gif_index = -1;
    request_next_gif = true;
    return;
  }

  divoom_client.ParseFile(divoom_files_list[divoom_current_gif_index].file_id, tmp_frames_data);
}

void setup() {
  FastLED.addLeds<WS2812B, PIN_LEDS, GRB>(leds, NUM_LEDS).setCorrection(TypicalSMD5050).setDither(0);
  FastLED.setBrightness(LED_BRIGHTNESS);
  FastLED.setMaxRefreshRate(0);

  center_x = LED_RES_X / 2;
  center_y = LED_RES_Y / 2;

  start_x = floor(center_x - LED_RES_X / 2);
  start_y = floor(center_y - LED_RES_Y / 2);

  Serial.begin(115200);

  WiFi.setSleep(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print(F("Connecting to network "));
  Serial.print(WIFI_SSID);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(F("."));
  }

  Serial.println(F(" WiFi connected"));
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  xTaskCreate(renderFrames, "renderFrames", DIVOOM_ALL_FRAMES_SIZE + 2000, NULL, 1, NULL);

  randomSeed(analogRead(0));

  while (!divoom_client.LogIn()) {
    Serial.println(F("Logging in..."));
  }

  divoom_client.OnParseSuccess(OnParseSuccess);
  divoom_client.OnParseError(OnParseError);

  tmp_frames_data = (byte*)malloc(DIVOOM_ALL_FRAMES_SIZE);

  request_next_gif = true;
}

void loop() {
  FastLED.show();

  if (millis() - watchdog_millis >= 30000) {
    ESP.restart();
  }

  if (request_next_gif) {
    request_next_gif = false;
    requestNextGif();
    Serial.print("Requesting GIF ");
    Serial.println(divoom_current_gif_index);
  }
}