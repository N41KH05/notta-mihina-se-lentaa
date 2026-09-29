// Waveshare ESP32-S3-Touch-LCD-7: 800x480 RGB LCD, GT911 touch, CH422G I/O expander.
#pragma once
#include <stdint.h>
#include "app.h"   // TouchPt

bool boardInit();                  // I2C, expander, LCD (double-buffered), touch
uint16_t* boardBackBuffer();       // frame buffer to draw the next frame into
void boardPresent();               // show it (tear-free) and swap buffers
void boardBacklight(bool on);

int boardTouch(TouchPt* pts, int max);   // current touch points (0 = none)
