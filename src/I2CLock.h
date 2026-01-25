#pragma once

#include "concurrency/LockGuard.h"

/**
 * Used to provide mutual exclusion for access to the I2C bus (Wire/I2C_NUM_0).
 *
 * This is needed on boards like LilyGo T5 S3 E-Paper Pro where the epdiy library
 * uses ESP-IDF's I2C driver directly while other code uses Arduino's Wire library.
 * Both access the same I2C bus and can conflict, causing hangs.
 *
 * Usage:
 *   concurrency::LockGuard g(i2cLock);
 */
extern concurrency::Lock *i2cLock;

/** Initialize the i2cLock. Call before any I2C operations. */
void initI2CLock();
