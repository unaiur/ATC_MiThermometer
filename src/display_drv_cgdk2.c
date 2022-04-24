#include <stdint.h>
#include "tl_common.h"
#include "app_config.h"
#if DEVICE_TYPE == DEVICE_CGDK2
#include "drivers.h"
#include "drivers/8258/gpio_8258.h"
#include "app.h"
#include "battery.h"
#include "i2c.h"
#include "display_drv.h"
#include "display_13seg_cell.h"
#include "display_3cell_line.h"


#define LCD_I2C_ADDR 0x7C   // I2C slave address of the LCD controller (including R/~W bit)

#define LCD_CMD_MORE 0x80

#define LCD_CMD_ADDRESS_SET_OPERATION       0 /* Address in nibbles (half byte). 5 LSB bits. MSB bit is handled using SET_IC command */

#define LCD_CMD_SET_IC_OPERATION            0x68
#define LCD_CMD_SET_IC_EXTERNAL_CLOCK       BIT(0)
#define LCD_CMD_SET_IC_RESET                BIT(1)
#define LCD_CMD_SET_IC_RAM_MSB              BIT(2)

#define LCD_CMD_DISPLAY_CONTROL_OPERATION   0x20
#define LCD_CMD_DISPLAY_CONTROL_POWER(X)    (X) /* (X <= 3) Lower power, less consumption but less visibility. Default: 2 */
#define LCD_CMD_DISPLAY_CONTROL_FRAME_INV   4 /* Frame inversion consumes less power: use it if possible */
#define LCD_CMD_DISPLAY_CONTROL_80HZ        (0 * BIT(3))
#define LCD_CMD_DISPLAY_CONTROL_71HZ        (1 * BIT(3))
#define LCD_CMD_DISPLAY_CONTROL_64HZ        (2 * BIT(3))
#define LCD_CMD_DISPLAY_CONTROL_53HZ        (3 * BIT(3)) /* Lower refresh rate, lower consumer but can be visible flicker */

#define LCD_CMD_MODE_SET_OPERATION          0x40
#define LCD_CMD_MODE_SET_1_3_BIAS           0
#define LCD_CMD_MODE_SET_1_2_BIAS           4
#define LCD_CMD_MODE_SET_DISPLAY_OFF        0
#define LCD_CMD_MODE_SET_DISPLAY_ON         8

#define LCD_CMD_BLINK_CONTROL_OPERATION     0x70
#define LCD_CMD_BLINK_OFF                   0
#define LCD_CMD_BLINK_HALF_HZ               1
#define LCD_CMD_BLINK_1_HZ                  2
#define LCD_CMD_BLINK_2_HZ                  3

#define LCD_CMD_ALL_PIXELS_OPERATION        0x7C
#define LCD_CMD_ALL_PIXELS_ON               BIT(1)
#define LCD_CMD_ALL_PIXELS_OFF              BIT(2)

#define SYM_TEMP_UNDERSCORE   NBIT(0, 4) // Used for Celsius sign
#define SYM_TEMP_MINUS        NBIT(0, 5) // Used for Fahrenheit sign
#define SYM_TEMP_COMMON       NBIT(0, 6) // Used for both C/F signs
#define SYM_BIG_ONE_HUNDRED   NBIT(2, 0) // Extra 1 to the left of top row
#define SYM_BATTERY_L1        NBIT(2, 1) // First battery stripe
#define SYM_BATTERY_L2        NBIT(2, 2) // Second battery stripe
#define SYM_BATTERY_L3        NBIT(2, 3) // Third battery stripe
#define SYM_BLUETOOTH         NBIT(2, 4) // Bluetooth sign
#define SYM_BATTERY           NBIT(2, 5) // Empty battery case
#define SYM_BATTERY_L5        NBIT(2, 6) // Fifth battery stripe
#define SYM_BATTERY_L4        NBIT(2, 7) // Forth battery stripe
#define SYM_BIG_DECIMAL_DOT   NBIT(6, 4) // Decimal dot for top row
#define SYM_SMALL_DECIMAL_DOT NBIT(9, 0) // Decimal dot for bottom row
#define SYM_PERCENTAGE        NBIT(9, 1) // Percentage sign in bottom row

// Sends an intermediate LCD command in currently open I2C transaction
static inline void send_lcd_cmd(uint8_t cmd)
{
    i2c_send_byte(LCD_CMD_MORE | cmd);
}

// Sends the last LCD command in currently open I2C transaction,
// some extra optional data and closes the I2C transaction.
static _attribute_ram_code_ void send_last_lcd_cmd(uint8_t cmd, uint8_t *dataBuf, uint32_t dataLen)
{
    i2c_send_byte(cmd);
    i2c_send_buff(dataBuf, dataLen);
    i2c_stop();
}

// Initalizes the LCD controller
static RAM uint8_t display_cmp_buff[12];
static RAM bool is_off;

void display_init(void)
{
    memset(display_buff, 0, sizeof display_buff);
    memset(display_cmp_buff, 0, sizeof display_cmp_buff);

    // Ensure than 100us has been elapsed since the IC was powered on
    pm_wait_us(100);

    // Ensure that there is no open I2C transaction
    i2c_start(LCD_I2C_ADDR);
    i2c_abort();

     // Send reset command
    i2c_write_tx_1byte(LCD_I2C_ADDR, LCD_CMD_SET_IC_OPERATION | LCD_CMD_SET_IC_RESET);

    // Configure and clean the display
    i2c_start(LCD_I2C_ADDR);
    send_lcd_cmd(LCD_CMD_DISPLAY_CONTROL_OPERATION
            | LCD_CMD_DISPLAY_CONTROL_FRAME_INV
            | LCD_CMD_DISPLAY_CONTROL_64HZ
            | LCD_CMD_DISPLAY_CONTROL_POWER(1));
    send_lcd_cmd(LCD_CMD_MODE_SET_OPERATION | LCD_CMD_MODE_SET_DISPLAY_ON);
    send_last_lcd_cmd(LCD_CMD_ADDRESS_SET_OPERATION, display_buff, sizeof display_buff);
}

void display_power_toggle()
{
    i2c_start(LCD_I2C_ADDR);
    u8 cmd_arg = is_off ? LCD_CMD_MODE_SET_DISPLAY_ON : LCD_CMD_MODE_SET_DISPLAY_OFF;
    send_last_lcd_cmd(LCD_CMD_MODE_SET_OPERATION | cmd_arg, 0, 0);
    is_off = !is_off;
}

// Sends the modified regions of the display buffer to the LCD controller
_attribute_ram_code_ void display_async_refresh()
{
    if (is_off) return;
    for (int i = 0; i < sizeof display_buff; ++i) {
        if (display_buff[i] != display_cmp_buff[i]) {
            int j = sizeof display_buff;
            while (j > i && display_buff[j-1] == display_cmp_buff[j-1]) {
                --j;
            }
            i2c_start(LCD_I2C_ADDR);
            send_last_lcd_cmd(LCD_CMD_ADDRESS_SET_OPERATION + i * 2, display_buff + i, j - i);
            memcpy(display_cmp_buff + i, display_buff + i, j - i);
            return;
        }
    }
}

void display_sync_refresh(void)
{
    display_async_refresh();
}

_attribute_ram_code_ void display_temp_symbol(uint8_t symbol)
{
    display_render_bit(SYM_TEMP_COMMON, symbol & 0x20);
    display_render_bit(SYM_TEMP_MINUS, symbol & 0x40);
    display_render_bit(SYM_TEMP_UNDERSCORE, symbol & 0x80);
}

_attribute_ram_code_ void display_battery_symbol(bool state)
{
    display_render_bit(SYM_BATTERY, state);
    display_render_bit(SYM_BATTERY_L1, state && battery_level >= 16);
    display_render_bit(SYM_BATTERY_L2, state && battery_level >= 33);
    display_render_bit(SYM_BATTERY_L3, state && battery_level >= 49);
    display_render_bit(SYM_BATTERY_L4, state && battery_level >= 67);
    display_render_bit(SYM_BATTERY_L5, state && battery_level >= 83);
}

_attribute_ram_code_ void display_ble_symbol(bool state)
{
    display_render_bit(SYM_BLUETOOTH, state);
}

static _attribute_ram_code_ void render_top_cell(int num_cell, enum display_char c)
{
    const static struct display_13seg_cell cells[] = {
	    {
            .segment_bits = {
                [SEG_1L] = NBIT(3, 0),
                [SEG_2L] = NBIT(3, 4),
                [SEG_3L] = NBIT(3, 5),
                [SEG_4L] = NBIT(3, 6),
                [SEG_5L] = NBIT(3, 7),
                [SEG_1M] = NBIT(3, 1),
                [SEG_3M] = NBIT(3, 2),
                [SEG_5M] = NBIT(3, 3),
                [SEG_1R] = NBIT(4, 4),
                [SEG_2R] = NBIT(4, 5),
                [SEG_3R] = NBIT(4, 6),
                [SEG_4R] = NBIT(4, 7),
                [SEG_5R] = NBIT(6, 7),
            }
        }, {
            .segment_bits = {
                [SEG_1L] = NBIT(5, 4),
                [SEG_2L] = NBIT(4, 0),
                [SEG_3L] = NBIT(4, 1),
                [SEG_4L] = NBIT(4, 2),
                [SEG_5L] = NBIT(4, 3),
                [SEG_1M] = NBIT(5, 5),
                [SEG_3M] = NBIT(5, 6),
                [SEG_5M] = NBIT(5, 7),
                [SEG_1R] = NBIT(5, 0),
                [SEG_2R] = NBIT(5, 1),
                [SEG_3R] = NBIT(5, 2),
                [SEG_4R] = NBIT(5, 3),
                [SEG_5R] = NBIT(6, 6),
            }
        }, {
            .segment_bits = {
                [SEG_1L] = NBIT(1, 0),
                [SEG_2L] = NBIT(1, 1),
                [SEG_3L] = NBIT(1, 2),
                [SEG_4L] = NBIT(1, 3),
                [SEG_5L] = NBIT(1, 7),
                [SEG_1M] = NBIT(1, 4),
                [SEG_3M] = NBIT(1, 5),
                [SEG_5M] = NBIT(1, 6),
                [SEG_1R] = NBIT(6, 5),
                [SEG_2R] = NBIT(0, 0),
                [SEG_3R] = NBIT(0, 1),
                [SEG_4R] = NBIT(0, 2),
                [SEG_5R] = NBIT(0, 3),
            }
        }
    };
    display_render_13seg_char(&cells[num_cell], c);
}

static _attribute_ram_code_ void render_top_decimal_dot(bool on)
{
    display_render_bit(SYM_BIG_DECIMAL_DOT, on);
}

static _attribute_ram_code_ void render_top_leftmost_one(bool on)
{
    display_render_bit(SYM_BIG_ONE_HUNDRED, on);
}

_attribute_ram_code_ void display_big_number_x10(int16_t number)
{
    const static struct display_3cell_line line = {
        .render_cell = render_top_cell,
        .render_leftmost_one = render_top_leftmost_one,
        .render_decimal_dot = render_top_decimal_dot,
    };
    display_render_3cell_number(&line, number);
}

static _attribute_ram_code_ void render_bottom_cell(int num_cell, enum display_char c)
{
    const static struct display_13seg_cell cells[] = {
        {
            .segment_bits = {
                [SEG_1L] = NBIT(7, 4),
                [SEG_2L] = NBIT(6, 0),
                [SEG_3L] = NBIT(6, 1),
                [SEG_4L] = NBIT(6, 2),
                [SEG_5L] = NBIT(6, 3),
                [SEG_1M] = NBIT(7, 5),
                [SEG_3M] = NBIT(7, 6),
                [SEG_5M] = NBIT(7, 7),
                [SEG_1R] = NBIT(7, 0),
                [SEG_2R] = NBIT(7, 1),
                [SEG_3R] = NBIT(7, 2),
                [SEG_4R] = NBIT(7, 3),
                [SEG_5R] = NBIT(9, 3),
            }
        }, {
            .segment_bits = {
                [SEG_1L] = NBIT(8, 0),
                [SEG_2L] = NBIT(8, 4),
                [SEG_3L] = NBIT(8, 5),
                [SEG_4L] = NBIT(8, 6),
                [SEG_5L] = NBIT(8, 7),
                [SEG_1M] = NBIT(8, 1),
                [SEG_3M] = NBIT(8, 2),
                [SEG_5M] = NBIT(8, 3),
                [SEG_1R] = NBIT(9, 4),
                [SEG_2R] = NBIT(9, 5),
                [SEG_3R] = NBIT(9, 6),
                [SEG_4R] = NBIT(9, 7),
                [SEG_5R] = NBIT(9, 2),
            }
        }, {
            .segment_bits = {
                [SEG_1L] = NBIT(10, 0),
                [SEG_2L] = NBIT(10, 4),
                [SEG_3L] = NBIT(10, 5),
                [SEG_4L] = NBIT(10, 6),
                [SEG_5L] = NBIT(10, 7),
                [SEG_1M] = NBIT(10, 1),
                [SEG_3M] = NBIT(10, 2),
                [SEG_5M] = NBIT(10, 3),
                [SEG_1R] = NBIT(11, 4),
                [SEG_2R] = NBIT(11, 5),
                [SEG_3R] = NBIT(11, 6),
                [SEG_4R] = NBIT(11, 7),
                [SEG_5R] = NBIT(0, 7),
            }
        }
    };
    display_render_13seg_char(&cells[num_cell], c);
}

static _attribute_ram_code_ void render_bottom_decimal_dot(bool on)
{
    display_render_bit(SYM_SMALL_DECIMAL_DOT, on);
}

_attribute_ram_code_ void display_small_number_x10(int16_t number, bool percent)
{
    const static struct display_3cell_line line = {
        .render_cell = render_bottom_cell,
        .render_decimal_dot = render_bottom_decimal_dot,
        .render_leftmost_one = NULL,
    };
    display_render_3cell_number(&line, number);
    display_render_bit(SYM_PERCENTAGE, percent);
}

#endif // DEVICE_TYPE == DEVICE_CGDK2
