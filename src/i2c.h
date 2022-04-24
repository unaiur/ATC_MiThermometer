#pragma once

#include <stdint.h>

// Check if there is a I2C slave listening to the given address
bool i2c_check_address(int address);

// Start, stop and abort an I2C transaction
void i2c_start(uint8_t address);
void i2c_stop(void);
void i2c_abort(void);

// Send a byte or a buffer in an open I2C transaction
void i2c_send_byte(uint8_t c);
void i2c_send_buff(const uint8_t *buff, unsigned len);

// Perform a full write transaction of 1 or 2 bytes
void i2c_write_tx_1byte(uint8_t address, uint8_t data);
void i2c_write_tx_1word(uint8_t address, uint16_t data);
