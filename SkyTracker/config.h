// ============================================================================
//  SkyTracker (Waveshare ESP32-S3-Touch-LCD-7 edition) - settings.
//  This is the only file you need to edit.
// ============================================================================
#pragma once

// ---- Wi-Fi (2.4 GHz only: the ESP32 can't see 5 GHz networks) ---------------
#define WIFI_SSID      "YourWiFiName"
#define WIFI_PASSWORD  "YourWiFiPassword"

// Screen language at first start: 0 = Finnish, 1 = English (can be changed in Settings).
#define DEFAULT_LANGUAGE  1

// Name shown on the screen and on the phone settings page.
#define APP_NAME  "Notta mihinä se lentää?"

// ---- Settings page -------------------------------------------------------------
// Home, night hours, photos and the AirLabs key below are starting values: they can be
// changed later on the phone settings page (the address is shown in Asetukset), which
// saves them in the board's memory. After that, the values here are not used.

// Anyone on the home Wi-Fi can open the page. To ask for a password first, set one
// here (the user name is "skytracker").
#define WEB_PASSWORD  ""

// ---- Home ---------------------------------------------------------------------
// The detailed map is built around home by tools/make_map.py (Helsinki by default).
// If you move the device far away, re-run that script with the new coordinates.
#define HOME_LAT   60.1699      // Helsinki city centre: set your own home on the device
#define HOME_LON   24.9384
#define HOME_NAME  ""          // label of the home marker (up to 23 bytes); empty = "Home" / "Koti"

// Time zone as a POSIX string (this one is Finland).
#define TZ_INFO    "EET-2EEST,M3.5.0/3,M10.5.0/4"
#define TIME_24H   true

// ---- Map view -----------------------------------------------------------------
#define START_ZOOM  8     // 2 = whole world, 5 = region, 8 = city area, 11 = airport
#define MIN_ZOOM    2
#define MAX_ZOOM    11
#define RETURN_HOME_AFTER_MIN  5   // back to the home view after this long untouched
// Circles around home, in the chosen distance unit (km or nm). Up to 8 values.
#define RANGE_RINGS  {5, 10, 25, 50, 100, 200}

// ---- Units --------------------------------------------------------------------
// Starting units. They can be changed on the screen (gear > Asetukset) and the
// choice is remembered after a restart.
#define DEFAULT_DISTANCE_KM   true    // true: km             false: nautical miles
#define DEFAULT_SPEED_KMH     true    // true: km/h           false: knots
#define DEFAULT_ALTITUDE_M    false   // true: metres         false: feet (flight levels)

// ---- Live data ------------------------------------------------------------------
// Free services (personal, non-commercial use). Tried in order; the first that
// answers is used until it stops answering.
#define API_SOURCES { \
  {"adsb.fi",        "https://opendata.adsb.fi/api/v3/lat/%.4f/lon/%.4f/dist/%d"}, \
  {"airplanes.live", "https://api.airplanes.live/v2/point/%.4f/%.4f/%d"}, \
  {"adsb.lol",       "https://api.adsb.lol/v2/lat/%.4f/lon/%.4f/dist/%d"}, \
}
#define POLL_SECONDS    4          // fresh positions this often (services allow 1/s)
#define POLL_SECONDS_BUSY 8        // ...but only this often with hundreds of planes in view:
#define BUSY_PLANES     250        //   big downloads; the planes keep gliding in between
#define FRAME_MS        200        // planes glide between fetches: redraw this often
#define FRAME_MS_TOUCH  50         // while a finger moves the map: at most 20 frames a second
// The screen has no memory of its own: the ESP32 sends it the whole picture ~30 times a
// second from PSRAM. Lower this (e.g. 10) if lines or a sideways shift still appear.
#define LCD_PCLK_MHZ    12
#define HIDE_ON_GROUND  true

// ---- Plane photos (Planespotters.net) -----------------------------------------------
// A photo of the selected plane appears in a card over the map. Planespotters asks
// apps to identify themselves with a contact address; put your e-mail or a web page here.
#define SHOW_PHOTOS     true
#define PHOTO_CONTACT   "your-email@example.com"

// ---- Departure and arrival times (AirLabs, optional) ---------------------------------
// Without a key the panel shows an estimated arrival time worked out from the plane's
// distance to its destination and its ground speed. With a free AirLabs key
// (https://airlabs.co, free plan: 1000 requests a month, personal use) it also shows
// the departure time and the airline's own arrival estimate. One request is made per
// selected flight (refreshed every TIMES_REFRESH_MIN minutes while it stays selected),
// and at most AIRLABS_DAILY_MAX a day so the free plan lasts the whole month.
#define AIRLABS_KEY         ""
#define AIRLABS_DAILY_MAX   30
#define TIMES_REFRESH_MIN   20

#define TRAIL_POINTS    40         // breadcrumbs per plane (40 x 4 s = under 3 min)
#define MAX_PLANES      400

// ---- Firmware updates ---------------------------------------------------------------
// GitHub Actions builds the firmware on every push and publishes it as a release (see
// .github/workflows/firmware.yml). The device checks every few hours and, like a phone,
// asks before installing: install now, later, or skip that version. If a new build doesn't
// start properly, the board goes back to the old one by itself. FW_BUILD is set by that
// build; a build from the Arduino IDE is 0.
#define UPDATE_REPO        "N41KH05/notta-mihina-se-lentaa"   // owner/repository on GitHub
#define UPDATE_CHECK_HOURS 3
#define UPDATE_REMIND_HOURS 24        // "Later" asks again after this long
#ifndef FW_BUILD
#define FW_BUILD           0
#endif

// ---- Screen -----------------------------------------------------------------------
// The backlight on this board can only be switched on or off (no dimming), so at
// night the screen turns off. A tap wakes it for a while.
#define NIGHT_START_HOUR  23       // set both the same to never switch off
#define NIGHT_END_HOUR    7
// Dark colours: 0 = never, 1 = always, 2 = from sunset to sunrise at home.
#define DEFAULT_DARK_MODE 0
