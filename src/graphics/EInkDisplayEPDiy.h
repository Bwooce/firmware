#pragma once

#ifdef USE_EINK_EPDIY

#include <OLEDDisplay.h>

// epdiy library headers
extern "C" {
#include "epdiy.h"
#include "epd_highlevel.h"
}

// Limit how often we push a full E-Ink refresh
#ifndef EINK_FORCE_DISPLAY_THROTTLE_MS
#define EINK_FORCE_DISPLAY_THROTTLE_MS 1000
#endif

/**
 * An adapter class that allows using the LilyGo epdiy library as an OLEDDisplay implementation.
 *
 * This is specifically for parallel-interface E-Paper displays like the ED047TC1
 * used in the LilyGo T5 S3 E-Paper Pro, which cannot use GxEPD2 (SPI-based).
 *
 * The epdiy library handles:
 * - Parallel 16-bit data bus via LCD peripheral
 * - PCA9535 IO expander for control signals
 * - TPS65185 PMIC for voltage rails and VCOM
 * - ED047TC1 waveforms and timing
 */
class EInkDisplayEPDiy : public OLEDDisplay
{
    /// How often should we update the display (slow updates after initial)
    uint32_t slowUpdateMsec = 5 * 60 * 1000;

  public:
    EInkDisplayEPDiy(uint8_t address, int sda, int scl, OLEDDISPLAY_GEOMETRY geometry, HW_I2C i2cBus);
    virtual ~EInkDisplayEPDiy();

    // Write the buffer to the display memory (for eink we only do this occasionally)
    virtual void display(void) override;

    /**
     * Force a display update if we haven't drawn within the specified msecLimit
     * @return true if we did draw the screen
     */
    virtual bool forceDisplay(uint32_t msecLimit = EINK_FORCE_DISPLAY_THROTTLE_MS);

    /**
     * Run any code needed to complete an update, after the physical refresh has completed.
     */
    virtual void endUpdate();

    /**
     * Shim to make the abstraction happy
     */
    void setDetected(uint8_t detected);

  protected:
    // The header size of the buffer used
    virtual int getBufferOffset(void) override { return 0; }

    // Send a command to the display (not used for epdiy)
    virtual void sendCommand(uint8_t com) override;

    // Connect to the display
    virtual bool connect() override;

  private:
    // epdiy high-level state
    EpdiyHighlevelState hl;

    // Track if epdiy has been initialized
    bool epdiyInitialized = false;

    // Timestamp of last display update
    uint32_t lastDrawMsec = 0;
};

// Dummy macros - EPDiy doesn't use dynamic display features
// These allow code using EINK_ADD_FRAMEFLAG to compile without changes
#define EINK_ADD_FRAMEFLAG(display, flag)
#define EINK_JOIN_ASYNCREFRESH(display)

#endif // USE_EINK_EPDIY
