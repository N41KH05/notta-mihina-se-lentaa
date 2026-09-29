// The photo card shown over the map for the selected plane (Planespotters.net).
#pragma once
#include <stdint.h>

enum PhotoState : uint8_t { PHOTO_NONE, PHOTO_LOADING, PHOTO_READY, PHOTO_MISSING };
static const int PHOTO_W = 272, PHOTO_H = 122;         // picture size inside the card

struct PhotoCard {
  char hex[8];             // which plane this is for
  char reg[12];
  PhotoState state;
  bool hidden;             // tapped away by the user
  uint16_t* pix;           // PHOTO_W x PHOTO_H pixels, RGB565 (kept in memory only)
  char photographer[48];
  char link[160];          // the photo's page (Planespotters asks for a link: shown as a QR code)
};
inline PhotoCard photo;
