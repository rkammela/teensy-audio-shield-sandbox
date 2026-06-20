/*
 * OledStatus - tiny status-screen helpers for a 128x64 SSD1306 OLED.
 * Renders an "AURA"-style yellow title band plus a few ready-made body
 * layouts (loading bar, single splash line, mode + hint with word wrap).
 *
 * The library owns its Adafruit_SSD1306 instance; the host project just
 * calls begin() once and the higher-level show*() helpers afterwards.
 */

#ifndef AURA_OLED_STATUS_H
#define AURA_OLED_STATUS_H

#include <Arduino.h>

namespace OledStatus {

  // Initialize the SSD1306 on the default Wire bus at the given I2C
  // address. Returns true on success, false if the controller did not
  // respond (caller can keep running headless).
  bool begin(uint8_t i2cAddr);

  // Boot-time progress screen. Yellow band keeps the "AURA" title, blue
  // band shows "Loading..." plus a "[step/total] label" line and a bar.
  // Safe to call any time after begin().
  void showLoading(const char* label, int step, int total);

  // One-line splash: yellow "AURA" + a single big centred line (e.g.
  // "Ready!"). Useful for short transition screens.
  void showSplash(const char* line);

  // Runtime status: yellow "AURA" + a centred body string (auto-wrapped
  // across two lines if it does not fit) + a small hint line along the
  // bottom of the screen.
  void showStatus(const char* body, const char* hint);

} // namespace OledStatus

#endif // AURA_OLED_STATUS_H
