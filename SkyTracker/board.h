// Waveshare ESP32-S3-Touch-LCD-7: 800x480 RGB LCD, GT911 touch, CH422G I/O expander.
#pragma once
#include <stdint.h>
#include "ui.h"    // TouchPt

bool boardInit();                  // I2C, expander, LCD (double-buffered), touch
uint16_t* boardBackBuffer();       // frame buffer to draw the next frame into
const uint16_t* boardFrontBuffer();  // the frame on screen now
void boardPresent();               // show it (tear-free) and swap buffers
void boardBacklight(bool on);
// Backlight off and the panel held in reset (the expander keeps it so through a restart).
// Works before boardInit() too.
void boardPanelOff();
// Deep sleep for ms, then a full start-up with the panel reset: the closest the firmware
// gets to a power cut. Note: waking from deep sleep starts the build that was running
// (the bootloader skips its normal choice), so this can't be used to start a new build.
[[noreturn]] void boardDeepRestart(uint32_t ms);

int boardTouch(TouchPt* pts, int max);
// Mount the micro SD card (SPI on GPIO 11-13; its chip select is on the I/O expander).
// Call once from setup(), before the touch task starts. False: no card, or unreadable.
bool boardSdBegin();   // current touch points (0 = none)
