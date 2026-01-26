#include "configuration.h"

#ifdef USE_EINK_EPDIY

#include "EInkDisplayEPDiy.h"
#include "I2CLock.h"
#include "main.h"
#include <Wire.h>
#include <SPI.h>
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <rom/ets_sys.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <soc/gpio_struct.h>
#include <soc/gpio_sig_map.h>

// Include the board definition for LilyGo T5 S3 E-Paper Pro (V7 board with ED047TC1)
extern "C" {
#include "epd_board.h"
#include "render.h"
extern const EpdBoardDefinition epd_board_v7;
extern const EpdDisplay_t ED047TC1;
}

// Diagnostic: dump GPIO matrix output routing for LoRa/display shared pins
static void dumpGpioMatrixState(const char *label)
{
    // Shared pins between epdiy LCD parallel bus and LoRa SPI:
    //   GPIO 14 = LCD D13 / LoRa SCK    (LCD_DATA_OUT13_IDX=146, FSPICLK_OUT_IDX=101)
    //   GPIO 13 = LCD D12 / LoRa MOSI   (LCD_DATA_OUT12_IDX=145, FSPID_OUT_IDX=103)
    //   GPIO 21 = LCD D14 / LoRa MISO   (LCD_DATA_OUT14_IDX=147, FSPIQ_IN_IDX=102)
    //   GPIO 10 = LCD D9  / LoRa DIO1   (LCD_DATA_OUT9_IDX=142)
    //   GPIO 47 = LCD D15 / LoRa BUSY   (LCD_DATA_OUT15_IDX=148)
    struct PinInfo {
        int gpio;
        const char *name;
        int lcd_sig;
        int spi_sig;
    };
    const PinInfo pins[] = {
        {14, "SCK",  146, 101},
        {13, "MOSI", 145, 103},
        {21, "MISO", 147, 102},
        {10, "DIO1", 142, -1},
        {47, "BUSY", 148, -1},
    };

    LOG_INFO("GPIO matrix state [%s]:", label);
    for (int i = 0; i < 5; i++) {
        int gpio = pins[i].gpio;
        uint32_t cfg = GPIO.func_out_sel_cfg[gpio].val;
        int func_sel = cfg & 0x1FF;        // bits [8:0] - peripheral signal index
        int oen_sel = (cfg >> 10) & 0x1;   // bit 10 - output enable source (0=GPIO, 1=peripheral)
        int oen_inv = (cfg >> 11) & 0x1;   // bit 11 - output enable invert

        const char *routed_to = "unknown";
        if (func_sel == pins[i].lcd_sig)
            routed_to = "LCD";
        else if (func_sel == pins[i].spi_sig)
            routed_to = "SPI";
        else if (func_sel == SIG_GPIO_OUT_IDX)
            routed_to = "GPIO";
        else if (func_sel == 256)
            routed_to = "GPIO(detached)";

        LOG_INFO("  GPIO %d (%s): func_sel=%d (%s), oen_sel=%d, oen_inv=%d",
                 gpio, pins[i].name, func_sel, routed_to, oen_sel, oen_inv);
    }
}

// Diagnostic: probe SX1262 via SPI to check if SPI bus works
static void probeSX1262(const char *label)
{
#if defined(LORA_SCK) && defined(LORA_CS)
    // Try reading SX1262 status via a simple SPI transaction
    // SX1262 GetStatus command: opcode 0xC0, returns status byte
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    delay(1);

    SPI.beginTransaction(SPISettings(2000000, MSBFIRST, SPI_MODE0));
    digitalWrite(LORA_CS, LOW);
    uint8_t status = SPI.transfer(0xC0);  // GetStatus opcode
    uint8_t resp = SPI.transfer(0x00);    // Read response
    digitalWrite(LORA_CS, HIGH);
    SPI.endTransaction();

    // SX1262 status byte format: bits [6:4] = chip mode, bits [3:1] = command status
    // If SPI is dead, we get 0x00 or 0xFF
    int chipMode = (resp >> 4) & 0x07;
    int cmdStatus = (resp >> 1) & 0x07;
    LOG_INFO("SX1262 SPI probe [%s]: status=0x%02X, resp=0x%02X (chipMode=%d, cmdStatus=%d) %s",
             label, status, resp, chipMode, cmdStatus,
             (resp == 0x00 || resp == 0xFF) ? "** SPI LIKELY DEAD **" : "SPI OK");
#endif
}

// Constructor
EInkDisplayEPDiy::EInkDisplayEPDiy(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus)
{
    // Set dimensions in OLEDDisplay base class
    this->geometry = GEOMETRY_RAWMODE;
    this->displayWidth = EINK_WIDTH;
    this->displayHeight = EINK_HEIGHT;

    // Calculate buffer size for 1bpp (round up to nearest byte)
    uint16_t shortSide = min(EINK_WIDTH, EINK_HEIGHT);
    uint16_t longSide = max(EINK_WIDTH, EINK_HEIGHT);
    if (shortSide % 8 != 0)
        shortSide = (shortSide | 7) + 1;

    this->displayBufferSize = longSide * (shortSide / 8);
}

EInkDisplayEPDiy::~EInkDisplayEPDiy()
{
    if (epdiyInitialized) {
        epd_deinit();
    }
}

// Connect to the display - initialize epdiy
bool EInkDisplayEPDiy::connect()
{
    LOG_INFO("Initializing epdiy for ED047TC1 display");

#ifdef PIN_EINK_EN
    // Frontlight control - start with light OFF to save power
    pinMode(PIN_EINK_EN, OUTPUT);
    digitalWrite(PIN_EINK_EN, LOW);
    LOG_INFO("E-Paper frontlight available on GPIO %d", PIN_EINK_EN);
#endif

    // Diagnostic: check GPIO matrix and SPI state BEFORE display init
    dumpGpioMatrixState("before epd_init");
    probeSX1262("before epd_init");

    // Initialize epdiy with the V7 board definition and ED047TC1 display
    // This configures:
    // - I2C for PCA9535 IO expander and TPS65185 PMIC
    // - 16-bit parallel data bus via LCD peripheral
    // - Waveform timing for ED047TC1
    //
    // NOTE: epdiy (with I2C bus sharing patch) checks for ESP_ERR_INVALID_STATE from
    // i2c_driver_install() and gpio_install_isr_service(). If Wire.begin() was already
    // called, the driver is already installed and epdiy reuses it.
    //
    // We do NOT delete the I2C driver before epd_init() - this allows Wire to remain
    // functional for GT911 touch, RTC, and other I2C devices after display init.
    LOG_DEBUG("epdiy: calling epd_init()");
    epd_init(&epd_board_v7, &ED047TC1, EPD_OPTIONS_DEFAULT);
    LOG_DEBUG("epdiy: epd_init() complete");

    // Diagnostic: check GPIO matrix and SPI state AFTER display init
    dumpGpioMatrixState("after epd_init");
    probeSX1262("after epd_init");

    // Set VCOM voltage (from LilyGo T5S3 example - 1560mV)
    // This controls display contrast
    LOG_DEBUG("epdiy: calling epd_set_vcom(1560)...");
    epd_set_vcom(1560);
    LOG_DEBUG("epdiy: epd_set_vcom() done");

    // Initialize the high-level state (allocates framebuffers in PSRAM)
    LOG_DEBUG("epdiy: calling epd_hl_init()...");
    hl = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    LOG_DEBUG("epdiy: epd_hl_init() done");

    // Set rotation for portrait mode (540 wide x 960 tall as seen by Meshtastic)
    // The ED047TC1 native is 960x540, so we rotate to get portrait
    LOG_DEBUG("epdiy: calling epd_set_rotation()...");
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    LOG_DEBUG("epdiy: epd_set_rotation() done");

    LOG_INFO("epdiy display: %d x %d (after rotation)", epd_rotated_display_width(), epd_rotated_display_height());

    // Clear the display on startup (following LilyGo example pattern)
    // 1. epd_clear() clears the physical display
    // 2. epd_hl_set_all_white() syncs the high-level framebuffer state
    // Hold I2C lock during display operations
    {
        concurrency::LockGuard guard(i2cLock);
        LOG_DEBUG("epdiy: calling epd_poweron()...");
        epd_poweron();
        LOG_DEBUG("epdiy: calling epd_clear()...");
        epd_clear();
        LOG_DEBUG("epdiy: calling epd_poweroff()...");
        epd_poweroff();
    }
    LOG_DEBUG("epdiy: calling epd_hl_set_all_white()...");
    epd_hl_set_all_white(&hl);

    // Diagnostic: check GPIO matrix after full display cycle (init + poweron + clear + poweroff)
    dumpGpioMatrixState("after display clear cycle");
    probeSX1262("after display clear cycle");

    epdiyInitialized = true;
    LOG_INFO("epdiy initialization complete");

    return true;
}

/**
 * Force a display update if we haven't drawn within the specified msecLimit
 */
bool EInkDisplayEPDiy::forceDisplay(uint32_t msecLimit)
{
    if (!epdiyInitialized) {
        return false;
    }

    // Deferred diagnostics: log GPIO matrix and SPI state on first display update
    // (connect() diagnostics fire before USB CDC is ready, so we repeat them here)
    static bool deferredDiagDone = false;
    if (!deferredDiagDone) {
        deferredDiagDone = true;
        LOG_INFO("=== Deferred epdiy diagnostics (USB CDC now ready) ===");
        dumpGpioMatrixState("first forceDisplay");
        probeSX1262("first forceDisplay");
    }

    uint32_t now = millis();
    uint32_t sinceLast = now - lastDrawMsec;

    if (sinceLast > msecLimit || lastDrawMsec == 0) {
        lastDrawMsec = now;
    } else {
        return false;
    }

    // Get the epdiy framebuffer (4bpp, in PSRAM)
    uint8_t *fb = epd_hl_get_framebuffer(&hl);

    // Convert OLEDDisplay 1bpp buffer to epdiy 4bpp format
    // OLEDDisplay buffer: each byte is 8 vertical pixels, bit set = black
    // epdiy: 4bpp, 0x00 = black, 0xF0 = white
    const bool flipped = config.display.flip_screen;

    for (uint32_t y = 0; y < displayHeight; y++) {
        for (uint32_t x = 0; x < displayWidth; x++) {
            // Get the byte containing this pixel
            auto b = buffer[x + (y / 8) * displayWidth];
            // Check if the bit for this y position is set
            auto isset = b & (1 << (y & 7));

            // Determine color: bit set = black (0x00), bit clear = white (0xF0)
            uint8_t color = isset ? 0x00 : 0xF0;

            // Draw to epdiy framebuffer
            if (flipped) {
                epd_draw_pixel((displayWidth - 1) - x, (displayHeight - 1) - y, color, fb);
            } else {
                epd_draw_pixel(x, y, color, fb);
            }
        }
    }

    // Power on, update screen, power off
    // Hold I2C lock during display operations (epdiy uses I2C for PCA9535/TPS65185)
    LOG_DEBUG("Updating E-Paper display via epdiy");
    {
        concurrency::LockGuard guard(i2cLock);
        epd_poweron();

        // Use MODE_GL16 for good quality grayscale transitions
        // Get ambient temperature from TPS65185 PMIC for optimal waveform timing
        int temperature = epd_ambient_temperature();
        enum EpdDrawError err = epd_hl_update_screen(&hl, MODE_GL16, temperature);

        if (err != EPD_DRAW_SUCCESS) {
            LOG_ERROR("epdiy draw error: 0x%X", err);
        }

        epd_poweroff();
    }

    // End update (cleanup if needed)
    endUpdate();

    LOG_DEBUG("E-Paper update complete");
    return true;
}

// End the update process
void EInkDisplayEPDiy::endUpdate()
{
    // Nothing needed - epd_poweroff() already called in forceDisplay()
    // The display retains the image with power off
}

// Write the buffer to the display memory
void EInkDisplayEPDiy::display(void)
{
    // Use throttling for E-Ink updates to prevent excessive refreshes
    // For the first frame (lastDrawMsec == 0), use 0 to draw immediately
    forceDisplay(lastDrawMsec ? slowUpdateMsec : 0);
}

// Send a command to the display (not used for epdiy - parallel interface)
void EInkDisplayEPDiy::sendCommand(uint8_t com)
{
    (void)com;
    // Not applicable for parallel epdiy displays
}

void EInkDisplayEPDiy::setDetected(uint8_t detected)
{
    (void)detected;
}

#endif // USE_EINK_EPDIY
