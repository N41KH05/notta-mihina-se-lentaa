// The spotter's logbook: every aircraft that passes within cfg.logKm of home.
//
// A pass starts when a plane is first reported inside the circle and ends once it hasn't
// been seen there for 10 minutes. It is then counted (today, all time, per aircraft and
// per type) and queued for the SD card, where SkyTracker.ino appends it to a monthly CSV
// file (/skytracker/2026-10.csv) that opens in a spreadsheet. At start-up the files are
// read back to rebuild the counts, so nothing is lost by a restart.
//
// Shared by the firmware and the simulator. Call everything with the state locked.
#pragma once
#include <stddef.h>
#include <stdint.h>
#include "model.h"

// One pass: where and when the plane came closest to home.
struct LogPass {
  uint32_t when;             // Unix time of the closest point
  uint32_t hex;              // ICAO address (24 bits)
  char cs[10], reg[12], type[6];
  float km;                  // closest distance from home
  int32_t altFt;             // altitude there (-1 = not known)
  bool first;                // the first pass of this aircraft in the log
  const char* label(char* buf, size_t n) const;   // callsign, registration or hex
};

struct LogStats {
  bool ready;                // the history has been read back (or there was none to read)
  bool saving;               // passes are written to the SD card; false: kept until restart
  uint32_t now;              // time of the latest traffic report seen
  uint32_t since;            // first pass in the log (0 = none yet)
  // Today (local time)
  int today, todayNew, open; // passes, first-time aircraft among them, planes inside the circle now
  uint16_t hours[24];        // passes per hour
  LogPass closest;           // closest pass today (km < 0: none yet)
  char rareType[6];          // today's rarest aircraft type...
  uint32_t rareCount;        // ...and how many passes it has had in all
  // All time
  uint32_t passes, aircraft, types;
  char topType[3][6];
  uint32_t topTypeN[3];
  LogPass regular;           // the aircraft seen most often
  uint32_t regularN;
  LogPass closestEver;
  LogPass recentNew[4];      // the latest first-time visitors, newest first
  int nRecentNew;
};

void logbookInit();
// A fresh traffic report covering a circle of radiusKm around (lat, lon). Ignored unless
// the circle holds the whole logbook circle (the map may be looking somewhere else).
void logbookObserve(const Plane* planes, int n, double lat, double lon, double radiusKm, uint32_t epoch);
// How far from (lat, lon) a traffic report has to reach to cover the logbook circle.
double logbookReachKm(double lat, double lon);
// On the map: is this plane inside the circle on its first visit ever? (Only once the log
// is a day old, or everything would be new.)
bool logbookIsNew(const char* hex);
int logbookSeen(const char* hex);                 // finished passes of this aircraft so far
const LogStats& logbookStats();                   // (the top lists are worked out here)

// Saving: passes waiting for the card, oldest first. Copy some, write them, then pop them.
int logbookPending(LogPass* out, int max);
void logbookPop(int n);
// The CSV file a pass belongs in ("/skytracker/2026-10.csv"), its header line (in the
// current language, which also sets the separator: ';' and decimal comma in Finnish, as
// a Finnish spreadsheet expects) and the pass as a line in a file using separator sep.
void logbookPath(const LogPass& p, char* out, size_t n);
void logbookHeader(char* out, size_t n);
void logbookLine(const LogPass& p, char sep, char* out, size_t n);

// Start-up: once the clock is set, say what time it is (so today's passes can be told
// apart), feed back every line of every file (oldest file first), then say it's done.
void logbookSetClock(uint32_t epoch);
void logbookReplay(const char* line);
void logbookReplayDone(bool saving);

// The simulator: a few weeks of made-up history, some of it for the demo planes.
void logbookDemoSeed(const AppState& s, uint32_t epoch);
