#pragma once

#include <stdint.h>

void init_i2c();
void i2c_reinit_after_deep_sleep();
int scan_i2c_addr(int address);

void i2c_send_byte(uint8_t c, int last);
void i2c_send_buff(const uint8_t *buff, unsigned len, int last);

