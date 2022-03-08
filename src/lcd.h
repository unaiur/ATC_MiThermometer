#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

// SUPPORTED DISPLAY TYPES
#define EPD 1 /* Electronic Paper Display */
#define LCD 2 /* Liquid Cristal Display */

// DISPLAY CONFIGURATION
#if DEVICE_TYPE == DEVICE_CGG1
#define DISPLAY_TYPE           EPD
#define DISPLAY_BUFF_LEN       18

#elif DEVICE_TYPE == DEVICE_CGDK2
#define DISPLAY_TYPE           LCD
#define DISPLAY_BUFF_LEN       12

#else
#error "Set DEVICE_TYPE!"
#endif

/* CGG1 no symbol 'smiley' ! */
#define SMILE_HAPPY 5 		// "(^-^)" happy
#define SMILE_SAD   6 		// "(-^-)" sad
#define TMP_SYM_C	0xA0	// "°C"
#define TMP_SYM_F	0x60	// "°F"

#define B14_I2C_ADDR		0x3C
#define B16_I2C_ADDR		0	// UART
#define B19_I2C_ADDR		0x3E
#define CGDK2_I2C_ADDR		0x3E

extern uint8_t lcd_i2c_addr;


void init_lcd();
void update_lcd();
/* 0x00 = "  "
 * 0x20 = "°Г"
 * 0x40 = " -"
 * 0x60 = "°F"
 * 0x80 = " _"
 * 0xA0 = "°C"
 * 0xC0 = " ="
 * 0xE0 = "°E"
 * Warning: MHO-C401 Symbols: "%", "°Г", "(  )", "." have one control bit! */
void show_temp_symbol(uint8_t symbol);
void show_battery_symbol(bool state);
void show_big_number_x10(int16_t number); // x0.1, (-995..19995), point auto: -99 .. -9.9 .. 199.9 .. 1999
void show_small_number_x10(int16_t number, bool percent); // -9 .. 99
void show_ble_symbol(bool state);
#if	USE_CLOCK
void show_clock(void);
#endif

extern uint8_t display_buff[DISPLAY_BUFF_LEN];

#if DISPLAY_TYPE == EPD
extern uint8_t stage_lcd;
int task_lcd(void);
#endif
