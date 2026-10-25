// agents deck on the Waveshare ESP32-S3-Touch-LCD-7: the deck as a 4 x 4 grid
// of tiles on the 800 x 480 screen, with touch.
//
// This file is the board: the ST7262 panel on RGB parallel, the GT911 touch,
// and the CH422G that the board uses as IO expander (resets, the backlight,
// the USB/CAN switch). Everything else (Deck Link, the fleet, the layout, the
// touch UI) is panel-core's grid_app.h.
//
// Pins, panel timing and the expander's bits are from Waveshare's BSP and
// demos for this board (ESP32-S3-Touch-LCD-7 on the Waveshare wiki,
// ESP32_Display_Panel's board config for it).
//
// GPIO0 (BOOT) is one of the panel's green lines here, so the board has no
// button for the firmware: back is a swipe right.
//
// Deck Link runs on the port marked UART (UART0 through the board's CH343),
// not the chip's own USB: platformio.ini builds with ARDUINO_USB_CDC_ON_BOOT=0,
// so Serial is UART0 at 115200.
//
// Two builds: arduino-esp32 2.0.17 (../platformio.ini, USB only) and 3.x
// (idf5/platformio.ini: Wi-Fi with TLS 1.3, and an optional RGB bounce buffer).
#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <Wire.h>
#include <esp32s3/rom/cache.h>

#include "grid_app.h"
#include "layout_big.h"
#include "status_bar.h"

#define FW_VERSION "0.1.0"

// ---------------------------------------------------------------- pins
constexpr int kW = 800, kH = 480;
constexpr int PIN_SDA = 8, PIN_SCL = 9, PIN_TP_INT = 4;
constexpr uint8_t ADDR_GT911 = 0x5D, ADDR_GT911_ALT = 0x14;

// The CH422G answers on several I2C addresses, one per register: 0x24 sets
// its mode (bit 0: IO0-7 are outputs), 0x38 writes IO0-7.
constexpr uint8_t CH422_MODE = 0x24, CH422_OUT = 0x38;
constexpr uint8_t EXIO_TP_RST = 1 << 1, EXIO_BL = 1 << 2, EXIO_LCD_RST = 1 << 3, EXIO_SD_CS = 1 << 4,
                  EXIO_USB_SEL = 1 << 5;
// What Waveshare's demos write (0x2C, then 0x2E with touch out of reset).
constexpr uint8_t EXIO_RUN = EXIO_BL | EXIO_LCD_RST | EXIO_USB_SEL;

// ---------------------------------------------------------------- display
// The ST7262 needs no init sequence: RGB565 on the parallel bus, 16 MHz,
// sampled on the falling edge. On arduino-esp32 3.x the panel scans out of a
// bounce buffer in internal RAM (LCD7_BOUNCE_PX pixels, 10 lines), which keeps
// the picture still while the CPU and Wi-Fi share the PSRAM; 2.0.17 has none
// and scans straight from PSRAM.
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#ifndef LCD7_BOUNCE_PX
#define LCD7_BOUNCE_PX 8000
#endif
#define LCD7_RGB_EXTRA , false, 0, 0, LCD7_BOUNCE_PX
#else
#define LCD7_RGB_EXTRA
#endif
static Arduino_ESP32RGBPanel* rgbPanel = new Arduino_ESP32RGBPanel(
    5 /* DE */, 3 /* VSYNC */, 46 /* HSYNC */, 7 /* PCLK */,
    1, 2, 42, 41, 40,            // R0..R4 (R3..R7 on the panel)
    39, 0, 45, 48, 47, 21,       // G0..G5 (G2..G7)
    14, 38, 18, 17, 10,          // B0..B4 (B3..B7)
    1, 8, 4, 8,                  // hsync polarity (1: idle high, as in the BSP), front porch, pulse width, back porch
    1, 8, 4, 8,                  // vsync
    1 /* pclk on the falling edge */, 16000000 LCD7_RGB_EXTRA);
static Arduino_RGB_Display* lcd = new Arduino_RGB_Display(kW, kH, rgbPanel, 0, false /* we flush */);
static uint16_t* panelFb = nullptr;
static bool haveCh422 = false;
static uint8_t exio = 0;
static uint8_t gt911 = 0;

// ---------------------------------------------------------------- I2C
static bool i2cPresent(uint8_t addr) {
  Wire.beginTransmission(addr);
  return Wire.endTransmission() == 0;
}

static bool i2cWrite1(uint8_t addr, uint8_t value) {
  Wire.beginTransmission(addr);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

static void exioSet(uint8_t bits) {
  exio = bits;
  i2cWrite1(CH422_OUT, exio);
}

// A stuck slave can hold SDA low after a reset: clock it free.
static void i2cRecover() {
  pinMode(PIN_SDA, INPUT_PULLUP);
  pinMode(PIN_SCL, OUTPUT);
  for (int i = 0; i < 9; i++) {
    digitalWrite(PIN_SCL, LOW);
    delayMicroseconds(5);
    digitalWrite(PIN_SCL, HIGH);
    delayMicroseconds(5);
  }
  pinMode(PIN_SCL, INPUT_PULLUP);
}

// LCD and touch out of reset, backlight on. The GT911 takes its I2C address
// from INT while it leaves reset: low gives 0x5D.
static void expanderInit() {
  i2cRecover();
  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  for (int tries = 0; tries < 5 && !haveCh422; tries++) {
    haveCh422 = i2cWrite1(CH422_MODE, 0x01);         // IO0-7 outputs
    if (!haveCh422) delay(50);
  }
  if (!haveCh422) return;
  exioSet(EXIO_RUN | EXIO_SD_CS);                     // touch held in reset, SD card deselected
  pinMode(PIN_TP_INT, OUTPUT);
  digitalWrite(PIN_TP_INT, LOW);
  delay(100);
  exioSet(EXIO_RUN | EXIO_SD_CS | EXIO_TP_RST);
  delay(200);
  pinMode(PIN_TP_INT, INPUT);
}

// ---------------------------------------------------------------- GT911
static bool gtRead(uint16_t reg, uint8_t* out, size_t n) {
  Wire.beginTransmission(gt911);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  if (Wire.endTransmission(false) != 0) return false;
  if (Wire.requestFrom(gt911, (uint8_t)n) != n) return false;
  for (size_t i = 0; i < n; i++) out[i] = Wire.read();
  return true;
}

static void gtWrite8(uint16_t reg, uint8_t v) {
  Wire.beginTransmission(gt911);
  Wire.write((uint8_t)(reg >> 8));
  Wire.write((uint8_t)reg);
  Wire.write(v);
  Wire.endTransmission();
}

// Touch in frame pixels: the panel and the touch share the screen's own
// landscape orientation.
static bool touch(int& x, int& y) {
  static bool lastDown = false;
  static int lastX = 0, lastY = 0;
  static uint32_t lastPoll = 0;
  if (!gt911) return false;
  if (millis() - lastPoll < 15) { x = lastX; y = lastY; return lastDown; }   // ~60 Hz is plenty
  lastPoll = millis();
  uint8_t status = 0;
  if (!gtRead(0x814E, &status, 1)) return lastDown = false;
  if (!(status & 0x80)) { x = lastX; y = lastY; return lastDown; }        // no new report: same as before
  const int n = status & 0x0F;
  if (n > 0) {
    uint8_t p[6];
    if (gtRead(0x8150, p, sizeof p)) {
      lastX = p[0] | p[1] << 8;
      lastY = p[2] | p[3] << 8;
      if (lastX >= kW) lastX = kW - 1;
      if (lastY >= kH) lastY = kH - 1;
    }
  }
  gtWrite8(0x814E, 0);
  lastDown = n > 0;
  x = lastX;
  y = lastY;
  return lastDown;
}

// ---------------------------------------------------------------- time and battery
// The status bar's clock: the deck sends UTC, so the board needs its time zone,
// as a POSIX TZ string (e.g. "CET-1CEST,M3.5.0,M10.5.0/3").
#ifndef PANEL_TZ
#define PANEL_TZ "UTC0"
#endif

// The board charges a Li-ion cell (CS8501) but doesn't wire its voltage to any
// pin. With a divider from the cell to an ADC pin (e.g. the Sensor AD header),
// build with -DLCD7_BAT_ADC=<gpio> and -DLCD7_BAT_DIVIDER=<ratio> and the
// status bar shows the charge; without it, no battery is shown.
#ifdef LCD7_BAT_ADC
#ifndef LCD7_BAT_DIVIDER
#define LCD7_BAT_DIVIDER 2.0f
#endif
static int batteryPct() {
  const float v = analogReadMilliVolts(LCD7_BAT_ADC) * LCD7_BAT_DIVIDER / 1000.f;
  const int pct = (int)((v - 3.3f) / (4.15f - 3.3f) * 100.f);   // a Li-ion cell: 3.3 V empty, 4.15 V full
  return pct < 0 ? 0 : pct > 100 ? 100 : pct;
}
#define LCD7_BATTERY batteryPct
#else
#define LCD7_BATTERY nullptr
#endif

// ---------------------------------------------------------------- settings
// What the status bar's settings can do (settings_shade.h): the board's Wi-Fi,
// set from its own screen through net_link.h, and a restart.
static ShadeWifi settingsWifi() {
  const net_link::LocalWifi l = net_link::localInfo();
  ShadeWifi w;
  snprintf(w.ssid, sizeof w.ssid, "%s", l.ssid);
  w.capable = l.capable;
  w.paired = l.paired;
  w.testing = l.testing;
  w.via = l.via;
  w.phase = l.phase;
  w.lastTest = l.lastTest;
  snprintf(w.ip, sizeof w.ip, "%s", l.ip);
  w.rssi = l.rssi;
  return w;
}

static int settingsNetworks(ShadeNet* out, int max) {
  static char ssids[kShadeNets][33];
  static int8_t rssi[kShadeNets];
  static bool secure[kShadeNets];
  const int n = net_link::scanResults(ssids, rssi, secure, max < kShadeNets ? max : kShadeNets);
  for (int i = 0; i < n; i++) {
    memcpy(out[i].ssid, ssids[i], sizeof out[i].ssid);
    out[i].rssi = rssi[i];
    out[i].secure = secure[i];
  }
  return n;
}

static const ShadeHooks kSettings{
    settingsWifi,
    [] { net_link::scanStart(); },
    settingsNetworks,
    [](const char* ssid, const char* password) { return net_link::localJoin(ssid, password); },
    [] { net_link::localForget(); },
    [] { ESP.restart(); },
};

// ---------------------------------------------------------------- the board
static bool boardBegin() {
  expanderInit();
  if (!lcd->begin()) return false;
  panelFb = lcd->getFramebuffer();
  gt911 = i2cPresent(ADDR_GT911) ? ADDR_GT911 : i2cPresent(ADDR_GT911_ALT) ? ADDR_GT911_ALT : 0;
  PANEL_LOG("lcd7: expander %s, gt911 0x%02X, framebuffer %p", haveCh422 ? "ch422g" : "NONE", gt911, panelFb);
  return panelFb != nullptr;
}

// One frame into the panel's own framebuffer (PSRAM), then out of the cache.
static void push(const uint16_t* src) {
  if (!panelFb) return;
  memcpy(panelFb, src, kW * kH * 2);
  Cache_WriteBack_Addr((uint32_t)panelFb, kW * kH * 2);
}

// Backlight: the CH422G can only switch it (EXIO2), so any level is on.
static void brightness(uint8_t level) {
  if (!haveCh422) return;
  const uint8_t want = level ? exio | EXIO_BL : exio & ~EXIO_BL;
  if (want != exio) exioSet(want);
}

void setup() {
  GridBoard b{};
  b.type = "waveshare-touch-lcd-7";
  setenv("TZ", PANEL_TZ, 1);
  tzset();
  static const BigPanel deckUi(specBig800(StatusBar::kHeight));   // the desk panel's UI at 2/3, across 800 px
  static const StatusBar layout(deckUi, LCD7_BATTERY, &kSettings);   // under a phone-style status bar
  b.layout = &layout;
  b.begin = boardBegin;
  b.push = push;
  b.touch = touch;
  b.brightness = brightness;
  b.bootPin = -1;                               // GPIO0 drives the panel
  b.brightActive = 255;
  b.brightIdle = 60;
  gridAppSetup(b, FW_VERSION);
}

void loop() { gridAppLoop(); }
