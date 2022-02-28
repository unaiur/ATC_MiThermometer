#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

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
/* ==================
 * LYWSD03MMC:
 * 0 = "     " off,
 * 1 = " ^_^ "
 * 2 = " -^- "
 * 3 = " ooo "
 * 4 = "(   )"
 * 5 = "(^_^)" happy
 * 6 = "(-^-)" sad
 * 7 = "(ooo)"
 * -------------------
 * MHO-C401:
 * 0 = "   " off,
 * 1 = " o "
 * 2 = "o^o"
 * 3 = "o-o"
 * 4 = "oVo"
 * 5 = "vVv" happy
 * 6 = "^-^" sad
 * 7 = "oOo" */
void show_smiley(uint8_t state);
void show_battery_symbol(bool state);
void show_big_number_x10(int16_t number); // x0.1, (-995..19995), point auto: -99 .. -9.9 .. 199.9 .. 1999
void show_ble_symbol(bool state);
#if	USE_CLOCK
void show_clock(void);
#endif

#if DEVICE_TYPE == DEVICE_MHO_C401
extern uint8_t display_buff[18];
extern uint8_t stage_lcd;
void show_small_number(int16_t number, bool percent); // -9 .. 99
int task_lcd(void);
#elif DEVICE_TYPE == DEVICE_CGG1
extern uint8_t display_buff[18];
extern uint8_t stage_lcd;
void show_small_number_x10(int16_t number, bool percent); // -9 .. 99
int task_lcd(void);
void show_batt_cgg1(void);
#elif DEVICE_TYPE == DEVICE_LYWSD03MMC
extern uint8_t display_buff[6];
void show_small_number(int16_t number, bool percent); // -9 .. 99
#elif DEVICE_TYPE == DEVICE_CGDK2
extern uint8_t display_buff[12];
void show_batt_cgdk2(void);
void show_small_number_x10(int16_t number, bool percent); // -9 .. 99
#elif DEVICE_TYPE == DEVICE_CGDK22
extern uint8_t display_buff[18];
void show_batt_cgdk22(void);
void show_small_number_x10(int16_t number, bool percent); // -9 .. 99
#else
#error "Set DEVICE_TYPE!"
#endif
