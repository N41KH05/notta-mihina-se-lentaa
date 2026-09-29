// Settings page for a phone (or any browser) on the same Wi-Fi. Units, home, night
// hours, plane photos and the AirLabs key can be changed here; everything is saved
// in flash like the on-screen settings. The page follows the device language.
#include "web.h"
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <time.h>
#include "net.h"

void webSaved(bool homeMoved, bool keyChanged);   // SkyTracker.ino: store and apply
const char* resetReasonText();                    // SkyTracker.ino: why it last started
uint32_t uptimeMinutes();

namespace {
WebServer server(80);
AppState* st = nullptr;
SemaphoreHandle_t mtx = nullptr;
bool started = false;

struct Locked {
  Locked() { xSemaphoreTake(mtx, portMAX_DELAY); }
  ~Locked() { xSemaphoreGive(mtx); }
};

char formToken[33];               // random per boot; every form must send it back

// Only answer requests addressed to this device (blocks DNS-rebinding attacks from web
// pages), and ask for the password if one is set.
bool allowed() {
  String host = server.hostHeader();
  int colon = host.indexOf(':');
  if (colon >= 0) host = host.substring(0, colon);
  host.toLowerCase();
  if (host != WiFi.localIP().toString() && host != "skytracker.local") {
    server.send(403, "text/plain", "Forbidden");
    return false;
  }
  if (!WEB_PASSWORD[0] || server.authenticate("skytracker", WEB_PASSWORD)) return true;
  server.requestAuthentication(BASIC_AUTH, APP_NAME);
  return false;
}
// Changes must come from our own page: right token, and no foreign Origin header.
// (Stops other websites from submitting forms to the device.)
bool fromOwnPage() {
  String origin = server.header("Origin");
  bool originOk = !origin.length() || origin == "http://" + server.hostHeader();
  if (originOk && server.arg("t") == formToken) return true;
  server.send(403, "text/plain", "Forbidden: please reload the settings page and try again");
  return false;
}
void securityHeaders() {
  server.sendHeader("Content-Security-Policy",
                    "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; "
                    "frame-ancestors 'none'; base-uri 'none'");
  server.sendHeader("X-Frame-Options", "DENY");
  server.sendHeader("X-Content-Type-Options", "nosniff");
  server.sendHeader("Referrer-Policy", "no-referrer");
  server.sendHeader("Cache-Control", "no-store");
}

// ---- small HTML helpers ---------------------------------------------------------------
void esc(String& out, const char* s) {
  for (; *s; s++) {
    switch (*s) {
      case '&': out += "&amp;"; break;
      case '<': out += "&lt;"; break;
      case '>': out += "&gt;"; break;
      case '"': out += "&quot;"; break;
      case '\'': out += "&#39;"; break;
      default: out += *s;
    }
  }
}
void decimal(String& out, double v, int digits) {   // decimal comma in Finnish
  char b[24];
  snprintf(b, sizeof b, "%.*f", digits, v);
  localDecimal(b);
  out += b;
}
void segment(String& out, const char* name, const char* a, const char* aLabel, const char* b,
             const char* bLabel, bool isA) {
  out += "<div class=seg>";
  for (int i = 0; i < 2; i++) {
    const char* v = i ? b : a;
    out += "<input type=radio name="; out += name; out += " id="; out += name; out += v;
    out += " value="; out += v; if ((i == 0) == isA) out += " checked"; out += ">";
    out += "<label for="; out += name; out += v; out += ">"; out += i ? bLabel : aLabel; out += "</label>";
  }
  out += "</div>";
}
void hourSelect(String& out, const char* name, int sel) {
  out += "<select name="; out += name; out += ">";
  for (int h = 0; h < 24; h++) {
    char b[48];
    snprintf(b, sizeof b, "<option value=%d%s>%02d:00</option>", h, h == sel ? " selected" : "", h);
    out += b;
  }
  out += "</select>";
}
const char* signalWord(int rssi) {
  return rssi > -55 ? TR("erinomainen", "excellent") : rssi > -67 ? TR("hyvä", "good")
       : rssi > -75 ? TR("kohtalainen", "fair") : TR("heikko", "weak");
}

const char STYLE[] PROGMEM = R"CSS(
:root{--navy:#162442;--accent:#e23a5e;--bg:#eef1f6;--card:#fff;--text:#1c202c;--text2:#686e7c;--line:#d6dae2;--good:#22a05a;--bad:#d61e1e}
@media (prefers-color-scheme:dark){:root{--bg:#0f1420;--card:#1a2130;--text:#e8ebf2;--text2:#9aa2b4;--line:#2c3547;--navy:#3a5a9a}}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--text);font:16px/1.45 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif}
header{background:#162442;color:#fff;padding:18px 16px}header h1{margin:0;font-size:22px;letter-spacing:.5px}header p{margin:2px 0 0;opacity:.75;font-size:14px}
main{max-width:560px;margin:0 auto;padding:12px 16px 32px}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px 16px;margin:12px 0}
h2{font-size:13px;letter-spacing:.8px;text-transform:uppercase;color:var(--text2);margin:0 0 10px}
dl{display:grid;grid-template-columns:auto 1fr;gap:6px 14px;margin:0}dt{color:var(--text2)}dd{margin:0;font-weight:600;overflow-wrap:anywhere}
label.f{display:block;font-size:14px;color:var(--text2);margin:10px 0 4px}
input[type=text],input[type=email],input[type=password],select{width:100%;font:inherit;padding:10px 12px;border:1px solid var(--line);border-radius:8px;background:var(--card);color:var(--text)}
.row{display:flex;gap:10px}.row>*{flex:1}
.seg{display:flex;border:1px solid var(--line);border-radius:8px;overflow:hidden;margin-bottom:4px}.seg input{display:none}
.seg label{flex:1;text-align:center;padding:9px 4px;cursor:pointer;font-weight:600;color:var(--navy)}
.seg input:checked+label{background:#162442;color:#fff}
.chk{display:flex;align-items:center;gap:10px;margin:6px 0;font-weight:600}.chk input{width:20px;height:20px}
.help{font-size:13px;color:var(--text2);margin:8px 0 0}
.msg{border-radius:10px;padding:12px 14px;margin:12px 0;font-weight:600}.ok{background:#e3f5ea;color:#16663a}.err{background:#fde8e8;color:#9b1c1c}
button{width:100%;font:inherit;font-weight:700;padding:14px;border-radius:10px;border:0;cursor:pointer}
.save{background:var(--accent);color:#fff;margin-top:4px}.ghost{background:transparent;color:var(--text2);border:1px solid var(--line);margin-top:12px}
footer{font-size:12px;color:var(--text2);text-align:center;margin-top:24px}
)CSS";

void sendPage(const char* msg, bool error) {
  // Snapshot of the state
  bool demo, apiOk;
  int planes;
  char source[16], apiErr[120];
  {
    Locked l;
    demo = st->demo;
    apiOk = st->apiOk;
    planes = st->nPlanes;
    snprintf(source, sizeof source, "%s", st->source);
    snprintf(apiErr, sizeof apiErr, "%s", st->apiError);
  }
  Config c = cfg;
  Units u = units;

  String o;
  o.reserve(11000);
  o += "<!doctype html><html lang=";
  o += TR("fi", "en");
  o += "><head><meta charset=utf-8>"
       "<meta name=viewport content='width=device-width,initial-scale=1'>"
       "<title>" APP_NAME "</title><style>";
  o += FPSTR(STYLE);
  o += "</style></head><body><header><h1>" APP_NAME "</h1><p>";
  o += TR("Asetukset", "Settings");
  o += "</p></header><main>";
  if (msg) { o += "<div class='msg "; o += error ? "err" : "ok"; o += "'>"; o += msg; o += "</div>"; }

  // ---- status
  o += "<section class=card><h2>"; o += TR("Tila", "Status"); o += "</h2><dl>";
  o += "<dt>Wi-Fi</dt><dd>"; esc(o, WiFi.SSID().c_str());
  o += " ("; o += signalWord(WiFi.RSSI()); o += TR(" signaali)</dd>", " signal)</dd>");
  o += "<dt>"; o += TR("Osoite", "Address"); o += "</dt><dd>"; o += WiFi.localIP().toString(); o += "</dd>";
  o += "<dt>"; o += TR("Lentotiedot", "Flight data"); o += "</dt><dd>";
  if (demo) o += TR("demokoneet (keksittyjä)", "demo aircraft (simulated)");
  else if (apiOk) { o += TR("palvelusta ", "from "); esc(o, source[0] ? source : "–"); }
  else {
    o += "<span style='color:var(--bad)'>"; o += TR("ei saada", "unavailable"); o += "</span>";
    if (apiErr[0]) { o += "<br><small>"; esc(o, apiErr); o += "</small>"; }
  }
  o += "</dd><dt>"; o += TR("Koneita", "Aircraft"); o += "</dt><dd>"; o += planes; o += "</dd>";
  if (c.airlabsKey[0]) {
    o += "<dt>AirLabs</dt><dd>"; o += netAirlabsCallsToday(); o += " / "; o += AIRLABS_DAILY_MAX;
    o += TR(" hakua tänään</dd>", " requests today</dd>");
  }
  uint32_t up = uptimeMinutes();
  char b[64];
  if (up >= 1440) snprintf(b, sizeof b, TR("%lu pv %lu t", "%lu d %lu h"), (unsigned long)(up / 1440), (unsigned long)(up / 60 % 24));
  else snprintf(b, sizeof b, TR("%lu t %lu min", "%lu h %lu min"), (unsigned long)(up / 60), (unsigned long)(up % 60));
  o += "<dt>"; o += TR("Käynnissä", "Uptime"); o += "</dt><dd>"; o += b; o += "</dd>";
  o += "<dt>"; o += TR("Käynnistyi", "Last start"); o += "</dt><dd>"; o += resetReasonText(); o += "</dd></dl></section>";

  o += "<form method=post action=/save><input type=hidden name=t value=";
  o += formToken;
  o += ">";
  // ---- language and units
  o += "<section class=card><h2>"; o += TR("Kieli ja yksiköt", "Language and units"); o += "</h2>";
  o += "<label class=f>"; o += TR("Kieli / Language", "Language / Kieli"); o += "</label>";
  segment(o, "lang", "fi", "Suomi", "en", "English", language != LANG_EN);
  o += "<label class=f>"; o += TR("Etäisyys", "Distance"); o += "</label>";
  segment(o, "dist", "km", "km", "nm", "nm", u.distKm);
  o += "<label class=f>"; o += TR("Nopeus", "Speed"); o += "</label>";
  segment(o, "speed", "kmh", "km/h", "kt", "kt", u.speedKmh);
  o += "<label class=f>"; o += TR("Korkeus", "Altitude"); o += "</label>";
  segment(o, "alt", "ft", TR("jalkaa (ft)", "feet (ft)"), "m", TR("metriä (m)", "metres (m)"), !u.altM);
  o += "</section>";

  // ---- home
  o += "<section class=card><h2>"; o += TR("Koti", "Home"); o += "</h2>";
  o += "<label class=f for=hn>"; o += TR("Nimi kartalla", "Name on the map");
  o += "</label><input type=text id=hn name=home_name maxlength=23 placeholder='";
  o += homeLabel("");
  o += "' value='";
  esc(o, isDefaultHomeName(c.homeName) ? "" : c.homeName); o += "'>";
  o += "<div class=row><div><label class=f for=la>"; o += TR("Leveysaste", "Latitude");
  o += "</label><input type=text inputmode=decimal id=la name=home_lat value='";
  decimal(o, c.homeLat, 5); o += "'></div><div><label class=f for=lo>"; o += TR("Pituusaste", "Longitude");
  o += "</label><input type=text inputmode=decimal id=lo name=home_lon value='";
  decimal(o, c.homeLon, 5); o += "'></div></div><p class=help>";
  o += TR("Koordinaatit saa esim. Google Mapsista: paina pitkään kohtaa kartalla. "
          "Tarkka kartta (rannat, järvet, pikkukaupungit) kattaa 1000 km:n säteen kartan keskipisteestä; "
          "sen ulkopuolella näkyy yksinkertaisempi maailmankartta.",
          "Coordinates can be copied from e.g. Google Maps: long-press a spot on the map. "
          "The detailed map (coastlines, lakes, small towns) covers a 1000 km radius around the map "
          "centre; outside it a simpler world map is shown.");
  o += "</p></section>";

  // ---- night
  bool nightOn = c.nightStart != c.nightEnd;
  o += "<section class=card><h2>"; o += TR("Yötila", "Night mode"); o += "</h2>";
  o += "<label class=chk><input type=checkbox name=night"; if (nightOn) o += " checked";
  o += "> "; o += TR("Sammuta näyttö yöksi", "Turn the screen off at night");
  o += "</label><div class=row><div><label class=f>"; o += TR("Alkaa", "Starts"); o += "</label>";
  hourSelect(o, "night_start", nightOn ? c.nightStart : NIGHT_START_HOUR);
  o += "</div><div><label class=f>"; o += TR("Päättyy", "Ends"); o += "</label>";
  hourSelect(o, "night_end", nightOn ? c.nightEnd : NIGHT_END_HOUR);
  o += "</div></div><p class=help>";
  o += TR("Näyttö sammuu, kun sitä ei ole kosketettu 5 minuuttiin. Napautus herättää sen.",
          "The screen turns off once it has not been touched for 5 minutes. A tap wakes it.");
  o += "</p></section>";

  // ---- photos
  o += "<section class=card><h2>"; o += TR("Koneiden kuvat", "Aircraft photos"); o += "</h2>";
  o += "<label class=chk><input type=checkbox name=photos"; if (c.photos) o += " checked";
  o += "> "; o += TR("Näytä valitun koneen kuva", "Show a photo of the selected aircraft"); o += "</label>";
  o += "<label class=f for=ct>"; o += TR("Yhteystieto Planespotters.netille", "Contact for Planespotters.net");
  o += "</label><input type=email id=ct name=contact maxlength=63 value='";
  esc(o, c.contact); o += "'><p class=help>";
  o += TR("Kuvapalvelu pyytää sovelluksia kertomaan yhteystiedon. "
          "Sähköpostiosoite riittää; sitä ei näytetä missään.",
          "The photo service asks apps to identify a contact. "
          "An email address is enough; it is not shown anywhere.");
  o += "</p></section>";

  // ---- AirLabs
  o += "<section class=card><h2>"; o += TR("Lähtö- ja saapumisajat", "Departure and arrival times"); o += "</h2>";
  o += "<label class=f for=ak>"; o += TR("AirLabs-avain", "AirLabs key");
  o += "</label><input type=password id=ak name=airlabs autocomplete=off maxlength=79 placeholder='";
  if (c.airlabsKey[0]) {
    int n = strlen(c.airlabsKey);
    o += TR("tallennettu: ••••", "saved: ••••"); esc(o, c.airlabsKey + (n > 4 ? n - 4 : 0));
  } else {
    o += TR("ei avainta", "no key");
  }
  o += "'>";
  if (c.airlabsKey[0]) {
    o += "<label class=chk><input type=checkbox name=airlabs_clear> "; o += TR("Poista avain", "Remove key"); o += "</label>";
  }
  o += "<p class=help>";
  o += TR("Ilman avainta laite arvioi saapumisajan itse. Ilmaisella avaimella "
          "(airlabs.co) näkyvät myös lähtöaika ja myöhästymiset. Jätä kenttä tyhjäksi, "
          "jos et halua muuttaa avainta.",
          "Without a key the device estimates the arrival time itself. With a free key "
          "(airlabs.co) the departure time and delays are shown as well. Leave the field empty "
          "to keep the current key.");
  o += "</p></section>";

  o += "<button class=save>"; o += TR("Tallenna", "Save"); o += "</button></form>";
  o += "<form method=post action=/restart><input type=hidden name=t value=";
  o += formToken;
  o += "><label class=chk><input type=checkbox name=sure required> ";
  o += TR("Vahvista uudelleenkäynnistys", "Confirm restart");
  o += "</label><button class=ghost>"; o += TR("Käynnistä laite uudelleen", "Restart the device"); o += "</button></form>";
  o += "<footer>" APP_NAME " · ";
  o += TR("kartta Natural Earth · kentät OurAirports · lentotiedot adsb.fi, airplanes.live, adsb.lol · reitit adsbdb",
          "map Natural Earth · airports OurAirports · flight data adsb.fi, airplanes.live, adsb.lol · routes adsbdb");
  o += "</footer></main></body></html>";
  securityHeaders();
  server.send(200, "text/html; charset=utf-8", o);
}

// ---- reading the form -----------------------------------------------------------------
bool parseNumber(String s, double lo, double hi, double& out) {
  s.trim();
  s.replace(",", ".");
  s.replace("°", "");
  if (!s.length()) return false;
  char* end;
  double v = strtod(s.c_str(), &end);
  while (*end == ' ') end++;
  if (*end || isnan(v) || v < lo || v > hi) return false;
  out = v;
  return true;
}
bool plainText(const String& s, const char* extra) {   // no control characters or quotes
  for (size_t i = 0; i < s.length(); i++) {
    uint8_t ch = s[i];
    if (ch < 0x20 || ch == 0x7F) return false;
    if (extra && strchr(extra, ch)) return false;
  }
  return true;
}

void handleSave() {
  if (!allowed() || !fromOwnPage()) return;
  String err;
  Config c = cfg;
  Units u = units;
  u.distKm = server.arg("dist") != "nm";
  u.speedKmh = server.arg("speed") != "kt";
  u.altM = server.arg("alt") == "m";
  uint8_t oldLanguage = language;
  if (server.hasArg("lang")) language = server.arg("lang") == "en" ? LANG_EN : LANG_FI;  // messages below in it

  String name = server.arg("home_name");
  name.trim();
  if (isDefaultHomeName(name.c_str())) name = "";  // default label, follows the language
  if (name.length() >= sizeof c.homeName || !plainText(name, nullptr))
    err += TR("Kodin nimi on liian pitkä (enintään 23 merkkiä, ääkköset vievät kaksi).<br>",
              "The home name is too long (at most 23 characters; letters like ä count as two).<br>");
  else snprintf(c.homeName, sizeof c.homeName, "%s", name.c_str());
  if (!parseNumber(server.arg("home_lat"), -85, 85, c.homeLat))
    err += TR("Leveysaste pitää olla luku väliltä −85…85 (esim. 60,170).<br>",
              "Latitude must be a number from −85 to 85 (e.g. 60.170).<br>");
  if (!parseNumber(server.arg("home_lon"), -180, 180, c.homeLon))
    err += TR("Pituusaste pitää olla luku väliltä −180…180 (esim. 24,938).<br>",
              "Longitude must be a number from −180 to 180 (e.g. 24.938).<br>");

  int ns = constrain(server.arg("night_start").toInt(), 0, 23), ne = constrain(server.arg("night_end").toInt(), 0, 23);
  if (server.hasArg("night")) {
    if (ns == ne) err += TR("Yötilan alku ja loppu eivät voi olla sama tunti.<br>",
                            "Night mode cannot start and end at the same hour.<br>");
    c.nightStart = ns;
    c.nightEnd = ne;
  } else {
    c.nightStart = c.nightEnd = 0;                // never switch off
  }

  c.photos = server.hasArg("photos");
  String contact = server.arg("contact");
  contact.trim();
  if (contact.length() >= sizeof c.contact || !plainText(contact, "()\"'<>;\\")) err += TR("Yhteystieto on liian pitkä tai siinä on kiellettyjä merkkejä.<br>",
                                                                                   "The contact is too long or contains characters that are not allowed.<br>");
  else snprintf(c.contact, sizeof c.contact, "%s", contact.c_str());

  String key = server.arg("airlabs");
  key.trim();
  if (server.hasArg("airlabs_clear")) {
    c.airlabsKey[0] = 0;
  } else if (key.length()) {
    bool ok = key.length() < sizeof c.airlabsKey;
    for (size_t i = 0; ok && i < key.length(); i++) ok = isalnum((uint8_t)key[i]) || key[i] == '-' || key[i] == '_';
    if (!ok) err += TR("AirLabs-avaimessa saa olla vain kirjaimia, numeroita, - ja _.<br>",
                       "The AirLabs key may only contain letters, digits, - and _.<br>");
    else snprintf(c.airlabsKey, sizeof c.airlabsKey, "%s", key.c_str());
  }

  if (err.length()) {                             // nothing is saved if anything is wrong
    err = String(TR("Ei tallennettu:<br>", "Not saved:<br>")) + err;
    sendPage(err.c_str(), true);
    language = oldLanguage;
    return;
  }
  {
    Locked l;
    bool homeMoved = fabs(c.homeLat - cfg.homeLat) > 1e-7 || fabs(c.homeLon - cfg.homeLon) > 1e-7;
    bool keyChanged = strcmp(c.airlabsKey, cfg.airlabsKey) != 0;
    cfg = c;
    units = u;
    webSaved(homeMoved, keyChanged);
  }
  server.sendHeader("Location", "/?ok=1");
  server.send(303);
}

void handleRestart() {
  if (!allowed() || !fromOwnPage()) return;
  if (!server.hasArg("sure")) { server.sendHeader("Location", "/"); server.send(303); return; }
  securityHeaders();
  server.send(200, "text/html; charset=utf-8",
              String("<!doctype html><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>"
                     "<meta http-equiv=refresh content='20;url=/'><body style='font:18px system-ui;padding:24px'>") +
                  TR("Käynnistetään uudelleen… Sivu latautuu hetken kuluttua.",
                     "Restarting… The page will reload in a moment.") + "</body>");
  delay(500);
  ESP.restart();
}
}  // namespace

void webInit(AppState* state, void* lock) {
  st = state;
  mtx = (SemaphoreHandle_t)lock;
  for (int i = 0; i < 4; i++) snprintf(formToken + i * 8, 9, "%08lx", (unsigned long)esp_random());
  const char* headers[] = {"Origin"};
  server.collectHeaders(headers, 1);
  server.on("/", HTTP_GET, [] {
    if (!allowed()) return;
    bool ok = server.hasArg("ok");
    sendPage(ok ? TR("Tallennettu. Muutokset näkyvät laitteella heti.", "Saved. The changes show on the device right away.")
                : nullptr, false);
  });
  server.on("/save", HTTP_POST, handleSave);
  server.on("/restart", HTTP_POST, handleRestart);
  server.onNotFound([] { server.sendHeader("Location", "/"); server.send(302); });
}

void webLoop() {
  if (!st) return;
  if (!started) {
    if (WiFi.status() != WL_CONNECTED) return;
    server.begin();
    MDNS.begin("skytracker");                     // also reachable as http://skytracker.local
    MDNS.addService("http", "tcp", 80);
    started = true;
    Serial.printf("Settings page: http://%s/\n", WiFi.localIP().toString().c_str());
  }
  server.handleClient();
}
