
#include "GxEPDDisplay.h"

#ifdef EXP_PIN_BACKLIGHT
  #include <PCA9557.h>
  extern PCA9557 expander;
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 3
#endif

#ifdef ESP32
  SPIClass SPI1 = SPIClass(FSPI);
#endif

bool GxEPDDisplay::begin() {
  display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
#ifdef ESP32
  SPI1.begin(PIN_DISPLAY_SCLK, PIN_DISPLAY_MISO, PIN_DISPLAY_MOSI, PIN_DISPLAY_CS);
#else
  SPI1.begin();
#endif
  display.init(115200, true, 2, false);
  display.setRotation(DISPLAY_ROTATION);
  setTextSize(1);  // Default to size 1
  // Two-phase boot clear:
  // Phase 1 - clearScreen() writes 0xFF to both controller frame buffers (0x26
  //   prev and 0x24 curr) then does a full refresh → panel visibly goes black
  //   then settles on white.
  // Phase 2 - _full_refresh_pending causes the first content frame to also use
  //   display(false) (full refresh) rather than a partial update, so content
  //   appears cleanly on the freshly cleared panel.
  display.clearScreen();
  _full_refresh_pending = true;
  display.setPartialWindow(0, 0, display.width(), display.height());

  #if DISP_BACKLIGHT
  digitalWrite(DISP_BACKLIGHT, LOW);
  pinMode(DISP_BACKLIGHT, OUTPUT);
  #endif
  _init = true;
  return true;
}

void GxEPDDisplay::turnOn() {
  if (!_init) begin();
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, HIGH);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, HIGH);
#endif
  _isOn = true;
}

void GxEPDDisplay::turnOff() {
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, LOW);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, LOW);
#endif
  _isOn = false;
}

void GxEPDDisplay::clear() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display_crc.reset();
}

void GxEPDDisplay::startFrame(Color bkg) {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(_curr_color = GxEPD_BLACK);
  display_crc.reset();
}

void GxEPDDisplay::setTextSize(int sz) {
  display_crc.update<int>(sz);
  switch(sz) {
    case 1:  // Small
      display.setFont(&FreeSans9pt7b);
      break;
    case 2:  // Medium Bold
      display.setFont(&FreeSansBold12pt7b);
      break;
    case 3:  // Large
      display.setFont(&FreeSans18pt7b);
      break;
    default:
      display.setFont(&FreeSans9pt7b);
      break;
  }
}

void GxEPDDisplay::setColor(Color c) {
  display_crc.update<Color> (c);
  // colours need to be inverted for epaper displays
  if (c == DARK) {
    display.setTextColor(_curr_color = GxEPD_WHITE);
  } else {
    display.setTextColor(_curr_color = GxEPD_BLACK);
  }
}

void GxEPDDisplay::setCursor(int x, int y) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display.setCursor((x+offset_x)*scale_x, (y+offset_y)*scale_y);
}

void GxEPDDisplay::print(const char* str) {
  display_crc.update<char>(str, strlen(str));
  display.print(str);
}

void GxEPDDisplay::fillRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.fillRect(x*scale_x, y*scale_y, w*scale_x, h*scale_y, _curr_color);
}

void GxEPDDisplay::drawRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.drawRect(x*scale_x, y*scale_y, w*scale_x, h*scale_y, _curr_color);
}

void GxEPDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display_crc.update<uint8_t>(bits, w * h / 8);

  // Use a uniform pixel scale so icons appear square regardless of display aspect ratio.
  // Center the icon within the logical bounding box so existing centering formulas still work.
  float icon_scale = (scale_x < scale_y) ? scale_x : scale_y;
  uint16_t startX = (uint16_t)(x * scale_x + (w * scale_x - w * icon_scale) / 2.0f);
  uint16_t startY = (uint16_t)(y * scale_y);

  // Width in bytes for bitmap processing
  uint16_t widthInBytes = (w + 7) / 8;

  // Process the bitmap row by row
  for (uint16_t by = 0; by < h; by++) {
    int y1 = startY + (int)(by * icon_scale);
    int y2 = startY + (int)((by + 1) * icon_scale);
    int block_h = y2 - y1;
    if (block_h < 1) block_h = 1;

    for (uint16_t bx = 0; bx < w; bx++) {
      int x1 = startX + (int)(bx * icon_scale);
      int x2 = startX + (int)((bx + 1) * icon_scale);
      int block_w = x2 - x1;
      if (block_w < 1) block_w = 1;

      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = pgm_read_byte(bits + byteOffset) & bitMask;

      if (bitSet) {
        display.fillRect(x1, y1, block_w, block_h, _curr_color);
      }
    }
  }
}

uint16_t GxEPDDisplay::getTextWidth(const char* str) {
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return ceil((w + 1) / scale_x);
}

void GxEPDDisplay::setNextFrameFullRefresh() {
  _full_refresh_pending = true;
}

void GxEPDDisplay::endFrame() {
  uint32_t crc = display_crc.finalize();
  if (crc != last_display_crc_value || _full_refresh_pending) {
    display.display(_full_refresh_pending ? false : true);  // full refresh when flagged, partial otherwise
    _full_refresh_pending = false;
    last_display_crc_value = crc;
  }
}
