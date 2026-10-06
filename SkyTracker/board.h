// Waveshare ESP32-S3-Touch-LCD-7: 800x480 RGB LCD, GT911 touch, CH422G I/O expander.
#pragma once
#include <stdint.h>
#include "ui.h"    // TouchPt

bool boardInit();                  // I2C, expander, LCD (double-buffered), touch
uint16_t* boardBackBuffer();       // frame buffer to draw the next frame into
const uint16_t* boardFrontBuffer();  // the frame on screen now
void boardPresent();               // show it (tear-free) and swap buffers
void boardBacklight(bool on);
// Restart with the screen held in reset: deep sleep for a second, then a full start-up.
// Needed after flash writes (updates): a plain restart could leave the picture streaked.
[[noreturn]] void boardHardRestart();

int boardTouch(TouchPt* pts, int max);
// Mount the micro SD card (SPI on GPIO 11-13; its chip select is on the I/O expander).
// Call once from setup(), before the touch task starts. False: no card, or unreadable.
bool boardSdBegin();   // current touch points (0 = none)
