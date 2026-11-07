#pragma once
#include <Arduino.h>

// Struktur tipleri burada durur ki, Arduino IDE-nin avtomatik prototipleri
// bu tipleri gorsun.

struct Wx  { bool ok; float temp, feels; int hum, press; float wind; int code; bool night; uint32_t updated; };
struct Fc  { int tmax, tmin, code, wday; };
struct Mkt { float brent, brentPrev, usdAzn, eurAzn, btc, btcChg; bool okBrent, okFx, okBtc; uint32_t updated; };
struct Btn { uint8_t pin; bool down; bool ev; uint32_t t; uint32_t nextRep; };
struct Net { char ssid[24]; int rssi; int ch; int enc; char bssid[18]; };
