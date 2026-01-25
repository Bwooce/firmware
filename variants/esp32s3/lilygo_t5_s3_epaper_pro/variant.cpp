/**
 * LilyGo T5 S3 E-Paper Pro variant initialization
 *
 * GT911 touch driver integration based on LilyGo T5S3-4.7-e-paper-PRO SDK
 * examples/touch/main/main.cpp
 *
 * IMPORTANT: All I2C devices share the same physical bus (SDA=39, SCL=40):
 * - epdiy: PCA9535 IO expander, TPS65185 PMIC
 * - GT911 touch controller
 * - PCF8563 RTC (I2C ops skipped due to epdiy conflict)
 * - BQ25896 charger, BQ27220 fuel gauge (disabled)
 *
 * All I2C access is protected by i2cLock mutex to prevent bus contention.
 */

#include "configuration.h"

#ifdef LILYGO_T5_S3_EPAPER_PRO

#include "I2CLock.h"
#include "input/TouchScreenImpl1.h"
#include <Wire.h>
#include <TouchDrvGT911.hpp>

// GT911 touch pins from LilyGo SDK
#define GT911_INT_PIN   3
#define GT911_RST_PIN   9

// GT911 touch driver instance
static TouchDrvGT911 touchDriver;
static bool touchInitialized = false;

/**
 * Read touch point callback for TouchScreenImpl1
 *
 * Returns true if screen is being touched, with coordinates in x,y
 */
bool readTouch(int16_t *x, int16_t *y)
{
    static uint32_t callCount = 0;
    static uint32_t lastLog = 0;
    static bool firstCall = true;

    if (firstCall) {
        LOG_INFO("GT911: readTouch first call");
        firstCall = false;
    }

    callCount++;

    // Log every 10 seconds to show we're still polling
    uint32_t now = millis();
    if (now - lastLog > 10000) {
        LOG_INFO("GT911: readTouch count=%lu, init=%d", (unsigned long)callCount, touchInitialized ? 1 : 0);
        lastLog = now;
    }

    if (!touchInitialized) {
        return false;
    }

    // Check if touch is pressed - this reads from I2C
    // Hold I2C lock during GT911 operations (shares Wire bus with epdiy)
    {
        concurrency::LockGuard guard(i2cLock);
        bool pressed = touchDriver.isPressed();
        if (pressed) {
            int16_t tx[5], ty[5];
            uint8_t touched = touchDriver.getPoint(tx, ty, touchDriver.getSupportTouchPoint());
            if (touched > 0) {
                *x = tx[0];
                *y = ty[0];
                LOG_INFO("GT911: Touch at %d,%d", *x, *y);
                return true;
            }
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
    // T5S3-specific PCA9555 IO expander initialization (address 0x20)
    // This supplements the upstream epdiy init with LilyGo board-specific config.
    // The display is already initialized at this point.
    {
        concurrency::LockGuard guard(i2cLock);
        const uint8_t PCA9555_ADDR = 0x20;
        // Set inversion registers to 0 (no inversion)
        Wire.beginTransmission(PCA9555_ADDR);
        Wire.write(0x04);  // REG_INVERT_PORT0
        Wire.write(0x00);
        Wire.endTransmission();
        Wire.beginTransmission(PCA9555_ADDR);
        Wire.write(0x05);  // REG_INVERT_PORT1
        Wire.write(0x00);
        Wire.endTransmission();
        // Set config register (all outputs)
        Wire.beginTransmission(PCA9555_ADDR);
        Wire.write(0x06);  // REG_CONFIG_PORT0
        Wire.write(0x00);
        Wire.endTransmission();
        // Set output values
        Wire.beginTransmission(PCA9555_ADDR);
        Wire.write(0x02);  // REG_OUTPUT_PORT0
        Wire.write(0xFF);
        Wire.endTransmission();
        LOG_DEBUG("PCA9555 IO expander configured for LilyGo T5 S3 E-Paper Pro");
    }

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

    // Optional: Home button callback
    touchDriver.setHomeButtonCallback([](void *user_data) {
        LOG_DEBUG("GT911 home button pressed");
    }, NULL);

    touchInitialized = true;

    // Create touch screen input handler
    touchScreenImpl1 = new TouchScreenImpl1(EINK_WIDTH, EINK_HEIGHT, readTouch);
    touchScreenImpl1->init();

    LOG_INFO("GT911 touch controller ready");
}

#endif // LILYGO_T5_S3_EPAPER_PRO
