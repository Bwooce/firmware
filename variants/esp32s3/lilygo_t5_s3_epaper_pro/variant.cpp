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
#define PCA9535_P12_BIT 0x04  // Bit 2 of port 1 = P12 (PWR button)

// GT911 touch driver instance
static TouchDrvGT911 touchDriver;
static bool touchInitialized = false;

/**
 * PWR button handler via PCA9535 IO expander
 *
 * Detects PWR button press via PCA9535 P12 input pin.
 * PCA9535 interrupt (GPIO 38) fires LOW on any port input change.
 * Polls at 50ms; only reads I2C when interrupt pending or button held.
 *
 * Short press (<1s) = toggle screen on/off (INPUT_BROKER_CANCEL)
 * Long press (>=1s) = software shutdown (INPUT_BROKER_SHUTDOWN)
 */
class PCA9535ButtonThread : public Observable<const InputEvent *>, public concurrency::OSThread
{
  public:
    PCA9535ButtonThread() : OSThread("PCA9535Btn")
    {
        if (inputBroker)
            inputBroker->registerSource(this);
    }

    int32_t runOnce() override
    {
        bool needRead = false;

        // Check for new PCA9535 events (GPIO 38 LOW = pending input change)
        if (digitalRead(PCA9535_INT) == LOW) {
            needRead = true;
        }
        // Continue polling I2C while button is held to detect release
        if (btnPressed) {
            needRead = true;
        }

        if (needRead) {
            uint8_t port1_val = 0xFF;
            {
                concurrency::LockGuard guard(i2cLock);
                Wire.beginTransmission(PCA9535_ADDR);
                Wire.write(PCA9535_REG_INPUT_PORT1);
                Wire.endTransmission();
                if (Wire.requestFrom((uint8_t)PCA9535_ADDR, (uint8_t)1) == 1) {
                    port1_val = Wire.read();
                }
            }
            // P12 is active LOW: bit clear = pressed
            bool pressed = !(port1_val & PCA9535_P12_BIT);

            if (pressed && !btnPressed) {
                btnPressed = true;
                pressStartTime = millis();
                longPressTriggered = false;
                LOG_DEBUG("PWR button pressed (PCA9535 P12)");
            } else if (!pressed && btnPressed) {
                btnPressed = false;
                uint32_t duration = millis() - pressStartTime;
                LOG_DEBUG("PWR button released after %lu ms", (unsigned long)duration);
                if (!longPressTriggered && duration < LONG_PRESS_TIME) {
                    InputEvent evt = {};
                    evt.source = "pwrBtn";
                    evt.inputEvent = INPUT_BROKER_CANCEL;
                    this->notifyObservers(&evt);
                }
            }
        }

        // Long press detection while button is held
        if (btnPressed && !longPressTriggered && (millis() - pressStartTime >= LONG_PRESS_TIME)) {
            longPressTriggered = true;
            LOG_INFO("PWR button long press - shutdown");
            InputEvent evt = {};
            evt.source = "pwrBtn";
            evt.inputEvent = INPUT_BROKER_SHUTDOWN;
            this->notifyObservers(&evt);
        }

        return 50;  // Poll every 50ms
    }

  private:
    static const uint32_t LONG_PRESS_TIME = 1000;
    bool btnPressed = false;
    uint32_t pressStartTime = 0;
    bool longPressTriggered = false;
};

static PCA9535ButtonThread *pwrButtonThread = nullptr;

/**
 * Read touch point callback for TouchScreenImpl1
 *
 * Returns true if screen is being touched, with coordinates in x,y.
 * Called at 20-100ms intervals by TouchScreenBase::runOnce().
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
            *x = tx[0];
            *y = ty[0];
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

    // Initialize PWR button handler via PCA9535 IO expander
    // PCA9535 interrupt pin (GPIO 38) goes LOW when any port input changes.
    // The button thread polls this pin and reads P12 state via I2C.
    pinMode(PCA9535_INT, INPUT_PULLUP);
    pwrButtonThread = new PCA9535ButtonThread();
    LOG_INFO("PWR button handler initialized (PCA9535 P12 via GPIO %d)", PCA9535_INT);
}

#endif // LILYGO_T5_S3_EPAPER_PRO
