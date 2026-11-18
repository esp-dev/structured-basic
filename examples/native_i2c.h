#ifndef EXAMPLE_NATIVE_I2C_H
#define EXAMPLE_NATIVE_I2C_H

#include "sbasic.h"

typedef struct {
    void *user;
    /* Return SB_OK after transferring all bytes. With tx and rx present,
     * issue a repeated START between write and read and finish with STOP. */
    SBError (*transfer)(void *user, uint8_t bus, uint8_t address, const uint8_t *tx,
                        uint16_t tx_length, uint8_t *rx, uint16_t rx_length);
} ExampleI2C;

SBError example_i2c_transfer(void *user, SBNativeCall *call);

#endif
