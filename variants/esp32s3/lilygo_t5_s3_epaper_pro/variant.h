#ifndef _VARIANT_LILYGO_T5_S3_EPAPER_PRO_H_
#define _VARIANT_LILYGO_T5_S3_EPAPER_PRO_H_

// Pin definitions from LilyGo T5S3-4.7-e-paper-PRO SDK
// https://github.com/Xinyuan-LilyGO/T5S3-4.7-e-paper-PRO
//
// Most pin definitions are in platformio.ini build_flags to avoid
// redefinition warnings. This file contains additional hardware info.

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
#define HAS_SDCARD
#define SPI_MOSI 13
#define SPI_MISO 21
#define SPI_SCK 14
#define SDCARD_CS 12
#define SD_CS SDCARD_CS

// RTC PCF8563 (on I2C bus)
#define PCF8563_RTC 0x51
#define RTC_INT 2

// IO Expander PCA9535 interrupt
#define PCA9535_INT 38

// E-Paper frontlight enable (directly controllable, active HIGH)
#define PIN_EINK_EN 11

// E-Paper ED047TC1 4.7" 960x540
// This display uses a 16-bit PARALLEL interface via the LCD peripheral,
// NOT SPI. It requires the epdiy library with epd_board_v7 configuration.
// The parallel data pins are directly connected to the LCD peripheral.
// Power management is via TPS65185 PMIC controlled through PCA9535 IO expander.
//
// Parallel data bus: D0-D7 = GPIO 5,6,7,15,16,17,18,8
// Control: CKV=48, STH=41, LEH=42, STV=45, CKH=4
// PMIC control via I2C through PCA9535

// Battery management via BQ25896 (charger) and BQ27220 (fuel gauge) over I2C
// No direct ADC battery pin - must use I2C fuel gauge
// NOTE: Do NOT define HAS_AXP192 or HAS_AXP2101 - this board uses BQ chips instead
// Defining them (even as 0) triggers HAS_PMU which expects AXP chips

// BQ25896 Power Path Management (charger) + BQ27220 Fuel Gauge
// I2C bus is shared with epdiy - protected by i2cLock mutex
#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896
#define HAS_BQ27220 1
#define BQ27220_DESIGN_CAPACITY 1500  // mAh - adjust if battery differs

#endif
