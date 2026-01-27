#ifndef _VARIANT_LILYGO_T5_S3_EPAPER_PRO_H_
#define _VARIANT_LILYGO_T5_S3_EPAPER_PRO_H_

// ===========================================================================
// LilyGo T5 S3 E-Paper Pro (v1.0) - Hardware Reference
// https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO
// ===========================================================================
//
// MCU: ESP32-S3 (dual-core LX7, 240 MHz)
// Flash: 16MB / PSRAM: 8MB OPI
// Display: ED047TC1 4.7" e-paper (960x540, 16-level grayscale)
// Radio: SX1262 LoRa (sub-GHz)
// GPS: L76K or MIA-M10Q (UART2)
// Touch: GT911 capacitive (5-point multi-touch)
// Power: BQ25896 charger + BQ27220 fuel gauge
// RTC: PCF8563
// IO Expander: PCA9535 (16-pin, I2C address 0x20)
//
// ===========================================================================
// Physical Buttons (4x side-mounted)
// ===========================================================================
//
// PWR  - BQ25896 QON pin. Hardware power on/off.
//        Toggles BATFET to connect/disconnect battery.
//        Also readable via PCA9535 P12 (software detection via GPIO 38 interrupt).
//        When device is powered off (BATFET disconnected), pressing PWR reconnects
//        battery power. Only works when on battery (not USB).
//        Wake sources: PWR button press or USB cable insertion.
//
// BOOT - GPIO 0. ESP32-S3 strapping pin.
//        Active LOW with external pullup.
//        Hold during reset to enter USB bootloader mode.
//        Used by Meshtastic as BUTTON_PIN (user button):
//          Single press = navigate UI
//          Long press (500ms) = select
//          Very long press (3.9s) = shutdown
//        Also configured as ext1 deep sleep wake source (ANY_LOW).
//
// RST  - ESP32-S3 EN (enable/reset) pin.
//        Hardware reset, not software controllable.
//        Directly resets the ESP32-S3 when pulled LOW.
//
// IO48 - GPIO 48. Physical side button.
//        Directly connected to ESP32-S3 GPIO 48.
//        Also used as CKV (Clock Vertical) by the epdiy e-paper driver.
//        CKV clocks the row shift register during display refresh, pulsing
//        at up to 200kHz for ~500ms per refresh cycle. Between refreshes
//        the pin is idle. LilyGo exposes this as a button, suggesting
//        hardware isolation or intended use during idle periods.
//        Currently not configured in Meshtastic - needs hardware testing
//        to confirm safe dual-use with epdiy before enabling.
//
// ===========================================================================
// Touch Controller - GT911 Capacitive
// ===========================================================================
//
// I2C address: 0x5D
// INT: GPIO 3 (active LOW, directly connected)
// RST: GPIO 9 (directly connected)
//
// Capabilities:
//   - 5-point simultaneous multi-touch
//   - Hardware gesture registers (0x8071-0x8078) - not exposed by driver
//   - Software gesture detection in Meshtastic: swipe L/R/U/D, tap, long press
//   - Virtual home button (see below)
//   - Configurable refresh rate and sensitivity
//
// Current Meshtastic mapping (TouchScreenImpl1):
//   Swipe left/right = INPUT_BROKER LEFT/RIGHT (screen navigation)
//   Swipe up/down    = INPUT_BROKER UP/DOWN
//   Tap              = INPUT_BROKER USER_PRESS (28)
//   Long press       = INPUT_BROKER SELECT (10)
//
// ===========================================================================
// GT911 Home Button ("menu_btn" in LilyGo factory firmware)
// ===========================================================================
//
// Virtual touch button at bottom-center of display.
// Not a physical button - detected by GT911 via bit 0x10 in status register
// (GT911_POINT_INFO at 0x814E).
//
// The GT911 supports up to 4 independent KEY zones, but only one is used
// on this device. The zone position is factory-configured in the GT911's
// internal firmware blob (not written at runtime). When ANY configured key
// zone is pressed, the GT911 sets bit 0x10 in the status register. The
// driver does not distinguish which of the 4 zones was activated.
//
// KEY registers (factory-configured, can be modified at runtime if needed):
//   0x8093-0x8096 = KEY_1 through KEY_4 position config
//   0x8097 = KEY_AREA size (0-7, higher = larger)
//   0x8098-0x8099 = Touch/release thresholds
//   0x809A-0x809B = Sensitivity for keys 1-4
//   0x809C-0x809D = Restraint (debounce) config
//
// In LilyGo factory firmware: triggers screen switch (scr_mgr_switch).
// In Meshtastic: callback injects INPUT_BROKER_HOME event to navigate
// to home screen (implemented in variant.cpp).
//
// ===========================================================================
// PCA9535 IO Expander (I2C address 0x20)
// ===========================================================================
//
// Directly managed by epdiy library for e-paper power sequencing.
// Directly managed by variant.cpp for initial configuration.
// Directly managed by Meshtastic for button detection (P12).
// All access must use i2cLock mutex to prevent bus contention.
//
// Port 0 (P00-P07): All unassigned / available
// Port 1:
//   P10 (0x0100) = EP_OE    - E-paper output enable
//   P11 (0x0200) = EP_MODE  - E-paper mode select
//   P12 (0x0400) = BUTTON   - PWR button software readback
//   P13 (0x0800) = TPS_PWRUP  - TPS65185 PMIC power up control
//   P14 (0x1000) = VCOM_CTRL  - TPS65185 VCOM control
//   P15 (0x2000) = TPS_WAKEUP - TPS65185 wakeup
//   P16 (0x4000) = TPS_PWR_GOOD - TPS65185 power good status (input)
//   P17 (0x8000) = TPS_INT    - TPS65185 interrupt (input)
//
// Interrupt: GPIO 38 (PCA9535_INT) fires LOW on any input pin change.
// Use for interrupt-driven PWR button detection (P12 state change).
//
// ===========================================================================
// BQ25896 Charger / BQ27220 Fuel Gauge
// ===========================================================================
//
// Both on shared I2C bus (protected by i2cLock).
// BQ25896 provides power path management (PPM):
//   - Charges battery from USB
//   - BATFET controls battery connection to system
//   - shutdown() disconnects BATFET (REG_09H bit 5)
//   - Shutdown only works on battery power (not USB)
//   - Wake via QON button press or USB insertion
// BQ27220 provides fuel gauge:
//   - State of charge, voltage, current, temperature
//   - Design capacity configured via BQ27220_DESIGN_CAPACITY
//
// NOTE: Do NOT define HAS_AXP192 or HAS_AXP2101 - this board uses BQ chips.
// Defining them (even as 0) triggers HAS_PMU which expects AXP chips.
//
// ===========================================================================
// Pin Definitions
// ===========================================================================
//
// Most pin definitions are in platformio.ini build_flags to avoid
// redefinition warnings. This file contains additional hardware config.
//
// Pins defined in platformio.ini:
//   BUTTON_PIN=0, HAS_BUTTON=1, BUTTON_NEED_PULLUP
//   I2C_SDA=39, I2C_SCL=40
//   GPS_RX_PIN=44, GPS_TX_PIN=43 (UART2)
//   LORA_CS=46, LORA_DIO1=10, LORA_RESET=1, LORA_BUSY=47
//   HAS_GPS=1, GPS_BAUDRATE=9600
//   USE_EINK, USE_EINK_EPDIY, EINK_HEIGHT=540, EINK_WIDTH=960
//   TOUCH_CS=0x5D (GT911 I2C addr), HAS_TOUCHSCREEN=1

// Map LORA_* pins to SX126X_* names expected by RadioLib/main.cpp
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_RESET LORA_RESET
#define SX126X_BUSY LORA_BUSY

// SX1262 module configuration from T5S3 SDK
// TCXO voltage for the temperature compensated crystal oscillator
#define SX126X_DIO3_TCXO_VOLTAGE 2.4
// DIO2 is used as RF switch control
#define SX126X_DIO2_AS_RF_SWITCH

// SD Card (shares SPI bus with LoRa)
// Useful for Range Test Module CSV logging and file transfer
#define HAS_SDCARD
#define SPI_MOSI 13
#define SPI_MISO 21
#define SPI_SCK 14
#define SDCARD_CS 12
#define SD_CS SDCARD_CS

// RTC PCF8563 (on I2C bus)
#define PCF8563_RTC 0x51
#define RTC_INT 2

// IO Expander PCA9535 interrupt (see PCA9535 section above)
#define PCA9535_INT 38

// E-Paper frontlight enable (directly controllable, active HIGH)
#define PIN_EINK_EN 11

// E-Paper ED047TC1 4.7" 960x540
// Uses 16-bit PARALLEL interface via LCD peripheral (NOT SPI).
// Requires epdiy library with epd_board_v7 configuration.
// Parallel data bus: D0-D7 = GPIO 5,6,7,15,16,17,18,8
// Control: CKV=48, STH=41, LEH=42, STV=45, CKH=4
// Power: TPS65185 PMIC via PCA9535 IO expander

// BQ25896 Power Path Management (charger) + BQ27220 Fuel Gauge
// I2C bus is shared with epdiy - protected by i2cLock mutex
#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896
#define HAS_BQ27220 1
#define BQ27220_DESIGN_CAPACITY 1500  // mAh - adjust if battery differs

#endif
