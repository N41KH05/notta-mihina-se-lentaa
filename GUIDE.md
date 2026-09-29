# Build and setup guide

Everything about building, installing and using *Notta mihinä se lentää?* on the Waveshare ESP32-S3-Touch-LCD-7. The name shown on screen is set by `APP_NAME` in `config.h`.

A 7" touch-screen desk display with a live flight map around home. The screen is in **English** by default, or **Finnish** (switchable in Settings); the code and settings are in English. Planes glide across the map in real time, coloured by altitude. Tap a plane to see where it's flying from and to, its altitude, speed and aircraft type.

## What you need

- **Waveshare ESP32-S3-Touch-LCD-7**, the touch version (800×480)
- A **USB-C cable** and a phone charger (5 V, at least 1 A)
- Optionally a case or stand: a 3D-printable enclosure with fold-out legs is in the `case` folder (see `case/README.md`), or use any small tablet stand.

No soldering or wiring is required.

## Installing (Arduino IDE)

1. Install the **Arduino IDE 2**.
2. Go to **File → Preferences → Additional boards manager URLs** and add:
   `https://espressif.github.io/arduino-esp32/package_esp32_index.json`
3. In **Tools → Board → Boards Manager**, install **esp32 by Espressif Systems**, version **3.x**.
4. In **Tools → Manage Libraries**, install:
   - **Adafruit GFX Library** (say yes to its dependencies)
   - **ArduinoJson** (by Benoit Blanchon, version 7)
   - **JPEGDEC** (by Larry Bank), used to show plane photos
5. Open `SkyTracker/SkyTracker.ino`. You don't need to change anything in the code: Wi-Fi is chosen on the screen, and everything else is set on the phone settings page (see below). Values can also be pre-filled in `config.h`.
6. Choose these options in the **Tools** menu:
   - Board: **ESP32S3 Dev Module**
   - Flash Size: **16MB**
   - PSRAM: **OPI PSRAM** (the screen won't start without this)
   - Partition Scheme: **Custom** (this uses the `partitions.csv` in the sketch folder)
   - USB CDC On Boot: **Enabled**
7. Plug the board's **USB** port into your computer and press **Upload**. The first build takes a few minutes because the map is compiled in.

If the upload won't start, hold **BOOT**, tap **RESET**, and release **BOOT**.

## First start: choosing Wi-Fi on the screen

The first time it starts, it shows **Valitse Wi-Fi** (choose Wi-Fi):

1. Tap your network. Only 2.4 GHz networks work; most home routers offer one.
2. Type the password on the on-screen keyboard (it has å, ä and ö). **Näytä** shows what you typed, **?123** switches to numbers and symbols, and double-tapping **⇧** turns on caps lock.
3. Tap **Yhdistä** (connect).

The network is saved in the board's memory, so it reconnects by itself after power cuts. **Kokeile demokoneilla** skips the setup and shows simulated traffic. A network with a hidden name can be added with **Muu verkko** (other network).

### Then: where is home?

After connecting, it asks **Missä koti on?** (where is home?). Home is the centre of the map, the range rings and the "distance from home" figures.

1. Type a street address or a town, e.g. "Aleksanterinkatu 1, Helsinki" or just "Helsinki", and tap **Hae** (search).
2. Tap the right place in the list. The search uses OpenStreetMap's address search, Nominatim. Without internet it searches the towns built into the map instead.
3. The map opens with a red cross on the place that was found. Drag the map until the cross sits on your home. **+** zooms in for more precision.
4. Tap **TALLENNA** (save).

**Ohita** (skip) keeps the default (Helsinki city centre), and it won't ask again. Home can be changed any time:
- with **Aseta koti** (set home) at the top of **Asetukset**
- on the phone settings page, by typing the coordinates

## Using it

| Gesture | What it does |
|---|---|
| Drag the map | Move around |
| Pinch, or the **+ / −** buttons | Zoom from the whole world down to an airport |
| **House** button | Back to the home view |
| Tap a plane (on the map or in the list) | Show its details and route |
| **SEURAA** (follow) | Keep the selected plane in the centre |
| **SULJE** (close), or tap empty map | Back to the list |
| **Gear** (bottom left) | **Asetukset / Settings**: Wi-Fi, units, language, home (**Aseta koti / Set home**), demo planes, live-data status |

- Fresh positions arrive every 4 seconds. In between, planes glide along their heading at their speed.
- Plane colours show altitude: red/orange is low, green is mid, blue and purple are cruising height.
- After 5 minutes untouched it returns home. At night (23:00–07:00 unless changed on the settings page) the screen switches off, because this board can't dim. A tap wakes it, and it comes back on by itself in the morning.
- The live-data services send planes within 250 nm of one point. When you zoom out further than that, the map shows a dotted circle and a label.

## Departure and arrival times

Under the route (e.g. ARN → OUL), the panel shows when the flight left and when it gets in:

- **Saapuu n. 15:42**: an estimate the device works out itself from the plane's distance to the destination airport and its current ground speed. It works for every flight with a known route and needs no setup. It's usually a few minutes early, because planes slow down for landing.
- **Lähti 14:05 / Saapuu 15:40**: the timetable times, shown when an AirLabs key is set (below). If the flight is running behind the timetable, the delay is shown in minutes, e.g. **(+25)**, and turns orange at 15 minutes or more.

The departure time needs timetable data, so it only appears with an AirLabs key. To set one up:

1. Sign up for the free plan at [airlabs.co](https://airlabs.co). It allows 1000 requests a month for personal use.
2. Paste the API key on the phone settings page, under **Lähtö- ja saapumisajat**. There's no need to upload again.

The device only asks when you tap a plane. It asks once per flight, and again every 20 minutes if the plane stays selected. It stops at 30 requests a day (`AIRLABS_DAILY_MAX`), so the free plan lasts the month. Private, military and some charter flights aren't in the timetables; for those you get only the estimate. All times are Finnish time.

## Plane photos

When you tap a plane, a photo card appears in the top-left corner of the map. It takes about a sixth of the screen (288 × 214 px) and moves to the top-right corner if the selected plane would be underneath it. Tap the card to hide it. Selecting another plane brings it back.

- The photos come from [Planespotters.net](https://www.planespotters.net). It looks for a photo of that exact aircraft, first by its transponder code, then by its registration.
- The card shows **"Haetaan kuvaa…"** (loading) while the photo downloads. It shows **"Tästä koneesta ei ole kuvaa"** (no photo of this plane) if there is none.
- Planespotters' terms say the photographer must be credited and the photo page linked. The card shows **"Kuva: name"** and a QR code, and scanning the QR code with a phone opens the photo's page. Photos are only held in memory, never saved.
- Planespotters asks every app to identify itself with contact details. Enter an email address on the phone settings page under **Koneiden kuvat**.
- Photos can be switched off on the same page.
- Demo planes are simulated, so they have no photos on the device. The simulator shows a sample card.

## Settings page on a phone

Other settings are changed from a phone, tablet or computer on the same Wi-Fi. Open **Asetukset** (the gear) on the device: at the bottom it shows the address, e.g. `http://192.168.1.42`, and a QR code. Scan the code with the phone camera, or type the address into a browser. `http://skytracker.local` usually works too. The page follows the device language and has:

| Section | What can be changed |
|---|---|
| Tila (status) | Nothing; shows the Wi-Fi, where plane data comes from, how many planes, AirLabs requests today, and time since start |
| Kieli ja yksiköt (language and units) | The same language and unit choices as on the screen |
| Koti | Name of the home marker, and its latitude and longitude. Decimal comma or point both work. |
| Yötila | Whether the screen switches off at night, and the hours |
| Koneiden kuvat | Photos on or off, and the contact email for Planespotters |
| Lähtö- ja saapumisajat | The AirLabs key. The page shows only its last 4 characters. Leave the field empty to keep the current key. |

**Tallenna** (save) applies the changes to the device at once and saves them in its memory. If anything is wrong (for example a latitude that isn't a number), nothing is saved and the page says what to fix. **Käynnistä laite uudelleen** restarts the device.

Anyone on the same Wi-Fi can open the page. To require a password, set `WEB_PASSWORD` in `config.h` (user name `skytracker`). Restarting from the page also requires ticking **Vahvista uudelleenkäynnistys** (confirm restart). Home, night hours, photos and the key in `config.h` are only starting values; once something is saved on the page, the page's values are used.

## Security

- **Outgoing connections** all use HTTPS, and server certificates are verified against the ESP32's built-in list of trusted root certificates.
- **API keys** (AirLabs) are stored only in the device's flash. They are never shown in full on the settings page or written to the serial log.
- **Settings page:**
  - It answers only requests addressed to the device itself (its IP address or `skytracker.local`), which blocks DNS-rebinding attacks.
  - Every change must carry a random per-boot token from the page itself, so other websites cannot change settings or restart the device through a visitor's browser.
  - It sends a strict Content Security Policy, and it cannot be embedded in other pages.
- **Values from the flight data** (callsigns, registrations, hex codes) are only put into request URLs if they contain letters, digits and dashes alone.
- **Limitations:**
  - The settings page is plain HTTP on the local network. An optional password (`WEB_PASSWORD`) keeps other people on the same network out of it, but it is sent unencrypted.
  - The Wi-Fi password and settings are stored unencrypted in flash, so someone with physical access to the board can read them.

## Unattended operation

The device is designed for unattended, long-term operation:

- **After a power cut, or if the router restarts,** it keeps trying the saved network. It tries every 30 seconds at first, then every few minutes. Meanwhile the panel shows **"Ei Wi-Fi-yhteyttä"** (no Wi-Fi) and the network name. When the connection is back, planes appear again by themselves. The Wi-Fi setup screen only opens by itself when no network has been saved yet. The network can always be changed under Asetukset.
- **If the program ever hangs** (for example if a service stops answering in the middle of a request), a watchdog notices within 90 seconds and restarts the board. The same happens if Wi-Fi has been gone for 30 minutes, which fully resets the radio.
- **Once a day at about 04:00** it restarts quickly, but only if nobody is using it. This clears anything that might build up over weeks. At night the screen stays dark through the restart. It also restarts if memory ever runs low.
- **The phone settings page shows** how long it has been running (Uptime) and why it last started (Last start): power on, hung (watchdog), voltage dip, and so on. "Voltage dip" means the charger or cable is too weak; use a 5 V charger of at least 1 A.

## Units and saved settings

In **Asetukset** you can choose each unit separately:

| Setting | Options | Starts as |
|---|---|---|
| Etäisyys (distance, range rings, scale bar) | km or nm | km |
| Nopeus (speed) | km/h or kt | km/h |
| Korkeus (altitude and climb rate) | feet (flight levels, ft/min) or metres (m, m/s) | feet |
| Kieli / Language | English or Suomi | English |

The starting values are set in `config.h` (`DEFAULT_DISTANCE_KM`, `DEFAULT_SPEED_KMH`, `DEFAULT_ALTITUDE_M`, `DEFAULT_LANGUAGE`).

Everything chosen on the screen is saved in the board's flash memory and survives restarts and power cuts: the Wi-Fi network and password, the units and language, whether demo planes were chosen, and everything saved on the phone settings page. **Unohda verkko** (forget network) removes only the network; the units stay. Uploading a new version of the firmware keeps the saved settings too. To wipe them, set **Tools → Erase All Flash Before Sketch Upload → Enabled** for one upload.

## Screen language

The device speaks **Finnish** or **English**. Switch with **Kieli / Language** in Settings, the **In English / Suomeksi** button on the first Wi-Fi screen, or the language option on the phone settings page. The choice is saved in flash. `DEFAULT_LANGUAGE` in `config.h` sets the language of a freshly flashed device (1 = English, the default; 0 = Finnish).

Everything follows the language: menus, flight panel, compass directions (P, KO, I... or N, NE, E...), number formatting (12 000 ft and 4,9 km in Finnish; 12,000 ft and 4.9 km in English), the phone settings page and map labels. Both texts sit side by side in the code as `TR("suomeksi", "in English")`, mostly in `render.cpp`, `ui.cpp` and `web.cpp`; `lang.h` holds the switch.

In Finnish, the map uses Finnish names:

- **Places** use their Finnish names where one exists: Tukholma, Uumaja, Luulaja, Tromssa, Kirkkoniemi, Pietari, Viipuri, Petroskoi, Kantalahti, Moskova, Tallinna, Riika, Kööpenhamina, Maarianhamina, Lontoo and so on. Russian names use Finnish transliteration (Tšerepovets, Ržev). Other places keep their own spelling (Jyväskylä, Örnsköldsvik, Trondheim).
- **Countries** are labelled in Finnish (SUOMI, RUOTSI, NORJA, VENÄJÄ, VIRO, AHVENANMAA...).
- **Seas and lakes** are named: Merenkurkku, Perämeri, Selkämeri, Pohjanlahti, Suomenlahti, Itämeri, Vienanmeri, Laatokka, Saimaa, Päijänne and others.

In English, the map uses the English names from Natural Earth (Stockholm, Saint Petersburg, Gulf of Finland, SWEDEN...). Both are stored in the map data and the address search matches either.

The Finnish names and the English sea and lake names are in `tools/finnish_names.py`. To change one, edit it and run `tools/make_map.py` again (needs `pip install numpy pycountry`).

## Moving it somewhere else

The default home is Helsinki city centre. The device asks for your home after the first Wi-Fi setup (see above). Later you can change it with **Aseta koti** in Asetukset, on the phone settings page (**Koti**: latitude and longitude), or in `HOME_LAT` and `HOME_LON` in `config.h` before uploading. The detailed map layer (coastlines, lakes, towns, airports and runways within 1000 km) is generated around **Helsinki** by default. This covers nearly all of Finland, the Baltic states, central Sweden and north-western Russia. For a home outside that area, or to centre the detail on another region, rebuild the map around it, for example Oulu:

```
pip install numpy pycountry
python tools/make_map.py 65.0121 25.4651      # latitude longitude
```

This rewrites `SkyTracker/mapdata.cpp`. Upload again, and set the new home on the settings page (or in `HOME_LAT` and `HOME_LON` in `config.h`). Outside the detailed area, a simpler world map is used.

## Troubleshooting

Open **Tools → Serial Monitor** at 115200 baud to see what it's doing.

- **The settings page won't open.** The phone must be on the same Wi-Fi as the device, not on mobile data or a guest network. Use the address shown in Asetukset; it can change after the router restarts.
- **Black screen, and the Serial Monitor says "LCD init failed" or "No PSRAM".** Set PSRAM to **OPI PSRAM**.
- **The picture shakes or shifts sideways.** This can happen while Wi-Fi is busy. The code already uses the recommended fix (bounce buffers). If it persists, please open an issue with a description or photo.
- **Touch doesn't respond.** The Serial Monitor should show "Touch controller GT911 at 0x5D" (or 0x14). If it doesn't, press RESET once.
- **"Can't get flight data".** The panel lists each service and why it failed. "Wi-Fi down" or "no connection" is a network problem. "HTTP 403" means that service refused the request, and the next one is tried automatically.
- **"JPEGDEC.h: No such file".** Install the **JPEGDEC** library (step 4).
- **Photos never appear.** The Serial Monitor shows a "Photo for ..." line for each plane, and any request that failed. Check that the contact email is filled in on the settings page. Many small or military planes simply have no photo.
- **The build fails with "Sketch too big".** Set Partition Scheme to **Custom**.

## Credits

- Live positions: [adsb.fi](https://adsb.fi), [airplanes.live](https://airplanes.live) and [adsb.lol](https://adsb.lol), all free for personal, non-commercial use
- Routes: [adsbdb.com](https://www.adsbdb.com)
- Address search when setting home: [Nominatim](https://nominatim.org), © [OpenStreetMap](https://www.openstreetmap.org/copyright) contributors
- Timetables (optional): [AirLabs](https://airlabs.co)
- Plane photos: [Planespotters.net](https://www.planespotters.net) and its photographers (credited on screen)
- JPEG decoding: JPEGDEC by Larry Bank; QR codes: QRCode by Richard Moore (MIT)
- Map: [Natural Earth](https://www.naturalearthdata.com)
- Airports: [OurAirports](https://ourairports.com)
- Font: DejaVu Sans
- Board pins and timings: Waveshare's board definition in ESP32_Display_Panel

## Try it on your computer first

`simulator/SkyTracker Simulator.html` runs the firmware's own drawing, touch and data-parsing code in a web browser. Open it in Chrome, Edge, Firefox or Safari. Drag with the mouse to move the map, scroll to zoom, and click planes and buttons. On a touch screen, pinch works too.

The aircraft in the browser are simulated, like the board's demo mode. The public flight-data services don't accept requests directly from web pages. To show live traffic in the browser, deploy the optional proxy described in [`proxy/README.md`](proxy/README.md). With the proxy set, a **Live data / Simulated** switch appears, and the page fetches the same services as the board (positions, routes, photos and address search) through it.

To rebuild it after changing the code, run:

```
pip install ziglang
python simulator/build.py <path to Adafruit_GFX_Library> <path to ArduinoJson/src>
```

Without arguments, the script looks for both libraries in `~/Documents/Arduino/libraries`.
