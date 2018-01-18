/*

I2C MODULE

Copyright (C) 2017-2018 by Xose Pérez <xose dot perez at gmail dot com>

*/

#if I2C_SUPPORT

unsigned int _i2c_locked[16] = {0};

#if I2C_USE_BRZO
#include "brzo_i2c.h"
unsigned long _i2c_scl_frequency = 0;
#else
#include "Wire.h"
#endif

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------

int _i2cClearbus(int sda, int scl) {

    #if defined(TWCR) && defined(TWEN)
        // Disable the Atmel 2-Wire interface so we can control the SDA and SCL pins directly
        TWCR &= ~(_BV(TWEN));
    #endif

    // Make SDA (data) and SCL (clock) pins inputs with pullup
    pinMode(sda, INPUT_PULLUP);
    pinMode(scl, INPUT_PULLUP);

    delay(2500);
    // Wait 2.5 secs. This is strictly only necessary on the first power
    // up of the DS3231 module to allow it to initialize properly,
    // but is also assists in reliable programming of FioV3 boards as it gives the
    // IDE a chance to start uploaded the program
    // before existing sketch confuses the IDE by sending Serial data.

    // If it is held low the device cannot become the I2C master
    // I2C bus error. Could not clear SCL clock line held low
    boolean scl_low = (digitalRead(scl) == LOW);
    if (scl_low) return 1;


    boolean sda_low = (digitalRead(sda) == LOW);
    int clockCount = 20; // > 2x9 clock

    // While SDA is low for at most 20 cycles
    while (sda_low && (clockCount > 0)) {

        clockCount--;

        // Note: I2C bus is open collector so do NOT drive SCL or SDA high
        pinMode(scl, INPUT);        // release SCL pullup so that when made output it will be LOW
        pinMode(scl, OUTPUT);       // then clock SCL Low
        delayMicroseconds(10);      // for >5uS
        pinMode(scl, INPUT);        // release SCL LOW
        pinMode(scl, INPUT_PULLUP); // turn on pullup resistors again
                                    // do not force high as slave may be holding it low for clock stretching

        delayMicroseconds(10);      // The >5uS is so that even the slowest I2C devices are handled

        //  loop waiting for SCL to become high only wait 2sec
        scl_low = (digitalRead(scl) == LOW);
        int counter = 20;
        while (scl_low && (counter > 0)) {
            counter--;
            delay(100);
            scl_low = (digitalRead(scl) == LOW);
        }

        // If still low after 2 sec error
        // I2C bus error. Could not clear. SCL clock line held low by slave clock stretch for >2sec
        if (scl_low) return 2;

        sda_low = (digitalRead(sda) == LOW); //   and check SDA input again and loop

    }

    // If still low
    // I2C bus error. Could not clear. SDA data line held low
    if (sda_low) return 3;

    // Pull SDA line low for "start" or "repeated start"
    pinMode(sda, INPUT);        // remove pullup
    pinMode(sda, OUTPUT);       // and then make it LOW i.e. send an I2C Start or Repeated start control

    // When there is only one I2C master a "start" or "repeat start" has the same function as a "stop" and clears the bus
    // A Repeat Start is a Start occurring after a Start with no intervening Stop.

    delayMicroseconds(10);      // wait >5uS
    pinMode(sda, INPUT);        // remove output low
    pinMode(sda, INPUT_PULLUP); // and make SDA high i.e. send I2C STOP control.

    delayMicroseconds(10);      // wait >5uS
    pinMode(sda, INPUT);        // and reset pins as tri-state inputs which is the default state on reset
    pinMode(scl, INPUT);

    // Everything OK
    return 0;

}

// ---------------------------------------------------------------------
// I2C API
// ---------------------------------------------------------------------

void i2c_write_uint8(uint8_t address, uint8_t reg, uint8_t value) {
    Wire.beginTransmission((uint8_t) address);
    Wire.write((uint8_t) reg);
    Wire.write((uint8_t) value);
    Wire.endTransmission();
}

uint8_t i2c_read_uint8(uint8_t address, uint8_t reg) {
    uint8_t value;
    Wire.beginTransmission((uint8_t) address);
    Wire.write((uint8_t) reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)address, (uint8_t) 1);
    value = Wire.read();
    Wire.endTransmission();
    return value;
};

uint16_t i2c_read_uint16(uint8_t address, uint8_t reg) {
    uint16_t value;
    Wire.beginTransmission((uint8_t) address);
    Wire.write((uint8_t) reg);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t) address, (uint8_t) 2);
    value = (Wire.read() << 8) | Wire.read();
    Wire.endTransmission();
    return value;
};

uint16_t i2c_read_uint16_le(uint8_t address, uint8_t reg) {
    uint16_t temp = i2c_read_uint16(address, reg);
    return (temp >> 8) | (temp << 8);
};

int16_t i2c_read_int16(uint8_t address, uint8_t reg) {
    return (int16_t) i2c_read_uint16(address, reg);
};

int16_t i2c_read_int16_le(uint8_t address, uint8_t reg) {
    return (int16_t) i2c_read_uint16_le(address, reg);
};

// -----------------------------------------------------------------------------
// Utils
// -----------------------------------------------------------------------------

void i2cClearBus() {
    unsigned char sda = getSetting("i2cSDA", I2C_SDA_PIN).toInt();
    unsigned char scl = getSetting("i2cSCL", I2C_SCL_PIN).toInt();
    DEBUG_MSG_P(PSTR("[I2C] Clear bus (response: %d)\n"), _i2cClearbus(sda, scl));
}

bool i2cCheck(unsigned char address) {
    #if I2C_USE_BRZO
        brzo_i2c_start_transaction(address, _i2c_scl_frequency);
        brzo_i2c_ACK_polling(1000);
        return brzo_i2c_end_transaction();
    #else
        Wire.beginTransmission(address);
        return Wire.endTransmission();
    #endif
}

bool i2cGetLock(unsigned char address) {
    unsigned char index = address / 8;
    unsigned char mask = 1 << (address % 8);
    if (_i2c_locked[index] & mask) return false;
    _i2c_locked[index] = _i2c_locked[index] | mask;
    DEBUG_MSG_P(PSTR("[I2C] Address 0x%02X locked\n"), address);
    return true;
}

bool i2cReleaseLock(unsigned char address) {
    unsigned char index = address / 8;
    unsigned char mask = 1 << (address % 8);
    if (_i2c_locked[index] & mask) {
        _i2c_locked[index] = _i2c_locked[index] & ~mask;
        return true;
    }
    return false;
}

unsigned char i2cFind(size_t size, unsigned char * addresses, unsigned char &start) {
    for (unsigned char i=start; i<size; i++) {
        if (i2cCheck(addresses[i]) == 0) {
            start = i;
            return addresses[i];
        }
    }
    return 0;
}

unsigned char i2cFind(size_t size, unsigned char * addresses) {
    unsigned char start = 0;
    return i2cFind(size, addresses, start);
}

unsigned char i2cFindAndLock(size_t size, unsigned char * addresses) {
    unsigned char start = 0;
    unsigned char address = 0;
    while (address = i2cFind(size, addresses, start)) {
        if (i2cGetLock(address)) break;
        start++;
    }
    return address;
}

void i2cScan() {
    unsigned char nDevices = 0;
    for (unsigned char address = 1; address < 127; address++) {
        unsigned char error = i2cCheck(address);
        if (error == 0) {
            DEBUG_MSG_P(PSTR("[I2C] Device found at address 0x%02X\n"), address);
            nDevices++;
        }
    }
    if (nDevices == 0) DEBUG_MSG_P(PSTR("[I2C] No devices found\n"));
}

void i2cSetup() {

    unsigned char sda = getSetting("i2cSDA", I2C_SDA_PIN).toInt();
    unsigned char scl = getSetting("i2cSCL", I2C_SCL_PIN).toInt();

    #if I2C_USE_BRZO
        unsigned long cst = getSetting("i2cCST", I2C_CLOCK_STRETCH_TIME).toInt();
        _i2c_scl_frequency = getSetting("i2cFreq", I2C_SCL_FREQUENCY).toInt();
        brzo_i2c_setup(sda, scl, cst);
    #else
        Wire.begin(sda, scl);
    #endif

    DEBUG_MSG_P(PSTR("[I2C] Using GPIO%u for SDA and GPIO%u for SCL\n"), sda, scl);

}

#endif
