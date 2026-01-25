# Meshtastic PR Plan: LilyGo T5 S3 E-Paper Pro Support

## Status: TESTING

## Commit Plan

### Commit 1: Add EInkDisplayEPDiy driver for parallel e-paper displays
**Files:**
- `src/graphics/EInkDisplayEPDiy.h` (new)
- `src/graphics/EInkDisplayEPDiy.cpp` (new)

**Description:** New display driver using epdiy library for parallel-interface e-paper displays (ED047TC1, etc). Based on OLEDDisplay like other e-ink drivers.

---

### Commit 2: Integrate EInkDisplayEPDiy into Screen class
**Files:**
- `src/graphics/Screen.h` (modified)
- `src/graphics/Screen.cpp` (modified)
- `src/graphics/EInkDisplay2.h` (modified)
- `src/graphics/EInkDisplay2.cpp` (modified)

**Description:** Wire up USE_EINK_EPDIY display type selection. EInkDisplay2 now excludes itself when USE_EINK_EPDIY is defined to avoid conflicts.

---

### Commit 3: Add LilyGo T5 S3 E-Paper Pro board support
**Files:**
- `boards/lilygo-t5-s3-epaper-pro.json` (new)
- `variants/esp32s3/lilygo_t5_s3_epaper_pro/variant.h` (new)
- `variants/esp32s3/lilygo_t5_s3_epaper_pro/pins_arduino.h` (new)
- `variants/esp32s3/lilygo_t5_s3_epaper_pro/platformio.ini` (new)
- `variants/esp32s3/lilygo_t5_s3_epaper_pro/miniz.h` (new)

**Description:** Full board support for LilyGo T5 S3 E-Paper Pro with 4.7" ED047TC1 display, SX1262 LoRa, L76K GPS, BQ27220 fuel gauge, and PCF8563 RTC. Uses epdiy pinned to ba7a419 (last version with ESP-IDF 4.x support).

---

## epdiy Dependency Note

epdiy is pinned to commit `ba7a4190102ebc430f4002009f3787ad5cbb321e` (Jan 26, 2025).

**Why:** Upstream vroland/epdiy dropped ESP-IDF 4.x support in commit `39fdfc4` (Mar 8, 2025). Arduino ESP32 2.x uses ESP-IDF 4.x, so we must use the older version.

**Future:** When Arduino ESP32 3.x releases (with ESP-IDF 5.x), update to latest epdiy.

---

## PR Description Template

```markdown
## Summary
- Adds support for LilyGo T5 S3 E-Paper Pro (4.7" parallel e-paper + LoRa)
- New EInkDisplayEPDiy driver for parallel-interface displays using epdiy library
- epdiy pinned to ba7a419 (last commit with ESP-IDF 4.x support)
- Can upgrade to latest epdiy when Arduino ESP32 3.x brings ESP-IDF 5.x

## Hardware Features
- Display: 4.7" ED047TC1 (960x540, 16-level grayscale) via epdiy
- Radio: SX1262 with TCXO 2.4V, DIO2 RF switch
- GPS: L76K on UART2
- Power: BQ25896 charger + BQ27220 fuel gauge
- RTC: PCF8563

## Test Plan
- [ ] Firmware builds successfully
- [ ] Device boots without crash
- [ ] Display shows Meshtastic UI
- [ ] LoRa transmits/receives
- [ ] GPS provides position
- [ ] Battery percentage reads correctly (with battery installed)
- [ ] Touch input works (GT911)
```

---

## Testing Checklist

- [ ] Build succeeds
- [ ] Upload to device
- [ ] Device boots (no crash/reboot loop)
- [ ] Display initializes and shows UI
- [ ] Display updates properly (no artifacts, correct orientation)
- [ ] LoRa radio works (check with second node or radio tools)
- [ ] GPS acquires fix
- [ ] Battery reading works
- [ ] Touch input functional
- [ ] Bluetooth pairing works
