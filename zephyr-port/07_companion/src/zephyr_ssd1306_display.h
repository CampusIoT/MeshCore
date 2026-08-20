#pragma once
/* MeshCore DisplayDriver backed by Zephyr's NATIVE SSD1306 driver + Character Framebuffer
 * (CFB); no Arduino Wire / Adafruit_GFX. This is the only new piece the OLED needs: the
 * common ui-tiny UITask draws to the abstract DisplayDriver interface and is reused unchanged.
 * The panel is the `zephyr,display` chosen node (oled.overlay): an SSD1306 on the XIAO I2C bus. */

#include <helpers/ui/DisplayDriver.h>
#include <zephyr/device.h>

class ZephyrSSD1306Display : public DisplayDriver {
  const struct device* _dev;
  int16_t _cx, _cy;
  uint8_t _font_small, _font_big, _font_w, _font_h;
  bool    _on;
  uint8_t _color;

public:
  ZephyrSSD1306Display();
  bool begin();                                  // returns false if no display is attached

  bool isOn() override { return _on; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override { _color = c; }
  void setCursor(int x, int y) override { _cx = (int16_t)x; _cy = (int16_t)y; }
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
