/**
 * miniz.h wrapper for LilyGo T5 S3 E-Paper Pro
 *
 * The epdiy library expects <miniz.h> on ESP-IDF 5.x, but the ESP32 ROM
 * already includes miniz with the proper flags to avoid namespace conflicts.
 * This wrapper redirects includes to the ROM version.
 */
#pragma once

#include <rom/miniz.h>
