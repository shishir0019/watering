#pragma once

#include <stdbool.h>

// Configure all relay pins with every pump OFF. Call first in app_main.
void relay_init(void);

// pump: 0-based index
void relay_set(int pump, bool on);
