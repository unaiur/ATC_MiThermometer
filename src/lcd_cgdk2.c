#include <stdint.h>
#include "tl_common.h"
#include "app_config.h"
#if DEVICE_TYPE == DEVICE_CGDK2
#include "drivers.h"
#include "drivers/8258/gpio_8258.h"
#include "app.h"
#include "battery.h"
#include "i2c.h"
#include "lcd.h"


RAM uint8_t display_buff[12];
static RAM uint8_t display_cmp_buff[12];

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

#define NBIT(BYTE, BIT) ((BYTE)*8+(BIT))

// This LCD contains 6 cells in two rows: a big row on top and a small one bellow.
//
// Left and middle cells are bigger than the right cell in both rows and
// we can display a decimal dot between middle and right cells.
//
// Also, you can display a 1 to the left of the top row, allowing to show numbers
// up to 199.9 or 1999 and you can display several symbols at the right (F, C, =, -)
//
// The bottom row on the other hand can display a percentage sign at the right.
enum cell_t {
    CELL_A, // Top Left
    CELL_B,
    CELL_C,
    CELL_X,
    CELL_Y,
    CELL_Z, // Bottom right
    NUM_CELLS
};

// These are all the characters that we can render in a cell
enum lcd_char_t {
    CHR_0,
    CHR_1,
    CHR_2,
    CHR_3,
    CHR_4,
    CHR_5,
    CHR_6,
    CHR_7,
    CHR_8,
    CHR_9,
    CHR_L,
    CHR_o,
    CHR_H,
    CHR_i,
    CHR_MINUS,
    CHR_SPACE,
    NUM_CHARS
};


// There are the segments of a cell, following SEG_<ROW><COL> format, where
//   ROW is a number from 1 to 5. 1 is the bottom, 3 the middle, 5 the top
//   COL is either L(eft), M(iddle) or R(ight)
enum cell_segment_t {
    SEG_1L,
    SEG_2L,
    SEG_3L,
    SEG_4L,
    SEG_5L,
    SEG_1M,
    SEG_3M,
    SEG_5M,
    SEG_1R,
    SEG_2R,
    SEG_3R,
    SEG_4R,
    SEG_5R,
    NUM_CELL_SEGMENTS
};

// Segment bitmaps associated with each character
static const uint16_t char_segment_bitmap[NUM_CHARS] = {
    [CHR_0] = BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_2R) | BIT(SEG_3L) | BIT(SEG_3R) | BIT(SEG_4L) | BIT(SEG_4R)
        | BIT(SEG_5M),
    [CHR_1] = BIT(SEG_1R) | BIT(SEG_2R) | BIT(SEG_3R) | BIT(SEG_4R) | BIT(SEG_5R),
    [CHR_2] = BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_3M) | BIT(SEG_4R) | BIT(SEG_5M),
    [CHR_3] = BIT(SEG_1M) | BIT(SEG_2R) | BIT(SEG_3M) | BIT(SEG_4R) | BIT(SEG_5M),
    [CHR_4] = BIT(SEG_1R) | BIT(SEG_2R) | BIT(SEG_3M) | BIT(SEG_3R) | BIT(SEG_4L) | BIT(SEG_4R) | BIT(SEG_5L),
    [CHR_5] = BIT(SEG_1M) | BIT(SEG_2R) | BIT(SEG_3L) | BIT(SEG_3M) | BIT(SEG_4L) | BIT(SEG_5L) | BIT(SEG_5M),
    [CHR_6] = BIT(SEG_1M) | BIT(SEG_2R) | BIT(SEG_2L) | BIT(SEG_3L) | BIT(SEG_3M) | BIT(SEG_4L) | BIT(SEG_5M),
    [CHR_7] = BIT(SEG_1R) | BIT(SEG_2R) | BIT(SEG_3R) | BIT(SEG_4R) | BIT(SEG_5M) | BIT(SEG_5R),
    [CHR_8] = BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_2R) | BIT(SEG_3M) | BIT(SEG_4L) | BIT(SEG_4R) | BIT(SEG_5M),
    [CHR_9] = BIT(SEG_1R) | BIT(SEG_2R) | BIT(SEG_3M) | BIT(SEG_3R) | BIT(SEG_4L) | BIT(SEG_4R) | BIT(SEG_5M),
    [CHR_L] = BIT(SEG_1R) | BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_3L) | BIT(SEG_4L),
    [CHR_o] = BIT(SEG_1M) | BIT(SEG_2L) | BIT(SEG_2R) | BIT(SEG_3M),
    [CHR_H] = BIT(SEG_1L) | BIT(SEG_1R) | BIT(SEG_2L) | BIT(SEG_2R) | BIT(SEG_3L) | BIT(SEG_3M) | BIT(SEG_3R)
            | BIT(SEG_4L) | BIT(SEG_4R) | BIT(SEG_5L) | BIT(SEG_5R),
    [CHR_i] = BIT(SEG_1L) | BIT(SEG_2L) | BIT(SEG_5L),
    [CHR_MINUS] = BIT(SEG_3M),
    [CHR_SPACE] = 0,
};

// Positions of each cell segment in the display buffer
static const uint8_t cell_segment_bits[NUM_CELLS][NUM_CELL_SEGMENTS] = {
    [CELL_A] = {
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
    },
    [CELL_B] = {
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
    },
    [CELL_C] = {
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
    },
    [CELL_X] = {
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
    },
    [CELL_Y] = {
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
    },
    [CELL_Z] = {
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
    },
};

// Positions of other symbols in the LCD
enum symbols_t {
    SYM_TEMP_UNDERSCORE  = NBIT(0, 4), // Used for Celsius sign
    SYM_TEMP_MINUS       = NBIT(0, 5), // Used for Fahrenheit sign
    SYM_TEMP_COMMON      = NBIT(0, 6), // Used for both C/F signs
    SYM_BIG_ONE_HUNDRED  = NBIT(2, 0), // Extra 1 to the left of top row
    SYM_BATTERY_L1       = NBIT(2, 1), // First battery stripe
    SYM_BATTERY_L2       = NBIT(2, 2), // Second battery stripe
    SYM_BATTERY_L3       = NBIT(2, 3), // Third battery stripe
    SYM_BLUETOOTH        = NBIT(2, 4), // Bluetooth sign
    SYM_BATTERY          = NBIT(2, 5), // Empty battery case
    SYM_BATTERY_L5       = NBIT(2, 6), // Fifth battery stripe
    SYM_BATTERY_L4       = NBIT(2, 7), // Forth battery stripe
    SYM_BIG_DECIMAL_DOT  = NBIT(6, 4), // Decimal dot for top row
    SYM_SMALL_DECIMAL_DOT= NBIT(9, 0), // Decimal dot for bottom row
    SYM_PERCENTAGE       = NBIT(9, 1), // Percentage sign in bottom row
};

// Starts a I2C transaction, sending the start symbol and the slave address
static _attribute_ram_code_ void i2c_start()
{
    if ((reg_clk_en0 & FLD_CLK0_I2C_EN) == 0) {
        init_i2c();
    }
    reg_i2c_id = LCD_I2C_ADDR;
    reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID;
    while (reg_i2c_status & FLD_I2C_CMD_BUSY);
}

// Sends one but in the currently open I2C transaction
static _attribute_ram_code_ void i2c_send(uint8_t byte)
{
    reg_i2c_do = byte;
    reg_i2c_ctrl = FLD_I2C_CMD_DO;
    while (reg_i2c_status & FLD_I2C_CMD_BUSY);
}

// Finishes current I2C transaction
static _attribute_ram_code_ void i2c_stop()
{
    reg_i2c_ctrl = FLD_I2C_CMD_STOP;
    while (reg_i2c_status & FLD_I2C_CMD_BUSY);
}

// Sends a LCD command in currently open I2C transaction
static inline void send_lcd_cmd(uint8_t cmd)
{
    i2c_send(LCD_CMD_MORE | cmd);
}

// Sends the last LCD command in currently open I2C transaction,
// some extra optional data and closes the I2C transaction.
static _attribute_ram_code_ void send_last_lcd_cmd(uint8_t cmd, uint8_t *dataBuf, uint32_t dataLen)
{
    i2c_send(cmd);
    while (dataLen--) {
        i2c_send(*dataBuf++);
    }
    i2c_stop();
}

// Initalizes the LCD controller
void init_lcd(void)
{
    memset(display_buff, 0, sizeof display_buff);
    memset(display_cmp_buff, 0, sizeof display_cmp_buff);

    // Ensure than 100us has been elapsed since the IC was powered on
    pm_wait_us(100);

    // Ensure that there is no open I2C transaction
    i2c_start();
    i2c_stop();

     // Send reset command
    i2c_start();
    send_last_lcd_cmd(LCD_CMD_SET_IC_OPERATION | LCD_CMD_SET_IC_RESET, 0, 0);

    // Configure and clean the display
    i2c_start();
    send_lcd_cmd(LCD_CMD_DISPLAY_CONTROL_OPERATION
            | LCD_CMD_DISPLAY_CONTROL_FRAME_INV
            | LCD_CMD_DISPLAY_CONTROL_64HZ
            | LCD_CMD_DISPLAY_CONTROL_POWER(1));
    send_lcd_cmd(LCD_CMD_MODE_SET_OPERATION | LCD_CMD_MODE_SET_DISPLAY_ON);
    send_last_lcd_cmd(LCD_CMD_ADDRESS_SET_OPERATION, display_buff, sizeof display_buff);
}

// Sends the modified regions of the display buffer to the LCD controller
_attribute_ram_code_ void update_lcd()
{
    for (int i = 0; i < sizeof display_buff; ++i) {
        if (display_buff[i] != display_cmp_buff[i]) {
            int j = sizeof display_buff;
            while (j > i && display_buff[j-1] == display_cmp_buff[j-1]) {
                --j;
            }
            i2c_start();
            send_last_lcd_cmd(LCD_CMD_ADDRESS_SET_OPERATION + i * 2, display_buff + i, j - i);
            memcpy(display_cmp_buff + i, display_buff + i, j - i);
            return;
        }
    }
}


// Sets a bit of the LCD buffer on or off
static _attribute_ram_code_ void set_lcd_bit(uint8_t bit, bool on)
{
    uint8_t byte = bit / 8;
    if (byte < sizeof display_buff) {
        if (on) {
            display_buff[byte] |= BIT(bit % 8);
        } else {
            display_buff[byte] &= ~BIT(bit % 8);
        }
    }
}

// Draws a character in a given cell
static _attribute_ram_code_ void draw_cell(enum cell_t cell, enum lcd_char_t c)
{
    if (cell < NUM_CELLS && c < NUM_CHARS) {
        uint16_t bitmap = char_segment_bitmap[c];
        const uint8_t *segment_bits = cell_segment_bits[cell];
        for (int segment = NUM_CELL_SEGMENTS; segment; --segment) {
            set_lcd_bit(*segment_bits++, bitmap & 1);
            bitmap >>= 1;
        }
    }
}

/* 0x00 = "  "
 * 0x20 = "°Г"
 * 0x40 = " -"
 * 0x60 = "°F"
 * 0x80 = " _"
 * 0xA0 = "°C"
 * 0xC0 = " ="
 * 0xE0 = "°E" */
_attribute_ram_code_ void show_temp_symbol(uint8_t symbol)
{
    set_lcd_bit(SYM_TEMP_COMMON, symbol & 0x20);
    set_lcd_bit(SYM_TEMP_MINUS, symbol & 0x40);
    set_lcd_bit(SYM_TEMP_UNDERSCORE, symbol & 0x80);
}

/* 0 = "     " off,
 * 1 = " ^-^ "
 * 2 = " -^- "
 * 3 = " ooo "
 * 4 = "(   )"
 * 5 = "(^-^)" happy
 * 6 = "(-^-)" sad
 * 7 = "(ooo)" */
_attribute_ram_code_ void show_smiley(uint8_t state)
{
    // No smiley in this LCD
}

_attribute_ram_code_ void show_ble_symbol(bool state)
{
    set_lcd_bit(SYM_BLUETOOTH, state);
}

_attribute_ram_code_ void show_battery_symbol(bool state)
{
    set_lcd_bit(SYM_BATTERY, state);
    set_lcd_bit(SYM_BATTERY_L1, state && battery_level >= 16);
    set_lcd_bit(SYM_BATTERY_L2, state && battery_level >= 33);
    set_lcd_bit(SYM_BATTERY_L3, state && battery_level >= 49);
    set_lcd_bit(SYM_BATTERY_L4, state && battery_level >= 67);
    set_lcd_bit(SYM_BATTERY_L5, state && battery_level >= 83);
}

/** Draws a number expressed in tenths in the top or bottom rows
 *  @param where is the left cell of the chosen row
 *  @param number is the value to write
 *  @param dot_symbol is the dot symbol to use
 *  @param hundreds_symbol is the hundreds symbol to use (0 for the bottom row)
 *
 * If the number is out of bound (lower than -994 or larger than 19994 or 9994),
 * it shows Lo and Hi respectively.
 *
 * Examples:
 *  * 19995 shows  Hi
 *  * 19994 shows 199.9 (or Hi in the bottom row)
 *  *  9994 shows  99 9
 *  *   999 shows  99.9
 *  *   -99 shows  -9.9
 *  *  -994 shows  -9 9
 *  *  -995 shows  Lo
 */
static _attribute_ram_code_ void draw_number(enum cell_t where, int16_t number, int dot_symbol, int hundreds_symbol)
{
    int max_value = (hundreds_symbol ? 1999 : 999);
    bool show_decimal = (number >= -99 && number <= max_value);
    if (!show_decimal) {
        // Divide by ten and round: first add 5 to absolute value
        number += (number < 0 ? -5 : 5);
        // Then divide by then (which always rounds towards zero
        number /= 10;
    }
    set_lcd_bit(dot_symbol, show_decimal);
    if (hundreds_symbol) {
        set_lcd_bit(hundreds_symbol, number >= 1000 && number <= 1999);
    }
    if (number < -99) {
        draw_cell(where++, CHR_L);
        draw_cell(where++, CHR_o);
        draw_cell(where, CHR_SPACE);
    } else if (number > max_value) {
        draw_cell(where++, CHR_H);
        draw_cell(where++, CHR_i);
        draw_cell(where, CHR_SPACE);
    } else if (number < 0) {
        number = -number;
        draw_cell(where+2, CHR_0 + number % 10);
        number /= 10;
        draw_cell(where+1, CHR_0 + number);
        draw_cell(where, CHR_MINUS);
    } else {
        draw_cell(where+2, CHR_0 + number % 10);
        number /= 10;
        draw_cell(where+1, CHR_0 + number % 10);
        number /= 10;
        if (number == 0) {
            draw_cell(where, CHR_SPACE);
        } else {
            draw_cell(where, CHR_0 + number % 10);
        }
    }
}

_attribute_ram_code_ void show_big_number_x10(int16_t number)
{
    draw_number(CELL_A, number, SYM_BIG_DECIMAL_DOT, SYM_BIG_ONE_HUNDRED);
}

_attribute_ram_code_ void show_small_number_x10(int16_t number, bool percent)
{
    draw_number(CELL_X, number, SYM_SMALL_DECIMAL_DOT, 0);
    set_lcd_bit(SYM_PERCENTAGE, percent);
}


_attribute_ram_code_ void show_batt_cgdk2(void)
{
    uint16_t battery_level = 0;
    if(measured_data.battery_mv > MIN_VBAT_MV) {
        battery_level = ((measured_data.battery_mv - MIN_VBAT_MV)*10)/((MAX_VBAT_MV - MIN_VBAT_MV)/100);
        if (battery_level > 999) {
            battery_level = 999;
        }
    }
    show_small_number_x10(battery_level, false);
}

#if USE_CLOCK
_attribute_ram_code_ void show_clock(void)
{
    uint32_t tmp = utc_time_sec / 60;
    uint32_t min = tmp % 60;
    uint32_t hrs = tmp / 60 % 24;
    draw_cell(CELL_A, CHR_0 + hrs / 10);
    draw_cell(CELL_B, CHR_0 + hrs % 10);
    draw_cell(CELL_C, CHR_SPACE);
    draw_cell(CELL_X, CHR_0 + min / 10);
    draw_cell(CELL_Y, CHR_0 + min % 10);
    draw_cell(CELL_Z, CHR_SPACE);
    set_lcd_bit(SYM_BIG_ONE_HUNDRED, false);
    set_lcd_bit(SYM_PERCENTAGE, false);
    set_lcd_bit(SYM_BIG_DECIMAL_DOT, false);
    set_lcd_bit(SYM_SMALL_DECIMAL_DOT, false);
    show_temp_symbol(0);
}
#endif // USE_CLOCK

#endif // DEVICE_TYPE == DEVICE_CGDK2
