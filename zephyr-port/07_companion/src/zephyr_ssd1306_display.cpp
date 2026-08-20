#include "zephyr_ssd1306_display.h"
#include <zephyr/display/cfb.h>
#include <zephyr/drivers/display.h>
#include <string.h>

ZephyrSSD1306Display::ZephyrSSD1306Display()
  : DisplayDriver(128, 64), _dev(nullptr), _cx(0), _cy(0),
    _font_small(0), _font_big(0), _font_w(6), _font_h(8), _on(false), _color(LIGHT) {}

bool ZephyrSSD1306Display::begin() {
  _dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_display));
  if (!device_is_ready(_dev) || cfb_framebuffer_init(_dev) != 0) { _dev = nullptr; return false; }

  // pick the smallest CFB font for text size 1, and the tallest font up to ~2.5x it for size 2
  int n = cfb_get_numof_fonts(_dev);
  uint8_t small_h = 255, big_h = 0, w, h;
  for (int i = 0; i < n; i++)
    if (cfb_get_font_size(_dev, i, &w, &h) == 0 && h < small_h) { small_h = h; _font_small = i; }
  _font_big = _font_small;
  for (int i = 0; i < n; i++)
    if (cfb_get_font_size(_dev, i, &w, &h) == 0 && h > big_h && h <= small_h * 5 / 2) { big_h = h; _font_big = i; }

  setTextSize(1);
  display_blanking_off(_dev);
  cfb_framebuffer_clear(_dev, true);
  _on = true;
  return true;
}

void ZephyrSSD1306Display::turnOn()  { if (_dev) { display_blanking_off(_dev); _on = true; } }
void ZephyrSSD1306Display::turnOff() { if (_dev) { display_blanking_on(_dev);  _on = false; } }
void ZephyrSSD1306Display::clear()   { if (_dev) cfb_framebuffer_clear(_dev, true); }

// a frame is: startFrame() [clear the RAM buffer] -> draw ops -> endFrame() [push to panel]
void ZephyrSSD1306Display::startFrame(Color) { _color = LIGHT; if (_dev) cfb_framebuffer_clear(_dev, false); }
void ZephyrSSD1306Display::endFrame() { if (_dev) cfb_framebuffer_finalize(_dev); }

void ZephyrSSD1306Display::setTextSize(int sz) {
  if (!_dev) return;
  // Keep it all small until the layout is retuned for the
  // available fonts. To restore two sizes: `uint8_t idx = (sz >= 2) ? _font_big : _font_small;`
  (void)sz;
  uint8_t idx = _font_small;
  if (cfb_framebuffer_set_font(_dev, idx) == 0) cfb_get_font_size(_dev, idx, &_font_w, &_font_h);
}

// Use cfb_draw_text (NOT cfb_print): cfb_print wraps the overflow onto the next 8px line,
// which stacked the scrolling status bar across the first few rows. Off-screen pixels are clipped.
void ZephyrSSD1306Display::print(const char* str) { if (_dev) cfb_draw_text(_dev, str, _cx, _cy); }
uint16_t ZephyrSSD1306Display::getTextWidth(const char* str) { return (uint16_t)strlen(str) * _font_w; }

// monochrome panel: a filled rect is an inverted region (on the dark-cleared frame == light fill)
void ZephyrSSD1306Display::fillRect(int x, int y, int w, int h) {
  if (_dev) cfb_invert_area(_dev, (int16_t)x, (int16_t)y, (uint16_t)w, (uint16_t)h);
}
void ZephyrSSD1306Display::drawRect(int x, int y, int w, int h) {
  if (!_dev) return;
  struct cfb_position a = { (int16_t)x, (int16_t)y }, b = { (int16_t)(x + w - 1), (int16_t)(y + h - 1) };
  cfb_draw_rect(_dev, &a, &b);
}
void ZephyrSSD1306Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  if (!_dev) return;
  int stride = (w + 7) / 8;                       // XBM: rows byte-padded, LSB = leftmost pixel
  for (int row = 0; row < h; row++)
    for (int col = 0; col < w; col++)
      if (bits[row * stride + (col >> 3)] & (1 << (col & 7))) {
        struct cfb_position p = { (int16_t)(x + col), (int16_t)(y + row) };
        cfb_draw_point(_dev, &p);
      }
}
