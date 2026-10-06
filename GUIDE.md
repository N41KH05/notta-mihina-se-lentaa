# Guide

How to build, set up and use the display. Most settings can be changed on the device or from a phone; the rest are in `SkyTracker/config.h`.

## What you need

- Waveshare ESP32-S3-Touch-LCD-7, the touch version (800×480)
- A USB-C cable and a phone charger (5 V, at least 1 A)
- Something to stand it in. There's a printable case in `case/`, or any small tablet stand works.

## Installing

1. Install the Arduino IDE 2.
2. Under File → Preferences → Additional boards manager URLs, add
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In Tools → Board → Boards Manager, install "esp32 by Espressif Systems", version 3.x.
4. In Tools → Manage Libraries, install:
   - Adafruit GFX Library (accept its dependencies)
   - ArduinoJson by Benoit Blanchon, version 7
5. Open `SkyTracker/SkyTracker.ino`. You don't have to edit anything: Wi-Fi and home are set on the screen. If you'd rather pre-fill them, they're in `config.h`.
6. In the Tools menu:
   - Board: ESP32S3 Dev Module
   - Flash Size: 16MB
   - PSRAM: OPI PSRAM (the screen won't start without it)
   - Partition Scheme: Custom (uses `partitions.csv` from the sketch folder)
   - USB CDC On Boot: Enabled
7. Connect the port marked USB and upload. The first build takes a few minutes because the map data is large.

If the upload doesn't start, hold BOOT, tap RESET, then let go of BOOT.

## First start

The first screen is **Choose Wi-Fi**. Tap your network (2.4 GHz only), type the password and tap **Connect**. On the keyboard, **Show** reveals the password, **?123** switches to numbers and symbols, and a double tap on ⇧ is caps lock. For a hidden network use **Other network**. **Try with demo aircraft** skips all this and shows simulated traffic. The **Suomeksi** button at the top switches the screen to Finnish.

Once it's connected it asks **Where is home?** Home is the centre of the map and the point distances are measured from.

1. Type an address or a town, e.g. "Aleksanterinkatu 1, Helsinki", and tap **Search**. The search uses OpenStreetMap's Nominatim. Without internet it only finds the towns built into the map.
2. Pick the right result.
3. The map opens with a red cross on the result. Drag the map until the cross is on your house (zoom in with + if you want it exact), then tap **SAVE**.

**Skip** leaves home in central Helsinki. You can set it later with **Set home** in Settings, or type coordinates on the phone settings page.

## Using it

- Drag to move the map, pinch or use + and − to zoom. The house button goes back to home.
- Tap a plane, on the map or in the list, to see its details. **FOLLOW** keeps it centred; **CLOSE** or a tap on empty map goes back to the list.
- The line under the flight number names the airline. For planes without one, such as private planes and flying clubs, it shows the aircraft's registered owner instead. When an airliner belongs to someone other than the airline flying it (usually a leasing company), the owner is shown under the registration. Both come from adsbdb.
- The selected plane's path since take-off is drawn on the map, coloured by altitude, and keeps growing as it flies. It comes from adsb.lol's track history. If that isn't available, a dashed line from the departure airport is shown instead.
- The gear in the bottom left opens Settings: Wi-Fi, units, language, dark mode, home, demo mode and the data status.
- Colours show altitude: red and orange are low, green is in between, blue and purple are at cruising height. The icon shows the kind of aircraft (airliner, wide-body, business jet, propeller plane, light aircraft, helicopter, glider), worked out from its type code or, failing that, the size class its transponder sends. Bigger aircraft get bigger icons.
- After 5 minutes without a touch it goes back to the home view.
- Dark mode swaps the colours for amber and green on black. In Settings, **Off** keeps the light colours, **On** keeps the dark ones, and **Auto** switches at sunset and back at sunrise at your home location (it needs the clock, so it stays light until Wi-Fi has set the time).
- At night (23–07 unless you change it) the screen turns off, since the backlight can't be dimmed. A tap wakes it.
- The position services return planes within 250 nm of a point. Zoom out further and you'll see a dotted circle marking that limit.

## Departure and arrival times

Below the route the panel shows something like **Arr ~15:42**. That's the device's own estimate from the distance to the destination and the current ground speed. It needs no setup, and it's usually a few minutes early because planes slow down before landing.

With an AirLabs key you get the timetable instead: **Dep 14:05** and **Arr 15:40**, plus the delay in minutes when a flight is late, e.g. (+25), in orange from 15 minutes up. To set it up, sign up for the free plan at [airlabs.co](https://airlabs.co) (1000 requests a month) and paste the key on the phone settings page.

The device only asks AirLabs about the plane you've tapped: once per flight, again every 20 minutes if it stays selected, and at most 30 times a day (`AIRLABS_DAILY_MAX`), which keeps it inside the free plan. Private, military and some charter flights aren't in the timetables, so for those you only get the estimate. Times are shown in the time zone set by `TZ_INFO` in `config.h` (Finnish time by default).

## Photos

When you tap a plane, a photo card appears in a top corner of the map. Tap the card to hide it.

Photos come from [Planespotters.net](https://www.planespotters.net), looked up by the plane's transponder code and then by registration. Their terms require crediting the photographer and linking the photo page, so the card shows the photographer's name and a QR code for the page. Nothing is saved on the device. Planespotters also asks apps to give a contact address; put your email on the phone settings page. Photos can be turned off on the same page.

## Settings page on your phone

Open Settings on the device. At the bottom it shows an address like `http://192.168.1.42` and a QR code; open that on a phone on the same Wi-Fi. `http://skytracker.local` often works too. The page is in the same language as the device and has:

- the device status: Wi-Fi, data source, number of planes, AirLabs requests today, uptime and the reason for the last restart
- language and units
- home: the label on the map and its coordinates (a decimal comma or point both work)
- dark mode (off, on, or after sunset), and turning the screen off at night with its hours
- photos on or off, and the contact email for Planespotters
- the AirLabs key (only the last 4 characters are shown; leave the field empty to keep it)

**Save** applies everything right away. If something doesn't make sense, like a latitude that isn't a number, nothing is saved and the page tells you what to fix. There's also a restart button, which needs a confirmation tick.

Anyone on your Wi-Fi can open the page. If that bothers you, set `WEB_PASSWORD` in `config.h`; the user name is `skytracker`. The home, night hours, photo and key values in `config.h` are only defaults. Once you've saved something on the page, the saved values win.

## Security notes

- All outgoing requests use HTTPS with certificate checks against the ESP32's built-in root certificates.
- The AirLabs key stays on the device. It's never shown in full on the page or printed to the serial log.
- The settings page only answers requests addressed to the device's own IP or `skytracker.local`, and every change has to include a token from the page itself. That stops other websites from changing settings through your browser.
- Callsigns and registrations from the flight data are only put into URLs if they're plain letters, digits and dashes.
- The page itself is plain HTTP, so the optional password is sent unencrypted on your network. The Wi-Fi password and settings are stored unencrypted in flash, so anyone holding the board can read them.

## Leaving it running

- After a power cut or a router restart it keeps retrying the saved network, every 30 seconds at first and then every few minutes. The panel says "No Wi-Fi connection" meanwhile. The Wi-Fi setup screen only opens on its own if no network has been saved.
- If the program hangs, for example on a request that never finishes, a watchdog restarts the board within 90 seconds. It also restarts after 30 minutes without Wi-Fi, which fully resets the radio.
- Around 04:00 it restarts itself if nobody is using it, and keeps the screen dark while doing so. It also restarts if memory gets low.
- The phone page shows the uptime and why it last started. "Voltage dip" means the charger or cable is too weak.

## Units and saved settings

Distance (km or nm), speed (km/h or kt), altitude (feet or metres) language (English or Suomi) and dark mode are switched in Settings. The defaults are in `config.h`: `DEFAULT_DISTANCE_KM`, `DEFAULT_SPEED_KMH`, `DEFAULT_ALTITUDE_M`, `DEFAULT_LANGUAGE` and `DEFAULT_DARK_MODE`.

Both colour sets are in `render.cpp` (`THEME_LIGHT` and `THEME_DARK`) if you want to change them.

Everything you set is saved in flash and survives restarts, power cuts and firmware updates. **Forget network** only removes the Wi-Fi network. To wipe everything, enable Tools → Erase All Flash Before Sketch Upload for one upload.

## Language

The device can run in English or Finnish. You can switch in Settings, with the button on the first Wi-Fi screen, or on the phone page. `DEFAULT_LANGUAGE` in `config.h` picks the language for a freshly flashed board (1 = English, 0 = Finnish).

The language also changes the number format (12,000 ft and 4.9 km vs. 12 000 ft and 4,9 km), compass letters and the map labels. In Finnish, places use their Finnish names where there is one (Tukholma, Pietari, Tallinna, Kööpenhamina...), and seas and lakes are labelled too (Suomenlahti, Pohjanlahti, Laatokka, Saimaa...). In English the map uses Natural Earth's English names. Address search matches either.

In the code, each text is written as `TR("suomeksi", "in English")`, mostly in `render.cpp`, `ui.cpp` and `web.cpp`. The Finnish place names and the English sea and lake names are in `tools/finnish_names.py`; after editing them, rerun `tools/make_map.py` (needs `pip install numpy pycountry`).

## Using it somewhere else

The detailed map (coastlines, lakes, towns, airports and runways within 1000 km) is generated around Helsinki. That covers most of Finland, the Baltics, central Sweden and north-western Russia. Outside it you get a simpler world map. To centre the detail somewhere else, for example Oulu:

```
pip install numpy pycountry
python tools/make_map.py 65.0121 25.4651
```

This rewrites `SkyTracker/mapdata.cpp`. Upload again and set your home on the device.

## Troubleshooting

Open Tools → Serial Monitor at 115200 baud (on the USB port) to see what it's doing.

- **Settings page won't open.** The phone has to be on the same Wi-Fi, not mobile data or a guest network. The address can change after a router restart; check it in Settings.
- **Black screen, and "LCD init failed" or "No PSRAM" in the log.** PSRAM must be set to OPI PSRAM.
- **The picture jitters, flickers or shifts sideways**, especially while dragging or zooming. The screen has no memory of its own, so the ESP32 streams the picture to it continuously from PSRAM, and heavy drawing can make that stream late. Lower `LCD_PCLK_MHZ` in `config.h` (14 by default; try 12) and upload again. If it still happens, please open an issue.
- **Touch doesn't work.** The log should show "Touch controller GT911 at 0x5D" (or 0x14). If not, press RESET.
- **"No flight data".** The panel lists each service and what went wrong. "Wi-Fi not connected" or "no connection to server" is your network; "HTTP 403" means that service refused, and the next one is tried.
- **No photos.** The log has a "Photo for ..." line per plane. Check that the contact email is set. Many small and military planes just don't have a photo.
- **"Sketch too big".** The partition scheme must be Custom.

## Browser simulator

The simulator (`docs/simulator.html`, with its `.css`, `.js` and the firmware build `sim.wasm`) runs the firmware's drawing, touch and parsing code in a browser. It's on the project site; to run it locally, serve the folder with `python -m http.server -d docs` and open `http://localhost:8000/simulator.html` (browsers won't load the `.wasm` from a file opened straight from disk). Drag with the mouse, scroll to zoom, click planes. The traffic is simulated, like the board's demo mode, and it starts in dark mode (Settings switches it).

To rebuild it after changing the code:

```
pip install ziglang
python simulator/build.py <path to Adafruit_GFX_Library>   # writes docs/sim.wasm
```

Without an argument it looks in `~/Documents/Arduino/libraries`.

## Credits

Positions from adsb.fi, airplanes.live and adsb.lol; flight paths from adsb.lol; routes and aircraft owners from adsbdb; address search from Nominatim (© OpenStreetMap contributors); timetables from AirLabs; photos from Planespotters.net and its photographers; map from Natural Earth; airports from OurAirports. stb_image by Sean Barrett, QRCode by Richard Moore, DejaVu Sans font. The board's pins and timings follow Waveshare's definition in ESP32_Display_Panel.
