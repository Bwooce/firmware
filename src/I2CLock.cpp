#include "I2CLock.h"
#include "configuration.h"
#include <assert.h>

concurrency::Lock *i2cLock = nullptr;

void initI2CLock()
{
    assert(!i2cLock);
    i2cLock = new concurrency::Lock();
}
