#include "OledStatus.h"

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

namespace {
  // Geometry of the AURA OLED panel. Yellow band is the top 16 px, the
  // rest is blue. The SSD1306 driver does not care about the colour split,
  // but the layout below was tuned for it.
  constexpr int OLED_W = 128;
  constexpr int OLED_H = 64;

  // size-2 font is roughly 12 px per character, size-1 about 6 px.
  constexpr int CHAR_W_SIZE2 = 12;
  constexpr int CHAR_W_SIZE1 = 6;

  Adafruit_SSD1306 display(OLED_W, OLED_H, &Wire, -1 /* no reset pin */);

  // Render the yellow "AURA" title at the top of the screen. Caller is
  // responsible for clearDisplay() before and display() after.
  void drawTitleAURA() {
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);
    // "AURA" = 4 chars * 12 px = 48 px wide; centred at (128-48)/2 = 40.
    display.setCursor(40, 0);
    display.println("AURA");
  }
}

namespace OledStatus {

bool begin(uint8_t i2cAddr) {
  bool ok = display.begin(SSD1306_SWITCHCAPVCC, i2cAddr);
  display.clearDisplay();
  drawTitleAURA();
  display.display();
  return ok;
}

void showLoading(const char* label, int step, int total) {
  display.clearDisplay();
  drawTitleAURA();

  display.setTextSize(1);

  // "Loading..." centred just below the title.
  const char* loading = "Loading...";
  int loadingWidth = (int)strlen(loading) * CHAR_W_SIZE1;
  display.setCursor((OLED_W - loadingWidth) / 2, 20);
  display.println(loading);

  // "[step/total] label" centred under that.
  char stepStr[32];
  snprintf(stepStr, sizeof(stepStr), "[%d/%d] %s", step, total, label);
  int stepWidth = (int)strlen(stepStr) * CHAR_W_SIZE1;
  int stepX = (OLED_W - stepWidth) / 2;
  if (stepX < 0) stepX = 0;
  display.setCursor(stepX, 34);
  display.println(stepStr);

  // Outlined progress bar with filled portion.
  const int barX = 14, barY = 50, barW = 100, barH = 8;
  display.drawRect(barX, barY, barW, barH, SSD1306_WHITE);
  if (total > 0) {
    int fillW = (barW - 2) * step / total;
    if (fillW > 0) display.fillRect(barX + 1, barY + 1, fillW, barH - 2, SSD1306_WHITE);
  }

  display.display();
}

void showSplash(const char* line) {
  display.clearDisplay();
  drawTitleAURA();

  display.setTextSize(2);
  int lineWidth = (int)strlen(line) * CHAR_W_SIZE2;
  int xPos = (OLED_W - lineWidth) / 2;
  if (xPos < 0) xPos = 0;
  display.setCursor(xPos, 28);
  display.println(line);

  display.display();
}

void showStatus(const char* body, const char* hint) {
  display.clearDisplay();
  drawTitleAURA();

  // Big centred body, auto-wrapped to two lines if it doesn't fit.
  display.setTextSize(2);
  int len = (int)strlen(body);
  if (len * CHAR_W_SIZE2 <= OLED_W) {
    int xPos = (OLED_W - len * CHAR_W_SIZE2) / 2;
    display.setCursor(xPos, 28);
    display.println(body);
  } else {
    // Walk back from the end to find a space where both halves fit.
    int splitAt = -1;
    for (int i = len - 1; i >= 0; i--) {
      if (body[i] == ' ' && i * CHAR_W_SIZE2 <= OLED_W
                         && (len - i - 1) * CHAR_W_SIZE2 <= OLED_W) {
        splitAt = i;
        break;
      }
    }
    if (splitAt < 0) {
      display.setCursor(0, 28);
      display.println(body);
    } else {
      char line1[20], line2[20];
      int l1 = splitAt, l2 = len - splitAt - 1;
      strncpy(line1, body, l1); line1[l1] = '\0';
      strncpy(line2, body + splitAt + 1, l2); line2[l2] = '\0';
      display.setCursor((OLED_W - l1 * CHAR_W_SIZE2) / 2, 20);
      display.println(line1);
      display.setCursor((OLED_W - l2 * CHAR_W_SIZE2) / 2, 38);
      display.println(line2);
    }
  }

  // Small hint line along the bottom.
  if (hint && hint[0]) {
    display.setTextSize(1);
    display.setCursor(0, 56);
    display.println(hint);
  }

  display.display();
}

} // namespace OledStatus
