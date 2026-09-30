// Settings and on-screen Wi-Fi setup.
#include "ui.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "render.h"
#include "mapdata.h"

#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
namespace {

const uint16_t C_BG = RGB(238, 241, 246), C_CARD = 0xFFFF, C_NAVY = RGB(22, 36, 66),
               C_TEXT = RGB(28, 32, 44), C_TEXT2 = RGB(104, 110, 124), C_LINE = RGB(214, 218, 226),
               C_EDGE = RGB(170, 178, 194), C_ACCENT = RGB(226, 58, 94), C_GOOD = RGB(34, 160, 90),
               C_BAD = RGB(214, 30, 30), C_KEY = 0xFFFF, C_KEY_DARK = RGB(206, 212, 224),
               C_WHITE = 0xFFFF, C_SOFT = RGB(200, 210, 230);
const int W = SCREEN_W, H = SCREEN_H;

// ---- text ------------------------------------------------------------------------
void fitCopy(char* dst, size_t n, const char* src, const Fnt& fn, int maxW) {
  utf8ToFont(src, dst, n);                 // one byte per character from here on
  int len = strlen(dst);
  if (textW(fn, dst) <= maxW) return;
  while (len > 1) {
    dst[--len] = 0;
    dst[len - 1] = '\x84';
    if (textW(fn, dst) <= maxW) return;
  }
}

// ---- simple widgets ------------------------------------------------------------------
struct Rect { int16_t x, y, w, h; bool hit(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; } };

void button(Adafruit_GFX& g, const Rect& r, const char* label, bool primary, const Fnt& f = B16) {
  g.fillRoundRect(r.x, r.y, r.w, r.h, 8, primary ? C_NAVY : C_CARD);
  g.drawRoundRect(r.x, r.y, r.w, r.h, 8, primary ? C_NAVY : C_EDGE);
  textC(g, r.x + r.w / 2, r.y + (r.h - f.size) / 2 - 1, label, f, primary ? C_WHITE : C_NAVY);
}
const Rect BACK = {12, 9, 138, 40};
const Rect SKIP = {W - 142, 9, 130, 40}, SET_HOME = {W - 182, 9, 170, 40};
void header(Adafruit_GFX& g, const char* title, bool back, const char* right = nullptr, const Rect* rightR = nullptr) {
  g.fillRect(0, 0, W, 58, C_NAVY);
  int tx = 24;
  if (back) {
    g.drawRoundRect(BACK.x, BACK.y, BACK.w, BACK.h, 8, C_SOFT);
    int cx = BACK.x + 22, cy = BACK.y + BACK.h / 2;
    for (int i = 0; i < 3; i++) {                   // chevron
      g.drawLine(cx + 6 + i, cy - 8, cx - 2 + i, cy, C_WHITE);
      g.drawLine(cx - 2 + i, cy, cx + 6 + i, cy + 8, C_WHITE);
    }
    text(g, BACK.x + 40, BACK.y + 11, TR("Takaisin", "Back"), B16, C_WHITE);
    tx = BACK.x + BACK.w + 20;
  }
  text(g, tx, 17, title, B22, C_WHITE);
  if (right && rightR) {
    g.drawRoundRect(rightR->x, rightR->y, rightR->w, rightR->h, 8, C_SOFT);
    textC(g, rightR->x + rightR->w / 2, rightR->y + 11, right, B16, C_WHITE);
  }
}
void card(Adafruit_GFX& g, int x, int y, int w, int h, const char* label) {
  g.fillRoundRect(x, y, w, h, 10, C_CARD);
  g.drawRoundRect(x, y, w, h, 10, C_LINE);
  text(g, x + 20, y + 14, label, R12, C_TEXT2);
}
void signalBars(Adafruit_GFX& g, int x, int y, int rssi, uint16_t c) {
  int bars = rssi >= -55 ? 4 : rssi >= -65 ? 3 : rssi >= -75 ? 2 : 1;
  for (int i = 0; i < 4; i++) {
    int h = 6 + i * 5;
    if (i < bars) g.fillRect(x + i * 7, y + 22 - h, 5, h, c);
    else g.drawRect(x + i * 7, y + 22 - h, 5, h, C_EDGE);
  }
}
void lockIcon(Adafruit_GFX& g, int x, int y, uint16_t c) {
  g.drawRoundRect(x + 3, y, 10, 12, 4, c);
  g.drawRoundRect(x + 4, y + 1, 8, 10, 3, c);
  g.fillRoundRect(x, y + 8, 16, 12, 2, c);
}
const char* signalWord(int rssi) {
  return rssi >= -55 ? TR("erinomainen", "excellent") : rssi >= -65 ? TR("hyvä", "good") : rssi >= -75 ? TR("kohtalainen", "fair") : TR("heikko", "weak");
}

// ---- state ---------------------------------------------------------------------------
enum Screen { S_NONE, S_SETTINGS, S_WIFI, S_KEYS, S_CONNECT, S_PLACES };
const WifiHooks* hooks = nullptr;
Screen screen = S_NONE;
bool firstRun = false;
char note[128] = "";
bool noteAlert = false;         // show the note in red

const int MAX_NETS = 30, PER_PAGE = 6;
WifiNet nets[MAX_NETS];
int nNets = -1;                 // -1 = searching
int page = 0;

// What the keyboard is typing: a Wi-Fi password, a network name, or a home address.
enum KbFor { KB_PASS, KB_SSID, KB_PLACE };
KbFor kbFor = KB_PASS;
bool homeFirst = false;         // asking for home right after the first Wi-Fi setup
char placeQuery[64] = "";
const int MAX_PLACES = 6;
Place places[MAX_PLACES];
int nPlaces = -1;               // -1 searching, -2 failed
char ssid[33] = "", pass[64] = "", field[64] = "";
bool secure = true, showPw = false;
int kbMode = 0;                 // 0 letters, 1 symbols, 2 more symbols
int shift = 0;                  // 0 off, 1 next letter, 2 caps lock
uint32_t shiftAt = 0;

uint32_t connectStart = 0, connectedAt = 0;
int connectResult = 0;          // 0 trying, 1 ok, -1 failed

// ---- screens: settings ----------------------------------------------------------------
const Rect SET_CHANGE = {44, 156, 230, 44}, SET_FORGET = {290, 156, 210, 44}, SET_DEMO = {516, 156, 240, 44};

// Units and language: four two-way switches.
const int N_SWITCHES = 4;
const char* switchLabel(int i) {
  switch (i) {
    case 0: return TR("ETÄISYYS", "DISTANCE");
    case 1: return TR("NOPEUS", "SPEED");
    case 2: return TR("KORKEUS", "ALTITUDE");
    default: return "KIELI / LANGUAGE";
  }
}
const char* switchText(int i, bool a) {
  static const char* t[N_SWITCHES][2] = {{"km", "nm"}, {"km/h", "kt"}, {"ft", "m"}, {"Suomi", "English"}};
  return t[i][a ? 0 : 1];
}
const int UNIT_Y = 262, UNIT_H = 42, UNIT_W = 169, UNIT_X0 = 44, UNIT_GAP = 12;
Rect unitHalf(int i, int half) {
  int x = UNIT_X0 + i * (UNIT_W + UNIT_GAP);
  return {(int16_t)(x + half * UNIT_W / 2), UNIT_Y, (int16_t)(UNIT_W / 2), UNIT_H};
}
bool unitIsA(int i) {
  switch (i) {
    case 0: return units.distKm;
    case 1: return units.speedKmh;
    case 2: return !units.altM;
    default: return language == LANG_FI;
  }
}
void unitSet(int i, bool a) {
  switch (i) {
    case 0: units.distKm = a; break;
    case 1: units.speedKmh = a; break;
    case 2: units.altM = !a; break;
    default: language = a ? LANG_FI : LANG_EN; break;
  }
}

void drawSettings(Adafruit_GFX& g, AppState& s) {
  header(g, TR("Asetukset", "Settings"), true, TR("Aseta koti", "Set home"), &SET_HOME);
  char cur[33] = "", ip[20] = "", t[120];
  int rssi = 0;
  hooks->current(cur, sizeof cur, ip, sizeof ip, &rssi);

  // Wi-Fi
  card(g, 24, 70, 752, 146, TR("WI-FI-YHTEYS", "WI-FI CONNECTION"));
  uint16_t dot = cur[0] ? C_GOOD : s.demo ? C_TEXT2 : C_BAD;
  g.fillCircle(52, 110, 7, dot);
  if (cur[0]) {
    char name[40];
    fitCopy(name, sizeof name, cur, B22, 560);
    snprintf(t, sizeof t, TR("Yhdistetty: %s", "Connected: %s"), name);
  } else {
    snprintf(t, sizeof t, "%s", s.demo ? TR("Ei yhteyttä (näytetään demokoneita)", "Not connected (showing demo aircraft)") : TR("Ei yhteyttä", "Not connected"));
  }
  text(g, 70, 98, t, B22, C_TEXT);
  if (cur[0]) snprintf(t, sizeof t, TR("Signaali %s  \x83  osoite %s", "Signal %s  \x83  address %s"), signalWord(rssi), ip);
  else snprintf(t, sizeof t, "%s", TR("Valitse verkko, niin näet koneet reaaliajassa.", "Choose a network to see live aircraft."));
  text(g, 70, 128, t, R14, C_TEXT2);
  button(g, SET_CHANGE, cur[0] ? TR("Vaihda verkko", "Change network") : TR("Valitse verkko", "Choose network"), true);
  if (cur[0]) button(g, SET_FORGET, TR("Unohda verkko", "Forget network"), false);
  if (!s.demo) button(g, SET_DEMO, TR("Näytä demokoneet", "Show demo aircraft"), false);

  // Units
  card(g, 24, 228, 752, 90, "");                         // each switch has its own label
  for (int i = 0; i < N_SWITCHES; i++) {
    int x = UNIT_X0 + i * (UNIT_W + UNIT_GAP);
    text(g, x, 242, switchLabel(i), R12, C_TEXT2);
    g.drawRoundRect(x, UNIT_Y, UNIT_W, UNIT_H, 8, C_EDGE);
    for (int half = 0; half < 2; half++) {
      Rect r = unitHalf(i, half);
      bool on = unitIsA(i) == (half == 0);
      if (on) g.fillRoundRect(r.x, r.y, r.w, r.h, 8, C_NAVY);
      textC(g, r.x + r.w / 2, r.y + 12, switchText(i, half == 0), B16, on ? C_WHITE : C_NAVY);
    }
  }

  // Live data + home
  card(g, 24, 330, 752, 130, TR("TILA", "STATUS"));
  if (s.demo) snprintf(t, sizeof t, "%s", TR("Demotila: keksittyjä koneita kodin lähellä", "Demo mode: made-up aircraft near home"));
  else if (s.apiOk && s.updatedEpoch) snprintf(t, sizeof t, TR("Koneet tulevat palvelusta %s", "Aircraft data from %s"), s.source);
  else if (!s.apiOk) snprintf(t, sizeof t, "%s", TR("Koneiden tietoja ei juuri nyt saada", "Aircraft data is unavailable right now"));
  else snprintf(t, sizeof t, "%s", TR("Odotetaan ensimmäistä päivitystä", "Waiting for the first update"));
  text(g, 44, 360, t, B18, s.apiOk || s.demo ? C_TEXT : C_BAD);
  if (!s.apiOk && !s.demo && s.apiError[0]) {
    char e[100];
    fitCopy(e, sizeof e, s.apiError, R14, 560);
    text(g, 44, 390, e, R14, C_TEXT2);
  } else {
    snprintf(t, sizeof t, TR("%d konetta seurannassa  \x83  uudet sijainnit %d s välein", "%d aircraft tracked  \x83  new positions every %d s"), s.nPlanes, POLL_SECONDS);
    text(g, 44, 390, t, R14, C_TEXT2);
  }
  // Phone settings page (web.cpp): address and a QR code to open it
  if (cur[0] && ip[0]) {
    char url[40];
    snprintf(url, sizeof url, "http://%s", ip);
    text(g, 44, 416, TR("Lisää asetuksia puhelimella (samassa Wi-Fissä):", "More settings on your phone (same Wi-Fi):"), R14, C_TEXT2);
    text(g, 44, 434, url, B16, C_NAVY);
    drawQr(g, 666, 342, url, 3);
  } else {
    text(g, 44, 418, TR("Lisää asetuksia puhelimella, kun Wi-Fi on yhdistetty.", "More settings on your phone once Wi-Fi is connected."), R14, C_TEXT2);
  }
}

// ---- screens: network list -------------------------------------------------------------
const Rect WIFI_LANG = {W - 182 - 12 - 150, 9, 150, 40};
const Rect WIFI_RESCAN = {W - 182, 9, 170, 40}, WIFI_DEMO = {24, H - 50, 250, 42},
           WIFI_PREV = {W - 212, H - 50, 90, 42}, WIFI_NEXT = {W - 114, H - 50, 90, 42};
const int ROW_Y = 92, ROW_H = 56;

int entries() { return nNets < 0 ? 0 : nNets + 1; }   // + "Other network..."
int pages() { return (entries() + PER_PAGE - 1) / PER_PAGE; }

void drawWifi(Adafruit_GFX& g, uint32_t now) {
  header(g, TR("Valitse Wi-Fi", "Choose Wi-Fi"), !firstRun, TR("Etsi uudelleen", "Scan again"), &WIFI_RESCAN);
  if (firstRun) button(g, WIFI_LANG, TR("In English", "Suomeksi"), false);
  g.fillRect(0, 58, W, H - 58, C_BG);
  text(g, 24, 66, note[0] ? note : firstRun ? TR("Tervetuloa! Valitse Wi-Fi-verkko, niin näet koneet reaaliajassa.", "Welcome! Choose a Wi-Fi network to see live aircraft.")
       : TR("Napauta verkkoasi. Laite toimii vain 2,4 GHz:n Wi-Fi-verkoissa.", "Tap your network. Only 2.4 GHz Wi-Fi networks work."), R14,
       noteAlert ? C_BAD : C_TEXT2);
  if (nNets < 0) {
    char t[40];
    int dots = (now / 400) % 4;
    const char* lbl = TR("Etsitään verkkoja", "Scanning for networks");
    snprintf(t, sizeof t, "%s%.*s", lbl, dots, "...");
    text(g, W / 2 - (textW(B22, lbl) + textW(B22, "...")) / 2, 220, t, B22, C_TEXT);
  } else {
    g.fillRoundRect(24, ROW_Y - 4, W - 48, PER_PAGE * ROW_H + 8, 10, C_CARD);
    g.drawRoundRect(24, ROW_Y - 4, W - 48, PER_PAGE * ROW_H + 8, 10, C_LINE);
    for (int k = 0; k < PER_PAGE; k++) {
      int i = page * PER_PAGE + k;
      if (i >= entries()) break;
      int y = ROW_Y + k * ROW_H;
      if (k) g.drawFastHLine(40, y, W - 80, C_LINE);
      if (i == nNets) {
        text(g, 90, y + 18, TR("Muu verkko (kirjoita nimi)\x84", "Other network (type the name)\x84"), B18, C_NAVY);
        continue;
      }
      signalBars(g, 44, y + 16, nets[i].rssi, C_NAVY);
      char name[40];
      fitCopy(name, sizeof name, nets[i].ssid, B18, 560);
      text(g, 90, y + 18, name, B18, C_TEXT);
      if (nets[i].secure) lockIcon(g, W - 72, y + 18, C_TEXT2);
    }
    if (nNets == 0) text(g, 90, ROW_Y + ROW_H + 18, TR("Verkkoja ei löytynyt. Napauta Etsi uudelleen.", "No networks found. Tap Scan again."), R14, C_TEXT2);
  }
  if (firstRun) button(g, WIFI_DEMO, TR("Kokeile demokoneilla", "Try with demo aircraft"), false);
  if (pages() > 1) {
    char t[32];
    snprintf(t, sizeof t, TR("Sivu %d/%d", "Page %d/%d"), page + 1, pages());
    text(g, WIFI_PREV.x - textW(R14, t) - 16, H - 36, t, R14, C_TEXT2);
    button(g, WIFI_PREV, TR("Edell.", "Prev"), false);
    button(g, WIFI_NEXT, TR("Seur.", "Next"), false);
  }
}

// ---- screens: keyboard ------------------------------------------------------------------
const Rect KB_FIELD = {24, 70, 632, 52}, KB_SHOW = {668, 70, 108, 52};
const int KEY_Y0 = 136, KEY_H = 60, KEY_GAP = 6, KEY_W = 70;
// Finnish keyboard: letters rows include å, ä and ö where a Finnish keyboard has them.
const char* ROWS[3][4] = {
  {"1234567890", "qwertyuiopå", "asdfghjklöä", "zxcvbnm"},
  {"1234567890", "!@#$%^&*()", "-_=+[]{};", ":'\",.?/"},
  {"1234567890", "~`|\\<>!@#$", "-_=+[]{};", ":'\",.?/"},
};
enum KeyKind { K_CHAR, K_SHIFT, K_BACK, K_MODE, K_SPACE, K_ACTION };
struct Key { Rect r; KeyKind kind; char s[5]; };   // s: the UTF-8 character it types
Key keys[64];
int nKeys = 0;

// Split a UTF-8 string into characters; returns how many.
int utf8Split(const char* in, char out[][5], int max) {
  int n = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && n < max) {
    int len = *p < 0x80 ? 1 : (*p & 0xE0) == 0xC0 ? 2 : (*p & 0xF0) == 0xE0 ? 3 : 4;
    memcpy(out[n], p, len);
    out[n][len] = 0;
    p += len;
    n++;
  }
  return n;
}
void upper(char* c) {
  if (c[0] >= 'a' && c[0] <= 'z') c[0] -= 32;
  else if (!strcmp(c, "å")) strcpy(c, "Å");
  else if (!strcmp(c, "ä")) strcpy(c, "Ä");
  else if (!strcmp(c, "ö")) strcpy(c, "Ö");
}

void layoutKeys() {
  nKeys = 0;
  for (int row = 0; row < 4; row++) {
    char chars[12][5];
    int n = utf8Split(ROWS[kbMode][row], chars, 12);
    int y = KEY_Y0 + row * (KEY_H + KEY_GAP);
    int kw = row == 3 ? KEY_W : (752 - (n - 1) * KEY_GAP) / (n < 10 ? 10 : n);
    int x = 24;
    if (row < 3 && n < 10) x = 24 + (10 - n) * (kw + KEY_GAP) / 2;   // centre short rows
    if (row == 3) {
      keys[nKeys] = {{24, (int16_t)y, 106, KEY_H}, K_SHIFT, ""};
      nKeys++;
      x = 24 + 106 + KEY_GAP + ((7 - n) * (KEY_W + KEY_GAP)) / 2;
    }
    for (int i = 0; i < n; i++) {
      Key& k = keys[nKeys++];
      k.r = {(int16_t)x, (int16_t)y, (int16_t)kw, KEY_H};
      k.kind = K_CHAR;
      snprintf(k.s, sizeof k.s, "%s", chars[i]);
      if (kbMode == 0 && shift) upper(k.s);
      x += kw + KEY_GAP;
    }
    if (row == 3) { keys[nKeys] = {{W - 24 - 106, (int16_t)y, 106, KEY_H}, K_BACK, ""}; nKeys++; }
  }
  int y = KEY_Y0 + 4 * (KEY_H + KEY_GAP);
  keys[nKeys] = {{24, (int16_t)y, 146, KEY_H}, K_MODE, ""}; nKeys++;
  keys[nKeys] = {{24 + 146 + KEY_GAP, (int16_t)y, 446, KEY_H}, K_SPACE, " "}; nKeys++;
  keys[nKeys] = {{W - 24 - 146, (int16_t)y, 146, KEY_H}, K_ACTION, ""}; nKeys++;
}

void drawKeys(Adafruit_GFX& g, uint32_t now) {
  char title[64], name[36];
  fitCopy(name, sizeof name, ssid, B22, 420);
  if (kbFor == KB_PLACE) snprintf(title, sizeof title, "%s", TR("Missä koti on?", "Where is home?"));
  else if ((kbFor == KB_SSID)) snprintf(title, sizeof title, "%s", TR("Verkon nimi", "Network name"));
  else snprintf(title, sizeof title, TR("Salasana: %s", "Password: %s"), name);
  if (kbFor == KB_PLACE && homeFirst) header(g, title, false, TR("Ohita", "Skip"), &SKIP);
  else header(g, title, true);
  g.fillRect(0, 58, W, H - 58, C_BG);

  // text field
  g.fillRoundRect(KB_FIELD.x, KB_FIELD.y, KB_FIELD.w, KB_FIELD.h, 8, C_CARD);
  g.drawRoundRect(KB_FIELD.x, KB_FIELD.y, KB_FIELD.w, KB_FIELD.h, 8, C_NAVY);
  char shown[70];
  int len = strlen(field);
  if (kbFor == KB_PASS && !showPw) { int c = utf8Len(field); memset(shown, '*', c); shown[c] = 0; }
  else utf8ToFont(field, shown, sizeof shown);               // one byte per character
  const char* vis = shown;                                   // keep the end visible
  while (textW(B22, vis) > KB_FIELD.w - 40 && *vis) vis++;
  if (!len) text(g, KB_FIELD.x + 16, KB_FIELD.y + 14, kbFor == KB_PLACE ? TR("Katuosoite tai paikkakunta, esim. Helsinki", "Street address or town, e.g. Helsinki") :
                 (kbFor == KB_SSID) ? TR("Kirjoita verkon nimi", "Type the network name") : TR("Kirjoita Wi-Fi-salasana", "Type the Wi-Fi password"), R14, C_TEXT2);
  else text(g, KB_FIELD.x + 16, KB_FIELD.y + 13, vis, B22, C_TEXT);
  if ((now / 500) % 2 == 0) {
    int cx = KB_FIELD.x + 16 + (len ? textW(B22, vis) : 0) + 2;
    g.fillRect(cx, KB_FIELD.y + 12, 2, 28, C_NAVY);
  }
  if (kbFor == KB_PASS) button(g, KB_SHOW, showPw ? TR("Piilota", "Hide") : TR("Näytä", "Show"), false);

  layoutKeys();
  for (int i = 0; i < nKeys; i++) {
    const Key& k = keys[i];
    bool dark = k.kind != K_CHAR && k.kind != K_SPACE;
    uint16_t bg = k.kind == K_ACTION ? C_NAVY : (k.kind == K_SHIFT && shift) ? C_NAVY : dark ? C_KEY_DARK : C_KEY;
    uint16_t fg = (k.kind == K_ACTION || (k.kind == K_SHIFT && shift)) ? C_WHITE : C_TEXT;
    g.fillRoundRect(k.r.x, k.r.y + 2, k.r.w, k.r.h, 7, C_EDGE);            // key shadow
    g.fillRoundRect(k.r.x, k.r.y, k.r.w, k.r.h, 7, bg);
    int cx = k.r.x + k.r.w / 2, cy = k.r.y + k.r.h / 2;
    char lab[24] = "";
    switch (k.kind) {
      case K_CHAR: snprintf(lab, sizeof lab, "%s", k.s); break;
      case K_MODE: snprintf(lab, sizeof lab, "%s", kbMode ? "ABC" : "?123"); break;
      case K_SPACE: snprintf(lab, sizeof lab, "%s", TR("välilyönti", "space")); break;
      case K_ACTION: snprintf(lab, sizeof lab, "%s", kbFor == KB_PLACE ? TR("Hae", "Search") : (kbFor == KB_SSID) ? TR("Seuraava", "Next") : TR("Yhdistä", "Connect")); break;
      case K_SHIFT:
        if (kbMode) { snprintf(lab, sizeof lab, "%s", kbMode == 1 ? TR("lisää", "more") : TR("takaisin", "back")); break; }
        g.fillTriangle(cx, cy - 12, cx - 12, cy + 1, cx + 12, cy + 1, fg);     // shift arrow
        g.fillRect(cx - 5, cy + 1, 10, 10, fg);
        if (shift == 2) g.fillRect(cx - 9, cy + 13, 18, 3, fg);                // caps lock bar
        break;
      case K_BACK:
        g.fillTriangle(cx - 18, cy, cx - 8, cy - 10, cx - 8, cy + 10, fg);     // backspace
        g.fillRect(cx - 8, cy - 10, 26, 21, fg);
        for (int d = -1; d <= 1; d++) {
          g.drawLine(cx - 1 + d, cy - 5, cx + 9 + d, cy + 5, bg);
          g.drawLine(cx + 9 + d, cy - 5, cx - 1 + d, cy + 5, bg);
        }
        break;
    }
    if (lab[0]) {
      const Fnt& f = k.kind == K_CHAR ? B22 : B16;
      textC(g, cx, cy - f.size / 2 - 2, lab, f, fg);
    }
  }
}

// ---- screens: connecting ----------------------------------------------------------------
const Rect CON_RETRY = {W / 2 - 250, 330, 240, 50}, CON_OTHER = {W / 2 + 10, 330, 240, 50};

void drawConnect(Adafruit_GFX& g, uint32_t now) {
  header(g, "Wi-Fi", false);
  g.fillRect(0, 58, W, H - 58, C_BG);
  char t[100], name[40];
  fitCopy(name, sizeof name, ssid, B26, 520);
  if (connectResult == 0) {
    for (int i = 0; i < 12; i++) {                      // spinner
      float a = (i / 12.0f) * 6.2832f;
      int phase = (int)((now / 90) % 12);
      int age = (i - phase + 12) % 12;
      uint16_t c = age < 3 ? C_NAVY : age < 6 ? C_EDGE : C_LINE;
      g.fillCircle(W / 2 + 34 * sinf(a), 190 - 34 * cosf(a), 6, c);
    }
    snprintf(t, sizeof t, TR("Yhdistetään: %s", "Connecting: %s"), name);
    textC(g, W / 2, 260, t, B26, C_TEXT);
    textC(g, W / 2, 300, TR("Tämä voi kestää jopa 20 sekuntia.", "This can take up to 20 seconds."), R14, C_TEXT2);
  } else if (connectResult > 0) {
    g.fillCircle(W / 2, 190, 40, C_GOOD);
    for (int d = -3; d <= 3; d++) {                     // check mark
      g.drawLine(W / 2 - 18, 190 + d, W / 2 - 5, 203 + d, C_WHITE);
      g.drawLine(W / 2 - 5, 203 + d, W / 2 + 20, 176 + d, C_WHITE);
    }
    snprintf(t, sizeof t, TR("Yhdistetty: %s", "Connected: %s"), name);
    textC(g, W / 2, 260, t, B26, C_TEXT);
    textC(g, W / 2, 300, TR("Nyt näet koneet reaaliajassa.", "Live aircraft are on their way."), R14, C_TEXT2);
  } else {
    g.fillCircle(W / 2, 190, 40, C_BAD);
    for (int d = -3; d <= 3; d++) {                     // cross
      g.drawLine(W / 2 - 16 + d, 174, W / 2 + 16 + d, 206, C_WHITE);
      g.drawLine(W / 2 + 16 + d, 174, W / 2 - 16 + d, 206, C_WHITE);
    }
    snprintf(t, sizeof t, TR("Yhteys ei onnistunut: %s", "Could not connect: %s"), name);
    textC(g, W / 2, 252, t, B26, C_TEXT);
    textC(g, W / 2, 290, secure ? TR("Tarkista salasana (isot ja pienet kirjaimet ovat eri merkkejä).", "Check the password (upper and lower case are different).")
                                : TR("Tarkista, että verkko on kantaman sisällä, ja yritä uudelleen.", "Check that the network is in range, and try again."), R14, C_TEXT2);
    button(g, CON_RETRY, TR("Yritä uudelleen", "Try again"), true);
    button(g, CON_OTHER, TR("Valitse toinen", "Choose another"), false);
  }
}

void startScan() {
  nNets = -1;
  page = 0;
  hooks->startScan();
}
void openKeys(bool name) {
  kbFor = name ? KB_SSID : KB_PASS;
  snprintf(field, sizeof field, "%s", name ? "" : pass);
  kbMode = 0;
  shift = 0;
  showPw = false;
  screen = S_KEYS;
}
// ---- screens: home search results --------------------------------------------------------
const Rect PL_RETRY = {W / 2 - 120, 300, 240, 50};
void startPlaceSearch() {
  nPlaces = -1;
  hooks->placeSearch(placeQuery);
  screen = S_PLACES;
}
void drawPlaces(Adafruit_GFX& g, uint32_t now) {
  header(g, TR("Valitse kotisi", "Choose your home"), true);
  g.fillRect(0, 58, W, H - 58, C_BG);
  char t[120], q[48];
  fitCopy(q, sizeof q, placeQuery, R14, 560);
  snprintf(t, sizeof t, TR("Haku: %s", "Search: %s"), q);
  text(g, 24, 66, t, R14, C_TEXT2);
  if (nPlaces == -1) {
    int dots = (now / 400) % 4;
    const char* lbl = TR("Haetaan", "Searching");
    snprintf(t, sizeof t, "%s%.*s", lbl, dots, "...");
    text(g, W / 2 - (textW(B22, lbl) + textW(B22, "...")) / 2, 220, t, B22, C_TEXT);
    return;
  }
  if (nPlaces <= 0) {
    textC(g, W / 2, 190, nPlaces == 0 ? TR("Paikkaa ei löytynyt", "Place not found") : TR("Haku ei onnistunut", "Search failed"), B22, C_TEXT);
    textC(g, W / 2, 230, nPlaces == 0 ? TR("Kokeile pelkkää paikkakuntaa tai tarkista kirjoitusasu.", "Try just the town name, or check the spelling.")
                                      : TR("Tarkista Wi-Fi-yhteys ja yritä uudelleen.", "Check the Wi-Fi connection and try again."), R14, C_TEXT2);
    button(g, PL_RETRY, TR("Muuta hakua", "Change search"), true);
    return;
  }
  g.fillRoundRect(24, ROW_Y - 4, W - 48, nPlaces * ROW_H + 8, 10, C_CARD);
  g.drawRoundRect(24, ROW_Y - 4, W - 48, nPlaces * ROW_H + 8, 10, C_LINE);
  for (int i = 0; i < nPlaces; i++) {
    int y = ROW_Y + i * ROW_H;
    if (i) g.drawFastHLine(40, y, W - 80, C_LINE);
    char name[60], detail[80];
    fitCopy(name, sizeof name, places[i].name, B18, W - 100);
    fitCopy(detail, sizeof detail, places[i].detail, R14, W - 100);
    text(g, 44, y + 7, name, B18, C_TEXT);
    text(g, 44, y + 31, detail, R14, C_TEXT2);
  }
  text(g, 24, H - 24, TR("Osoitehaku: (c) OpenStreetMapin tekijät, Nominatim", "Address search: (c) OpenStreetMap contributors, Nominatim"), R12, C_TEXT2);
}

void startConnect() {
  connectResult = 0;
  connectStart = 0;       // stamped on the next tick
  hooks->connect(ssid, pass);
  screen = S_CONNECT;
}

}  // namespace

// ---- public ---------------------------------------------------------------------------------
void uiInit(const WifiHooks* h) { hooks = h; }
bool uiActive() { return screen != S_NONE; }
bool uiBusy() { return screen == S_KEYS || screen == S_CONNECT || screen == S_PLACES || (screen == S_WIFI && firstRun); }
void uiClose() { screen = S_NONE; }
void uiOpenSettings() { screen = S_SETTINGS; }
void uiOpenHome(bool first) {
  homeFirst = first;
  kbFor = KB_PLACE;
  snprintf(field, sizeof field, "%s", placeQuery);
  kbMode = 0;
  shift = 1;                                  // capital first letter, like a place name
  screen = S_KEYS;
}
void uiOpenWifi(const char* n, bool first, bool alert) {
  snprintf(note, sizeof note, "%s", n ? n : "");
  noteAlert = alert;
  firstRun = first;
  screen = S_WIFI;
  startScan();
}

void uiTick(uint32_t now) {
  if (screen == S_WIFI && nNets < 0) {
    int n = hooks->scanResults(nets, MAX_NETS);
    if (n >= 0) nNets = n;
  }
  if (screen == S_CONNECT) {
    if (!connectStart) connectStart = now ? now : 1;
    if (connectResult == 0) {
      int st = hooks->connectStatus();
      if (st == 0 && now - connectStart > 20000) st = -1;
      if (st != 0) {
        connectResult = st;
        if (st > 0) { connectedAt = now; hooks->connected(ssid, pass); }
      }
    } else if (connectResult > 0 && now - connectedAt > 1800) {
      screen = S_NONE;                                 // back to the map
      firstRun = false;
      if (hooks->needHome()) uiOpenHome(true);        // first time: where is home?
    }
  }
  if (screen == S_PLACES && nPlaces == -1) {
    int n = hooks->placeResults(places, MAX_PLACES);
    if (n != -1) nPlaces = n;
  }
}

void uiTap(int x, int y, uint32_t now, AppState& s) {
  switch (screen) {
    case S_SETTINGS:
      if (BACK.hit(x, y)) screen = S_NONE;
      else if (SET_HOME.hit(x, y)) uiOpenHome(false);
      else if (SET_CHANGE.hit(x, y)) uiOpenWifi("", false, false);
      else if (SET_FORGET.hit(x, y)) {
        char cur[33] = "", ip[20];
        int r;
        hooks->current(cur, sizeof cur, ip, sizeof ip, &r);
        if (cur[0]) { hooks->forget(); uiOpenWifi(TR("Verkko unohdettiin. Valitse uusi verkko.", "Network forgotten. Choose a new network."), false, false); }
      } else if (SET_DEMO.hit(x, y) && !s.demo) { hooks->useDemo(); screen = S_NONE; }
      else {
        for (int i = 0; i < N_SWITCHES; i++)
          for (int half = 0; half < 2; half++)
            if (unitHalf(i, half).hit(x, y) && unitIsA(i) != (half == 0)) {
              unitSet(i, half == 0);
              hooks->saveSettings();                   // remembered after a restart
            }
      }
      break;
    case S_WIFI:
      if (!firstRun && BACK.hit(x, y)) { screen = S_SETTINGS; break; }
      if (WIFI_RESCAN.hit(x, y)) { note[0] = 0; noteAlert = false; startScan(); break; }
      if (firstRun && WIFI_LANG.hit(x, y)) {                 // switch language before setting up
        language = language == LANG_EN ? LANG_FI : LANG_EN;
        hooks->saveSettings();
        break;
      }
      if (firstRun && WIFI_DEMO.hit(x, y)) { hooks->useDemo(); firstRun = false; screen = S_NONE; break; }
      if (pages() > 1 && WIFI_PREV.hit(x, y)) { if (page > 0) page--; break; }
      if (pages() > 1 && WIFI_NEXT.hit(x, y)) { if (page < pages() - 1) page++; break; }
      if (nNets >= 0 && x >= 24 && x < W - 24 && y >= ROW_Y && y < ROW_Y + PER_PAGE * ROW_H) {
        int i = page * PER_PAGE + (y - ROW_Y) / ROW_H;
        if (i == nNets) { ssid[0] = 0; pass[0] = 0; secure = true; openKeys(true); }
        else if (i < nNets) {
          snprintf(ssid, sizeof ssid, "%s", nets[i].ssid);
          secure = nets[i].secure;
          pass[0] = 0;
          if (secure) openKeys(false);
          else startConnect();                         // open network: no password
        }
      }
      break;
    case S_KEYS: {
      if (kbFor == KB_PLACE) {
        if (homeFirst && SKIP.hit(x, y)) { hooks->homeSkipped(); homeFirst = false; screen = S_NONE; break; }
        if (!homeFirst && BACK.hit(x, y)) { screen = S_SETTINGS; break; }
      } else if (BACK.hit(x, y)) { screen = S_WIFI; if (nNets < 0) startScan(); break; }
      if (kbFor == KB_PASS && KB_SHOW.hit(x, y)) { showPw = !showPw; break; }
      layoutKeys();
      for (int i = 0; i < nKeys; i++) {
        const Key& k = keys[i];
        if (!k.r.hit(x, y)) continue;
        int len = strlen(field);
        switch (k.kind) {
          case K_CHAR:
          case K_SPACE: {
            int add = strlen(k.s);
            if (len + add <= ((kbFor == KB_SSID) ? 32 : 63)) strcat(field, k.s);
            if (shift == 1 && k.kind == K_CHAR) shift = 0;
            break;
          }
          case K_BACK: utf8Pop(field); break;
          case K_MODE: kbMode = kbMode ? 0 : 1; shift = 0; break;
          case K_SHIFT:
            if (kbMode) { kbMode = kbMode == 1 ? 2 : 1; break; }
            if (shift == 0) { shift = 1; shiftAt = now; }
            else if (shift == 1 && now - shiftAt < 450) shift = 2;   // double tap: caps lock
            else shift = 0;
            break;
          case K_ACTION:
            if (kbFor == KB_PLACE) {
              if (!field[0]) break;
              snprintf(placeQuery, sizeof placeQuery, "%s", field);
              startPlaceSearch();
            } else if ((kbFor == KB_SSID)) {
              if (!field[0]) break;
              snprintf(ssid, sizeof ssid, "%.32s", field);
              pass[0] = 0;
              secure = true;
              openKeys(false);        // then ask for the password (leave empty if none)
            } else {
              snprintf(pass, sizeof pass, "%s", field);
              secure = pass[0] != 0;
              startConnect();
            }
            break;
        }
        break;
      }
      break;
    }
    case S_PLACES:
      if (BACK.hit(x, y) || (nPlaces <= 0 && nPlaces != -1 && PL_RETRY.hit(x, y))) { uiOpenHome(homeFirst); break; }
      if (nPlaces > 0 && x >= 24 && x < W - 24 && y >= ROW_Y && y < ROW_Y + nPlaces * ROW_H) {
        hooks->placeChosen(places[(y - ROW_Y) / ROW_H]);
        homeFirst = false;
        screen = S_NONE;                               // the map, with a cross to fine-tune
      }
      break;
    case S_CONNECT:
      if (connectResult < 0 && CON_RETRY.hit(x, y)) { if (secure) openKeys(false); else startConnect(); }
      else if (connectResult < 0 && CON_OTHER.hit(x, y)) { screen = S_WIFI; startScan(); }
      break;
    default: break;
  }
}

void uiRender(Adafruit_GFX& g, AppState& s, uint32_t now) {
  g.setTextWrap(false);
  g.fillScreen(C_BG);
  switch (screen) {
    case S_SETTINGS: drawSettings(g, s); break;
    case S_WIFI: drawWifi(g, now); break;
    case S_KEYS: drawKeys(g, now); break;
    case S_CONNECT: drawConnect(g, now); break;
    case S_PLACES: drawPlaces(g, now); break;
    default: break;
  }
}

// ============================================================================
//  Touch gestures and map actions
// ============================================================================
void Gestures::update(const TouchPt* p, int n, uint32_t now, void (*emit)(const Ev&)) {
  if (n >= 2) {                                        // two fingers: pinch to zoom
    float d = hypotf(p[0].x - p[1].x, p[0].y - p[1].y);
    if (!pinching) {
      pinching = true;
      pinchStart = d;
      if (dragging) emit(Ev{EV_DRAG_END, 0, 0, 0, 0});
      dragging = false;
    } else if (pinchStart > 20 && (d > pinchStart * 1.45f || d < pinchStart / 1.45f)) {
      emit(Ev{EV_ZOOM, 0, 0, (int16_t)(d > pinchStart ? 1 : -1), 0});
      pinchStart = d;
    }
    down = true;
  } else if (n == 1) {
    if (!down) {
      down = true; dragging = false; pinching = false;
      sx = lx = p[0].x; sy = ly = p[0].y;
      t0 = now;
    } else if (!pinching) {
      if (!dragging && (abs(p[0].x - sx) > 10 || abs(p[0].y - sy) > 10) && sx < MAP_W) dragging = true;
      if (dragging && (p[0].x != lx || p[0].y != ly))
        emit(Ev{EV_DRAG, p[0].x, p[0].y, (int16_t)(p[0].x - lx), (int16_t)(p[0].y - ly)});
    }
    lx = p[0].x; ly = p[0].y;
  } else if (down) {                                   // finger lifted
    if (dragging) emit(Ev{EV_DRAG_END, 0, 0, 0, 0});
    else if (!pinching && now - t0 < 600) emit(Ev{EV_TAP, sx, sy, 0, 0});
    down = dragging = pinching = false;
  }
}

void appGoHome(AppState& s) {
  s.cx = mercX(cfg.homeLon);
  s.cy = mercY(cfg.homeLat);
  s.zoom = START_ZOOM;
  s.selHex[0] = 0;
  s.follow = false;
}

void appStartPickHome(AppState& s, const Place& p) {
  s.pickHome = true;
  snprintf(s.pickName, sizeof s.pickName, "%s", p.name);
  s.cx = s.pickX = mercX(p.lon);
  s.cy = s.pickY = mercY(p.lat);
  s.zoom = 10;
  s.selHex[0] = 0;
  s.follow = false;
}

void appEndPickHome(AppState& s, bool save) {
  if (save) {
    cfg.homeLat = latFromY(s.cy);
    cfg.homeLon = lonFromX(s.cx);
  }
  s.pickHome = false;
  appGoHome(s);
}

void appPan(AppState& s, int dx, int dy) {
  float m = metresPerPx(s.zoom);
  s.cx = fmaxf(-2.0e7f, fminf(2.0e7f, s.cx - dx * m));
  s.cy = fmaxf(-1.6e7f, fminf(1.6e7f, s.cy + dy * m));
  s.follow = false;
}

void appZoom(AppState& s, int d) {
  s.zoom += d;
  if (s.zoom < MIN_ZOOM) s.zoom = MIN_ZOOM;
  if (s.zoom > MAX_ZOOM) s.zoom = MAX_ZOOM;
}

static void selectPlane(AppState& s, const Plane& p, void (*requestRoute)(const char*)) {
  snprintf(s.selHex, sizeof s.selHex, "%s", p.hex);
  if (requestRoute) requestRoute(p.cs);
}

int appTap(AppState& s, int x, int y, uint32_t nowMs, void (*requestRoute)(const char*)) {
  int row = -1;
  UiHit hit = uiHitTest(x, y, s, &row);
  if (s.pickHome) {                         // setting home: only moving the map (the caller saves)
    if (hit == HIT_ZOOM_IN) appZoom(s, 1);
    else if (hit == HIT_ZOOM_OUT) appZoom(s, -1);
    else if (hit == HIT_HOME) { s.cx = s.pickX; s.cy = s.pickY; }
    return hit;
  }
  switch (hit) {
    case HIT_ZOOM_IN:  appZoom(s, 1); break;
    case HIT_ZOOM_OUT: appZoom(s, -1); break;
    case HIT_HOME:     appGoHome(s); break;
    case HIT_FOLLOW:   s.follow = !s.follow; break;
    case HIT_CLOSE:    s.selHex[0] = 0; s.follow = false; break;
    case HIT_PHOTO:    photo.hidden = true; break;          // tap the photo card to hide it
    case HIT_LIST_ROW: {
      const char* hex = listRowHex(row);
      Plane* p = hex ? s.find(hex) : nullptr;
      if (p) selectPlane(s, *p, requestRoute);
      break;
    }
    case HIT_MAP: {
      s.advance(nowMs);
      int i = planeAt(s, x, y, 30);
      if (i >= 0) selectPlane(s, s.planes[i], requestRoute);
      else if (s.selHex[0]) { s.selHex[0] = 0; s.follow = false; }   // empty map: back to list
      break;
    }
    default: break;
  }
  return hit;
}

// ============================================================================
//  Finding a town by name (setting home without internet)
// ============================================================================
namespace {
// Lower-case one font-coded character (see render.h: 0x88-0x8A are Ä, Ö, Å; 0x8C Š; 0x8E Ž).
char lowerFont(char c) {
  uint8_t u = (uint8_t)c;
  if (u >= 'A' && u <= 'Z') return (char)(u + 32);
  if (u == 0x88 || u == 0x89 || u == 0x8A) return (char)(u - 3);
  if (u == 0x8C || u == 0x8E) return (char)(u - 1);
  return c;
}
bool startsWith(const char* s, const char* q) {
  for (; *q; s++, q++)
    if (!*s || lowerFont(*s) != lowerFont(*q)) return false;
  return true;
}
// Match at the start of the name or of any word in it ("Kristiinankaupunki" is found
// with "kris", "Staraja Russa" with "russa").
bool matches(const char* name, const char* q) {
  if (startsWith(name, q)) return true;
  for (const char* p = name; *p; p++)
    if ((*p == ' ' || *p == '-') && startsWith(p + 1, q)) return true;
  return false;
}
}  // namespace

int placeSearchOffline(const char* query, Place* out, int max) {
  char q[64];
  utf8ToFont(query, q, sizeof q);
  // trim spaces and anything after a comma ("Helsinki, Suomi" -> "Helsinki")
  char* c = strchr(q, ',');
  if (c) *c = 0;
  char* s = q;
  while (*s == ' ') s++;
  for (int i = (int)strlen(s) - 1; i >= 0 && s[i] == ' '; i--) s[i] = 0;
  if (!*s) return 0;
  int n = 0;
  // Two passes: exact-start matches of big towns first, then the rest.
  for (int pass = 0; pass < 2 && n < max; pass++)
    for (uint32_t i = 0; i < PLACES_N && n < max; i++) {
      const MapPlace& p = PLACES[i];
      // Match either language's name; show the one for the chosen language.
      const char* fi = mapNameFi(p.name);
      const char* en = mapNameEn(p.name);
      bool first = (startsWith(fi, s) || startsWith(en, s)) && p.big;
      if (pass == 0 ? !first : (first || !(matches(fi, s) || matches(en, s)))) continue;
      const char* name = mapName(p.name, language == LANG_EN);
      Place& o = out[n++];
      snprintf(o.name, sizeof o.name, "%s", name);
      snprintf(o.detail, sizeof o.detail, "%s", TR("kaupunki kartalta (ilman nettiä)", "town from the map (offline)"));
      o.lat = latFromY((float)p.y);
      o.lon = lonFromX((float)p.x);
    }
  return n;
}
