#include "led_ll.h"
#include <furi_hal_light.h>
#include <furi.h>
#include <stdint.h>

#define TAG "LED_LL"

static uint32_t rgb_buf[LED_COUNT];
static uint32_t *rgb = rgb_buf;

static uint8_t brightness_i = 2; // Default to mid-brightness

static uint8_t apply_brightness(uint8_t v);
static uint8_t apply_gamma(uint8_t v);

void led_init()
{
  FURI_LOG_D(TAG, "led_init called");
  led_clear();
  led_update();
}

void led_deinit()
{
  FURI_LOG_D(TAG, "led_deinit called");
  led_clear();
  led_update();
}

int8_t led_update()
{
  uint16_t hardware_led_count = furi_hal_light_pixel_count();
  uint16_t update_count = (hardware_led_count < LED_COUNT) ? hardware_led_count : LED_COUNT;

  for (uint16_t i = 0; i < update_count; i++)
  {
    uint32_t v = rgb[i];
    uint8_t r = RGB_R(v);
    uint8_t g = RGB_G(v);
    uint8_t b = RGB_B(v);

    r = apply_gamma(r);
    r = apply_brightness(r);

    g = apply_gamma(g);
    g = apply_brightness(g);

    b = apply_gamma(b);
    b = apply_brightness(b);

    furi_hal_light_set_pixel(i, r, g, b);
  }

  // If there are more hardware LEDs than our app buffer, clear the rest
  for (uint16_t i = update_count; i < hardware_led_count; i++)
  {
    furi_hal_light_set_pixel(i, 0, 0, 0);
  }

  furi_hal_light_refresh();
  return 0;
}

uint8_t led_get_brightness() { return brightness_i; }

uint32_t led_get(uint16_t i) { 
  if (i >= LED_COUNT) return 0;
  return rgb[i]; 
}

void led_set(uint16_t i, uint32_t v) { 
  if (i < LED_COUNT) {
    rgb[i] = v; 
  }
}

void led_set_rgb(uint16_t i, uint32_t r, uint32_t g, uint32_t b)
{
  if (i >= LED_COUNT)
  {
    return;
  }

  if (r > 255) r = 255;
  if (g > 255) g = 255;
  if (b > 255) b = 255;

  led_set(i, RGB_UINT(r, g, b));
}

void led_set_rgbf(uint16_t i, float r, float g, float b)
{
  if (r < 0) r = 0;
  if (g < 0) g = 0;
  if (b < 0) b = 0;

  if (r > 1) r = 1;
  if (g > 1) g = 1;
  if (b > 1) b = 1;

  led_set_rgb(i, r * 255, g * 255, b * 255);
}

void led_clear()
{
  for (uint32_t i = 0; i < LED_COUNT; i++)
  {
    led_set(i, 0);
  }
}

void led_set_brightness(uint8_t brightness)
{
  if (brightness > LED_BRIGHTNESS_MAX - 1)
    brightness = LED_BRIGHTNESS_MAX - 1;

  brightness_i = brightness;
}

static uint8_t apply_brightness(uint8_t v)
{
  static uint8_t brightness_table[LED_BRIGHTNESS_MAX] = {50, 25, 12};

  return (uint16_t)v *
         brightness_table[(LED_BRIGHTNESS_MAX - 1) - brightness_i] / 255;
}

static uint8_t apply_gamma(uint8_t v)
{
  static uint8_t gamma_table[256] = {
      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
      0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 2,
      2, 2, 2, 2, 3, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5,
      5, 5, 6, 6, 6, 7, 7, 7, 8, 8, 8, 9, 9, 9, 10,
      10, 10, 11, 11, 12, 12, 13, 13, 13, 14, 14, 15, 15, 16, 16,
      17, 17, 18, 18, 19, 19, 20, 21, 21, 22, 22, 23, 23, 24, 25,
      25, 26, 27, 27, 28, 29, 29, 30, 31, 31, 32, 33, 33, 34, 35,
      36, 36, 37, 38, 39, 40, 40, 41, 42, 43, 44, 45, 45, 46, 47,
      48, 49, 50, 51, 52, 53, 54, 55, 55, 56, 57, 58, 59, 60, 61,
      62, 63, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 77, 78,
      79, 80, 81, 82, 84, 85, 86, 87, 88, 90, 91, 92, 93, 95, 96,
      97, 99, 100, 101, 103, 104, 105, 107, 108, 109, 111, 112, 114, 115, 117,
      118, 119, 121, 122, 124, 125, 127, 128, 130, 131, 133, 135, 136, 138, 139,
      141, 142, 144, 146, 147, 149, 151, 152, 154, 156, 157, 159, 161, 162, 164,
      166, 168, 169, 171, 173, 175, 176, 178, 180, 182, 184, 186, 187, 189, 191,
      193, 195, 197, 199, 201, 203, 205, 207, 209, 211, 213, 215, 217, 219, 221,
      223, 225, 227, 229, 231, 233, 235, 237, 239, 241, 244, 246, 248, 250, 252,
      255};

  return gamma_table[v];
}
