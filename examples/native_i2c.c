#include "native_i2c.h"

SBError example_i2c_transfer(void *user, SBNativeCall *call) {
    const ExampleI2C *driver = user;
    SBNativeValue bus, address, tx, length;
    uint8_t rx[32];
    SBError error;

    if ((error = sb_native_arg(call, 0, &bus)) != SB_OK ||
        (error = sb_native_arg(call, 1, &address)) != SB_OK ||
        (error = sb_native_arg(call, 2, &tx)) != SB_OK ||
        (error = sb_native_arg(call, 3, &length)) != SB_OK)
        return error;
    if (bus.type != SB_INTEGER || address.type != SB_INTEGER || tx.type != SB_STRING ||
        length.type != SB_INTEGER)
        return SB_TYPE;
    if (bus.as.integer < 0 || bus.as.integer > UINT8_MAX ||
        address.as.integer < 8 || address.as.integer > 119 ||
        length.as.integer < 0 || length.as.integer > (int32_t)sizeof(rx) ||
        (!tx.as.string.length && !length.as.integer))
        return SB_ARGUMENT;
    if (!driver || !driver->transfer)
        return SB_IO;

    error = driver->transfer(driver->user, (uint8_t)bus.as.integer, (uint8_t)address.as.integer,
                             tx.as.string.data, tx.as.string.length, rx, (uint16_t)length.as.integer);
    if (error != SB_OK)
        return error;
    return sb_native_return(call, (SBNativeValue){
        .type = SB_STRING, .as.string = {rx, (uint16_t)length.as.integer}
    });
}
