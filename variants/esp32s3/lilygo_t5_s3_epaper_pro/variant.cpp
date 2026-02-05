/**
 * LilyGo T5 S3 E-Paper Pro variant initialization
 *
 * GT911 touch driver integration based on LilyGo T5S3-4.7-e-paper-PRO SDK
 * examples/touch/main/main.cpp
 *
 * IMPORTANT: All I2C devices share the same physical bus (SDA=39, SCL=40):
 * - epdiy: PCA9535 IO expander, TPS65185 PMIC
 * - GT911 touch controller
 * - PCF8563 RTC
 * - BQ25896 charger, BQ27220 fuel gauge
 *
 * All I2C access is protected by i2cLock mutex to prevent bus contention.
 */

#include "configuration.h"

#ifdef LILYGO_T5_S3_EPAPER_PRO

#include "I2CLock.h"
#include "concurrency/OSThread.h"
#include "input/InputBroker.h"
#include "input/TouchScreenImpl1.h"
#include <Wire.h>
#include <TouchDrvGT911.hpp>

// GT911 touch pins from LilyGo SDK
#define GT911_INT_PIN   3
#define GT911_RST_PIN   9

// PCA9535 IO expander registers
#define PCA9535_ADDR 0x20
#define PCA9535_REG_INPUT_PORT1 0x01
#define PCA9535_REG_CONFIG_PORT1 0x07
#define PCA9535_P12_BIT 0x04  // Bit 2 of port 1 = P12 (PWR button)

// PCA9535 Port 1 pin mask: pins that must be configured as inputs.
// P12 (bit 2) = PWR button readback
// P16 (bit 6) = TPS65185 PWRGOOD status
// P17 (bit 7) = TPS65185 interrupt
//
// IMPORTANT: P12 is mapped as __CFG_PIN_STV in epdiy's epd_board_v7.c.
// During epd_board_deinit() (called after every display poweroff), epdiy
// reconfigures Port 1 and REMOVES P12 from the input mask, making it an
// output driven LOW. This looks like a permanent PWR button press, causing
// spurious shutdowns. We fix this by re-asserting P12 as input before
// every read in the button thread.
#define PCA9535_PORT1_INPUT_MASK 0xC4  // bits 2,6,7 = P12,P16,P17

// GT911 touch driver instance
static TouchDrvGT911 touchDriver;
static bool touchInitialized = false;

/**
 * PCA9535 P12 button handler - frontlight brightness control
 *
 * The fourth side button on the T5 S3 E-Paper Pro is connected to PCA9535 P12.
 * (The "PWR" button is BQ25896 QON - hardware-only BATFET toggle, not software-readable.)
 *
 * Always polls button state at 50ms intervals (not interrupt-driven, since epdiy
 * also uses the PCA9535 interrupt and may clear it before we see button events).
 *
 * Short press = cycle frontlight brightness (off → low → medium → high → off)
 * Uses analogWrite() PWM on GPIO 11 (PIN_EINK_EN) for brightness control.
 *
 * WORKAROUND for epdiy P12/STV pin conflict:
 * epdiy's epd_board_v7.c maps PCA9535 P12 as __CFG_PIN_STV. During
 * epd_board_deinit() (after every display poweroff), epdiy reconfigures
 * Port 1 and drops P12 from the input mask, making it an output driven LOW.
 * This looks like a permanent button press. We fix this by:
 * 1. Writing the config register to re-assert P12 as input before every read
 * 2. Using a "stuck" timeout - if button reads pressed for >5s, reset state
 *
 * The I2C read uses a repeated start (endTransmission(false)) to prevent epdiy's
 * raw ESP-IDF I2C operations from changing the PCA9535 register pointer.
 */
class PCA9535ButtonThread : public concurrency::OSThread
{
  public:
    // Frontlight PWM levels matching LilyGo SDK
    static const uint8_t BRIGHTNESS_OFF = 0;
    static const uint8_t BRIGHTNESS_LOW = 50;
    static const uint8_t BRIGHTNESS_MED = 100;
    static const uint8_t BRIGHTNESS_HIGH = 230;

    PCA9535ButtonThread() : OSThread("PCA9535Btn")
    {
        // Start with frontlight off
        brightnessLevel = 0;
        analogWrite(PIN_EINK_EN, BRIGHTNESS_OFF);
    }

    int32_t runOnce() override
    {
        // Always poll button state (don't rely on interrupt which epdiy may clear)
        uint8_t port1_val = 0xFF;
        {
            concurrency::LockGuard guard(i2cLock);

            // Re-assert P12 as input. epdiy's epd_board_deinit() reconfigures
            // Port 1 and drops P12 from the input mask (see header comment).
            Wire.beginTransmission(PCA9535_ADDR);
            Wire.write(PCA9535_REG_CONFIG_PORT1);
            Wire.write(PCA9535_PORT1_INPUT_MASK);
            Wire.endTransmission();

            // Read Port 1 input register using repeated start to keep the bus
            // locked between register address write and data read.
            Wire.beginTransmission(PCA9535_ADDR);
            Wire.write(PCA9535_REG_INPUT_PORT1);
            Wire.endTransmission(false);  // repeated start - keeps bus locked
            if (Wire.requestFrom((uint8_t)PCA9535_ADDR, (uint8_t)1) == 1) {
                port1_val = Wire.read();
            }
        }

        // P12 is active LOW: bit clear = pressed
        bool rawPressed = !(port1_val & PCA9535_P12_BIT);

        // Stuck detection: if button has been "pressed" for >5s, it's likely
        // epdiy holding P12 LOW. Reset state and ignore until we see a release.
        if (btnPressed && (millis() - pressStartTime > 5000)) {
            LOG_WARN("P12 button stuck for >5s (epdiy conflict?), resetting state");
            btnPressed = false;
            debounceCount = 0;
            stuckRecovery = true;  // Ignore presses until we see a clean release
        }

        // In stuck recovery mode, wait for a clean release before accepting new presses
        if (stuckRecovery) {
            if (!rawPressed) {
                stuckRecovery = false;
                LOG_DEBUG("P12 button released from stuck state");
            }
            return 50;
        }

        // Debounce: require DEBOUNCE_COUNT consecutive reads showing pressed
        if (rawPressed && !btnPressed) {
            debounceCount++;
            if (debounceCount >= DEBOUNCE_COUNT) {
                btnPressed = true;
                debounceCount = 0;
                pressStartTime = millis();
                LOG_DEBUG("P12 button pressed (debounced)");
            }
        } else if (!rawPressed && !btnPressed) {
            // Not pressed and not yet registered - reset debounce
            debounceCount = 0;
        } else if (!rawPressed && btnPressed) {
            btnPressed = false;
            debounceCount = 0;
            uint32_t duration = millis() - pressStartTime;
            LOG_DEBUG("P12 button released after %lu ms", (unsigned long)duration);

            // Cycle frontlight brightness on short press (50ms - 2s)
            if (duration >= 50 && duration <= 2000) {
                cycleBrightness();
            }
        }

        return 50;  // Poll every 50ms
    }

  private:
    void cycleBrightness()
    {
        brightnessLevel = (brightnessLevel + 1) % 4;
        uint8_t pwmVal;
        const char *levelName;

        switch (brightnessLevel) {
            case 0:
                pwmVal = BRIGHTNESS_OFF;
                levelName = "OFF";
                break;
            case 1:
                pwmVal = BRIGHTNESS_LOW;
                levelName = "LOW";
                break;
            case 2:
                pwmVal = BRIGHTNESS_MED;
                levelName = "MEDIUM";
                break;
            case 3:
                pwmVal = BRIGHTNESS_HIGH;
                levelName = "HIGH";
                break;
            default:
                pwmVal = BRIGHTNESS_OFF;
                levelName = "OFF";
                break;
        }

        analogWrite(PIN_EINK_EN, pwmVal);
        LOG_INFO("Frontlight: %s (level %d, PWM %d)", levelName, brightnessLevel, pwmVal);
    }

    static const uint8_t DEBOUNCE_COUNT = 2;  // 2 consecutive reads = 100ms (reduced from 3)
    bool btnPressed = false;
    uint32_t pressStartTime = 0;
    uint8_t debounceCount = 0;
    uint8_t brightnessLevel = 0;  // 0=off, 1=low, 2=med, 3=high
    bool stuckRecovery = false;   // True when recovering from stuck state
};

static PCA9535ButtonThread *p12ButtonThread = nullptr;

/**
 * Read touch point callback for TouchScreenImpl1
 *
 * Returns true if screen is being touched, with coordinates in x,y.
 * Called at 20-100ms intervals by TouchScreenBase::runOnce().
 *
 * GT911 returns coordinates in native landscape orientation (960x540).
 * The display uses EPD_ROT_INVERTED_PORTRAIT, rotating to 540x960 portrait.
 * We must apply the inverse rotation to map touch → display coordinates:
 *   display_x = gt_y
 *   display_y = (native_width - 1) - gt_x
 */
bool readTouch(int16_t *x, int16_t *y)
{
    if (!touchInitialized) {
        return false;
    }

    // Hold I2C lock during GT911 operations (shares Wire bus with epdiy)
    concurrency::LockGuard guard(i2cLock);
    if (touchDriver.isPressed()) {
        int16_t tx[5], ty[5];
        uint8_t touched = touchDriver.getPoint(tx, ty, touchDriver.getSupportTouchPoint());
        if (touched > 0) {
            // Rotate from GT911 native landscape (960x540) to display portrait (540x960)
            // Inverse of EPD_ROT_INVERTED_PORTRAIT: swap + invert
            *x = ty[0];                // display X = GT911 Y (0..539)
            *y = (960 - 1) - tx[0];   // display Y = inverted GT911 X (0..959)
            return true;
        }
    }
    return false;
}

/**
 * Late variant initialization - called after main system init
 *
 * Initializes GT911 touch controller on Wire (shared with epdiy).
 * All I2C devices share the same physical bus (SDA=39, SCL=40).
 * Mutex protection ensures only one device accesses the bus at a time.
 */
void lateInitVariant()
{
    // NOTE: PCA9535 Port 0 config (LoRa+GPS power enable) is done early in main.cpp setup(),
    // before SPI and radio init. Do not duplicate it here.

    // Initialize GT911 touch controller using the same approach as LilyGo factory example:
    // - Wire is already initialized before epdiy (in main.cpp)
    // - epdiy's driver install fails silently if Wire driver exists
    // - GT911 uses Wire with RST pin (GPIO 9) - same pin as epdiy D8, but works because
    //   the LCD peripheral only drives the data pins during active display updates
    LOG_INFO("LilyGo T5 S3 E-Paper Pro: Initializing GT911 touch controller on Wire (shared I2C bus)");

    // Set up RST and INT pins - following LilyGo factory example
    // GPIO 9 is shared with epdiy D8, but the LCD peripheral only drives it during updates
    LOG_DEBUG("GT911: Setting pins RST=%d, INT=%d", GT911_RST_PIN, GT911_INT_PIN);
    touchDriver.setPins(GT911_RST_PIN, GT911_INT_PIN);
    LOG_INFO("GT911: Pins configured");

    // Initialize GT911 on Wire with mutex protection
    // Following LilyGo factory example: touch.begin(Wire, GT911_SLAVE_ADDRESS_L, SDA, SCL)
    LOG_INFO("GT911: Calling begin() on Wire...");
    bool initOk;
    {
        concurrency::LockGuard guard(i2cLock);
        initOk = touchDriver.begin(Wire, GT911_SLAVE_ADDRESS_L, I2C_SDA, I2C_SCL);
    }
    if (!initOk) {
        LOG_ERROR("GT911 touch controller not found on Wire");
        return;
    }

    LOG_INFO("GT911 touch controller initialized on Wire");

    // Set interrupt mode - low level when idle, trigger when touched
    {
        concurrency::LockGuard guard(i2cLock);
        touchDriver.setInterruptMode(0x03);  // LOW_LEVEL_QUERY
    }
    LOG_DEBUG("GT911: Interrupt mode set");

    // GT911 home button (virtual touch zone at bottom-center of display)
    // Injects INPUT_BROKER_HOME event to navigate to home screen
    touchDriver.setHomeButtonCallback([](void *user_data) {
        LOG_DEBUG("GT911 home button - navigating to home screen");
        if (inputBroker) {
            InputEvent evt = {};
            evt.source = "homeBtn";
            evt.inputEvent = INPUT_BROKER_HOME;
            inputBroker->injectInputEvent(&evt);
        }
    }, NULL);

    touchInitialized = true;

    // Create touch screen input handler
    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();

    LOG_INFO("GT911 touch controller ready");

    // Initialize PCA9535 P12 button handler (frontlight toggle)
    // PCA9535 interrupt pin (GPIO 38) goes LOW when any port input changes.
    // The button thread polls this pin and reads P12 state via I2C.
    // Note: epdiy also registers an ISR on GPIO 38 (CFG_INTR) for PCA9535
    // interrupt-driven power management. Our digitalRead() coexists with that ISR.
    //
    // NOTE: GPIO 48 is CKV (e-paper clock vertical), not a physical button.
    // The "PWR" button is BQ25896 QON (hardware BATFET toggle, not software-readable).
    // The fourth side button connects to PCA9535 P12.
    pinMode(PCA9535_INT, INPUT_PULLUP);
    p12ButtonThread = new PCA9535ButtonThread();
    LOG_INFO("P12 frontlight button initialized (PCA9535 P12, frontlight GPIO %d with PWM brightness)",
             PIN_EINK_EN);

    // Light sleep and USB-CDC interaction on ESP32-S3:
    // Entering light sleep disconnects native USB-CDC. If a host serial monitor is
    // connected, it detects the disconnect, reconnects, and opening the port toggles
    // DTR which resets the ESP32-S3 - creating an infinite reboot loop.
    // This is only a problem when USB serial is actively connected (development/debug).
    // For untethered operation (battery + LoRa only), light sleep saves significant power.
    // We log the risk but do NOT force-disable light sleep - the user can configure
    // ls_secs=0 via Meshtastic settings if they need persistent USB serial.
    if (config.power.ls_secs != 0) {
        LOG_WARN("Light sleep enabled (ls_secs=%lu). Note: USB-CDC will disconnect during sleep, "
                 "which may cause reboot loops if a serial monitor is connected.",
                 (unsigned long)config.power.ls_secs);
    }
}

#endif // LILYGO_T5_S3_EPAPER_PRO
