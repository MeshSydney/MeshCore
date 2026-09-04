
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

#ifndef EPD_WASHING_MACHINE_CYCLES
  #define EPD_WASHING_MACHINE_CYCLES 0
#endif

// Color scheme
ColorVal UIColor::window_bkg = GxEPD_WHITE;
ColorVal UIColor::title_bkg = GxEPD_WHITE;
ColorVal UIColor::title_txt = GxEPD_BLACK;
ColorVal UIColor::primary_txt = GxEPD_BLACK;
ColorVal UIColor::secondary_txt = GxEPD_BLACK;
ColorVal UIColor::warning_txt = GxEPD_BLACK;
ColorVal UIColor::popup_bkg = GxEPD_WHITE;
ColorVal UIColor::popup_txt = GxEPD_BLACK;
ColorVal UIColor::corp_blue = GxEPD_BLACK;

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

  display.setFullWindow();

  for (int i = 0; i < EPD_WASHING_MACHINE_CYCLES; i++) {
    display.fillScreen(GxEPD_BLACK);
    display.display(false);
    delay(2000);
    display.fillScreen(GxEPD_WHITE);
    display.display(false);
    delay(2000);
  }

#ifdef HELTEC_MESH_POCKET
  // The stock SSD1680 init sequence (run above, as part of the washing-machine
  // cycle) sets BorderWaveform (controller register 0x3C) to 0x05, which
  // actively drives the panel's border strip through a black/white transition
  // on every refresh, producing a visible dark flash/frame around the edge of
  // the screen. Override it to 0x80 (border left floating/HiZ instead of
  // driven) so the border no longer flashes. This has to be done via raw SPI
  // here (rather than a GxEPD2 API call) since the controller command/data
  // helpers are protected library internals. It must be sent after the
  // panel's own init sequence above (which unconditionally resets it), and
  // survives afterwards since this board never hibernates the display
  // (AUTO_OFF_MILLIS=0), so the library never re-runs its init sequence.
  {
    SPISettings spi_settings(4000000, MSBFIRST, SPI_MODE0);
    SPI1.beginTransaction(spi_settings);
    digitalWrite(PIN_DISPLAY_DC, LOW);   // command
    digitalWrite(PIN_DISPLAY_CS, LOW);
    SPI1.transfer(0x3C);
    digitalWrite(PIN_DISPLAY_CS, HIGH);
    digitalWrite(PIN_DISPLAY_DC, HIGH);
    SPI1.endTransaction();

    SPI1.beginTransaction(spi_settings);
    digitalWrite(PIN_DISPLAY_CS, LOW);   // data
    SPI1.transfer(0x80);
    digitalWrite(PIN_DISPLAY_CS, HIGH);
    SPI1.endTransaction();
  }
#endif

  display.setPartialWindow(0, 0, display.width(), display.height());
  resetPartialRefreshCounter();
  // Schedule a clear on the first rendered frame via endFrame().
  // endFrame() will call clearScreen() (Mode 1, panel goes white) then
  // display(true) (Mode 2 partial with 0x26=white vs 0x24=content),
  // giving proper WB transitions for all content pixels.
  _full_refresh_pending = true;

  #if DISP_BACKLIGHT
  digitalWrite(DISP_BACKLIGHT, LOW);
  pinMode(DISP_BACKLIGHT, OUTPUT);
  #endif
  _init = true;
  _isOn = true;
  return true;
}

void GxEPDDisplay::turnOn() {
  if (!_init) begin();
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, HIGH);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, HIGH);
#endif
  if (!_isOn) {
    _isOn = true;
  }
}

void GxEPDDisplay::turnOff() {
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, LOW);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, LOW);
#endif
  _isOn = false;
  // do full refresh before powering off to clear screen
  // no full refresh needed at wakeup
  display.clearScreen(0xFF); // Clears microcontroller side RAM
  display.writeScreenBuffer(0xFF); // Forces 0xFF (White) into the display controller's history registers
  resetPartialRefreshCounter();
  last_display_crc_value=0;
  display.hibernate();
}

void GxEPDDisplay::clear() {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(GxEPD_BLACK);
  display_crc.reset();
}

void GxEPDDisplay::startFrame(ColorVal bkg) {
  display.fillScreen(bkg);
  display.setTextColor(_curr_color = UIColor::primary_txt);
  display_crc.reset();
  if (_cycles_before_full_refresh != 0) {
    display.setPartialWindow(0, 0, display.width(), display.height());
  } else {
    // forces a full wipe of the screen ...
    display.clearScreen(0xFF); // Clears microcontroller side RAM
    display.writeScreenBuffer(0xFF); // Forces 0xFF (White) into the display controller's history registers
    // we'll need a partial refresh after that (whatever crc value is)
    last_display_crc_value = 0;
    resetPartialRefreshCounter();
  }
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

void GxEPDDisplay::setColor(ColorVal c) {
  display_crc.update<ColorVal> (c);
  display.setTextColor(_curr_color = c);
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
  if (_isOn == false) return;
  uint32_t crc = display_crc.finalize();
  if (crc != last_display_crc_value || _full_refresh_pending) {
    bool full_deghost = _full_refresh_pending;
    if (full_deghost) {
      // Wipe to solid white with a full-waveform update first, so there is
      // a real black/white transition to force out accumulated ghosting
      // (writing the same content to both prev/curr buffers, as a single
      // display(false) does, leaves prior ghosting untouched underneath).
      display.clearScreen(0xFF);
    }
    // After a deghost wipe, draw content with another full-waveform update
    // so it settles cleanly against the known-white state (partial mode
    // here left visible residue). Otherwise use the cheap partial update
    // for normal incremental redraws.
    display.display(!full_deghost);
    _full_refresh_pending = false;
    if (_cycles_before_full_refresh > 0) {
      _cycles_before_full_refresh--;
    }
  }
  last_display_crc_value = crc;
}
