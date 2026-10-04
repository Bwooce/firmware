#pragma once

#include <Arduino.h>

// Functions that are unique to particular target types (esp32, bare, nrf52 etc...)

// Enable/disable bluetooth.
void setBluetoothEnable(bool enable);

// Switch BLE connection to ultra-low-power params when idle (screen off).
// idle=true: 100-500ms interval, ~2s effective. idle=false: restore normal params.
void setBluetoothIdleMode(bool idle);

void getMacAddr(uint8_t *dmac);