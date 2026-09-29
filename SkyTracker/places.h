// Finding a place by name, for setting the home position.
#pragma once
#include "model.h"

// Search the towns built into the map (works without internet). Names match from
// the start of the name or of any word, ignoring case. Returns how many were found.
int placeSearchOffline(const char* query, Place* out, int max);
