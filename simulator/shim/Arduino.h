// Tiny Arduino stand-in so Adafruit_GFX and the renderer build on a PC.
#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include <string>
#define PROGMEM
#define pgm_read_byte(a) (*(const unsigned char*)(a))
#define pgm_read_word(a) (*(const unsigned short*)(a))
#define pgm_read_dword(a) (*(const unsigned long*)(a))
typedef bool boolean;
#define radians(d) ((d) * 0.017453292519943295)
#define degrees(r) ((r) * 57.29577951308232)
class __FlashStringHelper;
class String {
 public:
  std::string s;
  String(const char* c = "") : s(c) {}
  const char* c_str() const { return s.c_str(); }
  unsigned int length() const { return s.size(); }
};
class Print {
 public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t* b, size_t n) { size_t k = 0; while (n--) k += write(*b++); return k; }
  size_t write(const char* s) { return write((const uint8_t*)s, strlen(s)); }
  size_t print(const char* s) { return write(s); }
  size_t print(const String& s) { return write(s.c_str()); }
};
