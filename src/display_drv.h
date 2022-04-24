#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "display_config.h"

#define NBIT(BYTE, BIT) ((BYTE)*8+(BIT))
#define TMP_SYM_C	0xA0	// "°C"
#define TMP_SYM_F	0x60	// "°F"
#define TMP_SYM_EQ	0xC0	// "="

extern uint8_t display_buff[DISPLAY_BUFF_LEN];
void display_render_bit(uint8_t bit, bool on);

void display_init();
void display_async_refresh();
void display_sync_refresh();

/* 0x00 = "  "
 * 0x20 = "°Г"
 * 0x40 = " -"
 * 0x60 = "°F"
 * 0x80 = " _"
 * 0xA0 = "°C"
 * 0xC0 = " ="
 * 0xE0 = "°E"
 */
void display_temp_symbol(uint8_t symbol);
void display_battery_symbol(bool state);
void display_ble_symbol(bool state);
void display_big_number_x10(int16_t number);
void display_small_number_x10(int16_t number, bool percent);
