// The settings page for a phone or computer on the same Wi-Fi (http://<address>/).
// The address is shown on the device under Asetukset.
#pragma once
#include "model.h"

void webInit(AppState* state, void* lock);
void webLoop();          // call often from the main loop; starts once Wi-Fi is up
