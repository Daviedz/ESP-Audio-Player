// Winamp-style SD audio player for the 2.8" ESP32-S3 touch board (FNK0104B) - LANDSCAPE 320x240
// Libraries: ESP32-audioI2S (schreibfaul1), arduino-audio-driver (pschatzmann), LovyanGFX
// Board settings: ESP32S3 Dev Module, PSRAM = OPI PSRAM, Flash 16MB (QIO)
//
// Features: up to 4000 songs (stored in PSRAM), subfolders, playlists, scrollable list with big scrollbar,
//           sort (A-Z / ARTIST / ALBUM / NEWEST / CARD), shuffle, repeat OFF/ALL/ONE,
//           10-band-style EQ with presets, saved settings (volume, EQ, shuffle, repeat, sort, source).
// Also:     soft power off (BOOT or an external button wakes it), optional external VOL-/VOL+/POWER buttons on the IO connector, VOL -/+ buttons, battery + charging indicator,
//           internet radio streaming over Wi-Fi (edit stations below; Wi-Fi networks come from /wifi.txt on the SD card).
// SD card:  songs can be in any folder (up to 5 levels). Playlists are text files /playlists/NAME.m3u
//           (one song path per line) - you can create them on the PC or on the player (long-press a song).
//           A tag cache /espamp.idx is created so later boots are fast; it updates itself when files change.

#include <Arduino.h>
#if !CONFIG_IDF_TARGET_ESP32S3
#error "Wrong board: select Tools > Board > ESP32 Arduino > ESP32S3 Dev Module (and PSRAM: OPI PSRAM)"
#endif
// LovyanGFX must come BEFORE AudioBoard.h (GPIO name clash)
#define LGFX_USE_V1
#include <LovyanGFX.hpp>
#include <Wire.h>
#include <SD_MMC.h>
#include <Preferences.h>
#include <algorithm>
#include <WiFi.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include <Audio.h>
#include "AudioBoard.h"
#if defined(ARDUINO_USB_MODE) && ARDUINO_USB_MODE == 0     // Tools > USB Mode = "USB-OTG (TinyUSB)" -> USB drive mode available
#include "USB.h"
#include "USBMSC.h"
#include "diskio.h"      // multi-sector SD access (much faster than one sector at a time)
#define HAVE_USB_MSC 1
#else
#define HAVE_USB_MSC 0
#endif

// ---------------- pins ----------------
#define I2C_SDA 16
#define I2C_SCL 15
#define I2S_MCLK 4
#define I2S_BCLK 5
#define I2S_LRC  7
#define I2S_DOUT 8
#define I2S_DIN  6
#define AUDIO_EN 1
#define AMP_ON_LEVEL LOW
#define SD_CLK 38
#define SD_CMD 40
#define SD_D0  39
#define SD_D1  41
#define SD_D2  48
#define SD_D3  47
#define LCD_BL 45
#define TP_RST 18
#define BAT_PIN 9                    // battery voltage through a 200K/200K divider
#define POWER_BTN 0                  // BOOT button: hold 1.5 s = power off, press = wake up
#define RGB_PIN 42                   // on-board RGB LED (kept off)
#define CHRG_PIN -1                  // optional: GPIO wired to the TP4054 CHRG pin (low = charging). -1 = not wired
#define EXTPWR_PIN 21                // optional external POWER push button (other side to GND) on the IO connector, -1 = none
#define VOLDN_PIN 2                  // optional external VOL- push button, -1 = none
#define VOLUP_PIN 14                 // optional external VOL+ push button, -1 = none
// ---- Bluetooth transmitter (e.g. KCX_BT_EMITTER) fed from the ES8311 line output, before the SC8002B amp ----
#define BT_EN_PIN 3                  // switches the transmitter's power (via a MOSFET / transistor), -1 = module always powered
#define BT_ON_LEVEL HIGH             // level on BT_EN_PIN that powers the module
#define BT_LINK_PIN -1               // optional: module "connected" LED/status pin (HIGH = paired), -1 = not wired
#define VBAT_OFFSET_MV 0             // calibration: add/subtract mV if the shown voltage differs from a multimeter
#define LOW_BATT_SHUTDOWN_MV 3350    // auto power-off below this battery voltage (when not charging)

// ---- Wi-Fi + internet radio stations ----
// Wi-Fi networks are read from /wifi.txt on the SD card (one "NAME,PASSWORD" per line, up to 8).
// The two lines below are an optional built-in network (leave as is to ignore it).
#define WIFI_SSID "YOUR_WIFI_NAME"
#define WIFI_PASS "YOUR_WIFI_PASSWORD"
const int MAX_NETS = 8;
struct Net { char ssid[33]; char pass[65]; };
Net nets[MAX_NETS]; int netCount = 0, netIdx = 0, netAttempt = 0;
// Radio stations come from /radio.txt on the SD card (one per line:  Name, http://url  - # = comment).
// The list below is only used when that file is missing or empty; the player creates a sample /radio.txt from it.
struct DefStation { const char *name; const char *url; };
const DefStation defStations[] = {
  {"Radio 1", "http://136.243.44.41:8010/stream"},
};
#define MAX_STATIONS 60
struct Station { char name[40]; char url[200]; };
Station stations[MAX_STATIONS]; int STATION_N = 0;

// ---------------- display / touch settings ----------------
#define LCD_INVERT    true
#define TOUCH_SWAP_XY false
#define TOUCH_FLIP_X  false
#define TOUCH_FLIP_Y  false
// ---- volume curve: 30 steps, each ~1.6 dB, set in the ES8311 codec (no digital boost = no distortion) ----
#define VOL_STEPS   30
#define VOL_MIN_DB -46.0f      // loudness of step 1 (quietest audible step)
#define VOL_MAX_DB   0.0f      // loudness at 100 %; 0 dB = no boost. Too quiet: try 3..6. Distorts: try -3..-6
#define AUTOPLAY_ON_BOOT 0   // 1 = start playing the first song at power-up, 0 = wait for PLAY
#define LCD_ROTATION  1        // 1 = USB-C on the right, 3 = USB-C on the left
#define BOOT_ANIM     1        // 1 = animated boot screen (spinning record + spectrum), 0 = plain text
#define CLOCK_24H     0        // live radio shows the local time (from the internet): 1 = 24 h, 0 = 12 h
#define TZ_DEFAULT    "UTC-8"  // POSIX time zone, e.g. "CET-1CEST,M3.5.0,M10.5.0/3" (Europe), "EST5EDT,M3.2.0,M11.1.0" (US East),
                               // "PST8PDT,M3.2.0,M11.1.0" (US West). Can also be set with a one-line /timezone.txt on the SD card.

class LGFX : public lgfx::LGFX_Device {
  lgfx::Panel_ILI9341 _panel;
  lgfx::Bus_SPI _bus;
public:
  LGFX() {
    auto b = _bus.config();
    b.spi_host = SPI2_HOST; b.spi_mode = 0;
    b.freq_write = 40000000; b.freq_read = 16000000;
    b.dma_channel = SPI_DMA_CH_AUTO;
    b.pin_sclk = 12; b.pin_mosi = 11; b.pin_miso = 13; b.pin_dc = 46;
    _bus.config(b);
    _panel.setBus(&_bus);
    auto p = _panel.config();
    p.pin_cs = 10; p.pin_rst = -1; p.pin_busy = -1;
    p.panel_width = 240; p.panel_height = 320;
    p.readable = false; p.invert = LCD_INVERT; p.rgb_order = false;
    _panel.config(p);
    setPanel(&_panel);
  }
};

LGFX lcd;
lgfx::LGFX_Sprite fb(&lcd);

DriverPins pins;
AudioBoard board(AudioDriverES8311, pins);
Audio audio;
Preferences prefs;
SemaphoreHandle_t mtx;
#define LOCK()   xSemaphoreTake(mtx, portMAX_DELAY)
#define UNLOCK() xSemaphoreGive(mtx)

// ---------------- colours ----------------
#define C(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|((b)>>3)))
const uint16_t C_BODY = C(0x3A,0x3C,0x4A), C_LIGHT = C(0x8A,0x8E,0xA8), C_DARK = C(0x14,0x15,0x1C);
const uint16_t C_LCD = C(0,0,0), C_GRN = C(0x00,0xE8,0x00), C_DGRN = C(0x00,0x38,0x00);
const uint16_t C_TXT = C(0xE8,0xE8,0xF0), C_YEL = C(0xF0,0xD8,0x20), C_ORG = C(0xF0,0x80,0x10);
const uint16_t C_KNOB = C(0xB0,0xB4,0xC8), C_ACT = C(0x50,0x54,0x70);

// ---------------- track database (lives in PSRAM) ----------------
const int MAX_TRACKS = 4000;          // raise if you need more (each song uses 256 bytes of PSRAM)
const int MAX_FOLDERS = 100;
const int MAX_PL = 6;
const int PATH_LEN = 128;
struct Track {                        // 256 bytes, plain data (also written to the cache file)
  char path[PATH_LEN];
  char artist[36];
  char title[44];
  char album[36];
  uint32_t size;
  uint32_t mtime;
  uint16_t trackNo;
  uint8_t folderId;
  uint8_t played;                     // shuffle bookkeeping
};
Track *tr = nullptr;                  // all songs, in scan order
uint16_t *vis = nullptr;              // song ids of the active list, in display/play order
uint16_t *rankArr = nullptr;          // natural position of each song inside the active list
uint16_t *byPath = nullptr;           // song ids sorted by path (for playlist loading)
int trackCount = 0, visCount = 0;

char folders[MAX_FOLDERS][96]; uint16_t folderN[MAX_FOLDERS]; int folderCount = 0;
char plName[MAX_PL][32]; uint16_t plN[MAX_PL]; int plCount = 0;
uint16_t *plTr = nullptr;             // plTr[p * MAX_TRACKS + i] = song id
uint8_t srcType = 0;                  // 0 = all songs, 1 = folder, 2 = playlist
int srcId = 0;

uint8_t sortMode = 0;                 // 0 = A-Z, 1 = ARTIST, 2 = ALBUM, 3 = NEWEST, 4 = CARD / playlist order
const int SORT_MODES = 5;
const char *sortName[SORT_MODES] = {"A-Z", "ARTIST", "ALBUM", "NEWEST", "CARD"};

// ---------------- state ----------------
enum PlayState { ST_STOP, ST_PLAY, ST_PAUSE };
enum View { V_MAIN, V_EQ, V_PL, V_SRC, V_ADD, V_PWR };
PlayState state = ST_STOP;
View view = V_MAIN;
int curId = -1;                       // song id being played
int volume = 10;                       // 0..VOL_STEPS
int balance = 0;                       // -16 (left) .. +16 (right)
bool eqOn = true, shuffleOn = false;
uint8_t outMode = 0;                   // 0 = speaker, 1 = Bluetooth transmitter, 2 = both
const char *OUT_NAME[3] = {"SPK", "BT", "BOTH"};
uint8_t repMode = 1;                  // 0 = OFF, 1 = ALL, 2 = ONE
volatile bool nextPending = false;
uint32_t lastStartMs = 0;
int resumeId = -1; uint32_t resumeSec = 0, resumePending = 0;   // continue the last song where it was at power-off
uint8_t eqPreset = 0;                 // 0 FLAT, 1 ROCK, 2 JAZZ, 3 CLASSIC, 4 POP, 5 CUSTOM
int8_t customEq[11] = {0,0,0,0,0,0,0,0,0,0,0};
const int8_t presets[5][11] = {       // [0] = preamp, [1..10] = 70Hz .. 16kHz
  {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  {0, 5, 4, 3, 1,-1,-1, 1, 3, 4, 5},
  {0, 4, 3, 1, 2,-2,-2, 0, 1, 3, 4},
  {0, 0, 0, 0, 0, 0, 0,-4,-4,-4,-6},
  {0,-2,-1, 0, 2, 4, 4, 2, 0,-1,-2}
};
const char *presetName[6] = {"FLAT", "ROCK", "JAZZ", "CLASSIC", "POP", "CUSTOM"};
int8_t eq[11] = {0,0,0,0,0,0,0,0,0,0,0};       // [0]=preamp, [1..10]=bands, -12..+12 dB
String id3Title, id3Artist, titleText = "NO TRACK";
int marquee = 0;
const int NB = 25, MAXB = 12;
uint8_t lvl[NB] = {0}, peak[NB] = {0};
int grab = 0;                                   // 0 none, 1 volume, 2 seek, 3+i = eq slider i, 10 = list scrollbar
float seekFrac = 0;
uint32_t lastTouch = 0;
int batMv = 0, batPct = 0; bool charging = false;      // battery state
uint32_t repT = 0; bool repFirst = false;                 // volume +/- auto-repeat
bool radioMode = false; int radioIdx = 0; uint8_t radioSt = 0;   // 0 off, 1 wifi connecting, 3 streaming
uint32_t radioT = 0, radioStartMs = 0; int radioTries = 0; volatile bool radioLost = false;
void stopRadio(); void startRadio(int k); void enterUsbDrive();
int popId = -1;                                 // song shown in the "add to playlist" popup

// ---------------- layout ----------------
// main screen layout (RetroPlay style)
const int LCD_X = 4, LCD_Y = 19, LCD_W = 132, LCD_H = 97;         // time + spectrum display
const int RC_X = 140;                                             // right column
const int FB_Y = 69, FB_H = 47, FB_W = 41, FB_STEP = 45;          // EQ / PL / BT / PWR buttons
const int VOL_X = 26, VOL_W = 166, VOL_Y = 125, VOL_H = 10, VOL_KW = 20, VOL_KE = 5;
const int BAL_X = 222, BAL_W = 94;
const int SEEK_X = 4, SEEK_W = 312, SEEK_Y = 152, SEEK_KW = 20;
const int TY = 172, TH = 62;                                      // transport row
const int TB_X[8] = {4, 42, 80, 118, 156, 198, 240, 286}, TB_W[8] = {34, 34, 34, 34, 34, 36, 42, 30};
const int EQ_Y0 = 84, EQ_Y1 = 190;
const int PL_ROWS = 11, PL_ROW_H = 17, PL_Y0 = 20;
const int SB_X = 286, SB_W = 28, SB_UP_Y = 18, SB_BTN_H = 24, SB_TR_Y = 46, SB_TR_H = 136, SB_DN_Y = 186;
const char *bandLbl[11] = {"PRE","70","180","320","600","1K","3K","6K","12K","14K","16K"};
int sliderX(int i) { return i == 0 ? 28 : 70 + (i - 1) * 26; }
int lvTop = 0, lvCount = 0;                     // scroll position / item count of the list on screen

// ---------------- drawing helpers ----------------
void bevel(int x, int y, int w, int h, bool raised, uint16_t fill) {
  fb.fillRect(x, y, w, h, fill);
  uint16_t a = raised ? C_LIGHT : C_DARK, b = raised ? C_DARK : C_LIGHT;
  fb.drawFastHLine(x, y, w, a);         fb.drawFastVLine(x, y, h, a);
  fb.drawFastHLine(x, y + h - 1, w, b); fb.drawFastVLine(x + w - 1, y, h, b);
}

void drawBattery() {                                        // shown in every title bar
  const int x = 292, y = 2;
  uint16_t col = batPct > 30 ? C_GRN : (batPct > 15 ? C_YEL : C(0xF0,0x40,0x40));
  fb.drawRect(x, y, 20, 9, C_TXT); fb.fillRect(x + 20, y + 3, 2, 3, C_TXT);
  fb.fillRect(x + 2, y + 2, (16 * batPct) / 100, 5, col);
  if (charging) {                                           // lightning bolt
    fb.fillTriangle(x + 11, y + 1, x + 7, y + 5, x + 11, y + 5, C_TXT);
    fb.fillTriangle(x + 9, y + 4, x + 13, y + 4, x + 8, y + 8, C_TXT);
  }
  char b[8]; snprintf(b, sizeof(b), "%d%%", batPct);
  fb.setFont(&fonts::Font0); fb.setTextSize(1); fb.setTextColor(C_TXT);
  fb.setCursor(x - (int)strlen(b) * 6 - 3, 3); fb.print(b);
}

const uint16_t C_SILVER = C(0xC4,0xC6,0xD2), C_SILVER_HI = C(0xE8,0xEA,0xF2), C_SILVER_LO = C(0x80,0x82,0x92);
const uint16_t C_ICON = C(0x1E,0x20,0x2C), C_GOLD = C(0xC8,0xB4,0x78), C_GOLD2 = C(0x8A,0x7C,0x56), C_TITLE = C(0xE4,0xDC,0xC4);
const uint16_t C_PANEL = C(0x2C,0x2E,0x46), C_GROOVE = C(0x12,0x13,0x20), C_VOLFILL = C(0xA8,0xB0,0x48), C_SEEKFILL = C(0xF0,0x80,0x10);   // orange seek fill

void titleBar(const char *txt) {                            // RetroPlay title: menu icon, gold double lines, spaced title
  fb.fillRect(0, 0, 320, 16, C(0x34,0x36,0x52));
  fb.drawFastHLine(0, 15, 320, C_DARK);
  for (int i = 0; i < 3; i++) fb.fillRect(5, 3 + i * 4, 9, 2, C_TITLE);
  int n = strlen(txt), sp = (n * 8 - 2 <= 190) ? 8 : 6;            // letter-spaced; long titles use normal spacing
  n = min(n, 230 / sp);                                              // never run into the battery
  int tw = n * sp - (sp - 6), tx = max(20, 150 - tw / 2);
  fb.setFont(&fonts::Font0); fb.setTextSize(1); fb.setTextColor(C_TITLE);
  for (int i = 0; i < n; i++) { fb.setCursor(tx + i * sp, 4); fb.print(txt[i]); }
  int l1 = 20, r1 = tx - 6, l2 = tx + tw + 6, r2 = 258;        // gold lines either side (skipped where there is no room)
  for (int k = 0; k < 2; k++) {
    fb.drawFastHLine(l1, 6 + k * 3, max(0, r1 - l1), k ? C_GOLD2 : C_GOLD);
    fb.drawFastHLine(l2, 6 + k * 3, max(0, r2 - l2), k ? C_GOLD2 : C_GOLD);
  }
  drawBattery();
}

// silver push button (RetroPlay style); pressed = drawn sunken
void sbutton(int x, int y, int w, int h, bool pressed) {
  fb.fillRect(x, y, w, h, pressed ? C(0xA8,0xAA,0xB8) : C_SILVER);
  fb.fillRect(x + 1, y + 1, w - 2, h / 3, pressed ? C(0xB0,0xB2,0xC0) : C(0xD2,0xD4,0xDE));   // soft top highlight
  uint16_t a = pressed ? C_SILVER_LO : C_SILVER_HI, b = pressed ? C_SILVER_HI : C(0x50,0x52,0x62);
  fb.drawFastHLine(x, y, w, a); fb.drawFastVLine(x, y, h, a);
  fb.drawFastHLine(x, y + h - 1, w, b); fb.drawFastVLine(x + w - 1, y, h, b);
  fb.drawFastHLine(x + 1, y + h - 2, w - 2, C_SILVER_LO);
}
void led(int x, int y, uint16_t col) { fb.fillRect(x, y, 5, 5, col); fb.drawRect(x - 1, y - 1, 7, 7, C(0x70,0x72,0x80)); }
const uint16_t LED_ON = C(0x30,0xE0,0x30), LED_OFF = C(0x20,0x40,0x20);

// silver button with a centred caption and an optional indicator light (hasLed)
void sbtn(int x, int y, int w, int h, const char *t, bool hasLed = false, uint16_t ledCol = 0, bool pressed = false, bool big = false) {
  sbutton(x, y, w, h, pressed);
  if (big) fb.setFont(&fonts::Font2); else fb.setFont(&fonts::Font0);
  fb.setTextSize(1); fb.setTextColor(C_ICON);
  int tw = fb.textWidth(t), th = big ? 14 : 8, lw = hasLed ? 9 : 0;
  int tx = x + (w - tw - lw) / 2 + lw, ty = y + (h - th) / 2 + (big ? 0 : 1);
  if (hasLed) led(tx - 9, y + h / 2 - 3, ledCol);
  fb.setCursor(tx, ty); fb.print(t);
}

// green -> yellow -> orange colour for position t (0..1) along a bar
uint16_t gradCol(float t) {
  t = constrain(t, 0.0f, 1.0f);
  int r, g, b;
  if (t < 0.5f) { float u = t * 2; r = (int)(0x00 + (0xF0 - 0x00) * u); g = (int)(0xE0 + (0xE0 - 0xE0) * u); b = (int)(0x30 * (1 - u)); }
  else          { float u = (t - 0.5f) * 2; r = 0xF0; g = (int)(0xE0 + (0x80 - 0xE0) * u); b = (int)(0x10 * u); }
  return C(r, g, b);
}

// retro slider: dark groove, optional coloured fill, silver knob with grip lines
// fillMode: 0 = solid `fill` from the left, 1 = green->yellow->orange gradient from the left,
//           2 = balance: green over the whole groove when centred, red from the centre to the knob otherwise
void rslider(int x, int y, int w, int h, float frac, int kw, uint16_t fill, bool centreMark = false, int fillMode = 0) {
  fb.fillRect(x, y, w, h, C_GROOVE);
  fb.drawFastHLine(x, y, w, C_DARK); fb.drawFastHLine(x, y + h - 1, w, C(0x3A,0x3C,0x58));
  int tx = x + (int)(frac * (w - kw));
  if (fillMode == 1) {
    int fw = max(0, tx - x + kw / 2 - 1);
    for (int i = 0; i < fw; i++) fb.drawFastVLine(x + 1 + i, y + 2, h - 4, gradCol((float)i / max(1, w - 2)));
  } else if (fillMode == 2) {
    int cx = x + w / 2, kc = tx + kw / 2;
    if (abs(kc - cx) <= 1) fb.fillRect(x + 1, y + 2, w - 2, h - 4, C(0x00,0xC0,0x20));
    else fb.fillRect(min(cx, kc), y + 2, abs(kc - cx), h - 4, C(0xE0,0x20,0x20));
  } else if (fill) fb.fillRect(x + 1, y + 2, max(0, tx - x + kw / 2 - 1), h - 4, fill);
  if (centreMark) fb.drawFastVLine(x + w / 2, y + 1, h - 2, C(0x50,0x52,0x70));
  int ky = y - 4, kh = h + 8;
  sbutton(tx, ky, kw, kh, false);
  for (int g = -1; g <= 1; g++) fb.drawFastVLine(tx + kw / 2 + g * 3, ky + 4, kh - 8, C_ICON);
}

void seg7(int x, int y, int d, int w, int h, int t) {
  static const uint8_t m[10] = {0x3F,0x06,0x5B,0x4F,0x66,0x6D,0x7D,0x07,0x7F,0x6F};
  const int hh = h / 2;
  uint8_t s = d < 0 ? 0x40 : m[d % 10];                  // d < 0 = dash (clock not synced yet)
  auto seg = [&](int bit, int sx, int sy, int sw, int sh) { fb.fillRect(sx, sy, sw, sh, (s >> bit) & 1 ? C_GRN : C_DGRN); };
  seg(0, x + t, y,           w - 2 * t, t);
  seg(1, x + w - t, y + t,   t, hh - t);
  seg(2, x + w - t, y + hh,  t, hh - t);
  seg(3, x + t, y + h - t,   w - 2 * t, t);
  seg(4, x, y + hh,          t, hh - t);
  seg(5, x, y + t,           t, hh - t);
  seg(6, x + t, y + hh - t / 2, w - 2 * t, t);
}

void button(int x, int y, int w, int h, bool active) { bevel(x, y, w, h, !active, active ? C_ACT : C_BODY); }

void label(int x, int y, const char *s, uint16_t col) {
  fb.setFont(&fonts::Font0); fb.setTextSize(1); fb.setTextColor(col); fb.setCursor(x, y); fb.print(s);
}
void label2(int x, int y, const char *s, uint16_t col) {
  fb.setFont(&fonts::Font2); fb.setTextSize(1); fb.setTextColor(col); fb.setCursor(x, y); fb.print(s);
}

void drawHSlider(int x, int y, int w, int h, float frac, int kw, int ke, bool ticks = false) {
  bevel(x, y, w, h, false, C_DARK);
  int cy = y + h / 2, tx = x + (int)(frac * (w - kw));
  if (ticks) {                                               // tick marks above and below the groove
    for (int i = 0; i <= 21; i++) {
      int px = x + kw / 2 + i * (w - kw) / 21, tl = (i % 3 == 0) ? 3 : 1;
      fb.drawFastVLine(px, y + 1, tl, C_LIGHT);
      fb.drawFastVLine(px, y + h - 1 - tl, tl, C_LIGHT);
    }
  }
  fb.fillRect(x + 4, cy - 2, w - 8, 4, C_LCD);               // recessed groove
  fb.drawFastHLine(x + 4, cy - 3, w - 8, C_DARK);
  fb.drawFastHLine(x + 4, cy + 2, w - 8, C_LIGHT);
  fb.fillRect(x + 4, cy - 2, max(0, tx - x - 4 + kw / 2), 4, C_GRN);   // filled part
  bevel(tx, y - ke, kw, h + 2 * ke, true, C_KNOB);
  for (int g = -1; g <= 1; g++) {                            // grip ridges on the knob
    fb.drawFastVLine(tx + kw / 2 + g * 3, y - ke + 3, h + 2 * ke - 6, C_DARK);
    fb.drawFastVLine(tx + kw / 2 + g * 3 + 1, y - ke + 3, h + 2 * ke - 6, C_LIGHT);
  }
}
float fracAt(int px, int X, int W, int kw) { return constrain((px - X - kw / 2) / (float)(W - kw), 0.0f, 1.0f); }

uint32_t prevMs = 0, nextMs = 0;          // prev / next lights flash briefly when pressed
void drawTransport() {                    // active functions light up (green light + green icon), buttons never look pressed
  const uint16_t G = C(0x20,0xB0,0x30);
  for (int i = 0; i < 8; i++) {
    int x = TB_X[i], w = TB_W[i], cx = x + w / 2, cy = TY + TH / 2;
    bool act = (i == 0 && millis() - prevMs < 400) || (i == 1 && state == ST_PLAY) || (i == 2 && state == ST_PAUSE) ||
               (i == 3 && state == ST_STOP) || (i == 4 && millis() - nextMs < 400) ||
               (i == 5 && (srcType != 0 || radioMode)) ||                      // eject: a folder / playlist / radio is selected
               (i == 6 && shuffleOn) || (i == 7 && repMode);
    sbutton(x, TY, w, TH, false);
    uint16_t c = act ? G : C_ICON;
    led(x + 4, TY + 4, act ? LED_ON : LED_OFF);
    switch (i) {
      case 0: fb.fillRect(cx - 7, cy - 7, 3, 14, c); fb.fillTriangle(cx + 7, cy - 7, cx + 7, cy + 7, cx - 4, cy, c); break;          // prev
      case 1: fb.fillTriangle(cx - 5, cy - 8, cx - 5, cy + 8, cx + 8, cy, c); break;                                                 // play
      case 2: fb.fillRect(cx - 6, cy - 7, 4, 14, c); fb.fillRect(cx + 2, cy - 7, 4, 14, c); break;                                     // pause
      case 3: fb.fillRect(cx - 6, cy - 6, 12, 12, c); break;                                                                           // stop
      case 4: fb.fillRect(cx + 5, cy - 7, 3, 14, c); fb.fillTriangle(cx - 7, cy - 7, cx - 7, cy + 7, cx + 4, cy, c); break;            // next
      case 5: fb.fillTriangle(cx - 8, cy + 2, cx + 8, cy + 2, cx, cy - 8, c); fb.fillRect(cx - 8, cy + 5, 17, 3, c); break;            // eject = sources
      case 6: fb.setFont(&fonts::Font0); fb.setTextColor(c); fb.setCursor(cx - 12, cy - 3); fb.print("SHUF"); break;                  // shuffle
      case 7: fb.drawArc(cx, cy, 7, 5, 0, 240, c); fb.drawArc(cx, cy, 7, 5, 300, 360, c);                                            // repeat
              fb.fillTriangle(cx - 2, cy - 9, cx + 3, cy - 6, cx - 2, cy - 3, c);
              if (repMode == 2) { fb.setFont(&fonts::Font0); fb.setTextColor(c); fb.setCursor(cx - 2, cy - 3); fb.print("1"); }
              break;
    }
  }
}

// spectrum inside the time display: smooth bars, green -> yellow -> orange, white peak caps
void drawAnalyzer(uint16_t vu) {
  const int bars = 18, bw = 6, gap = 1, x0 = LCD_X + 4, base = LCD_Y + LCD_H - 5, hmax = 50;
  int target = (state == ST_PLAY) ? min(MAXB, (int)(((vu >> 8) + (vu & 0xFF)) * MAXB / 280)) : 0;
  for (int i = 0; i < bars; i++) {
    int wgt = constrain(100 - abs(i - 5) * 5, 40, 100);
    int h = target * wgt / 100 * (55 + random(0, 45)) / 100;
    if (h > lvl[i]) lvl[i] = h; else if (lvl[i] > 0) lvl[i]--;
    if (lvl[i] >= peak[i]) peak[i] = lvl[i]; else if (peak[i] > 0 && (millis() / 60) % 2) peak[i]--;
    int x = x0 + i * (bw + gap), hp = lvl[i] * hmax / MAXB;
    for (int r = 0; r < hp; r++) {
      float f = r / (float)hmax;                                  // 0 bottom .. 1 top
      uint16_t col = f < 0.5f ? C((uint8_t)(0x60 + f * 2 * 0x90), 0xD0, 0x30) : C(0xF0, (uint8_t)(0xD0 - (f - 0.5f) * 2 * 0x80), 0x30);
      fb.drawFastHLine(x, base - r, bw, col);
    }
    if (peak[i] > 0) fb.fillRect(x, base - peak[i] * hmax / MAXB - 3, bw, 2, C(0xD0,0xD0,0xE0));
  }
}

// ---------------- boot animation ----------------
// Runs in its own task on core 0 while setup() reads the SD card on core 1, so it stays smooth
// even while the card is busy. setup() only updates bootStatus / bootCount / bootTotal.
volatile const char *bootStatus = "STARTING...";
volatile int bootCount = 0, bootTotal = 0;
volatile bool bootDone = false, bootAnimRunning = false, bootAnimFinished = true;
const uint32_t BOOT_MIN_MS = 2800, BOOT_OUTRO_MS = 750;

static float easeOutCubic(float x) { x = constrain(x, 0.0f, 1.0f); return 1 - powf(1 - x, 3); }
static float easeOutBack(float x)  { x = constrain(x, 0.0f, 1.0f); const float c1 = 1.70158f, c3 = c1 + 1; return 1 + c3 * powf(x - 1, 3) + c1 * powf(x - 1, 2); }
static float phase(uint32_t t, uint32_t a, uint32_t b) { return t <= a ? 0 : (t >= b ? 1 : (float)(t - a) / (b - a)); }

void bootFrame(uint32_t t, float outro) {
  static uint16_t bg[240]; static bool bgInit = false; static float peak[30];
  if (!bgInit) {                                                            // gradient + scanlines, computed once
    for (int y = 0; y < 240; y++) {
      float k = y / 239.0f;
      uint8_t r = 10 + 26 * k, g = 12 + 4 * k, b = 26 + 30 * k;
      if (y % 3 == 2) { r = r * 3 / 4; g = g * 3 / 4; b = b * 3 / 4; }
      bg[y] = C(r, g, b);
    }
    for (int i = 0; i < 30; i++) peak[i] = 0;
    bgInit = true;
  }
  for (int y = 0; y < 240; y++) fb.drawFastHLine(0, y, 320, bg[y]);

  // ---- spinning record (drops in with a little bounce) ----
  const int cx = 86, cy = 96; float sc = easeOutBack(phase(t, 150, 800));
  int R = 60 * sc;
  if (R > 2) {
    float a = t * 0.0045f;
    fb.fillCircle(cx + 3, cy + 4, R, C(4, 4, 10));                         // shadow
    fb.fillCircle(cx, cy, R, C(14, 14, 18));
    for (int k = 0; k < 2; k++) {                                         // two light reflections turning with the disc
      float b = a + k * PI;
      fb.fillTriangle(cx, cy, cx + R * cosf(b - 0.22f), cy + R * sinf(b - 0.22f), cx + R * cosf(b + 0.22f), cy + R * sinf(b + 0.22f), C(52, 52, 64));
    }
    for (int r = R * 0.38f; r < R - 2; r += 3) fb.drawCircle(cx, cy, r, (r / 3) & 1 ? C(30, 30, 38) : C(22, 22, 28));   // grooves
    fb.drawCircle(cx, cy, R, C(90, 90, 104)); fb.drawCircle(cx, cy, R - 1, C(60, 60, 72));
    int L = R * 0.33f;                                                    // label
    fb.fillCircle(cx, cy, L, C_ORG); fb.drawCircle(cx, cy, L, C(255, 170, 70));
    if (L > 8) {
      fb.fillCircle(cx + (L - 6) * cosf(a), cy + (L - 6) * sinf(a), 2, C_TXT);
      fb.fillArc(cx, cy, L - 9, L - 11, (a * 57.3f) + 120, (a * 57.3f) + 200, C(255, 220, 160));
    }
    fb.fillCircle(cx, cy, 3, C(5, 5, 8));
  }
  // ---- tonearm swings onto the record ----
  { const int px = 170, py = 30;
    float sw = easeOutCubic(phase(t, 650, 1250));
    float ang = (1 - sw) * 1.85f + sw * 2.35f;                          // rest (off the record) -> playing
    int sx = px + 64 * cosf(ang), sy = py + 64 * sinf(ang);
    fb.fillCircle(px, py, 9, C(150, 150, 160)); fb.fillCircle(px, py, 4, C(40, 40, 48));
    fb.drawWideLine(px, py, sx, sy, 2, C(200, 200, 210));
    fb.drawWideLine(sx, sy, sx - 9 * cosf(ang - 0.6f), sy - 9 * sinf(ang - 0.6f) + 0, 3, C_ORG); }

  // ---- logo slides in from the right ----
  { float lo = easeOutCubic(phase(t, 500, 1150)); int ox = (1 - lo) * 160;
    fb.setTextDatum(0);                                                 // top-left
    fb.setFont(&fonts::FreeSansBold18pt7b); fb.setTextColor(C_ORG); fb.drawString("ESP", 200 + ox, 34);
    fb.setFont(&fonts::FreeSansBold9pt7b);  fb.setTextColor(C_TXT); fb.drawString("AUDIO", 202 + ox, 74); fb.drawString("PLAYER", 202 + ox, 94);
    int ux = 202 + ox, uw = 110;                                         // underline with a light running along it
    fb.fillRect(ux, 118, uw, 2, C(120, 60, 10));
    int kx = ux + (int)((sinf(t * 0.003f) * 0.5f + 0.5f) * (uw - 24));
    fb.fillRect(kx, 117, 24, 4, C(255, 190, 90));
    if (t > 1250) { fb.setFont(&fonts::Font0); fb.setTextColor(C_LIGHT); fb.drawString("ESP32-S3  //  HI-FI", 202, 128); } }

  // ---- spectrum analyzer ----
  { float env = easeOutCubic(phase(t, 900, 1700)) * (1 + outro * 0.6f);
    const int base = 202, segH = 4, segs = 10;
    for (int i = 0; i < 30; i++) {
      float tt = t * 0.001f;
      float v = 0.30f + 0.70f * fabsf(sinf(tt * (2.1f + i * 0.17f) + i * 0.7f)) * (0.6f + 0.4f * sinf(tt * 1.3f + i * 0.31f));
      v *= env * (1.12f - i / 40.0f);
      v = constrain(v, 0.0f, 1.0f);
      if (v > peak[i]) peak[i] = v; else peak[i] = max(0.0f, peak[i] - 0.025f);
      int x = 10 + i * 10, n = v * segs;
      for (int k = 0; k < n; k++) {
        uint16_t col = k < 5 ? C(40, 220, 70) : (k < 7 ? C(230, 220, 40) : (k < 9 ? C_ORG : C(240, 60, 50)));
        fb.fillRect(x, base - (k + 1) * segH, 8, segH - 1, col);
      }
      int pk = peak[i] * segs; if (pk > 0) fb.fillRect(x, base - pk * segH - 2, 8, 1, C_TXT);
    }
  }
  // ---- status line + progress ----
  { char b[64];
    const char *st = outro > 0 ? "READY" : (const char *)bootStatus;
    int n = bootCount, tot = bootTotal;
    if (outro > 0) snprintf(b, sizeof(b), "READY  -  %d songs", n);
    else if (n > 0) snprintf(b, sizeof(b), "%s  %d songs", st, n);
    else snprintf(b, sizeof(b), "%s", st);
    fb.setFont(&fonts::Font2); fb.setTextColor(outro > 0 ? C_YEL : C_GRN);
    fb.drawString(b, 10, 208);
    if (outro == 0 && (t / 400) % 2) fb.fillRect(10 + fb.textWidth(b) + 3, 211, 6, 10, C_GRN);   // blinking cursor
    fb.drawRect(10, 228, 300, 6, C(60, 60, 80));
    if (outro > 0 || bootDone) fb.fillRect(11, 229, 298, 4, C_ORG);
    else if (tot > 0) fb.fillRect(11, 229, 298 * min(n, tot) / tot, 4, C_ORG);
    else { int w = 60, x = 11 + (int)((sinf(t * 0.004f) * 0.5f + 0.5f) * (298 - w)); fb.fillRect(x, 229, w, 4, C_ORG); }   // busy
  }
  // ---- outro: shutter closes ----
  if (outro > 0.45f) { int h = easeOutCubic((outro - 0.45f) / 0.55f) * 121; fb.fillRect(0, 0, 320, h, TFT_BLACK); fb.fillRect(0, 240 - h, 320, h, TFT_BLACK); }
}

void bootAnimTask(void *) {
  uint32_t t0 = millis(), tOut = 0;
  for (;;) {
    uint32_t t = millis() - t0;
    if (!tOut && bootDone && t >= BOOT_MIN_MS) tOut = t;
    float outro = tOut ? min(1.0f, (float)(t - tOut) / BOOT_OUTRO_MS) : 0;
    bootFrame(t, outro);
    fb.pushSprite(0, 0);
    if (outro >= 1) break;
    vTaskDelay(pdMS_TO_TICKS(15));
  }
  bootAnimRunning = false; bootAnimFinished = true;
  vTaskDelete(NULL);
}
void startBootAnim() {
#if BOOT_ANIM
  bootAnimRunning = true; bootAnimFinished = false;
  xTaskCreatePinnedToCore(bootAnimTask, "boot", 6144, NULL, 1, NULL, 0);
#endif
}
void finishBootAnim() {                                        // called when setup() is done: wait for the outro
  bootDone = true;
  while (!bootAnimFinished) delay(10);
}

void showMsg(const char *a, int n) {                       // progress screen while scanning
  bootStatus = a; bootCount = n;
  if (bootAnimRunning) return;                             // the boot animation draws it
  fb.fillScreen(C_BODY); titleBar("ESPAMP");
  label2(20, 90, a, C_GRN);
  char b[32]; snprintf(b, sizeof(b), "%d songs found", n);
  label2(20, 116, b, C_TXT);
  fb.pushSprite(0, 0);
}

// ---------------- ID3 tag reading ----------------
static uint32_t synchsafe(const uint8_t *b) { return ((uint32_t)(b[0] & 0x7F) << 21) | ((uint32_t)(b[1] & 0x7F) << 14) | ((uint32_t)(b[2] & 0x7F) << 7) | (b[3] & 0x7F); }

String decodeText(const uint8_t *d, size_t n) {
  String s;
  if (n < 2) return s;
  uint8_t enc = d[0];
  if (enc == 0 || enc == 3) {
    for (size_t i = 1; i < n && d[i]; i++) s += (d[i] >= 128) ? '?' : (char)d[i];
  } else {
    size_t i = 1; bool le = true;
    if (n >= 3 && d[1] == 0xFF && d[2] == 0xFE) i = 3;
    else if (n >= 3 && d[1] == 0xFE && d[2] == 0xFF) { le = false; i = 3; }
    else if (enc == 2) le = false;
    for (; i + 1 < n; i += 2) {
      uint8_t lo = le ? d[i] : d[i + 1], hi = le ? d[i + 1] : d[i];
      if (!lo && !hi) break;
      s += (hi == 0 && lo < 128) ? (char)lo : '?';
    }
  }
  s.trim();
  return s;
}

void readID3(const char *path, String &artist, String &title, String &album, int &trackNo) {
  File f = SD_MMC.open(path);
  if (!f) return;
  uint8_t h[10];
  if (f.read(h, 10) != 10 || memcmp(h, "ID3", 3) != 0) { f.close(); return; }
  uint8_t ver = h[3];
  uint32_t end = 10 + synchsafe(h + 6), pos = 10;
  if (ver >= 3 && (h[5] & 0x40)) {
    uint8_t eh[4]; f.read(eh, 4);
    uint32_t es = (ver == 4) ? synchsafe(eh) : (((uint32_t)eh[0] << 24) | ((uint32_t)eh[1] << 16) | ((uint32_t)eh[2] << 8) | eh[3]) + 4;
    pos += es;
  }
  const int hl = (ver == 2) ? 6 : 10;
  while (pos + hl <= end && (artist.isEmpty() || title.isEmpty() || album.isEmpty() || !trackNo)) {
    uint8_t fh[10];
    f.seek(pos);
    if (f.read(fh, hl) != hl || fh[0] == 0) break;
    uint32_t sz = (ver == 2) ? (((uint32_t)fh[3] << 16) | ((uint32_t)fh[4] << 8) | fh[5])
                : (ver == 4) ? synchsafe(fh + 4)
                : (((uint32_t)fh[4] << 24) | ((uint32_t)fh[5] << 16) | ((uint32_t)fh[6] << 8) | fh[7]);
    char id[5] = {0}; memcpy(id, fh, ver == 2 ? 3 : 4);
    bool isArt = !strcmp(id, ver == 2 ? "TP1" : "TPE1"), isTit = !strcmp(id, ver == 2 ? "TT2" : "TIT2");
    bool isAlb = !strcmp(id, ver == 2 ? "TAL" : "TALB"), isTrk = !strcmp(id, ver == 2 ? "TRK" : "TRCK");
    if ((isArt || isTit || isAlb || isTrk) && sz > 1) {
      uint8_t buf[120];
      size_t got = f.read(buf, min(sz, (uint32_t)sizeof(buf)));
      String s = decodeText(buf, got);
      if (isArt && artist.isEmpty()) artist = s;
      if (isTit && title.isEmpty()) title = s;
      if (isAlb && album.isEmpty()) album = s;
      if (isTrk && !trackNo) trackNo = s.toInt();
    }
    pos += hl + sz;
  }
  f.close();
}

// ---------------- scanning (all folders) + tag cache ----------------
Track *cache = nullptr; int cacheN = 0, cacheHint = 0; bool cacheDirty = false;

const Track *findCache(const char *path) {
  for (int k = 0; k < cacheN; k++) {
    int i = (cacheHint + k) % cacheN;
    if (!strcmp(cache[i].path, path)) { cacheHint = i + 1; return &cache[i]; }
  }
  return nullptr;
}

uint8_t folderIdFor(const char *path) {
  char dir[96];
  const char *sl = strrchr(path, '/');
  size_t n = (sl && sl > path) ? (size_t)(sl - path) : 0;
  if (n >= sizeof(dir)) n = sizeof(dir) - 1;
  if (n == 0) { dir[0] = '/'; dir[1] = 0; } else { memcpy(dir, path, n); dir[n] = 0; }
  for (int i = 0; i < folderCount; i++) if (!strcasecmp(folders[i], dir)) { folderN[i]++; return i; }
  if (folderCount < MAX_FOLDERS) { strlcpy(folders[folderCount], dir, sizeof(folders[0])); folderN[folderCount] = 1; return folderCount++; }
  folderN[0]++; return 0;
}

void addFile(const char *path, uint32_t size, uint32_t mtime) {
  if (trackCount >= MAX_TRACKS || strlen(path) >= (size_t)PATH_LEN) return;
  Track &t = tr[trackCount];
  memset(&t, 0, sizeof(Track));
  strlcpy(t.path, path, PATH_LEN); t.size = size; t.mtime = mtime;
  const Track *c = findCache(path);
  if (c && c->size == size && c->mtime == mtime) {
    strlcpy(t.artist, c->artist, sizeof(t.artist)); strlcpy(t.title, c->title, sizeof(t.title));
    strlcpy(t.album, c->album, sizeof(t.album));    t.trackNo = c->trackNo;
  } else {
    cacheDirty = true;
    String artist, title, album; int trackNo = 0;
    String low = path; low.toLowerCase();
    if (low.endsWith(".mp3")) readID3(path, artist, title, album, trackNo);
    String base = path; int sl = base.lastIndexOf('/'); if (sl >= 0) base = base.substring(sl + 1);
    int dot = base.lastIndexOf('.'); if (dot > 0) base = base.substring(0, dot);
    int sep = base.indexOf(" - ");                              // fallback: "Artist - Title.mp3"
    if (artist.isEmpty() && sep > 0) { artist = base.substring(0, sep); if (title.isEmpty()) title = base.substring(sep + 3); }
    if (title.isEmpty()) title = base;
    strlcpy(t.artist, artist.c_str(), sizeof(t.artist)); strlcpy(t.title, title.c_str(), sizeof(t.title));
    strlcpy(t.album, album.c_str(), sizeof(t.album));    t.trackNo = trackNo;
  }
  t.folderId = folderIdFor(path);
  trackCount++;
  if (bootAnimRunning) { bootStatus = "SCANNING SD CARD..."; bootCount = trackCount; }
  else if (trackCount % 20 == 0) showMsg("SCANNING SD CARD...", trackCount);
}

void scanDir(const char *dirPath, int depth) {
  File d = SD_MMC.open(dirPath);
  if (!d || !d.isDirectory()) return;
  for (File f = d.openNextFile(); f; f = d.openNextFile()) {
    String nm = f.name();
    if (nm.startsWith(".")) continue;
    String full = String(strcmp(dirPath, "/") ? dirPath : "") + "/" + nm;
    if (f.isDirectory()) {
      if (depth < 5 && !nm.equalsIgnoreCase("System Volume Information") && !nm.equalsIgnoreCase("playlists")) scanDir(full.c_str(), depth + 1);
    } else {
      String low = nm; low.toLowerCase();
      if (low.endsWith(".mp3") || low.endsWith(".wav") || low.endsWith(".flac")) addFile(full.c_str(), f.size(), (uint32_t)f.getLastWrite());
    }
  }
}

const uint32_t CACHE_MAGIC = 0x45534132;
void loadCache() {
  File f = SD_MMC.open("/espamp.idx");
  if (!f) return;
  uint32_t hdr[3];
  if (f.read((uint8_t *)hdr, sizeof(hdr)) != sizeof(hdr) || hdr[0] != CACHE_MAGIC || hdr[2] != sizeof(Track) || hdr[1] == 0 || hdr[1] > (uint32_t)MAX_TRACKS) { f.close(); return; }
  size_t want = (size_t)hdr[1] * sizeof(Track);
  cache = (Track *)ps_malloc(want);
  if (cache && f.read((uint8_t *)cache, want) == want) cacheN = hdr[1];
  bootTotal = cacheN;                                        // expected song count for the boot progress bar
  f.close();
}
void saveCache() {
  File w = SD_MMC.open("/espamp.idx", FILE_WRITE);
  if (!w) return;
  uint32_t hdr[3] = {CACHE_MAGIC, (uint32_t)trackCount, (uint32_t)sizeof(Track)};
  w.write((uint8_t *)hdr, sizeof(hdr));
  w.write((uint8_t *)tr, (size_t)trackCount * sizeof(Track));
  w.close();
}

// ---------------- playlists (/playlists/NAME.m3u) ----------------
void buildPathIndex() {
  for (int i = 0; i < trackCount; i++) byPath[i] = i;
  std::sort(byPath, byPath + trackCount, [](uint16_t a, uint16_t b) { return strcasecmp(tr[a].path, tr[b].path) < 0; });
}
int findPath(const char *p) {
  int lo = 0, hi = trackCount - 1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2, c = strcasecmp(p, tr[byPath[mid]].path);
    if (!c) return byPath[mid];
    if (c < 0) hi = mid - 1; else lo = mid + 1;
  }
  return -1;
}
void plPath(int p, char *out, size_t n) { snprintf(out, n, "/playlists/%s.m3u", plName[p]); }
void savePlaylist(int p) {
  char path[64]; plPath(p, path, sizeof(path));
  File w = SD_MMC.open(path, FILE_WRITE);
  if (!w) return;
  for (int i = 0; i < plN[p]; i++) { w.print(tr[plTr[p * MAX_TRACKS + i]].path); w.print("\n"); }
  w.close();
}
int plFind(int p, int id) { for (int i = 0; i < plN[p]; i++) if (plTr[p * MAX_TRACKS + i] == id) return i; return -1; }
void plToggle(int p, int id) {
  int i = plFind(p, id);
  if (i >= 0) { memmove(&plTr[p * MAX_TRACKS + i], &plTr[p * MAX_TRACKS + i + 1], (plN[p] - i - 1) * sizeof(uint16_t)); plN[p]--; }
  else if (plN[p] < MAX_TRACKS) plTr[p * MAX_TRACKS + plN[p]++] = id;
  savePlaylist(p);
}
int createPlaylist(const char *name) {
  if (plCount >= MAX_PL) return -1;
  strlcpy(plName[plCount], name, sizeof(plName[0])); plN[plCount] = 0;
  savePlaylist(plCount);
  return plCount++;
}
void loadPlaylists() {
  SD_MMC.mkdir("/playlists");
  buildPathIndex();
  File d = SD_MMC.open("/playlists");
  if (d && d.isDirectory()) {
    for (File f = d.openNextFile(); f && plCount < MAX_PL; f = d.openNextFile()) {
      String nm = f.name(), low = nm; low.toLowerCase();
      if (f.isDirectory() || !low.endsWith(".m3u")) continue;
      int p = plCount++;
      strlcpy(plName[p], nm.substring(0, nm.length() - 4).c_str(), sizeof(plName[0])); plN[p] = 0;
      while (f.available()) {
        String line = f.readStringUntil('\n'); line.trim();
        if (!line.length() || line[0] == '#') continue;
        line.replace('\\', '/');
        if (line[0] != '/') line = "/" + line;
        int id = findPath(line.c_str());
        if (id >= 0 && plN[p] < MAX_TRACKS && plFind(p, id) < 0) plTr[p * MAX_TRACKS + plN[p]++] = id;
      }
    }
  }
  if (plCount == 0) createPlaylist("Favorites");
}

// ---------------- active list, sorting, playback ----------------
static bool cmpTrack(uint16_t ia, uint16_t ib) {
  const Track &a = tr[ia], &b = tr[ib];
  int c;
  switch (sortMode) {
    case 4: return rankArr[ia] < rankArr[ib];
    case 3: if (a.mtime != b.mtime) return a.mtime > b.mtime; return rankArr[ia] < rankArr[ib];
    case 2: {
      bool ea = !a.album[0], eb = !b.album[0];
      if (ea != eb) return eb;
      c = strcasecmp(a.album, b.album); if (c) return c < 0;
      int na = a.trackNo ? a.trackNo : 999, nb = b.trackNo ? b.trackNo : 999;
      if (na != nb) return na < nb;
      break;
    }
    case 1: {
      bool ea = !a.artist[0], eb = !b.artist[0];
      if (ea != eb) return eb;
      c = strcasecmp(a.artist, b.artist); if (c) return c < 0;
      break;
    }
  }
  c = strcasecmp(a.title, b.title); if (c) return c < 0;
  return rankArr[ia] < rankArr[ib];
}
void sortVis() { std::sort(vis, vis + visCount, cmpTrack); }

int visPos(int id) { for (int i = 0; i < visCount; i++) if (vis[i] == id) return i; return -1; }
void clearPlayed() { for (int i = 0; i < trackCount; i++) tr[i].played = 0; if (curId >= 0) tr[curId].played = 1; }

void buildVis() {
  visCount = 0;
  if (srcType == 2 && srcId < plCount) {
    for (int i = 0; i < plN[srcId]; i++) { int id = plTr[srcId * MAX_TRACKS + i]; rankArr[id] = visCount; vis[visCount++] = id; }
  } else if (srcType == 1 && srcId < folderCount) {
    for (int i = 0; i < trackCount; i++) if (tr[i].folderId == srcId) { rankArr[i] = visCount; vis[visCount++] = i; }
  } else {
    srcType = 0; srcId = 0;
    for (int i = 0; i < trackCount; i++) { rankArr[i] = i; vis[visCount++] = i; }
  }
  sortVis();
}

void dispName(char *buf, size_t n, int id) {
  const Track &t = tr[id];
  if (t.artist[0]) snprintf(buf, n, "%s - %s", t.artist, t.title); else snprintf(buf, n, "%s", t.title);
}
void srcName(char *buf, size_t n) {
  if (srcType == 1 && srcId < folderCount) snprintf(buf, n, "%s", strcmp(folders[srcId], "/") ? folders[srcId] + 1 : "(root)");
  else if (srcType == 2 && srcId < plCount) snprintf(buf, n, "%s", plName[srcId]);
  else snprintf(buf, n, "ALL SONGS");
}

void saveLastSong(uint32_t sec) {                                  // remembered across power-off
  if (curId < 0 || radioMode) return;
  prefs.putString("lastP", tr[curId].path); prefs.putUInt("lastT", sec);
}
uint32_t curSec() { LOCK(); uint32_t c = audio.getAudioCurrentTime(); UNLOCK(); return c; }
void playTrack(int id) {
  if (id < 0 || id >= trackCount) return;
  if (radioMode) stopRadio();
  curId = id;
  id3Title = ""; id3Artist = "";
  char nm[100]; dispName(nm, sizeof(nm), id); titleText = nm;
  marquee = 0;
  LOCK(); audio.connecttoFS(SD_MMC, tr[id].path); UNLOCK();
  state = ST_PLAY;
  tr[id].played = 1;
  lastStartMs = millis();
  if (id == resumeId && resumeSec > 3) resumePending = resumeSec;    // first play after power-on: continue where it was
  resumeId = -1;
  saveLastSong(0);
  Serial.printf("Playing: %s\n", tr[id].path);
}

int pickShuffle(bool wrap) {
  static uint16_t cand[MAX_TRACKS];
  int n = 0;
  for (int i = 0; i < visCount; i++) { int id = vis[i]; if (!tr[id].played && id != curId) cand[n++] = id; }
  if (!n) {
    if (!wrap) return -1;
    for (int i = 0; i < visCount; i++) tr[vis[i]].played = 0;
    if (curId >= 0) tr[curId].played = 1;
    for (int i = 0; i < visCount; i++) if (vis[i] != curId) cand[n++] = vis[i];
  }
  if (!n) return curId;
  return cand[random(n)];
}

void stopPlayback() { LOCK(); audio.stopSong(); UNLOCK(); state = ST_STOP; if (radioMode) stopRadio(); else saveLastSong(0); }
void togglePause()  { if (state == ST_STOP) return; if (state == ST_PLAY) saveLastSong(curSec()); LOCK(); audio.pauseResume(); UNLOCK(); state = (state == ST_PLAY) ? ST_PAUSE : ST_PLAY; }

void playNext() {                                                  // Next button
  if (radioMode) { startRadio((radioIdx + 1) % STATION_N); return; }
  if (!visCount) return;
  if (shuffleOn) { int n = pickShuffle(true); if (n >= 0) playTrack(n); return; }
  int pos = visPos(curId);
  playTrack(vis[(pos + 1) % visCount]);
}
void playPrev() {
  if (radioMode) { startRadio((radioIdx + STATION_N - 1) % STATION_N); return; }
  if (!visCount) return;
  int pos = visPos(curId);
  playTrack(vis[pos <= 0 ? visCount - 1 : pos - 1]);
}
void advanceAuto() {                                               // song ended by itself
  if (!visCount) { stopPlayback(); return; }
  if (repMode == 2 && curId >= 0) { playTrack(curId); return; }
  if (shuffleOn) { int n = pickShuffle(repMode == 1); if (n >= 0) playTrack(n); else stopPlayback(); return; }
  int pos = visPos(curId);
  if (pos < 0) playTrack(vis[0]);
  else if (pos < visCount - 1) playTrack(vis[pos + 1]);
  else if (repMode == 1) playTrack(vis[0]);
  else stopPlayback();
}

// ---------------- output routing ----------------
// Speaker: amp on.  BT: amp off (speaker silent), transmitter powered.  BOTH: amp + transmitter.
// The ES8311 always plays; the transmitter takes its line-level output, so the volume slider works in every mode.
void applyOutput() {
  bool amp = outMode != 1, bt = outMode != 0;
  digitalWrite(AUDIO_EN, amp ? AMP_ON_LEVEL : (AMP_ON_LEVEL == HIGH ? LOW : HIGH));
  if (BT_EN_PIN >= 0) digitalWrite(BT_EN_PIN, bt ? BT_ON_LEVEL : (BT_ON_LEVEL == HIGH ? LOW : HIGH));
  Serial.printf("Output: %s\n", OUT_NAME[outMode]);
}

// ---------------- screens ----------------
void drawMain() {
  fb.fillRect(0, 16, 320, 224, C_PANEL);
  titleBar("ESP AUDIO PLAYER");

  // ---- time + spectrum display ----
  fb.fillRect(LCD_X, LCD_Y, LCD_W, LCD_H, C_LCD);
  fb.drawRect(LCD_X - 1, LCD_Y - 1, LCD_W + 2, LCD_H + 2, C(0x50,0x52,0x70));
  if (state == ST_PLAY) fb.fillTriangle(10, 26, 10, 36, 16, 31, C_GRN);
  else if (state == ST_PAUSE) { fb.fillRect(10, 26, 2, 10, C_GRN); fb.fillRect(14, 26, 2, 10, C_GRN); }
  else fb.fillRect(10, 27, 8, 8, C_GRN);
  LOCK();
  uint32_t cur = audio.getAudioCurrentTime(), dur = audio.getAudioFileDuration();
  uint32_t br = audio.getBitRate(), sr = audio.getSampleRate(); uint8_t ch = audio.getChannels();
  uint16_t vu = audio.getVUlevel();
  UNLOCK();
  if (grab == 2 && dur) cur = (uint32_t)(seekFrac * dur);
  if (radioMode) cur = (radioSt == 3) ? (millis() - radioStartMs) / 1000 : 0;
  int mm = (cur / 60) % 100, ss = cur % 60;
  int pm = -1;                                                         // -1 = no AM/PM marker, 0 = AM, 1 = PM
  if (radioMode) {                                                     // live radio: show the local clock HH:MM instead of a timer
    struct tm t; bool ok = radioSt == 3 && getLocalTime(&t, 0) && t.tm_year > 120;
    int h = ok ? t.tm_hour : 0; if (ok && !CLOCK_24H) { pm = h >= 12; h %= 12; if (!h) h = 12; }
    mm = ok ? h : -1; ss = ok ? t.tm_min : -1;
  }
  if (!(state == ST_PAUSE && (millis() / 500) % 2)) {                  // blinks while paused
    seg7(22, 24, mm < 0 ? -1 : mm / 10, 18, 30, 3); seg7(44, 24, mm < 0 ? -1 : mm % 10, 18, 30, 3);
    fb.fillRect(66, 32, 3, 3, C_GRN); fb.fillRect(66, 44, 3, 3, C_GRN);
    seg7(73, 24, ss < 0 ? -1 : ss / 10, 18, 30, 3); seg7(95, 24, ss < 0 ? -1 : ss % 10, 18, 30, 3);
    if (pm >= 0) { label(118, 28, "AM", pm == 0 ? C_GRN : C_DGRN); label(118, 44, "PM", pm == 1 ? C_GRN : C_DGRN); }
  }
  drawAnalyzer(vu);

  // ---- song title (scrolling) ----
  fb.fillRect(RC_X, 19, 176, 18, C_LCD); fb.drawRect(RC_X - 1, 18, 178, 20, C(0x50,0x52,0x70));
  fb.setClipRect(RC_X + 2, 19, 172, 18);
  fb.setFont(&fonts::Font0); fb.setTextSize(1); fb.setTextColor(C_GRN); fb.setTextWrap(false);
  int tw = titleText.length() * 6;
  if (tw > 168) { marquee = (marquee + 1) % (tw + 40); fb.setCursor(RC_X + 4 - marquee, 24); fb.print(titleText); fb.setCursor(RC_X + 4 - marquee + tw + 40, 24); fb.print(titleText); }
  else { marquee = 0; fb.setCursor(RC_X + 4, 24); fb.print(titleText); }
  fb.clearClipRect();

  // ---- kbps / kHz / mono-stereo (middle) / track + volume (right) ----
  char b[24];
  bool stopped = state == ST_STOP;
  fb.fillRect(RC_X, 42, 24, 15, C_LCD);
  snprintf(b, sizeof(b), stopped ? "---" : "%3u", (unsigned)(br / 1000)); label(RC_X + 3, 46, b, C_GRN);
  label(RC_X + 27, 46, "kbps", C_TXT);
  fb.fillRect(RC_X + 54, 42, 18, 15, C_LCD);
  snprintf(b, sizeof(b), stopped ? "--" : "%2u", (unsigned)(sr / 1000)); label(RC_X + 57, 46, b, C_GRN);
  label(RC_X + 75, 46, "kHz", C_TXT);
  uint16_t dim = C(0x5A,0x5C,0x76);
  label(RC_X + 102, 42, "mono", (!stopped && ch == 1) ? C_TXT : dim);
  label(RC_X + 102, 53, "stereo", (!stopped && ch == 2) ? C_TXT : dim);
  fb.fillRect(RC_X + 136, 41, 40, 24, C_LCD);
  int pos = visPos(curId);
  if (radioMode) snprintf(b, sizeof(b), "R%d/%d", radioIdx + 1, STATION_N);
  else { snprintf(b, sizeof(b), "%d/%d", pos >= 0 ? pos + 1 : 0, visCount); if (strlen(b) > 6) snprintf(b, sizeof(b), "#%d", pos + 1); }
  label(RC_X + 176 - 2 - strlen(b) * 6, 44, b, C_GRN);
  snprintf(b, sizeof(b), "V %d%%", volume * 100 / VOL_STEPS);
  label(RC_X + 176 - 2 - strlen(b) * 6, 55, b, C_GRN);

  // ---- EQ / PL / BT / PWR ----
  const char *fl[4] = {"EQ", "PL", "BT", "PWR"};
  uint16_t fc[4] = {
    eqOn ? LED_ON : LED_OFF,
    (srcType != 0 || radioMode) ? LED_ON : LED_OFF,                             // a folder, playlist or radio is selected
    outMode == 0 ? LED_OFF : (outMode == 1 ? C(0x40,0xA0,0xFF) : C_YEL),        // off = speaker, blue = BT, yellow = both
    charging ? C_YEL : (batPct <= 15 ? C(0xF0,0x40,0x40) : LED_OFF) };
  for (int i = 0; i < 4; i++) {
    int x = RC_X + i * FB_STEP;
    sbutton(x, FB_Y, FB_W, FB_H, false);
    fb.setFont(&fonts::Font2); fb.setTextColor(C_ICON);
    int lw = fb.textWidth(fl[i]), lx = x + (FB_W - lw - 8) / 2 + 8;
    led(lx - 8, FB_Y + FB_H / 2 - 3, fc[i]);
    fb.setCursor(lx, FB_Y + FB_H / 2 - 7); fb.print(fl[i]);
  }

  // ---- VOL / BAL ----
  label(4, VOL_Y + 1, "VOL", C_TXT);
  rslider(VOL_X, VOL_Y, VOL_W, VOL_H, volume / (float)VOL_STEPS, VOL_KW, C_VOLFILL);
  label(BAL_X - 22, VOL_Y + 1, "BAL", C_TXT);
  rslider(BAL_X, VOL_Y, BAL_W, VOL_H, (balance + 16) / 32.0f, VOL_KW, 0, true, 2);

  // ---- seek ----
  if (radioMode) {
    // live radio has no length: sweep 0 -> 100 % over 60 min, then jump back to the left
    float frac = (radioSt == 3) ? (float)((millis() - radioStartMs) % 3600000UL) / 3600000.0f : 0;
    rslider(SEEK_X, SEEK_Y, SEEK_W, 10, frac, SEEK_KW, 0, false, 1);
  } else {
    float frac = (grab == 2) ? seekFrac : (dur ? (float)cur / dur : 0);
    rslider(SEEK_X, SEEK_Y, SEEK_W, 10, constrain(frac, 0.0f, 1.0f), SEEK_KW, 0, false, 1);
  }
  drawTransport();
}

void drawEQView() {
  titleBar("EQUALIZER");
  sbtn(8, 20, 50, 22, eqOn ? "ON" : "OFF", true, eqOn ? LED_ON : LED_OFF);
  fb.fillRect(64, 20, 192, 22, C_LCD); fb.drawRect(63, 19, 194, 24, C(0x50,0x52,0x70));
  for (int j = 1; j < 10; j++) {
    int x0 = 72 + (j - 1) * 20, x1 = x0 + 20;
    fb.drawLine(x0, 31 - eq[j] * 9 / 12, x1, 31 - eq[j + 1] * 9 / 12, C_GRN);
  }
  sbtn(262, 20, 50, 22, "DONE");
  for (int i = 0; i < 6; i++) sbtn(8 + i * 50, 46, 48, 22, presetName[i], true, eqPreset == i ? LED_ON : LED_OFF);
  for (int i = 0; i < 11; i++) {
    int x = sliderX(i);
    fb.fillRect(x - 3, EQ_Y0, 6, EQ_Y1 - EQ_Y0, C_GROOVE);
    for (int k = 0; k <= 8; k++) fb.drawFastHLine(x - 9, EQ_Y0 + k * (EQ_Y1 - EQ_Y0) / 8, 4, C_LIGHT);
    int ty = EQ_Y1 - (eq[i] + 12) * (EQ_Y1 - EQ_Y0) / 24;
    fb.fillRect(x - 2, ty, 4, EQ_Y1 - ty, C_VOLFILL);
    sbutton(x - 10, ty - 6, 20, 12, false);                               // silver knob
    for (int g = -1; g <= 1; g++) fb.drawFastHLine(x - 6, ty + g * 3, 12, C_ICON);
    label(x - (int)strlen(bandLbl[i]) * 3, 202, bandLbl[i], C_TXT);
  }
  label(8, 222, "+12 dB top / -12 dB bottom   closes automatically", C_LIGHT);
}

// ---- scrollable lists (playlist + source picker share this) ----
int maxTop() { return max(0, lvCount - PL_ROWS); }
int thumbH() { return constrain(SB_TR_H * PL_ROWS / max(lvCount, 1), 30, SB_TR_H); }
int thumbY() { int mt = maxTop(); return SB_TR_Y + (mt ? (SB_TR_H - thumbH()) * lvTop / mt : 0); }
void clampTop() { lvTop = constrain(lvTop, 0, maxTop()); }

void drawScrollbar() {
  fb.fillRect(SB_X - 2, 16, SB_W + 4, 196, C_GROOVE);
  sbutton(SB_X, SB_UP_Y, SB_W, SB_BTN_H, false);
  fb.fillTriangle(SB_X + 14, SB_UP_Y + 7, SB_X + 7, SB_UP_Y + 17, SB_X + 21, SB_UP_Y + 17, C_ICON);
  sbutton(SB_X, SB_DN_Y, SB_W, SB_BTN_H, false);
  fb.fillTriangle(SB_X + 7, SB_DN_Y + 7, SB_X + 21, SB_DN_Y + 7, SB_X + 14, SB_DN_Y + 17, C_ICON);
  fb.fillRect(SB_X + 11, SB_TR_Y, 6, SB_TR_H, C_LCD);                       // groove
  int ty = thumbY(), th = thumbH();
  sbutton(SB_X, ty, SB_W, th, false);                                       // big silver thumb
  for (int k = -1; k <= 1; k++) fb.drawFastHLine(SB_X + 7, ty + th / 2 + k * 4, 14, C_ICON);
}

void clipStr(char *s, size_t maxChars) { if (strlen(s) > maxChars) s[maxChars] = 0; }

void drawPLView() {
  char sn[64], tt[96]; srcName(sn, sizeof(sn));
  snprintf(tt, sizeof(tt), "PLAYLIST: %s", sn); clipStr(tt, 34);
  titleBar(tt);
  lvCount = visCount; clampTop();
  bevel(4, 16, 280, 196, false, C_LCD);
  fb.setFont(&fonts::Font2); fb.setTextSize(1); fb.setTextWrap(false);
  if (!visCount) label2(14, 40, "NO SONGS HERE", C_GRN);
  for (int r = 0; r < PL_ROWS; r++) {
    int idx = lvTop + r; if (idx >= visCount) break;
    int y = PL_Y0 + r * PL_ROW_H, id = vis[idx];
    if (id == curId) fb.fillRect(6, y, 276, PL_ROW_H - 1, C(0x00,0x20,0x80));
    char nm[100], line[110]; dispName(nm, sizeof(nm), id);
    snprintf(line, sizeof(line), "%d. %s", idx + 1, nm); clipStr(line, 33);
    label2(9, y, line, id == curId ? C_TXT : C_GRN);
  }
  drawScrollbar();
  sbtn(4, 216, 66, 22, "SOURCE");
  char sb[24]; snprintf(sb, sizeof(sb), "SORT: %s", sortName[sortMode]);
  sbtn(74, 216, 96, 22, sb);
  char pg[24]; snprintf(pg, sizeof(pg), "%d/%d", visCount ? lvTop + 1 : 0, visCount);
  fb.fillRect(176, 217, 74, 20, C_LCD); label(180, 224, pg, C_GRN);
  sbtn(256, 216, 60, 22, "DONE");
}

void srcRow(int i, char *buf, size_t n) {
  if (i == 0) snprintf(buf, n, "ALL SONGS (%d)", trackCount);
  else if (i <= folderCount) {
    int f = i - 1; const char *nm = strcmp(folders[f], "/") ? folders[f] + 1 : "(root)";
    size_t l = strlen(nm);
    if (l > 24) snprintf(buf, n, "[DIR] ~%s (%u)", nm + l - 23, folderN[f]); else snprintf(buf, n, "[DIR] %s (%u)", nm, folderN[f]);
  } else if (i - 1 - folderCount < plCount) { int p = i - 1 - folderCount; snprintf(buf, n, "[LIST] %s (%u)", plName[p], plN[p]); }
  else { int k = i - 1 - folderCount - plCount; snprintf(buf, n, "[RADIO] %s", stations[k].name); }
}
int srcActiveIndex() { if (radioMode) return 1 + folderCount + plCount + radioIdx; return srcType == 0 ? 0 : (srcType == 1 ? 1 + srcId : 1 + folderCount + srcId); }

void drawSrcView() {
  titleBar("CHOOSE SOURCE");
  lvCount = 1 + folderCount + plCount + STATION_N; clampTop();
  bevel(4, 16, 280, 196, false, C_LCD);
  fb.setFont(&fonts::Font2); fb.setTextSize(1); fb.setTextWrap(false);
  for (int r = 0; r < PL_ROWS; r++) {
    int idx = lvTop + r; if (idx >= lvCount) break;
    int y = PL_Y0 + r * PL_ROW_H;
    bool act = (idx == srcActiveIndex());
    if (act) fb.fillRect(6, y, 276, PL_ROW_H - 1, C(0x00,0x20,0x80));
    char line[80]; srcRow(idx, line, sizeof(line)); clipStr(line, 33);
    label2(9, y, line, act ? C_TXT : C_GRN);
  }
  drawScrollbar();
  label(6, 220, "tap to select", C_LIGHT); label(6, 230, "long-press a song: add to playlist", C_LIGHT);
  sbtn(256, 216, 60, 22, "BACK");
}

const int ADD_Y0 = 38, ADD_ROW = 24;
void drawAddView() {
  titleBar("ADD / REMOVE SONG");
  char nm[100]; if (popId >= 0) dispName(nm, sizeof(nm), popId); else nm[0] = 0;
  clipStr(nm, 38); label2(10, 19, nm, C_YEL);
  for (int p = 0; p < plCount; p++) {
    int y = ADD_Y0 + p * ADD_ROW; bool in = plFind(p, popId) >= 0;
    sbutton(8, y, 300, ADD_ROW - 4, false);
    led(16, y + (ADD_ROW - 4) / 2 - 3, in ? LED_ON : LED_OFF);
    char line[60]; strlcpy(line, plName[p], sizeof(line)); clipStr(line, 32);
    fb.setFont(&fonts::Font2); fb.setTextColor(C_ICON); fb.setCursor(30, y + 2); fb.print(line);
  }
  if (plCount < MAX_PL) {
    int y = ADD_Y0 + plCount * ADD_ROW;
    sbutton(8, y, 300, ADD_ROW - 4, false);
    fb.setFont(&fonts::Font2); fb.setTextColor(C_ICON); fb.setCursor(16, y + 2); fb.print("+  NEW PLAYLIST");
  }
  sbtn(256, 214, 56, 22, "DONE");
}

void drawPwrView() {
  titleBar("POWER");
  fb.setFont(&fonts::Font4); fb.setTextSize(1); fb.setTextColor(C_TXT);
  fb.setCursor(78, 44); fb.print("POWER OFF?");
  label2(37, 80, "Press the BOOT button to turn it on again", C_LIGHT);
  sbutton(36, 104, 120, 54, false);
  led(62, 128, C(0xF0,0x40,0x40));
  fb.setFont(&fonts::Font4); fb.setTextColor(C(0xB0,0x20,0x20)); fb.setCursor(74, 120); fb.print("OFF");
  sbutton(164, 104, 120, 54, false);
  fb.setTextColor(C_ICON); fb.setCursor(176, 120); fb.print("CANCEL");
  sbtn(36, 166, 248, 30, "USB DRIVE (ADD SONGS FROM PC)", true, C(0x40,0xC0,0xF0), false, true);
  label(48, 212, "(or hold the BOOT button for 1.5 seconds anytime)", C_LIGHT);
}

void drawUI() {
  fb.fillScreen(C_PANEL);
  switch (view) {
    case V_EQ:  drawEQView();  break;
    case V_PL:  drawPLView();  break;
    case V_SRC: drawSrcView(); break;
    case V_ADD: drawAddView(); break;
    case V_PWR: drawPwrView(); break;
    default:    drawMain();    break;
  }
}

// ---------------- audio control ----------------
void applyEQ() {
  LOCK();
  if (!eqOn) { audio.setTone(0, 0, 0); UNLOCK(); return; }
  auto avg = [&](int a, int b) { int s = 0; for (int i = a; i <= b; i++) s += eq[i]; return s / (b - a + 1) + eq[0]; };
  auto map = [](int db) { return (int8_t)constrain(db, -20, 6); };   // library max boost is +6 dB
  audio.setTone(map(avg(1, 3)), map(avg(4, 7)), map(avg(8, 10)));
  UNLOCK();
}

void audio_id3data(const char *info) {
  String s = info;
  if (s.startsWith("Title: "))  id3Title = s.substring(7);
  if (s.startsWith("Artist: ")) id3Artist = s.substring(8);
  if (id3Title.length()) titleText = (id3Artist.length() ? id3Artist + " - " : "") + id3Title;
}
void audio_eof_mp3(const char *info) { nextPending = true; }
void audio_eof_stream(const char *info) { if (radioMode) { radioLost = true; radioT = millis(); } }
void audio_showstreamtitle(const char *info) {
  if (radioMode && info && info[0]) titleText = String(stations[radioIdx].name) + " - " + info;
}

void audioTask(void *) {
  for (;;) { LOCK(); audio.loop(); UNLOCK(); vTaskDelay(1); }
}

// ---------------- saved settings ----------------
void setPreset(uint8_t p) {
  eqPreset = p;
  memcpy(eq, p == 5 ? customEq : presets[p], sizeof(eq));
}
void saveEQ() {
  prefs.putBool("eqOn", eqOn);
  prefs.putUChar("eqPre", eqPreset);
  prefs.putBytes("cust", customEq, sizeof(customEq));
}
void saveSource() { prefs.putUChar("srcT", srcType); prefs.putUShort("srcI", srcId); }
void loadSettings() {
  volume    = constrain((int)prefs.getUChar("vol2", 10), 0, VOL_STEPS);
  outMode   = prefs.getUChar("out", 0) % 3;
  balance   = constrain((int)prefs.getChar("bal", 0), -16, 16);
  shuffleOn = prefs.getBool("shuf", false);
  repMode   = prefs.getUChar("rep", 1) % 3;
  eqOn      = prefs.getBool("eqOn", true);
  sortMode  = prefs.getUChar("sort2", 0) % SORT_MODES;
  srcType   = prefs.getUChar("srcT", 0);
  srcId     = prefs.getUShort("srcI", 0);
  prefs.getBytes("cust", customEq, sizeof(customEq));
  uint8_t pre = prefs.getUChar("eqPre", 0);
  setPreset(pre > 5 ? 0 : pre);
}

// ---------------- list helpers for touch ----------------
void centerOnCurrent() {
  int pos = visPos(curId);
  lvCount = visCount;
  lvTop = pos > 3 ? pos - 3 : 0;
  clampTop();
}
void enterPL() { view = V_PL; centerOnCurrent(); }
void enterSrc() {
  view = V_SRC; lvCount = 1 + folderCount + plCount + STATION_N;
  int a = srcActiveIndex(); lvTop = a > 3 ? a - 3 : 0; clampTop();
}
void selectSource(int idx) {
  int radioBase = 1 + folderCount + plCount;
  if (idx >= radioBase) { startRadio(idx - radioBase); view = V_MAIN; return; }
  if (idx == 0) { srcType = 0; srcId = 0; }
  else if (idx <= folderCount) { srcType = 1; srcId = idx - 1; }
  else { srcType = 2; srcId = idx - 1 - folderCount; }
  saveSource();
  buildVis();
  clearPlayed();
  enterPL();
}
void closeAdd() { if (srcType == 2) { buildVis(); } view = V_PL; centerOnCurrent(); }
void setTopFromBar(int y) {
  int th = thumbH(), mt = maxTop();
  float f = constrain((y - SB_TR_Y - th / 2) / (float)(SB_TR_H - th), 0.0f, 1.0f);
  lvTop = (int)(f * mt + 0.5f);
  clampTop();
}

// ---------------- battery, power, volume buttons, radio ----------------
int pctFromMv(int mv) {                                    // typical 3.7V LiPo discharge curve
  static const int tv[21] = {3270,3610,3690,3710,3730,3750,3770,3790,3800,3820,3840,3850,3870,3910,3950,3980,4020,4080,4110,4150,4200};
  if (mv <= tv[0]) return 0;
  if (mv >= tv[20]) return 100;
  for (int i = 1; i < 21; i++) if (mv < tv[i]) return (i - 1) * 5 + 5 * (mv - tv[i - 1]) / (tv[i] - tv[i - 1]);
  return 100;
}

bool powerKeyDown() {
  return digitalRead(POWER_BTN) == LOW || (EXTPWR_PIN >= 0 && digitalRead(EXTPWR_PIN) == LOW);
}

void powerOff(const char *why) {
  if (state != ST_STOP) saveLastSong(curSec());                       // remember song + position
  LOCK(); audio.stopSong(); UNLOCK();
  stopRadio();
  fb.fillScreen(C_BODY); titleBar("POWER");
  fb.setFont(&fonts::Font4); fb.setTextSize(1); fb.setTextColor(C_TXT);
  fb.setCursor(60, 80); fb.print("POWERING OFF");
  if (why && why[0]) label2(60, 120, why, C_YEL);
  label2(40, 160, "Press BOOT to turn on again", C_LIGHT);
  fb.pushSprite(0, 0);
  delay(1800);
  digitalWrite(AUDIO_EN, AMP_ON_LEVEL == HIGH ? LOW : HIGH);          // amplifier off
  if (BT_EN_PIN >= 0) digitalWrite(BT_EN_PIN, BT_ON_LEVEL == HIGH ? LOW : HIGH);   // BT transmitter off
  neopixelWrite(RGB_PIN, 0, 0, 0);
  SD_MMC.end();
  Wire.beginTransmission(0x38); Wire.write(0xA5); Wire.write(0x03); Wire.endTransmission();   // touch chip sleep
  digitalWrite(LCD_BL, LOW);
  lcd.sleep();
  while (powerKeyDown()) delay(10);                                   // wait until the power key(s) are released
  delay(300);
  gpio_hold_en((gpio_num_t)AUDIO_EN); gpio_hold_en((gpio_num_t)LCD_BL);
  if (BT_EN_PIN >= 0) gpio_hold_en((gpio_num_t)BT_EN_PIN);
  gpio_deep_sleep_hold_en();
  uint64_t mask = 1ULL << POWER_BTN;
  if (EXTPWR_PIN >= 0) mask |= 1ULL << EXTPWR_PIN;
  esp_sleep_enable_ext1_wakeup_io(mask, ESP_EXT1_WAKEUP_ANY_LOW);     // BOOT or the external power button wakes the player
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en((gpio_num_t)POWER_BTN); rtc_gpio_pulldown_dis((gpio_num_t)POWER_BTN);
  if (EXTPWR_PIN >= 0) { rtc_gpio_pullup_en((gpio_num_t)EXTPWR_PIN); rtc_gpio_pulldown_dis((gpio_num_t)EXTPWR_PIN); }
  esp_deep_sleep_start();
}

void checkPowerButton() {                                           // hold BOOT or the external power button for 1.5 s
  static bool armed = false; static uint32_t t0 = 0;                  // ignore the press that woke us up
  if (!powerKeyDown()) { armed = true; t0 = 0; return; }
  if (!armed) return;
  if (!t0) t0 = millis(); else if (millis() - t0 > 1500) powerOff("");
}

void updateBattery() {
  static uint32_t t = 0; static int hist[8], n = 0; static float smooth = 0; static int lowCount = 0;
  if (millis() - t < 1000) return;
  t = millis();
  uint32_t sum = 0; for (int i = 0; i < 16; i++) sum += analogReadMilliVolts(BAT_PIN);
  int mv = (int)(sum / 16) * 2 + VBAT_OFFSET_MV;                      // divider is 1:2
  hist[n % 8] = mv; n++;
#if CHRG_PIN >= 0
  charging = (digitalRead(CHRG_PIN) == LOW);
#else
  if (n >= 8) {                                                       // estimate: voltage jumps ~50-150 mV when USB is plugged in/out
    int newest = 0, older = 0;
    for (int k = 0; k < 3; k++) newest += hist[(n - 1 - k) % 8];
    for (int k = 3; k < 8; k++) older += hist[(n - 1 - k) % 8];
    int d = newest / 3 - older / 5;
    if (d > 40 && !charging)      { charging = true;  n = 0; smooth = 0; }
    else if (d < -40 && charging) { charging = false; n = 0; smooth = 0; }
  }
#endif
  if (smooth < 1) smooth = mv; else smooth += (mv - smooth) * 0.1f;
  batMv = (int)smooth;
  batPct = pctFromMv(batMv - (charging ? 60 : 0));
  if (!charging && batMv > 2500 && batMv < LOW_BATT_SHUTDOWN_MV && millis() > 30000) { if (++lowCount >= 10) powerOff("BATTERY LOW"); }
  else lowCount = 0;
}

// codec DAC volume register: 0xBF (191) = 0 dB, 0.5 dB per step; the driver takes 0..100 % (= register * 100 / 255)
int codecPct(int v) {
  if (v <= 0) return 0;                                                   // mute
  float db = VOL_MIN_DB + (VOL_MAX_DB - VOL_MIN_DB) * (v - 1) / (float)(VOL_STEPS - 1);
  return constrain((int)lroundf((191 + db * 2) / 2.55f), 0, 100);
}
void applyVolume() { board.setVolume(codecPct(volume)); }
void volStep(int d) { volume = constrain(volume + d, 0, VOL_STEPS); applyVolume(); }

void checkButtons() {                                               // external VOL- / VOL+ push buttons (to GND)
  struct Btn { int pin; int dir; bool down; uint32_t t0, rep; };
  static Btn b[2] = {{VOLDN_PIN, -1, false, 0, 0}, {VOLUP_PIN, +1, false, 0, 0}};
  for (auto &k : b) {
    if (k.pin < 0) continue;
    bool low = digitalRead(k.pin) == LOW;
    if (low && !k.down) {
      if (millis() - k.rep < 40) continue;                            // debounce
      k.down = true; k.t0 = millis(); k.rep = millis(); volStep(k.dir);
    } else if (low && k.down) {
      if (millis() - k.t0 > 450 && millis() - k.rep > 120) { volStep(k.dir); k.rep = millis(); }   // hold = repeat
    } else if (!low && k.down) {
      k.down = false; k.rep = millis(); prefs.putUChar("vol2", volume);
    }
  }
}

void addStation(const char *name, const char *url) {
  if (STATION_N >= MAX_STATIONS || !url || strlen(url) >= sizeof(stations[0].url)) return;
  strlcpy(stations[STATION_N].name, name, sizeof(stations[0].name));
  strlcpy(stations[STATION_N].url, url, sizeof(stations[0].url));
  STATION_N++;
}
void loadStations() {                                              // /radio.txt lines: Name, http://url   (# = comment)
  STATION_N = 0;
  File f = SD_MMC.open("/radio.txt");
  bool hadFile = f;
  if (f) {
    while (f.available() && STATION_N < MAX_STATIONS) {
      String line = f.readStringUntil('\n'); line.trim();
      if (!line.length() || line[0] == '#') continue;
      int u = line.indexOf("http://"); if (u < 0) u = line.indexOf("https://");
      if (u < 0) continue;                                          // no URL on this line
      String url = line.substring(u), name = line.substring(0, u);
      url.trim(); name.trim();
      while (name.length() && (name.endsWith(",") || name.endsWith("|") || name.endsWith(";"))) { name.remove(name.length() - 1); name.trim(); }
      if (!name.length()) {                                         // no name: use the host name
        int a = url.indexOf("//") + 2, b = url.indexOf('/', a), c = url.indexOf(':', a);
        if (c > 0 && (b < 0 || c < b)) b = c;
        name = url.substring(a, b > 0 ? b : url.length());
      }
      addStation(name.c_str(), url.c_str());
    }
    f.close();
  }
  if (!STATION_N) {                                                  // no file / no stations: built-in list
    for (auto &d : defStations) addStation(d.name, d.url);
    if (!hadFile && SD_MMC.cardType() != CARD_NONE) {                // no file yet: write a sample to edit
      File w = SD_MMC.open("/radio.txt", FILE_WRITE);
      if (w) {
        w.println("# Radio stations - one per line:  Name, http://stream-url");
        w.println("# Lines starting with # are ignored. Max 60 stations.");
        for (auto &d : defStations) w.printf("%s, %s\n", d.name, d.url);
        w.close();
      }
    }
  }
  if (radioIdx >= STATION_N) radioIdx = 0;
  Serial.printf("%d radio stations\n", STATION_N);
}

void loadWifi() {                                                  // /wifi.txt lines: NAME,PASSWORD   (# = comment)
  netCount = 0;
  File f = SD_MMC.open("/wifi.txt");
  if (f) {
    while (f.available() && netCount < MAX_NETS) {
      String line = f.readStringUntil('\n'); line.trim();
      if (!line.length() || line[0] == '#') continue;
      int c = line.indexOf(',');
      String ss = c > 0 ? line.substring(0, c) : line, pw = c > 0 ? line.substring(c + 1) : String("");
      ss.trim(); pw.trim();
      if (!ss.length() || ss.length() > 32 || pw.length() > 64) continue;
      strlcpy(nets[netCount].ssid, ss.c_str(), sizeof(nets[0].ssid));
      strlcpy(nets[netCount].pass, pw.c_str(), sizeof(nets[0].pass));
      netCount++;
    }
    f.close();
  }
  if (strcmp(WIFI_SSID, "YOUR_WIFI_NAME") && netCount < MAX_NETS) {      // built-in network from the code
    strlcpy(nets[netCount].ssid, WIFI_SSID, sizeof(nets[0].ssid));
    strlcpy(nets[netCount].pass, WIFI_PASS, sizeof(nets[0].pass));
    netCount++;
  }
  netIdx = netCount ? prefs.getUChar("wnet", 0) % netCount : 0;         // start with the network that worked last time
  Serial.printf("%d Wi-Fi networks known\n", netCount);
}
void beginNet() {
  titleText = String("WIFI: ") + nets[netIdx].ssid + " ...";
  WiFi.disconnect(false);
  WiFi.mode(WIFI_STA);
  WiFi.begin(nets[netIdx].ssid, nets[netIdx].pass);
  radioSt = 1; radioT = millis();
}

String tzStr = TZ_DEFAULT;
void loadTimezone() {                                              // optional /timezone.txt: one POSIX TZ string
  File f = SD_MMC.open("/timezone.txt");
  if (!f) return;
  String l = f.readStringUntil('\n'); l.trim(); f.close();
  if (l.length() && l.length() < 64) tzStr = l;
}
void connectStream() {
  configTzTime(tzStr.c_str(), "pool.ntp.org", "time.google.com");   // (re)start NTP; Wi-Fi is up here
  titleText = String(stations[radioIdx].name) + " ...";
  LOCK(); bool ok = audio.connecttohost(stations[radioIdx].url); UNLOCK();
  radioSt = 3; radioT = millis(); radioLost = !ok;
}
void stopRadio() {
  if (radioMode) { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }
  radioMode = false; radioSt = 0; radioLost = false;
}
void startRadio(int k) {
  if (k < 0 || k >= STATION_N) return;
  LOCK(); audio.stopSong(); UNLOCK();
  if (netCount == 0) { titleText = "NO WIFI: PUT wifi.txt (NAME,PASSWORD) ON THE SD CARD"; state = ST_STOP; return; }
  radioMode = true; radioIdx = k; radioTries = 0; radioLost = false; radioStartMs = millis();
  state = ST_PLAY;
  if (WiFi.status() == WL_CONNECTED) { connectStream(); }
  else { netAttempt = 0; beginNet(); }
}
void radioTick() {
  if (!radioMode) return;
  if (radioSt == 1) {
    if (WiFi.status() == WL_CONNECTED) { prefs.putUChar("wnet", netIdx); radioStartMs = millis(); connectStream(); }
    else if (millis() - radioT > 7000) {                                  // try the next known network
      if (++netAttempt < netCount) { netIdx = (netIdx + 1) % netCount; beginNet(); }
      else { stopRadio(); titleText = "WIFI CONNECTION FAILED"; state = ST_STOP; }
    }
  } else if (radioSt == 3 && radioLost && millis() - radioT > 3000) {
    radioLost = false;
    if (++radioTries <= 5) { titleText = "RECONNECTING..."; connectStream(); }
    else { stopRadio(); titleText = "STREAM LOST"; state = ST_STOP; }
  }
}

// ---------------- touch (FT6336, I2C 0x38) ----------------
bool readTouch(int &x, int &y) {
  Wire.beginTransmission(0x38); Wire.write(0x02);
  if (Wire.endTransmission(false)) return false;
  if (Wire.requestFrom(0x38, 5) < 5) return false;
  uint8_t n = Wire.read() & 0x0F, xh = Wire.read(), xl = Wire.read(), yh = Wire.read(), yl = Wire.read();
  if (!n) return false;
  int px = ((xh & 0x0F) << 8) | xl, py = ((yh & 0x0F) << 8) | yl;
  if (TOUCH_SWAP_XY) { int t = px; px = py; py = t; }
  if (TOUCH_FLIP_X) px = 239 - px;
  if (TOUCH_FLIP_Y) py = 319 - py;
#if LCD_ROTATION == 1
  x = 319 - py; y = px;
#else
  x = py; y = 239 - px;
#endif
  return true;
}

bool inRect(int x, int y, int rx, int ry, int rw, int rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
void setEqFromY(int i, int y) { eq[i] = constrain(12 - (y - EQ_Y0) * 24 / (EQ_Y1 - EQ_Y0), -12, 12); }

bool listPress = false, listMoved = false, listLong = false;
int pressY = 0, pressTop = 0, pressRow = 0; uint32_t pressT = 0;

void onPress(int x, int y) {
  grab = 0;
  if (view == V_MAIN) {
    if (y >= FB_Y && y < FB_Y + FB_H && x >= RC_X) {                   // EQ / PL / BT / PWR
      switch ((x - RC_X) / FB_STEP) {
        case 0: view = V_EQ; break;
        case 1: enterPL(); break;
        case 2: outMode = (outMode + 1) % 3; applyOutput(); prefs.putUChar("out", outMode); break;
        default: view = V_PWR; break;
      }
    }
    else if (inRect(x, y, VOL_X - 4, VOL_Y - 10, VOL_W + 8, 30)) grab = 1;
    else if (inRect(x, y, BAL_X - 4, VOL_Y - 10, BAL_W + 8, 30)) grab = 13;
    else if (!radioMode && inRect(x, y, SEEK_X, SEEK_Y - 9, SEEK_W, 26)) grab = 2;
    else if (y >= TY && y < TY + TH) {
      int k = -1;
      for (int i = 0; i < 8; i++) if (x >= TB_X[i] - 2 && x < TB_X[i] + TB_W[i] + 2) k = i;
      switch (k) {
        case 0: prevMs = millis(); playPrev(); break;
        case 1: if (state == ST_PAUSE) togglePause(); else if (state == ST_STOP) { if (curId >= 0) playTrack(curId); else if (visCount) playTrack(vis[0]); } break;
        case 2: togglePause(); break;
        case 3: stopPlayback(); break;
        case 4: nextMs = millis(); playNext(); break;
        case 5: enterSrc(); break;                                         // eject: pick folder / playlist / radio
        case 6: shuffleOn = !shuffleOn; clearPlayed(); prefs.putBool("shuf", shuffleOn); break;
        case 7: repMode = (repMode + 1) % 3; prefs.putUChar("rep", repMode); break;
      }
    }
  } else if (view == V_EQ) {
    if (inRect(x, y, 8, 20, 50, 22)) { eqOn = !eqOn; applyEQ(); saveEQ(); }
    else if (inRect(x, y, 262, 20, 50, 22)) view = V_MAIN;
    else if (inRect(x, y, 8, 46, 300, 22)) {
      int pi = (x - 8) / 50;
      if (pi < 6 && (x - 8) % 50 < 48) { setPreset(pi); applyEQ(); saveEQ(); }
    }
    else if (y >= EQ_Y0 - 16 && y <= EQ_Y1 + 16) {
      for (int i = 0; i < 11; i++) if (abs(x - sliderX(i)) < 13) { grab = 3 + i; break; }
    }
  } else if (view == V_PL || view == V_SRC) {
    if (y >= 214) {
      if (view == V_PL) {
        if (inRect(x, y, 4, 216, 66, 22)) enterSrc();
        else if (inRect(x, y, 74, 216, 96, 22)) {
          sortMode = (sortMode + 1) % SORT_MODES; prefs.putUChar("sort2", sortMode);
          sortVis(); centerOnCurrent();
        }
        else if (inRect(x, y, 256, 216, 60, 22)) view = V_MAIN;
      } else if (inRect(x, y, 256, 216, 60, 22)) enterPL();
    } else if (x >= SB_X - 2) {                                          // scrollbar
      if (inRect(x, y, SB_X, SB_UP_Y, SB_W, SB_BTN_H)) lvTop -= PL_ROWS - 1;
      else if (inRect(x, y, SB_X, SB_DN_Y, SB_W, SB_BTN_H)) lvTop += PL_ROWS - 1;
      else if (y >= SB_TR_Y && y < SB_TR_Y + SB_TR_H) { grab = 10; setTopFromBar(y); }
      clampTop();
    } else if (y >= PL_Y0 && y < PL_Y0 + PL_ROWS * PL_ROW_H) {           // list rows
      listPress = true; listMoved = false; listLong = false;
      pressY = y; pressTop = lvTop; pressT = millis(); pressRow = (y - PL_Y0) / PL_ROW_H;
    }
  } else if (view == V_PWR) {
    if (inRect(x, y, 36, 104, 120, 54)) powerOff("");
    else if (inRect(x, y, 164, 104, 120, 54)) view = V_MAIN;
    else if (inRect(x, y, 36, 166, 248, 30)) enterUsbDrive();
  } else if (view == V_ADD) {
    if (inRect(x, y, 256, 214, 56, 22)) closeAdd();
    else if (y >= ADD_Y0 && y < ADD_Y0 + (plCount + 1) * ADD_ROW) {
      int r = (y - ADD_Y0) / ADD_ROW;
      if (r < plCount) plToggle(r, popId);
      else if (r == plCount && plCount < MAX_PL) {                      // new playlist
        char nm[32]; int n = 1;
        for (;; n++) {
          snprintf(nm, sizeof(nm), "List %d", n);
          bool used = false; for (int p = 0; p < plCount; p++) if (!strcasecmp(plName[p], nm)) used = true;
          if (!used) break;
        }
        int p = createPlaylist(nm);
        if (p >= 0) plToggle(p, popId);
      }
    }
  }
}

void onDrag(int x, int y) {
  if (grab == 11 || grab == 12) {                                         // hold VOL -/+ to repeat
    if (millis() - repT > (repFirst ? 450 : 120)) { volStep(grab == 11 ? -1 : 1); repT = millis(); repFirst = false; }
  }
  else if (grab == 1) { int v = (int)(fracAt(x, VOL_X, VOL_W, VOL_KW) * VOL_STEPS + 0.5f); if (v != volume) { volume = v; applyVolume(); } }
  else if (grab == 2) seekFrac = fracAt(x, SEEK_X, SEEK_W, SEEK_KW);
  else if (grab == 13) {                                                  // balance, snaps to centre
    balance = (int)lroundf(fracAt(x, BAL_X, BAL_W, VOL_KW) * 32) - 16;
    if (abs(balance) <= 1) balance = 0;
    LOCK(); audio.setBalance(balance); UNLOCK();
  }
  else if (grab >= 3 && grab < 10) { setEqFromY(grab - 3, y); eqPreset = 5; }
  else if (grab == 10) setTopFromBar(y);
  if (listPress) {
    if (abs(y - pressY) > 10) listMoved = true;
    if (listMoved) { lvTop = pressTop + (pressY - y) / PL_ROW_H; clampTop(); }
    else if (!listLong && view == V_PL && millis() - pressT > 700) {     // long press -> add to playlist popup
      int idx = lvTop + pressRow;
      listLong = true; listPress = false;
      if (idx < visCount) { popId = vis[idx]; view = V_ADD; }
    }
  }
}

void onRelease() {
  if (grab == 2) {
    LOCK(); uint32_t dur = audio.getAudioFileDuration(); if (dur) audio.setAudioPlayPosition((uint16_t)(seekFrac * dur)); UNLOCK();
  }
  if (grab == 1 || grab == 11 || grab == 12) prefs.putUChar("vol2", volume);
  if (grab == 13) prefs.putChar("bal", balance);
  if (grab >= 3 && grab < 10) { applyEQ(); memcpy(customEq, eq, sizeof(eq)); saveEQ(); }
  grab = 0;
  if (listPress) {
    listPress = false;
    if (!listMoved) {
      int idx = lvTop + pressRow;
      if (view == V_PL && idx < visCount) { playTrack(vis[idx]); view = V_MAIN; }
      else if (view == V_SRC && idx < lvCount) selectSource(idx);
    }
  }
}

void handleTouch() {
  static bool wasDown = false;
  static uint32_t lastSeen = 0; static int lx = 0, ly = 0;
  int x, y;
  bool down = readTouch(x, y);
  if (down) { lastSeen = millis(); lx = x; ly = y; }
  else if (millis() - lastSeen < 120) { x = lx; y = ly; down = true; }   // bridge missed touch reads
  if (down) lastTouch = millis();
  if (down && !wasDown) onPress(x, y);
  else if (down) onDrag(x, y);
  if (!down && wasDown) onRelease();
  wasDown = down;
}

// ---------------- USB drive mode (SD card appears as a drive on the PC) ----------------
// PWR menu > USB DRIVE reboots into this mode: no playback, the SD card is exported over the native USB port.
// Eject the drive on the PC, then touch EXIT (or press BOOT) to reboot back into the player (the song list rescans itself).
#if HAVE_USB_MSC
USBMSC usbMsc;
#define USB_WRITABLE 1           // 1 = PC can copy songs onto the card, 0 = read-only
RTC_NOINIT_ATTR volatile uint32_t usbRd, usbWr, usbFail, usbLastLba, usbStage;   // survive a crash so the next boot can show them
static uint8_t usbPdrv = 0xFF;                       // FATFS drive number of the SD card (0xFF = unknown -> slow single-sector path)
static int32_t usbRead(uint32_t lba, uint32_t offset, void *buf, uint32_t size) {
  uint8_t *b = (uint8_t *)buf; uint32_t n = size / 512; usbRd++; usbLastLba = lba; usbStage = 1;
  if (usbPdrv != 0xFF) { if (disk_read(usbPdrv, b, lba, n) == 0) return (int32_t)size; usbFail++; return -1; }
  for (uint32_t i = 0; i < n; i++) if (!SD_MMC.readRAW(b + i * 512, lba + i)) { usbFail++; return -1; }
  return (int32_t)size;
}
static int32_t usbWrite(uint32_t lba, uint32_t offset, uint8_t *buf, uint32_t size) {
  uint32_t n = size / 512; usbWr++; usbLastLba = lba; usbStage = 2;
  if (usbPdrv != 0xFF) { if (disk_write(usbPdrv, buf, lba, n) == 0) return (int32_t)size; usbFail++; return -1; }
  for (uint32_t i = 0; i < n; i++) if (!SD_MMC.writeRAW(buf + i * 512, lba + i)) { usbFail++; return -1; }
  return (int32_t)size;
}
#endif

void enterUsbDrive() {
#if HAVE_USB_MSC
  LOCK(); audio.stopSong(); UNLOCK();
  if (state != ST_STOP) saveLastSong(curSec());
  stopRadio();
  prefs.putBool("usbm", true);                                    // next boot = USB drive mode
  fb.fillScreen(C_BODY); titleBar("USB DRIVE");
  label2(60, 110, "STARTING...", C_LIGHT);
  fb.pushSprite(0, 0);
  delay(300);
  ESP.restart();
#else
  showMsg("SET TOOLS > USB MODE TO", 0);                          // sketch was built without TinyUSB
  delay(2500);
#endif
}

void usbDriveMode() {
#if HAVE_USB_MSC
  prefs.putString("dbg", "USB: sd mount");
  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  bool ok = SD_MMC.begin("/sdcard", false, false, 40000, 20) && SD_MMC.cardSize() > 0;
  uint32_t blocks = (uint32_t)(SD_MMC.cardSize() / 512);
  usbRd = usbWr = usbFail = usbLastLba = usbStage = 0;
  if (ok) {                                                        // find the SD drive number and check it reads the same data
    static uint8_t a[512] __attribute__((aligned(4))), c[512] __attribute__((aligned(4)));
    for (uint8_t d = 0; d < 4; d++) {
      if (!SD_MMC.readRAW(a, 0) || disk_read(d, c, 0, 1) != 0) continue;
      if (!memcmp(a, c, 512)) { usbPdrv = d; break; }
    }
  }
  Serial.printf("SD ok=%d blocks=%u\n", ok, (unsigned)blocks);
  if (ok) {
    prefs.putString("dbg", "USB: msc begin");
    usbMsc.vendorID("ESPAMP"); usbMsc.productID("SD Card"); usbMsc.productRevision("1.0");
    usbMsc.onRead(usbRead); usbMsc.onWrite(usbWrite);
    usbMsc.isWritable(USB_WRITABLE);
    usbMsc.mediaPresent(true);
    usbMsc.begin(blocks, 512);
    prefs.putString("dbg", "USB: usb begin");
    USB.begin();
    prefs.putString("dbg", "USB: running");
  }
  digitalWrite(AUDIO_EN, AMP_ON_LEVEL == HIGH ? LOW : HIGH);      // speaker off
  uint32_t t0 = millis(); bool wasDown = false;
  while (true) {
    fb.fillScreen(C_BODY); titleBar("USB DRIVE");
    fb.setFont(&fonts::Font4); fb.setTextSize(1); fb.setTextColor(ok ? C_TXT : C(0xF0,0x60,0x60));
    fb.setCursor(40, 60); fb.print(ok ? "SD CARD CONNECTED" : "NO SD CARD");
    label2(30, 100, ok ? "Plug the USB-C cable into the PC and copy" : "Insert a card and restart", C_LIGHT);
    if (ok) { label2(30, 118, "your songs. Eject the drive on the PC", C_LIGHT); label2(30, 136, "before you press EXIT.", C_LIGHT); }
    { char st[64]; snprintf(st, sizeof(st), "rd %u wr %u fail %u lba %u", (unsigned)usbRd, (unsigned)usbWr, (unsigned)usbFail, (unsigned)usbLastLba); label(30, 152, st, C_LIGHT); }
    sbtn(80, 170, 160, 44, "EXIT", true, C(0x40,0xC0,0xF0), false, true);
    fb.pushSprite(0, 0);
    for (int i = 0; i < 10; i++) {                                // ~0.5 s, poll touch + BOOT
      int x, y; bool d = readTouch(x, y);
      if ((d && !wasDown && inRect(x, y, 80, 170, 160, 44)) || (digitalRead(POWER_BTN) == LOW && millis() - t0 > 1500)) {
        prefs.remove("dbg");
        delay(200);
        ESP.restart();
      }
      wasDown = d; delay(50);
    }
  }
#endif
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200);
  mtx = xSemaphoreCreateMutex();
  gpio_hold_dis((gpio_num_t)AUDIO_EN); gpio_hold_dis((gpio_num_t)LCD_BL); gpio_deep_sleep_hold_dis();   // after wake-up from "power off"
  if (BT_EN_PIN >= 0) { gpio_hold_dis((gpio_num_t)BT_EN_PIN); pinMode(BT_EN_PIN, OUTPUT); digitalWrite(BT_EN_PIN, BT_ON_LEVEL == HIGH ? LOW : HIGH); }
  if (BT_LINK_PIN >= 0) pinMode(BT_LINK_PIN, INPUT);
  neopixelWrite(RGB_PIN, 0, 0, 0);                                    // keep the RGB LED dark
  { const int bp[] = {POWER_BTN, EXTPWR_PIN, VOLDN_PIN, VOLUP_PIN};  // release RTC pad settings after wake-up, use pull-ups
    for (int pn : bp) if (pn >= 0) { rtc_gpio_deinit((gpio_num_t)pn); pinMode(pn, INPUT_PULLUP); } }
#if CHRG_PIN >= 0
  pinMode(CHRG_PIN, INPUT);
#endif
  analogSetPinAttenuation(BAT_PIN, ADC_11db);
  updateBattery();
  prefs.begin("espamp", false);
  loadSettings();

  lcd.init(); lcd.setRotation(LCD_ROTATION); lcd.fillScreen(TFT_BLACK);
  pinMode(LCD_BL, OUTPUT); digitalWrite(LCD_BL, HIGH);
  fb.setPsram(true); fb.setColorDepth(16);
  if (!fb.createSprite(320, 240)) Serial.println("Sprite alloc failed - enable OPI PSRAM!");
  else startBootAnim();                                     // animated boot screen while the rest starts up

  pinMode(TP_RST, OUTPUT); digitalWrite(TP_RST, LOW); delay(10); digitalWrite(TP_RST, HIGH); delay(60);
  Wire.begin(I2C_SDA, I2C_SCL); Wire.setClock(400000);

  pins.addI2C(PinFunction::CODEC, I2C_SCL, I2C_SDA, 0x18);
  pins.addI2S(PinFunction::CODEC, I2S_MCLK, I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_DIN);
  if (!board.begin()) Serial.println("ES8311 init failed");
  applyVolume();                                            // volume lives in the codec (see VOL_MIN_DB / VOL_MAX_DB)
  pinMode(AUDIO_EN, OUTPUT); applyOutput();                         // speaker / BT / both, as last used

  if (prefs.getBool("usbm", false)) {                       // requested from the PWR menu: act as a USB drive
    prefs.putBool("usbm", false);                           // one-shot: if drive mode ever crashes, the next boot is normal
    if (HAVE_USB_MSC) { finishBootAnim(); usbDriveMode(); }   // boot animation must stop first (it shares the display bus)                       // never returns (EXIT reboots)
  }

  tr      = (Track *)ps_malloc((size_t)MAX_TRACKS * sizeof(Track));
  vis     = (uint16_t *)ps_malloc(MAX_TRACKS * sizeof(uint16_t));
  rankArr = (uint16_t *)ps_malloc(MAX_TRACKS * sizeof(uint16_t));
  byPath  = (uint16_t *)ps_malloc(MAX_TRACKS * sizeof(uint16_t));
  plTr    = (uint16_t *)ps_malloc((size_t)MAX_PL * MAX_TRACKS * sizeof(uint16_t));
  bool memOk = tr && vis && rankArr && byPath && plTr;
  if (!memOk) Serial.println("PSRAM allocation failed - enable OPI PSRAM!");

  SD_MMC.setPins(SD_CLK, SD_CMD, SD_D0, SD_D1, SD_D2, SD_D3);
  if (memOk && SD_MMC.begin("/sdcard", false, false, 40000, 20)) {
    showMsg("READING SD CARD...", 0);
    loadCache();
    scanDir("/", 0);
    if (cacheDirty || trackCount != cacheN) { showMsg("SAVING INDEX...", trackCount); saveCache(); }
    if (cache) { free(cache); cache = nullptr; }
    Serial.printf("%d songs, %d folders\n", trackCount, folderCount);
    loadPlaylists();
    loadWifi();
    if ((srcType == 1 && srcId >= folderCount) || (srcType == 2 && srcId >= plCount)) { srcType = 0; srcId = 0; }
    buildVis();
    if (!visCount && srcType != 0) { srcType = 0; srcId = 0; buildVis(); }
  } else { Serial.println("SD mount failed"); showMsg("NO SD CARD", 0); }
  loadTimezone();
  loadStations();                                       // /radio.txt (or the built-in list)

  audio.setPinout(I2S_BCLK, I2S_LRC, I2S_DOUT, I2S_MCLK);
  audio.setVolume(21);                                      // decoder at full scale; the codec does the volume
  audio.setBalance(balance);
  applyEQ();
  finishBootAnim();                                         // let the boot animation finish its outro
  xTaskCreatePinnedToCore(audioTask, "audio", 8192, NULL, 3, NULL, 1);
#if HAVE_USB_MSC
  { String d = prefs.getString("dbg", "");                    // drive mode ended without EXIT: show where / why
    if (d.length()) { char m[80]; snprintf(m, sizeof(m), "%s RST%d r%u w%u f%u s%u", d.c_str(), (int)esp_reset_reason(), (unsigned)usbRd, (unsigned)usbWr, (unsigned)usbFail, (unsigned)usbStage); showMsg(m, (int)usbLastLba); delay(9000); prefs.remove("dbg"); } }
#endif
  if (visCount) {
    String lp = prefs.getString("lastP", "");
    int last = lp.length() ? findPath(lp.c_str()) : -1;                // the song playing at power-off
    if (last >= 0) { resumeId = last; resumeSec = prefs.getUInt("lastT", 0); }
    int start = last >= 0 ? last : vis[0];
#if AUTOPLAY_ON_BOOT
    playTrack(start);
#else
    curId = start;                                      // select it but wait for PLAY
    char nm[100]; dispName(nm, sizeof(nm), curId); titleText = nm;
#endif
  }
}

void loop() {
  if (resumePending && state == ST_PLAY && millis() - lastStartMs > 400) {   // jump to the remembered position
    LOCK(); uint32_t dur = audio.getAudioFileDuration();
    if (dur > resumePending) { audio.setAudioPlayPosition((uint16_t)resumePending); resumePending = 0; }
    UNLOCK();
    if (millis() - lastStartMs > 5000) resumePending = 0;               // give up (e.g. file without duration)
  }
  handleTouch();
  updateBattery();
  checkPowerButton();
  checkButtons();
  radioTick();
  uint32_t idle = millis() - lastTouch;
  if (view == V_EQ && idle > 5000) view = V_MAIN;
  if (view == V_PWR && idle > 8000) view = V_MAIN;
  if ((view == V_PL || view == V_SRC) && idle > 20000) view = V_MAIN;
  if (view == V_ADD && idle > 20000) { closeAdd(); view = V_MAIN; }
  if (nextPending) {
    nextPending = false;
    if (millis() - lastStartMs > 2000) {
      Serial.printf("Song ended (shuffle=%d, repeat=%d)\n", shuffleOn, repMode);
      if (!radioMode) advanceAuto();
    }
  }
  static uint32_t last = 0;
  if (millis() - last > 60) { last = millis(); drawUI(); fb.pushSprite(0, 0); }
}
