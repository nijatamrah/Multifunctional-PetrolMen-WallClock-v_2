/*
  ============================================================
   DIVAR SAATI v2.0  -  ESP32 + ST7735 (160x128)
   Nicat ucun hazirlanib
  ============================================================
  Kitabxanalar (Library Manager-den qur):
    - Adafruit GFX Library
    - Adafruit ST7735 and ST7789 Library
    - ArduinoJson   (v6 ve ya v7 ise dusur)
  Qalanlar (WiFi, HTTPClient, ArduinoOTA, Preferences, SPI) ESP32 core ile gelir.

  Duymeler:
    SAAT ekrani : MID = menyu | LEFT/RIGHT = reqemsal / analoq
    MENYU       : LEFT/RIGHT/UP/DOWN = sec | MID = ac
    Tetbiqler   : LEFT = geri
    Ayarlar     : UP/DOWN = sec | LEFT/RIGHT = deyis | MID = saxla ve geri
*/

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <sys/time.h>
#include <math.h>
#include "secrets.h"
#include "types.h"

// ===================== PINLER (evvelki ile eynidir) =====================
#define TFT_CS    5
#define TFT_DC    27
#define TFT_RST   33
#define TFT_MOSI  15
#define TFT_SCK   14
#define BL_PIN    22

#define BTN_UP    18
#define BTN_DOWN  19
#define BTN_LEFT  32
#define BTN_RIGHT 26
#define BTN_MID   25

#define BL_CH     0          // yalniz kohne ESP32 core (2.x) ucun

// ===================== AYARLAR =====================
#define WEATHER_CITY   "Baku,AZ"
const long GMT_OFFSET = 4 * 3600;     // Baki UTC+4

#define SW 160
#define SH 128

// Hardware SPI (daha suretli). Ekran bos qalsa, bu iki setiri
// asagidaki "software SPI" setri ile evez et ve setup()-daki tftSPI.begin(...) setrini sil:
//   Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCK, TFT_RST);
SPIClass tftSPI(HSPI);
Adafruit_ST7735 tft(&tftSPI, TFT_CS, TFT_DC, TFT_RST);

GFXcanvas16 cv(SW, SH);               // butun kadr evvelce burada cekilir -> titreme yoxdur

// ===================== RENGLER =====================
#define COL(r,g,b) ((uint16_t)((((r)&0xF8)<<8)|(((g)&0xFC)<<3)|(((b)&0xFF)>>3)))
const uint16_t C_BLACK = 0x0000;
const uint16_t C_WHITE = 0xFFFF;
const uint16_t C_CYAN  = COL(0, 220, 255);
const uint16_t C_MAG   = COL(255, 60, 200);
const uint16_t C_GREEN = COL(0, 255, 100);
const uint16_t C_RED   = COL(255, 70, 70);
const uint16_t C_YEL   = COL(255, 210, 60);
const uint16_t C_ORG   = COL(255, 150, 30);
const uint16_t C_PURP  = COL(170, 110, 255);
const uint16_t C_GRAY  = COL(120, 130, 150);
const uint16_t C_DIM   = COL(40, 48, 66);
const uint16_t C_PANEL = COL(16, 22, 38);
const uint16_t C_TILE  = COL(18, 24, 40);

// ===================== EKRANLAR =====================
enum Screen { S_CLOCK = 0, S_WEATHER, S_MARKET, S_WIFI, S_POMO, S_CYBER, S_SETTINGS, S_MENU };
int screen = S_CLOCK;
const int NAPPS = 7;
const char* APP_NAME[NAPPS] = {"Saat", "Hava", "Bazar", "WiFi Skan", "Pomodoro", "Cyberman", "Ayarlar"};
const uint16_t APP_COL[NAPPS] = {C_CYAN, C_ORG, C_YEL, C_PURP, C_RED, C_GREEN, COL(150, 170, 200)};

// ===================== MELUMAT STRUKTURLARI =====================

Wx  wxShared;
Fc  fcShared[3];
int fcCountShared = 0;
Mkt mktShared;
SemaphoreHandle_t dataMux;

volatile bool netBusy = false;
volatile bool forceRefresh = false;

Wx getWx()  { Wx w;  xSemaphoreTake(dataMux, portMAX_DELAY); w = wxShared;  xSemaphoreGive(dataMux); return w; }
Mkt getMkt(){ Mkt m; xSemaphoreTake(dataMux, portMAX_DELAY); m = mktShared; xSemaphoreGive(dataMux); return m; }
int getFc(Fc* out) {
  xSemaphoreTake(dataMux, portMAX_DELAY);
  int n = fcCountShared;
  for (int i = 0; i < n; i++) out[i] = fcShared[i];
  xSemaphoreGive(dataMux);
  return n;
}

// ===================== GLOBAL VETERLER =====================
Preferences prefs;
int  bri = 200;
bool nightMode = true;
bool analogStyle = false;

int  curRssi = -100;
bool wifiOk = false;
uint32_t lastRssiT = 0;
bool otaReady = false;

// duymeler
Btn bUp    = {BTN_UP,    false, false, 0, 0};
Btn bDown  = {BTN_DOWN,  false, false, 0, 0};
Btn bLeft  = {BTN_LEFT,  false, false, 0, 0};
Btn bRight = {BTN_RIGHT, false, false, 0, 0};
Btn bMid   = {BTN_MID,   false, false, 0, 0};
uint32_t lastInput = 0;

// menyu
int   menuTarget = 0;
float menuPos = 0;

// hava
int wxPage = 0;

// wifi skaner
Net nets[20];
int netCount = 0, wsel = 0, wtop = 0;
bool scanning = false, wantScan = false;
uint32_t lastScan = 0;

// pomodoro
enum { P_IDLE, P_RUN, P_PAUSE, P_DONE };
int pomoState = P_IDLE;
bool pomoBreak = false;
int focusMin = 25, breakMin = 5;
uint32_t pomoRemain = 25UL * 60000UL, pomoEnd = 0, pomoDoneAt = 0;
int pomoCount = 0;

// ayarlar
int setSel = 0;

// cyber
const char* TIPS[] = {
  "Her sayt ucun ferqli ve uzun parol istifade et.",
  "2 merheleli giris (2FA) ac. Parol oglansa da hesab qorunur.",
  "Acik WiFi-da bank ve parol daxil etme. VPN istifade et.",
  "Linke klikleyende evvel domen adini diqqetle yoxla.",
  "Sistemi ve tetbiqleri mutleq yenile: yamaqlar boslugu baglayir.",
  "Parol meneceri istifade et: unikal parol yadda saxlama.",
  "Phishing: tecili ve qorxudan mesaj gelirse dayan, yoxla.",
  "3-2-1 qaydasi: 3 nusxe, 2 media, 1-i ev xaricinde.",
  "API acarini koda yazma. secrets.h kimi ayri fayla qoy.",
  "Routerin standart parolunu deyis, WPS-i sondur.",
  "WEP ve kohne WPA zeifdir. WPA2 ve ya WPA3 sec.",
  "Namelum USB cihazini komputere taxma.",
  "HTTPS kilidi saytin tehlukesiz oldugunu demek deyil.",
  "Minimum imtiyaz prinsipi: yalniz lazim olan icazeni ver."
};
const int NTIPS = sizeof(TIPS) / sizeof(TIPS[0]);
int tipIdx = 0;
uint32_t tipT = 0;

// matrix
#define MCOLS 26
#define MROWS 16
struct MCol { float head; float speed; uint8_t len; };
MCol mc[MCOLS];
char mch[MCOLS][MROWS];
const char MCHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ@#$%&*+=<>?/|";

// adlar
const char* DAY_FULL[7]  = {"Bazar", "Bazar ertesi", "Cersenbe axsami", "Cersenbe", "Cume axsami", "Cume", "Senbe"};
const char* DAY_SHORT[7] = {"Baz", "BzE", "CxA", "Cer", "CmA", "Cum", "Sen"};
const char* MON[12]      = {"Yan", "Fev", "Mar", "Apr", "May", "Iyn", "Iyl", "Avq", "Sen", "Okt", "Noy", "Dek"};

void enter(int s);

// ===================== UMUMI FUNKSIYALAR =====================
static inline int P(float v) { return (int)lroundf(v); }

void ltext(const char* s, int x, int y, uint8_t sz, uint16_t col) {
  cv.setTextSize(sz); cv.setTextColor(col); cv.setCursor(x, y); cv.print(s);
}
void ctext(const char* s, int cx, int y, uint8_t sz, uint16_t col) {
  ltext(s, cx - (int)strlen(s) * 3 * sz, y, sz, col);
}
void rtext(const char* s, int xr, int y, uint8_t sz, uint16_t col) {
  ltext(s, xr - (int)strlen(s) * 6 * sz, y, sz, col);
}

uint16_t shade(uint16_t c, int pct) {
  int r = ((c >> 11) & 31) * pct / 100;
  int g = ((c >> 5) & 63) * pct / 100;
  int b = (c & 31) * pct / 100;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

void gradient(int r1, int g1, int b1, int r2, int g2, int b2) {
  for (int y = 0; y < SH; y++) {
    int r = r1 + (r2 - r1) * y / (SH - 1);
    int g = g1 + (g2 - g1) * y / (SH - 1);
    int b = b1 + (b2 - b1) * y / (SH - 1);
    cv.drawFastHLine(0, y, SW, COL(r, g, b));
  }
}

inline void present() { tft.drawRGBBitmap(0, 0, cv.getBuffer(), SW, SH); }

bool getNow(struct tm &t, int &ms) {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  if (tv.tv_sec < 1700000000L) return false;   // saat hele sinxron olmayib
  time_t s = tv.tv_sec;
  localtime_r(&s, &t);
  ms = tv.tv_usec / 1000;
  return true;
}

void ageStr(uint32_t upd, char* o, size_t n) {
  if (!upd) { snprintf(o, n, "--"); return; }
  uint32_t m = (millis() - upd) / 60000UL;
  if (m == 0) snprintf(o, n, "indi");
  else snprintf(o, n, "%lu dq", (unsigned long)m);
}

void pageDots(int n, int cur, int y, uint16_t col) {
  for (int i = 0; i < n; i++) {
    int x = 80 + P((i - (n - 1) / 2.0f) * 8);
    cv.fillCircle(x, y, i == cur ? 2 : 1, i == cur ? col : shade(col, 45));
  }
}

int rssiLevel(int r) { return r > -55 ? 4 : r > -65 ? 3 : r > -75 ? 2 : r > -85 ? 1 : 0; }

void drawWifiBars(int x, int yb, int rssi, bool ok, uint16_t col) {
  if (!ok) {
    cv.drawLine(x, yb - 7, x + 8, yb, C_RED);
    cv.drawLine(x + 8, yb - 7, x, yb, C_RED);
    return;
  }
  int lv = rssiLevel(rssi);
  for (int i = 0; i < 4; i++) {
    int h = 2 + i * 2;
    cv.fillRect(x + i * 4, yb - h + 1, 3, h, i < lv ? col : C_DIM);
  }
}

void header(const char* title, uint16_t col) {
  cv.fillRect(0, 0, SW, 15, COL(12, 16, 28));
  cv.drawFastHLine(0, 15, SW, col);
  ltext(title, 5, 4, 1, col);
  drawWifiBars(144, 11, curRssi, wifiOk, col);
}

void footer(const char* s) {
  cv.fillRect(0, SH - 11, SW, 11, COL(12, 16, 28));
  ltext(s, 3, SH - 9, 1, C_GRAY);
}

void drawWrapped(const char* s, int x, int y, int maxChars, int lineH, uint16_t col, int maxLines) {
  char line[48];
  int len = strlen(s), pos = 0, ln = 0;
  if (maxChars > 40) maxChars = 40;
  cv.setTextSize(1); cv.setTextColor(col);
  while (pos < len && ln < maxLines) {
    int end = pos + maxChars;
    if (end >= len) end = len;
    else {
      int sp = end;
      while (sp > pos && s[sp] != ' ') sp--;
      if (sp > pos) end = sp;
    }
    int n = end - pos;
    memcpy(line, s + pos, n); line[n] = 0;
    cv.setCursor(x, y + ln * lineH); cv.print(line);
    pos = end;
    while (pos < len && s[pos] == ' ') pos++;
    ln++;
  }
}

// ===================== HAVA IKONLARI =====================
void drawCloud(int cx, int cy, float r, uint16_t col) {
  cv.fillCircle(cx - P(r * 0.5f),  cy + P(r * 0.15f), P(r * 0.4f),  col);
  cv.fillCircle(cx - P(r * 0.05f), cy - P(r * 0.15f), P(r * 0.55f), col);
  cv.fillCircle(cx + P(r * 0.5f),  cy + P(r * 0.15f), P(r * 0.4f),  col);
  cv.fillRect(cx - P(r * 0.5f), cy + P(r * 0.15f), P(r * 1.0f), P(r * 0.4f), col);
}

void drawSun(int cx, int cy, float r, uint16_t col) {
  cv.fillCircle(cx, cy, P(r * 0.5f), col);
  float rot = millis() / 2500.0f;
  for (int i = 0; i < 8; i++) {
    float a = rot + i * 0.7854f;
    cv.drawLine(cx + P(cosf(a) * r * 0.68f), cy + P(sinf(a) * r * 0.68f),
                cx + P(cosf(a) * r * 0.95f), cy + P(sinf(a) * r * 0.95f), col);
  }
}

void drawMoon(int cx, int cy, float r, uint16_t col, uint16_t bg) {
  cv.fillCircle(cx, cy, P(r * 0.55f), col);
  cv.fillCircle(cx + P(r * 0.3f), cy - P(r * 0.2f), P(r * 0.5f), bg);
}

void drawDrops(int cx, int cy, float r, bool snow) {
  uint32_t t = millis();
  int span = P(r * 0.7f); if (span < 3) span = 3;
  for (int i = 0; i < 4; i++) {
    int off = (int)((t / (snow ? 80 : 40) + i * 7) % (uint32_t)span);
    int x = cx - P(r * 0.6f) + i * P(r * 0.4f);
    int y = cy + P(r * 0.55f) + off;
    if (snow) cv.fillCircle(x, y, r > 10 ? 2 : 1, C_WHITE);
    else cv.drawLine(x, y, x - P(r * 0.12f), y + P(r * 0.25f), COL(90, 170, 255));
  }
}

void drawWxIcon(int cx, int cy, float r, int code, bool night, uint16_t bg) {
  uint16_t sunC = COL(255, 200, 40), moonC = COL(235, 235, 200);
  uint16_t cl = COL(235, 240, 248), cd = COL(135, 145, 162);
  if (code == 800) {
    if (night) drawMoon(cx, cy, r, moonC, bg); else drawSun(cx, cy, r, sunC);
  } else if (code == 801 || code == 802) {
    if (night) drawMoon(cx - P(r * 0.3f), cy - P(r * 0.3f), r * 0.8f, moonC, bg);
    else drawSun(cx - P(r * 0.3f), cy - P(r * 0.3f), r * 0.8f, sunC);
    drawCloud(cx + P(r * 0.15f), cy + P(r * 0.2f), r * 0.8f, cl);
  } else if (code >= 803 && code < 900) {
    drawCloud(cx, cy, r, code == 804 ? cd : cl);
  } else if (code >= 200 && code < 300) {
    int cyy = cy - P(r * 0.22f);
    drawCloud(cx, cyy, r * 0.9f, cd);
    if ((millis() / 500) % 4 != 0) {
      uint16_t y = COL(255, 225, 40);
      cv.fillTriangle(cx + P(r * 0.12f), cy + P(r * 0.05f), cx - P(r * 0.2f), cy + P(r * 0.55f), cx + P(r * 0.08f), cy + P(r * 0.55f), y);
      cv.fillTriangle(cx - P(r * 0.06f), cy + P(r * 0.45f), cx + P(r * 0.22f), cy + P(r * 0.45f), cx - P(r * 0.1f), cy + P(r * 0.95f), y);
    }
  } else if (code >= 300 && code < 600) {
    int cyy = cy - P(r * 0.25f);
    drawCloud(cx, cyy, r * 0.9f, cd);
    drawDrops(cx, cyy, r * 0.9f, false);
  } else if (code >= 600 && code < 700) {
    int cyy = cy - P(r * 0.25f);
    drawCloud(cx, cyy, r * 0.9f, cl);
    drawDrops(cx, cyy, r * 0.9f, true);
  } else if (code >= 700 && code < 800) {
    for (int i = 0; i < 4; i++) {
      int y = cy - P(r * 0.45f) + i * P(r * 0.32f);
      int x0 = cx - P(r * 0.8f) + ((i % 2) ? P(r * 0.2f) : 0);
      cv.fillRect(x0, y, P(r * 1.4f), max(1, P(r * 0.1f)), COL(190, 195, 205));
    }
  } else {
    drawCloud(cx, cy, r, cl);
  }
}

const char* wxText(int c) {
  if (c >= 200 && c < 300) return "Ildirim";
  if (c >= 300 && c < 400) return "Seping";
  if (c >= 500 && c < 600) return "Yagis";
  if (c >= 600 && c < 700) return "Qar";
  if (c >= 700 && c < 800) return "Dumanli";
  if (c == 800) return "Aciq";
  if (c == 801 || c == 802) return "Az buludlu";
  if (c >= 803 && c < 900) return "Buludlu";
  return "--";
}

uint16_t wxBg(const Wx &w) {
  if (!w.ok) return COL(22, 34, 66);
  if (w.night) return COL(10, 16, 42);
  int c = w.code;
  if (c == 800 || c == 801) return COL(28, 110, 205);
  if (c >= 200 && c < 300) return COL(44, 36, 76);
  if (c >= 300 && c < 600) return COL(44, 60, 90);
  if (c >= 600 && c < 700) return COL(100, 125, 155);
  if (c >= 700 && c < 800) return COL(100, 104, 112);
  return COL(70, 92, 120);
}

// ===================== MENYU IKONLARI =====================
void arcPts(int cx, int cy, float R, int a0, int a1, int step, uint16_t col) {
  for (int a = a0; a <= a1; a += step) {
    float t = a * 0.0174533f;
    cv.fillCircle(cx + P(cosf(t) * R), cy - P(sinf(t) * R), 1, col);
  }
}

void drawAppIcon(int app, int cx, int cy, float r, uint16_t col) {
  switch (app) {
    case S_CLOCK:
      cv.drawCircle(cx, cy, P(r), col);
      cv.drawCircle(cx, cy, P(r) - 1, col);
      cv.drawLine(cx, cy, cx, cy - P(r * 0.6f), col);
      cv.drawLine(cx, cy, cx + P(r * 0.45f), cy + P(r * 0.2f), col);
      cv.fillCircle(cx, cy, max(1, P(r * 0.1f)), col);
      break;
    case S_WEATHER:
      drawSun(cx - P(r * 0.3f), cy - P(r * 0.3f), r * 0.8f, COL(255, 200, 40));
      drawCloud(cx + P(r * 0.1f), cy + P(r * 0.25f), r * 0.85f, COL(235, 240, 248));
      break;
    case S_MARKET: {
      int base = cy + P(r * 0.8f), w = max(2, P(r * 0.4f));
      cv.fillRect(cx - P(r * 0.85f), base - P(r * 0.5f), w, P(r * 0.5f), col);
      cv.fillRect(cx - P(r * 0.2f),  base - P(r * 0.95f), w, P(r * 0.95f), col);
      cv.fillRect(cx + P(r * 0.45f), base - P(r * 1.45f), w, P(r * 1.45f), col);
      cv.drawFastHLine(cx - P(r * 1.0f), base + 1, P(r * 2.0f), col);
      break;
    }
    case S_WIFI: {
      int oy = cy + P(r * 0.7f);
      cv.fillCircle(cx, oy, max(1, P(r * 0.12f)), col);
      arcPts(cx, oy, r * 0.55f, 45, 135, 6, col);
      arcPts(cx, oy, r * 1.0f,  45, 135, 4, col);
      arcPts(cx, oy, r * 1.45f, 45, 135, 3, col);
      break;
    }
    case S_POMO:
      cv.fillCircle(cx, cy + P(r * 0.15f), P(r * 0.8f), col);
      cv.fillTriangle(cx, cy - P(r * 0.55f), cx - P(r * 0.4f), cy - P(r * 0.85f), cx - P(r * 0.05f), cy - P(r * 0.95f), C_GREEN);
      cv.fillTriangle(cx, cy - P(r * 0.55f), cx + P(r * 0.4f), cy - P(r * 0.85f), cx + P(r * 0.05f), cy - P(r * 0.95f), C_GREEN);
      break;
    case S_CYBER:
      cv.fillRect(cx - P(r * 0.75f), cy - P(r * 0.95f), P(r * 1.5f), P(r * 1.1f), col);
      cv.fillTriangle(cx - P(r * 0.75f), cy + P(r * 0.1f), cx + P(r * 0.75f), cy + P(r * 0.1f), cx, cy + P(r * 1.0f), col);
      cv.fillCircle(cx, cy - P(r * 0.25f), max(1, P(r * 0.2f)), C_TILE);
      cv.fillRect(cx - max(1, P(r * 0.07f)), cy - P(r * 0.2f), max(2, P(r * 0.14f)), P(r * 0.45f), C_TILE);
      break;
    case S_SETTINGS:
      for (int i = 0; i < 8; i++) {
        float a = i * 0.7854f;
        cv.fillCircle(cx + P(cosf(a) * r * 0.85f), cy + P(sinf(a) * r * 0.85f), max(1, P(r * 0.2f)), col);
      }
      cv.fillCircle(cx, cy, P(r * 0.7f), col);
      cv.fillCircle(cx, cy, P(r * 0.3f), C_TILE);
      break;
  }
}

// ===================== MATRIX =====================
char rndCh() { return MCHARS[random((long)(sizeof(MCHARS) - 1))]; }

void resetCol(int c, bool initial) {
  mc[c].head  = initial ? -(float)random(0, MROWS + 8) : -(float)random(0, 6);
  mc[c].speed = 0.12f + random(0, 25) / 100.0f;
  mc[c].len   = 6 + random(0, 10);
}

void matrixInit() {
  for (int c = 0; c < MCOLS; c++) {
    resetCol(c, true);
    for (int r = 0; r < MROWS; r++) mch[c][r] = rndCh();
  }
}

void matrixDraw() {
  for (int c = 0; c < MCOLS; c++) {
    mc[c].head += mc[c].speed;
    int h = (int)floorf(mc[c].head);
    if (h - (int)mc[c].len > MROWS) resetCol(c, false);
    for (int k = 0; k < mc[c].len; k++) {
      int r = h - k;
      if (r < 0 || r >= MROWS) continue;
      uint16_t col;
      if (k == 0) col = COL(200, 255, 210);
      else { int g = 235 - (k * 210) / mc[c].len; col = COL(0, g, g / 6); }
      cv.drawChar(c * 6, r * 8, mch[c][r], col, col, 1);
    }
  }
  for (int i = 0; i < 10; i++) mch[random(MCOLS)][random(MROWS)] = rndCh();
}

// ===================== DUYMELER =====================
void pollBtn(Btn &b, uint32_t now, bool rep) {
  bool d = digitalRead(b.pin) == LOW;
  b.ev = false;
  if (d && !b.down) {
    if (now - b.t > 30) { b.down = true; b.t = now; b.ev = true; b.nextRep = now + 450; lastInput = now; }
  } else if (!d && b.down) {
    if (now - b.t > 30) { b.down = false; b.t = now; }
  } else if (d && b.down && rep && b.nextRep != 0xFFFFFFFFUL && now >= b.nextRep) {
    b.ev = true; b.nextRep = now + 110; lastInput = now;
  }
}

void pollAll(uint32_t now) {
  pollBtn(bUp, now, true);
  pollBtn(bDown, now, true);
  pollBtn(bLeft, now, true);
  pollBtn(bRight, now, true);
  pollBtn(bMid, now, false);
}

void swallow() {
  Btn* a[5] = {&bUp, &bDown, &bLeft, &bRight, &bMid};
  for (int i = 0; i < 5; i++) { a[i]->ev = false; a[i]->nextRep = 0xFFFFFFFFUL; }
}

void enter(int s) {
  if (s < S_MENU) { int cur = ((menuTarget % NAPPS) + NAPPS) % NAPPS; menuTarget += s - cur; }
  screen = s;
  lastInput = millis();
  swallow();
  if (s == S_WIFI) wantScan = true;
  if (s == S_CYBER) tipT = millis();
  if (s == S_WEATHER) wxPage = 0;
}

// ===================== AYARLAR / ARXA ISIQ =====================
void loadSettings() {
  prefs.begin("divar", false);
  bri = prefs.getUChar("bri", 200);
  nightMode = prefs.getBool("night", true);
  analogStyle = prefs.getBool("analog", false);
  if (bri < 10) bri = 10;
}
void saveSettings() {
  prefs.putUChar("bri", (uint8_t)bri);
  prefs.putBool("night", nightMode);
  prefs.putBool("analog", analogStyle);
}

void setBL(int v) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcWrite(BL_PIN, v);
#else
  ledcWrite(BL_CH, v);
#endif
}
void initBL() {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcAttach(BL_PIN, 5000, 8);
#else
  ledcSetup(BL_CH, 5000, 8);
  ledcAttachPin(BL_PIN, BL_CH);
#endif
  setBL(bri);
}

void applyBacklight(uint32_t now) {
  static int curBri = -1;
  int target = bri;
  if (nightMode) {
    struct tm t; int ms;
    if (getNow(t, ms) && (t.tm_hour >= 23 || t.tm_hour < 7) && target > 25) target = 25;
  }
  if (pomoState == P_DONE && now - pomoDoneAt < 8000UL) target = ((now / 250) % 2) ? 255 : 20;
  if (target != curBri) { curBri = target; setBL(target); }
}

// ===================== POMODORO MANTIQI =====================
uint32_t pomoTotal() { return (uint32_t)(pomoBreak ? breakMin : focusMin) * 60000UL; }
uint32_t pomoLeft(uint32_t now) {
  if (pomoState == P_RUN) return (int32_t)(pomoEnd - now) > 0 ? (pomoEnd - now) : 0;
  return pomoRemain;
}
void pomoTick(uint32_t now) {
  if (pomoState == P_RUN && (int32_t)(pomoEnd - now) <= 0) {
    pomoState = P_DONE; pomoRemain = 0; pomoDoneAt = now;
    if (!pomoBreak) pomoCount++;
    if (screen != S_POMO) enter(S_POMO);
  }
}

// ===================== SEBEKE (ayri task, ekrani dondurmur) =====================
String httpGet(const String &url) {
  String body = "";
  WiFiClientSecure client;
  client.setInsecure();               // sertifikat yoxlanmir (sade variant)
  HTTPClient http;
  http.setTimeout(9000);
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(client, url)) return body;
  http.addHeader("User-Agent", "Mozilla/5.0 (ESP32; DivarSaati)");
  int code = http.GET();
  if (code == HTTP_CODE_OK) body = http.getString();
  else Serial.printf("HTTP xeta: %d\n", code);
  http.end();
  return body;
}

bool fetchWeather() {
  String b = httpGet(String("https://api.openweathermap.org/data/2.5/weather?q=") + WEATHER_CITY +
                     "&units=metric&appid=" + OWM_API_KEY);
  if (!b.length()) return false;
  StaticJsonDocument<256> filter;
  filter["main"]["temp"] = true;
  filter["main"]["feels_like"] = true;
  filter["main"]["humidity"] = true;
  filter["main"]["pressure"] = true;
  filter["wind"]["speed"] = true;
  filter["weather"][0]["id"] = true;
  filter["weather"][0]["icon"] = true;
  StaticJsonDocument<768> doc;
  if (deserializeJson(doc, b, DeserializationOption::Filter(filter))) return false;
  if (doc["main"].isNull()) return false;
  Wx w = {};
  w.ok = true;
  w.temp  = doc["main"]["temp"] | 0.0f;
  w.feels = doc["main"]["feels_like"] | 0.0f;
  w.hum   = doc["main"]["humidity"] | 0;
  w.press = doc["main"]["pressure"] | 0;
  w.wind  = doc["wind"]["speed"] | 0.0f;
  w.code  = doc["weather"][0]["id"] | 800;
  const char* ic = doc["weather"][0]["icon"] | "01d";
  w.night = (strlen(ic) >= 3 && ic[2] == 'n');
  w.updated = millis() | 1;
  xSemaphoreTake(dataMux, portMAX_DELAY); wxShared = w; xSemaphoreGive(dataMux);
  return true;
}

bool fetchForecast() {
  String b = httpGet(String("https://api.openweathermap.org/data/2.5/forecast?q=") + WEATHER_CITY +
                     "&units=metric&cnt=32&appid=" + OWM_API_KEY);
  if (!b.length()) return false;
  StaticJsonDocument<192> filter;
  filter["list"][0]["dt"] = true;
  filter["list"][0]["main"]["temp"] = true;
  filter["list"][0]["weather"][0]["id"] = true;
  static StaticJsonDocument<6144> doc;
  doc.clear();
  if (deserializeJson(doc, b, DeserializationOption::Filter(filter))) return false;

  Fc tmp[4];
  int bestDiff[4];
  int di = -1;
  long lastDay = -1;
  for (JsonObject it : doc["list"].as<JsonArray>()) {
    long t = it["dt"].as<long>() + GMT_OFFSET;
    long dayNum = t / 86400;
    if (dayNum != lastDay) {
      di++;
      if (di > 3) break;
      lastDay = dayNum;
      tmp[di].tmax = -100; tmp[di].tmin = 100; tmp[di].code = 800;
      tmp[di].wday = (int)((dayNum + 4) % 7);
      bestDiff[di] = 99;
    }
    int tp = (int)lroundf(it["main"]["temp"] | 0.0f);
    if (tp > tmp[di].tmax) tmp[di].tmax = tp;
    if (tp < tmp[di].tmin) tmp[di].tmin = tp;
    int hour = (int)((t % 86400) / 3600);
    int diff = abs(hour - 13);
    if (diff < bestDiff[di]) { bestDiff[di] = diff; tmp[di].code = it["weather"][0]["id"] | 800; }
  }
  Fc out[3]; int n = 0;
  for (int i = 1; i <= 3 && i <= di; i++) out[n++] = tmp[i];
  if (n == 0) return false;
  xSemaphoreTake(dataMux, portMAX_DELAY);
  for (int i = 0; i < n; i++) fcShared[i] = out[i];
  fcCountShared = n;
  xSemaphoreGive(dataMux);
  return true;
}

bool fetchMarket() {
  Mkt m = getMkt();
  bool any = false;

  // Brent neft (Yahoo Finance, resmi olmayan acar-siz endpoint)
  String b = httpGet("https://query1.finance.yahoo.com/v8/finance/chart/BZ%3DF?interval=1d&range=1d");
  if (b.length()) {
    StaticJsonDocument<192> f;
    f["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
    f["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;
    StaticJsonDocument<384> d;
    if (!deserializeJson(d, b, DeserializationOption::Filter(f))) {
      float p  = d["chart"]["result"][0]["meta"]["regularMarketPrice"] | 0.0f;
      float pv = d["chart"]["result"][0]["meta"]["chartPreviousClose"] | 0.0f;
      if (p > 0) { m.brent = p; m.brentPrev = pv; m.okBrent = true; any = true; }
    }
  }

  // USD ve EUR -> AZN
  b = httpGet("https://open.er-api.com/v6/latest/USD");
  if (b.length()) {
    StaticJsonDocument<96> f;
    f["rates"]["AZN"] = true;
    f["rates"]["EUR"] = true;
    StaticJsonDocument<192> d;
    if (!deserializeJson(d, b, DeserializationOption::Filter(f))) {
      float azn = d["rates"]["AZN"] | 0.0f;
      float eur = d["rates"]["EUR"] | 0.0f;
      if (azn > 0 && eur > 0) { m.usdAzn = azn; m.eurAzn = azn / eur; m.okFx = true; any = true; }
    }
  }

  // Bitcoin
  b = httpGet("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd&include_24hr_change=true");
  if (b.length()) {
    StaticJsonDocument<96> f;
    f["bitcoin"]["usd"] = true;
    f["bitcoin"]["usd_24h_change"] = true;
    StaticJsonDocument<192> d;
    if (!deserializeJson(d, b, DeserializationOption::Filter(f))) {
      float p = d["bitcoin"]["usd"] | 0.0f;
      float c = d["bitcoin"]["usd_24h_change"] | 0.0f;
      if (p > 0) { m.btc = p; m.btcChg = c; m.okBtc = true; any = true; }
    }
  }

  if (any) {
    m.updated = millis() | 1;
    xSemaphoreTake(dataMux, portMAX_DELAY); mktShared = m; xSemaphoreGive(dataMux);
  }
  return any;
}

void netTask(void*) {
  uint32_t tWx = 0, tFc = 0, tMk = 0, tWifi = millis();
  for (;;) {
    uint32_t now = millis();
    if (WiFi.status() != WL_CONNECTED) {
      if (now - tWifi > 30000UL) { tWifi = now; WiFi.disconnect(); WiFi.begin(WIFI_SSID, WIFI_PASS); }
      vTaskDelay(pdMS_TO_TICKS(1000));
      continue;
    }
    tWifi = now;
    if (WiFi.scanComplete() == WIFI_SCAN_RUNNING) { vTaskDelay(pdMS_TO_TICKS(500)); continue; }

    netBusy = true;
    if (forceRefresh) { tWx = 0; tFc = 0; tMk = 0; forceRefresh = false; }
    if (tWx == 0 || millis() - tWx > 600000UL) {
      bool ok = fetchWeather();
      tWx = ok ? millis() : millis() - 540000UL; if (tWx == 0) tWx = 1;
    }
    if (tFc == 0 || millis() - tFc > 1800000UL) {
      bool ok = fetchForecast();
      tFc = ok ? millis() : millis() - 1740000UL; if (tFc == 0) tFc = 1;
    }
    if (tMk == 0 || millis() - tMk > 300000UL) {
      bool ok = fetchMarket();
      tMk = ok ? millis() : millis() - 240000UL; if (tMk == 0) tMk = 1;
    }
    netBusy = false;
    vTaskDelay(pdMS_TO_TICKS(1500));
  }
}

// ===================== OTA =====================
void setupOTA() {
  ArduinoOTA.setHostname("divar-saati");
  ArduinoOTA.setPassword(OTA_PASS);
  ArduinoOTA.onStart([]() {
    tft.fillScreen(C_BLACK);
    tft.setTextSize(1); tft.setTextColor(C_CYAN);
    tft.setCursor(10, 50); tft.print("OTA yenileme...");
    tft.drawRect(10, 70, 140, 8, C_WHITE);
  });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    int w = (int)((uint64_t)p * 138 / t);
    tft.fillRect(11, 71, w, 6, C_GREEN);
  });
  ArduinoOTA.onEnd([]() {
    tft.fillScreen(C_BLACK);
    tft.setCursor(10, 60); tft.print("Hazir! Restart...");
  });
  ArduinoOTA.begin();
}

// ===================== EKRAN: SAAT =====================
void drawNoTime(uint32_t now) {
  gradient(6, 10, 26, 0, 0, 6);
  ctext("Vaxt sinxronlasir", 80, 44, 1, C_CYAN);
  for (int i = 0; i < 8; i++) {
    float a = now / 200.0f - i * 0.785f;
    cv.fillCircle(80 + P(cosf(a) * 14), 70 + P(sinf(a) * 14), 2, shade(C_CYAN, 100 - i * 11));
  }
  ctext(wifiOk ? "WiFi: OK" : "WiFi: qosulur...", 80, 100, 1, C_GRAY);
}

void drawDigital(const tm &t, int ms) {
  gradient(6, 10, 26, 0, 0, 6);
  Wx w = getWx();
  char b[24];
  if (w.ok) {
    drawWxIcon(11, 9, 6.5f, w.code, w.night, COL(6, 10, 26));
    snprintf(b, sizeof b, "%dC", (int)lroundf(w.temp));
    ltext(b, 22, 5, 1, C_WHITE);
  }
  drawWifiBars(144, 11, curRssi, wifiOk, C_CYAN);

  snprintf(b, sizeof b, "%02d%c%02d", t.tm_hour, ms < 500 ? ':' : ' ', t.tm_min);
  ltext(b, 7, 23, 4, COL(0, 60, 80));       // kolge
  ltext(b, 6, 22, 4, C_CYAN);
  snprintf(b, sizeof b, "%02d", t.tm_sec);
  ltext(b, 131, 36, 2, C_MAG);

  float sec = t.tm_sec + ms / 1000.0f;
  int bw = (int)(sec / 60.0f * 148);
  cv.drawRoundRect(5, 59, 150, 7, 3, C_DIM);
  for (int x = 0; x < bw; x++) {
    int r = 255 * x / 148, g = 220 - 160 * x / 148, bl = 255 - 55 * x / 148;
    cv.drawFastVLine(6 + x, 60, 5, COL(r, g, bl));
  }

  snprintf(b, sizeof b, "%02d %s %d", t.tm_mday, MON[t.tm_mon], 1900 + t.tm_year);
  ctext(b, 80, 74, 2, C_WHITE);
  const char* dn = DAY_FULL[t.tm_wday];
  int sz = ((int)strlen(dn) * 12 <= 150) ? 2 : 1;
  ctext(dn, 80, sz == 2 ? 95 : 99, sz, C_MAG);

  if (w.ok) {
    snprintf(b, sizeof b, "%s  %d%%  %.0fm/s", wxText(w.code), w.hum, w.wind);
    ctext(b, 80, 118, 1, C_GRAY);
  }
}

void drawHand(int cx, int cy, float deg, float len, float w, float tail, uint16_t col) {
  float a = deg * 0.0174533f;
  float s = sinf(a), c = cosf(a);
  int tx = cx + P(s * len), ty = cy - P(c * len);
  int bx = P(c * w), by = P(s * w);
  int ex = cx - P(s * tail), ey = cy + P(c * tail);
  cv.fillTriangle(tx, ty, cx - bx, cy - by, cx + bx, cy + by, col);
  cv.fillTriangle(ex, ey, cx - bx, cy - by, cx + bx, cy + by, col);
}

void drawAnalog(const tm &t, int ms) {
  gradient(6, 10, 26, 0, 0, 6);
  const int cx = 80, cy = 64;
  cv.fillCircle(cx, cy, 61, COL(8, 12, 26));
  cv.drawCircle(cx, cy, 61, C_CYAN);
  cv.drawCircle(cx, cy, 60, COL(0, 90, 120));
  for (int i = 0; i < 60; i++) {
    float a = i * 0.10472f;
    float s = sinf(a), c = cosf(a);
    bool big = (i % 5 == 0);
    float r1 = big ? 50 : 55, r2 = 58;
    cv.drawLine(cx + P(s * r1), cy - P(c * r1), cx + P(s * r2), cy - P(c * r2), big ? C_CYAN : C_GRAY);
  }
  ctext("12", cx, cy - 46, 1, C_WHITE);
  ctext("6",  cx, cy + 39, 1, C_WHITE);
  ctext("3",  cx + 43, cy - 3, 1, C_WHITE);
  ctext("9",  cx - 43, cy - 3, 1, C_WHITE);

  char b[16];
  snprintf(b, sizeof b, "%02d %s", t.tm_mday, MON[t.tm_mon]);
  ctext(b, cx, cy + 14, 1, C_YEL);
  ctext(DAY_SHORT[t.tm_wday], cx, cy + 24, 1, C_MAG);
  Wx w = getWx();
  if (w.ok) { snprintf(b, sizeof b, "%dC", (int)lroundf(w.temp)); ctext(b, cx, cy - 22, 1, C_WHITE); }
  drawWifiBars(144, 11, curRssi, wifiOk, C_CYAN);

  float sec = t.tm_sec + ms / 1000.0f;
  float mn = t.tm_min + sec / 60.0f;
  float hr = (t.tm_hour % 12) + mn / 60.0f;
  drawHand(cx, cy, hr * 30, 32, 4, 6, C_WHITE);
  drawHand(cx, cy, mn * 6, 48, 3, 6, C_CYAN);
  float a = sec * 6 * 0.0174533f;
  float s = sinf(a), c = cosf(a);
  cv.drawLine(cx - P(s * 12), cy + P(c * 12), cx + P(s * 54), cy - P(c * 54), C_MAG);
  cv.fillCircle(cx, cy, 4, C_MAG);
  cv.fillCircle(cx, cy, 2, C_BLACK);
}

void screenClock(uint32_t now) {
  if (bMid.ev) { enter(S_MENU); return; }
  if (bLeft.ev || bRight.ev) { analogStyle = !analogStyle; saveSettings(); }
  struct tm t; int ms;
  if (!getNow(t, ms)) { drawNoTime(now); return; }
  if (analogStyle) drawAnalog(t, ms); else drawDigital(t, ms);
}

// ===================== EKRAN: MENYU =====================
void screenMenu(uint32_t now) {
  if (bLeft.ev || bUp.ev) menuTarget--;
  if (bRight.ev || bDown.ev) menuTarget++;
  if (bMid.ev) { enter(((menuTarget % NAPPS) + NAPPS) % NAPPS); return; }
  if (now - lastInput > 30000UL) { enter(S_CLOCK); return; }

  menuPos += (menuTarget - menuPos) * 0.3f;
  gradient(12, 16, 34, 2, 3, 8);

  struct tm t; int ms; char b[16];
  if (getNow(t, ms)) { snprintf(b, sizeof b, "%02d:%02d", t.tm_hour, t.tm_min); ltext(b, 5, 4, 1, C_GRAY); }
  drawWifiBars(144, 11, curRssi, wifiOk, C_GRAY);

  int sel = ((menuTarget % NAPPS) + NAPPS) % NAPPS;
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < NAPPS; i++) {
      float d = i - menuPos;
      while (d > 3.5f) d -= NAPPS;
      while (d < -3.5f) d += NAPPS;
      float ad = fabsf(d);
      if (ad > 2.0f) continue;
      bool center = ad < 0.75f;
      if ((pass == 0) == center) continue;
      float sc = 1.0f - 0.40f * fminf(ad, 1.5f);
      int ts = P(58 * sc);
      int x = 80 + P(d * 60), y = 48;
      cv.fillRoundRect(x - ts / 2, y - ts / 2, ts, ts, 6, C_TILE);
      uint16_t bc = center ? APP_COL[i] : C_DIM;
      cv.drawRoundRect(x - ts / 2, y - ts / 2, ts, ts, 6, bc);
      if (center) cv.drawRoundRect(x - ts / 2 + 1, y - ts / 2 + 1, ts - 2, ts - 2, 5, shade(bc, 60));
      drawAppIcon(i, x, y, ts * 0.30f, APP_COL[i]);
    }
  }
  ctext(APP_NAME[sel], 80, 84, 2, APP_COL[sel]);
  ctext("<   MID   >", 80, 106, 1, C_GRAY);
  pageDots(NAPPS, sel, 121, APP_COL[sel]);
}

// ===================== EKRAN: HAVA =====================
void drawWxCurrent(const Wx &w) {
  uint16_t bg = wxBg(w);
  cv.fillScreen(bg);
  ltext("BAKI", 6, 4, 1, C_WHITE);
  drawWifiBars(144, 11, curRssi, wifiOk, C_WHITE);
  char b[32];
  if (!w.ok) {
    ctext("Hava melumati yoxdur", 80, 52, 1, C_WHITE);
    ctext(wifiOk ? "Yuklenir..." : "WiFi yoxdur", 80, 68, 1, COL(200, 210, 230));
  } else {
    uint16_t pn = shade(bg, 60);
    drawWxIcon(38, 50, 27, w.code, w.night, bg);
    snprintf(b, sizeof b, "%d", (int)lroundf(w.temp));
    int tw = (int)strlen(b) * 18;
    ltext(b, 80, 24, 3, C_WHITE);
    cv.drawCircle(80 + tw + 4, 27, 2, C_WHITE);
    ltext("C", 80 + tw + 9, 27, 2, C_WHITE);
    ltext(wxText(w.code), 80, 54, 1, COL(225, 235, 250));

    cv.fillRoundRect(4, 76, 152, 36, 5, pn);
    snprintf(b, sizeof b, "Hiss %dC", (int)lroundf(w.feels));   ltext(b, 10, 81, 1, C_WHITE);
    snprintf(b, sizeof b, "Nem %d%%", w.hum);                    ltext(b, 10, 96, 1, C_WHITE);
    snprintf(b, sizeof b, "Kulek %.0fm/s", w.wind);              ltext(b, 86, 81, 1, C_WHITE);
    snprintf(b, sizeof b, "Tezyiq %d", w.press);                 ltext(b, 86, 96, 1, C_WHITE);

    char ag[16]; ageStr(w.updated, ag, sizeof ag);
    snprintf(b, sizeof b, "yenilendi: %s", ag);
    ctext(b, 80, 115, 1, COL(200, 210, 230));
  }
  pageDots(2, 0, 125, C_WHITE);
}

void drawWxForecast(const Fc* f, int n) {
  gradient(14, 24, 58, 4, 8, 22);
  header("Proqnoz - Baki", C_ORG);
  if (n == 0) ctext("Melumat yoxdur", 80, 64, 1, C_GRAY);
  for (int i = 0; i < n; i++) {
    int x = 5 + i * 52, y = 22, w = 48, h = 92;
    int cx = x + w / 2;
    cv.fillRoundRect(x, y, w, h, 5, COL(22, 34, 72));
    cv.drawRoundRect(x, y, w, h, 5, COL(50, 70, 130));
    ctext(DAY_SHORT[f[i].wday % 7], cx, y + 6, 2, C_YEL);
    drawWxIcon(cx, y + 44, 15, f[i].code, false, C_BLACK);
    char b[12];
    snprintf(b, sizeof b, "%d", f[i].tmax);
    ctext(b, cx, y + 62, 2, C_ORG);
    snprintf(b, sizeof b, "min %d", f[i].tmin);
    ctext(b, cx, y + 80, 1, C_CYAN);
  }
  pageDots(2, 1, 122, C_ORG);
}

void screenWeather(uint32_t now) {
  if (bLeft.ev) { enter(S_MENU); return; }
  if (bRight.ev || bUp.ev || bDown.ev) wxPage ^= 1;
  if (bMid.ev) forceRefresh = true;
  if (wxPage == 0) { Wx w = getWx(); drawWxCurrent(w); }
  else { Fc f[3]; int n = getFc(f); drawWxForecast(f, n); }
}

// ===================== EKRAN: BAZAR =====================
void mcard(int x, int y, const char* label, const char* val, uint16_t col, const char* sub, int dir) {
  cv.fillRoundRect(x, y, 74, 46, 5, C_PANEL);
  cv.drawRoundRect(x, y, 74, 46, 5, shade(col, 45));
  ltext(label, x + 6, y + 5, 1, col);
  ltext(val, x + 6, y + 17, 2, C_WHITE);
  if (dir != 0) {
    uint16_t cc = dir > 0 ? C_GREEN : C_RED;
    if (dir > 0) cv.fillTriangle(x + 7, y + 38, x + 13, y + 38, x + 10, y + 32, cc);
    else         cv.fillTriangle(x + 7, y + 32, x + 13, y + 32, x + 10, y + 38, cc);
    ltext(sub, x + 17, y + 33, 1, cc);
  } else {
    ltext(sub, x + 6, y + 33, 1, C_GRAY);
  }
}

void screenMarket(uint32_t now) {
  if (bLeft.ev) { enter(S_MENU); return; }
  if (bMid.ev || bRight.ev) forceRefresh = true;
  Mkt m = getMkt();
  gradient(6, 18, 16, 2, 6, 6);
  header("Bazar", C_YEL);
  char ag[16]; ageStr(m.updated, ag, sizeof ag);
  rtext(ag, 138, 4, 1, C_GRAY);

  char v[16], s[16];
  // Brent
  if (m.okBrent) {
    snprintf(v, sizeof v, m.brent >= 100 ? "%.1f" : "%.2f", m.brent);
    float ch = m.brentPrev > 0 ? (m.brent - m.brentPrev) / m.brentPrev * 100.0f : 0;
    snprintf(s, sizeof s, "%+.1f%%", ch);
    mcard(4, 19, "BRENT $/bbl", v, C_ORG, s, ch >= 0 ? 1 : -1);
  } else mcard(4, 19, "BRENT $/bbl", "--", C_ORG, "yoxdur", 0);
  // USD
  if (m.okFx) {
    snprintf(v, sizeof v, "%.2f", m.usdAzn);
    mcard(82, 19, "USD", v, C_GREEN, "AZN", 0);
    snprintf(v, sizeof v, "%.2f", m.eurAzn);
    mcard(4, 68, "EUR", v, C_CYAN, "AZN", 0);
  } else {
    mcard(82, 19, "USD", "--", C_GREEN, "yoxdur", 0);
    mcard(4, 68, "EUR", "--", C_CYAN, "yoxdur", 0);
  }
  // BTC
  if (m.okBtc) {
    if (m.btc >= 1000) snprintf(v, sizeof v, "%.0fk", m.btc / 1000.0f);
    else snprintf(v, sizeof v, "%.0f", m.btc);
    snprintf(s, sizeof s, "%+.1f%%", m.btcChg);
    mcard(82, 68, "BTC $ 24s", v, C_YEL, s, m.btcChg >= 0 ? 1 : -1);
  } else mcard(82, 68, "BTC $ 24s", "--", C_YEL, "yoxdur", 0);

  footer("MID: yenile   LEFT: geri");
}

// ===================== EKRAN: WIFI SKANER =====================
const char* encName(int e) {
  switch (e) {
    case 0: return "OPEN";
    case 1: return "WEP";
    case 2: return "WPA";
    case 3: return "WPA2";
    case 4: return "WPA/WPA2";
    case 5: return "WPA2-ENT";
    case 6: return "WPA3";
    case 7: return "WPA2/WPA3";
    default: return "?";
  }
}
uint16_t secColor(int e) { return e == 0 ? C_RED : (e <= 2 ? C_ORG : C_GREEN); }

void collectScan(int sc) {
  int n = sc > 40 ? 40 : sc;
  int idx[40];
  for (int i = 0; i < n; i++) idx[i] = i;
  for (int i = 1; i < n; i++) {
    int k = idx[i], j = i - 1;
    while (j >= 0 && WiFi.RSSI(idx[j]) < WiFi.RSSI(k)) { idx[j + 1] = idx[j]; j--; }
    idx[j + 1] = k;
  }
  netCount = n > 20 ? 20 : n;
  for (int k = 0; k < netCount; k++) {
    int i = idx[k];
    String s = WiFi.SSID(i);
    if (s.length() == 0) strlcpy(nets[k].ssid, "<gizli>", sizeof nets[k].ssid);
    else strlcpy(nets[k].ssid, s.c_str(), sizeof nets[k].ssid);
    nets[k].rssi = WiFi.RSSI(i);
    nets[k].ch = WiFi.channel(i);
    nets[k].enc = (int)WiFi.encryptionType(i);
    strlcpy(nets[k].bssid, WiFi.BSSIDstr(i).c_str(), sizeof nets[k].bssid);
  }
  WiFi.scanDelete();
  if (wsel >= netCount) wsel = netCount > 0 ? netCount - 1 : 0;
}

void radar(int cx, int cy, int R) {
  for (int r = R / 3; r <= R; r += R / 3) cv.drawCircle(cx, cy, r, COL(0, 70, 30));
  cv.drawFastHLine(cx - R, cy, 2 * R, COL(0, 50, 20));
  cv.drawFastVLine(cx, cy - R, 2 * R, COL(0, 50, 20));
  float a = millis() / 500.0f;
  for (int k = 0; k < 9; k++) {
    float aa = a - k * 0.12f;
    cv.drawLine(cx, cy, cx + P(cosf(aa) * R), cy + P(sinf(aa) * R), shade(C_GREEN, 100 - k * 11));
  }
}

void screenWifi(uint32_t now) {
  if (bLeft.ev) { enter(S_MENU); return; }
  if (bUp.ev && wsel > 0) wsel--;
  if (bDown.ev && wsel < netCount - 1) wsel++;
  if (bMid.ev) wantScan = true;
  if (wsel < wtop) wtop = wsel;
  if (wsel >= wtop + 9) wtop = wsel - 8;

  int sc = WiFi.scanComplete();
  if (scanning) {
    if (sc >= 0) { collectScan(sc); scanning = false; lastScan = now; }
    else if (sc == WIFI_SCAN_FAILED) { scanning = false; lastScan = now; }
  } else if (now - lastScan > 20000UL) wantScan = true;
  if (wantScan && !scanning && !netBusy) { WiFi.scanNetworks(true, true); scanning = true; wantScan = false; }

  cv.fillScreen(COL(6, 8, 18));
  char b[40];
  snprintf(b, sizeof b, "WiFi Skaner (%d)", netCount);
  header(b, C_PURP);
  if (scanning && (now / 300) % 2) cv.fillCircle(132, 7, 2, C_GREEN);

  if (netCount == 0) {
    if (scanning) { radar(80, 62, 40); ctext("Taranir...", 80, 100, 1, C_GREEN); }
    else ctext("Sebeke tapilmadi", 80, 60, 1, C_GRAY);
    return;
  }
  for (int r = 0; r < 9; r++) {
    int k = wtop + r;
    if (k >= netCount) break;
    Net &nw = nets[k];
    int y = 18 + r * 10;
    if (k == wsel) cv.fillRect(0, y - 1, SW, 10, COL(24, 32, 80));
    cv.fillCircle(6, y + 4, 2, secColor(nw.enc));
    char nm[15]; strlcpy(nm, nw.ssid, sizeof nm);
    ltext(nm, 14, y, 1, nw.enc == 0 ? C_RED : C_WHITE);
    drawWifiBars(104, y + 8, nw.rssi, true, C_PURP);
    char rs[8]; snprintf(rs, sizeof rs, "%d", nw.rssi);
    rtext(rs, 148, y, 1, C_GRAY);
  }
  if (netCount > 9) {
    int th = 90 * 9 / netCount;
    cv.fillRect(157, 18, 2, 90, C_DIM);
    cv.fillRect(157, 18 + 90 * wtop / netCount, 2, th, C_PURP);
  }
  cv.fillRect(0, 109, SW, 19, COL(12, 16, 28));
  cv.drawFastHLine(0, 109, SW, C_PURP);
  Net &s = nets[wsel];
  snprintf(b, sizeof b, "%s ch%d %ddBm", encName(s.enc), s.ch, s.rssi);
  ltext(b, 4, 111, 1, secColor(s.enc));
  ltext(s.bssid, 4, 120, 1, C_GRAY);
}

// ===================== EKRAN: POMODORO =====================
void ring(int cx, int cy, int R, float frac, uint16_t col, uint16_t track) {
  for (int a = 0; a < 360; a += 3) {
    float t = (a - 90) * 0.0174533f;
    cv.fillCircle(cx + P(cosf(t) * R), cy + P(sinf(t) * R), 3, (a < frac * 360.0f) ? col : track);
  }
}

void screenPomo(uint32_t now) {
  if (bLeft.ev) { enter(S_MENU); return; }
  if (bMid.ev) {
    switch (pomoState) {
      case P_IDLE:  pomoEnd = now + pomoRemain; pomoState = P_RUN; break;
      case P_RUN:   pomoRemain = pomoLeft(now); pomoState = P_PAUSE; break;
      case P_PAUSE: pomoEnd = now + pomoRemain; pomoState = P_RUN; break;
      case P_DONE:  pomoBreak = !pomoBreak; pomoRemain = pomoTotal(); pomoState = P_IDLE; break;
    }
  }
  if (bRight.ev) {
    if (pomoState == P_IDLE) { pomoBreak = !pomoBreak; pomoRemain = pomoTotal(); }
    else { pomoState = P_IDLE; pomoRemain = pomoTotal(); }
  }
  if (pomoState == P_IDLE && (bUp.ev || bDown.ev)) {
    int &m = pomoBreak ? breakMin : focusMin;
    m += bUp.ev ? 1 : -1;
    if (m < 1) m = 1;
    if (m > 90) m = 90;
    pomoRemain = pomoTotal();
  }

  uint32_t left = pomoLeft(now), tot = pomoTotal();
  uint16_t col = pomoBreak ? C_GREEN : C_RED;
  if (pomoBreak) gradient(6, 26, 18, 0, 6, 4); else gradient(32, 10, 14, 6, 2, 4);
  header(pomoBreak ? "Pomodoro - Ara" : "Pomodoro - Fokus", col);
  float frac = tot ? (float)left / (float)tot : 0;
  ring(80, 67, 45, frac, col, shade(col, 22));

  char b[24];
  uint32_t sec = (left + 999) / 1000;
  snprintf(b, sizeof b, "%02lu:%02lu", (unsigned long)(sec / 60), (unsigned long)(sec % 60));
  ctext(b, 80, 58, 2, C_WHITE);
  switch (pomoState) {
    case P_IDLE:  snprintf(b, sizeof b, "UP/DN: %d dq", pomoBreak ? breakMin : focusMin); ctext(b, 80, 44, 1, C_GRAY); break;
    case P_RUN:   ctext("davam edir", 80, 44, 1, col); break;
    case P_PAUSE: if ((now / 500) % 2) ctext("dayandi", 80, 44, 1, C_YEL); break;
    case P_DONE:  if ((now / 400) % 2) ctext("BITDI!", 80, 44, 1, C_YEL); break;
  }
  snprintf(b, sizeof b, "Sessiya: %d", pomoCount);
  ctext(b, 80, 80, 1, C_GRAY);

  switch (pomoState) {
    case P_IDLE:  footer("MID basla UP/DN dq RT:mod"); break;
    case P_RUN:   footer("MID: dayan   RT: sifirla"); break;
    case P_PAUSE: footer("MID: davam   RT: sifirla"); break;
    case P_DONE:  footer("MID: novbeti merhele"); break;
  }
}

// ===================== EKRAN: CYBERMAN =====================
void screenCyber(uint32_t now) {
  if (bLeft.ev) { enter(S_MENU); return; }
  if (bRight.ev || bMid.ev || bDown.ev) { tipIdx = (tipIdx + 1) % NTIPS; tipT = now; }
  if (bUp.ev) { tipIdx = (tipIdx + NTIPS - 1) % NTIPS; tipT = now; }
  if (now - tipT > 9000UL) { tipIdx = (tipIdx + 1) % NTIPS; tipT = now; }

  cv.fillScreen(C_BLACK);
  matrixDraw();
  cv.fillRoundRect(40, 3, 80, 16, 4, C_BLACK);
  cv.drawRoundRect(40, 3, 80, 16, 4, C_GREEN);
  ctext("[ CYBERMAN ]", 80, 7, 1, C_GREEN);

  cv.fillRoundRect(3, 72, 154, 53, 5, COL(0, 10, 0));
  cv.drawRoundRect(3, 72, 154, 53, 5, C_GREEN);
  char b[16];
  snprintf(b, sizeof b, "TIP %d/%d", tipIdx + 1, NTIPS);
  ltext(b, 8, 76, 1, COL(0, 150, 60));
  drawWrapped(TIPS[tipIdx], 8, 87, 24, 9, C_WHITE, 4);
}

// ===================== EKRAN: AYARLAR =====================
void drawToggle(int x, int y, bool on) {
  cv.fillRoundRect(x, y, 26, 10, 5, on ? C_GREEN : C_DIM);
  cv.fillCircle(on ? x + 20 : x + 5, y + 5, 4, C_WHITE);
}

void screenSettings(uint32_t now) {
  if (bUp.ev) setSel = (setSel + 2) % 3;
  if (bDown.ev) setSel = (setSel + 1) % 3;
  int dx = 0;
  if (bLeft.ev) dx = -1;
  if (bRight.ev) dx = 1;
  if (dx) {
    if (setSel == 0) { bri += dx * 15; if (bri < 10) bri = 10; if (bri > 255) bri = 255; }
    else if (setSel == 1) nightMode = !nightMode;
    else analogStyle = !analogStyle;
  }
  if (bMid.ev) { saveSettings(); enter(S_MENU); return; }

  gradient(14, 18, 32, 3, 4, 10);
  header("Ayarlar", COL(150, 170, 200));
  const char* lab[3] = {"Parlaqliq", "Gece rejimi 23-07", "Saat stili"};
  char b[24];
  for (int i = 0; i < 3; i++) {
    int y = 20 + i * 24;
    cv.fillRoundRect(4, y, 152, 22, 4, i == setSel ? COL(26, 36, 70) : C_PANEL);
    if (i == setSel) cv.drawRoundRect(4, y, 152, 22, 4, C_CYAN);
    ltext(lab[i], 10, y + 3, 1, C_WHITE);
    if (i == 0) {
      snprintf(b, sizeof b, "%d%%", bri * 100 / 255);
      rtext(b, 150, y + 3, 1, C_YEL);
      cv.drawRoundRect(10, y + 13, 132, 6, 3, C_DIM);
      cv.fillRoundRect(10, y + 13, max(3, 132 * (bri - 10) / 245), 6, 3, C_YEL);
    } else if (i == 1) {
      drawToggle(124, y + 5, nightMode);
    } else {
      rtext(analogStyle ? "Analoq" : "Reqemsal", 150, y + 3, 1, C_CYAN);
    }
  }
  cv.fillRoundRect(4, 94, 152, 20, 4, C_PANEL);
  snprintf(b, sizeof b, "IP %s", wifiOk ? WiFi.localIP().toString().c_str() : "--");
  ltext(b, 8, 97, 1, C_GRAY);
  snprintf(b, sizeof b, "%ddBm  %luk  %lum", curRssi, (unsigned long)(ESP.getFreeHeap() / 1024), (unsigned long)(millis() / 60000UL));
  ltext(b, 8, 106, 1, C_GRAY);
  footer("LT/RT deyis  MID: saxla");
}

// ===================== SETUP / LOOP =====================
void splash() {
  matrixInit();
  uint32_t t0 = millis();
  while (millis() - t0 < 8000UL) {
    uint32_t el = millis() - t0;
    cv.fillScreen(C_BLACK);
    matrixDraw();
    cv.fillRoundRect(8, 36, 144, 56, 6, C_BLACK);
    cv.drawRoundRect(8, 36, 144, 56, 6, C_GREEN);
    ctext("DIVAR SAATI", 80, 44, 2, C_GREEN);
    ctext("v2.0  |  by Nicat", 80, 64, 1, C_CYAN);
    bool ok = WiFi.status() == WL_CONNECTED;
    if (ok) {
      char b[32]; snprintf(b, sizeof b, "WiFi OK %s", WiFi.localIP().toString().c_str());
      ctext(b, 80, 77, 1, C_GRAY);
    } else if (el < 7000UL) {
      ctext("WiFi baglanir...", 80, 77, 1, C_GRAY);
    } else {
      ctext("oflayn rejim", 80, 77, 1, C_ORG);
    }
    present();
    if (ok && el > 2500UL) { delay(600); break; }
    delay(33);
  }
}

void setup() {
  Serial.begin(115200);
  loadSettings();

  pinMode(BTN_UP, INPUT_PULLUP);
  pinMode(BTN_DOWN, INPUT_PULLUP);
  pinMode(BTN_LEFT, INPUT_PULLUP);
  pinMode(BTN_RIGHT, INPUT_PULLUP);
  pinMode(BTN_MID, INPUT_PULLUP);

  initBL();

  tftSPI.begin(TFT_SCK, -1, TFT_MOSI, -1);      // hardware SPI, pinler yeniden xeritelenir
  tft.initR(INITR_BLACKTAB);
  tft.setRotation(1);                           // landscape 160x128
  tft.fillScreen(C_BLACK);

  if (!cv.getBuffer()) {
    Serial.println("Canvas ucun yaddas ayrilmadi!");
    tft.setTextColor(C_RED); tft.setCursor(5, 50); tft.print("RAM xetasi");
    while (true) delay(1000);
  }
  cv.setTextWrap(false);

  dataMux = xSemaphoreCreateMutex();

  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  configTime(GMT_OFFSET, 0, "pool.ntp.org", "time.google.com");

  xTaskCreatePinnedToCore(netTask, "net", 16384, NULL, 1, NULL, 0);

  splash();
  enter(S_CLOCK);
}

uint32_t lastFrame = 0;

void loop() {
  uint32_t now = millis();

  if (otaReady) ArduinoOTA.handle();
  else if (WiFi.status() == WL_CONNECTED) { setupOTA(); otaReady = true; }

  if (now - lastFrame < 33) { delay(1); return; }   // ~30 FPS
  lastFrame = now;

  if (now - lastRssiT > 2000UL) {
    lastRssiT = now;
    wifiOk = (WiFi.status() == WL_CONNECTED);
    if (wifiOk) curRssi = WiFi.RSSI();
  }

  pollAll(now);
  pomoTick(now);
  applyBacklight(now);

  switch (screen) {
    case S_CLOCK:    screenClock(now);    break;
    case S_WEATHER:  screenWeather(now);  break;
    case S_MARKET:   screenMarket(now);   break;
    case S_WIFI:     screenWifi(now);     break;
    case S_POMO:     screenPomo(now);     break;
    case S_CYBER:    screenCyber(now);    break;
    case S_SETTINGS: screenSettings(now); break;
    case S_MENU:     screenMenu(now);     break;
  }
  present();
}
