#pragma once

#include <stdbool.h>

// Station mode with automatic reconnect. Never blocks; failures are logged only.
void wifi_start(void);
bool wifi_connected(void);
int wifi_rssi(void);   // dBm, 0 if not connected
