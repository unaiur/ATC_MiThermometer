#include <stdint.h>
#include "tl_common.h"
#include "app_config.h"
#if DEVICE_TYPE == DEVICE_CGG1
/* Based on source: https://github.com/znanev/ATC_MiThermometer */
#include "app.h"
#include "epd.h"
#include "battery.h"
#include "drivers/8258/pm.h"
#include "drivers/8258/timer.h"
#include "display_drv.h"
#include "display_13seg_cell.h"
#include "display_3cell_line.h"

#define DEF_EPD_REFRESH_CNT     32

RAM uint8_t display_cmp_buff[18];
RAM uint8_t stage_lcd;
RAM uint8_t flg_lcd_init;
RAM uint8_t lcd_refresh_cnt;
RAM uint8_t epd_updated;
//----------------------------------
// LUTV, LUT_KK and LUT_KW values taken from the actual device with a
// logic analyzer
//----------------------------------
const uint8_t T_LUTV_init[15] = {0x47, 0x47, 0x01,  0x87, 0x87, 0x01,  0x47, 0x47, 0x01,  0x87, 0x87, 0x01,  0x81, 0x81, 0x01};
const uint8_t T_LUT_KK_init[15] = {0x87, 0x87, 0x01,  0x87, 0x87, 0x01,  0x47, 0x47, 0x01,  0x47, 0x47, 0x01,  0x81, 0x81, 0x01};
const uint8_t T_LUT_KW_init[15] = {0x47, 0x47, 0x01,  0x47, 0x47, 0x01,  0x87, 0x87, 0x01,  0x87, 0x87, 0x01,  0x81, 0x81, 0x01};
const uint8_t T_LUT_KK_update[15] = {0x87, 0x87, 0x01,  0x87, 0x87, 0x01,  0x87, 0x87, 0x01,  0x87, 0x87, 0x01,  0x81, 0x81, 0x01};
const uint8_t T_LUT_KW_update[15] = {0x47, 0x47, 0x01,  0x47, 0x47, 0x01,  0x47, 0x47, 0x01,  0x47, 0x47, 0x01,  0x81, 0x81, 0x01};

#define delay_SPI_end_cycle() cpu_stall_wakeup_by_timer0((CLOCK_SYS_CLOCK_1US*15)/10) // real clk 4.4 + 4.4 us : 114 kHz)
#define delay_EPD_SCL_pulse() cpu_stall_wakeup_by_timer0((CLOCK_SYS_CLOCK_1US*15)/10) // real clk 4.4 + 4.4 us : 114 kHz)

_attribute_ram_code_ static void transmit_bit(bool bit) {
    // Set the clock low and output the bit
    gpio_write(EPD_SCL, LOW);
    gpio_write(EPD_SDA, bit ? HIGH : LOW);
    // the data is read at rising clock (halfway the time MOSI is set)
    delay_EPD_SCL_pulse();
    gpio_write(EPD_SCL, HIGH);
    delay_EPD_SCL_pulse();
}

_attribute_ram_code_ __attribute__((optimize("-Os"))) static void transmit(bool cd, uint8_t data_to_send) {
    // enable SPI
    gpio_write(EPD_SCL, LOW);
    gpio_write(EPD_CSB, LOW);
    delay_EPD_SCL_pulse();

    // send bits
    transmit_bit(cd);
    for (int i = 0x80; i; i >>= 1) {
        transmit_bit(data_to_send & i);
    }

    // finish by ending the clock cycle and disabling SPI
    gpio_write(EPD_SCL, LOW);
    delay_SPI_end_cycle();
    gpio_write(EPD_CSB, HIGH);
    delay_SPI_end_cycle();
}

void display_init(void) {
    // pulse RST_N low for 110 microseconds
    gpio_write(EPD_RST, LOW);
    pm_wait_us(110);
    lcd_refresh_cnt = DEF_EPD_REFRESH_CNT;
    stage_lcd = 1;
    epd_updated = 0;
    flg_lcd_init = 1;
    gpio_write(EPD_RST, HIGH);
    // EPD_BUSY: Low 866 us
}

void display_power_toggle()
{
    // ePaper does not need to be turned off to save power
}

_attribute_ram_code_ void display_async_refresh(void){
    if(!stage_lcd && memcmp(&display_cmp_buff, &display_buff, sizeof(display_buff))) {
        memcpy(&display_cmp_buff, &display_buff, sizeof(display_buff));
        if(lcd_refresh_cnt) {
            lcd_refresh_cnt--;
            flg_lcd_init = 0;
            stage_lcd = 1;
        } else {
            display_init(); // pulse RST_N low for 110 microseconds
        }
    }
    if (stage_lcd != 0) {
        if (task_lcd() == 0) {
            cpu_set_gpio_wakeup(EPD_BUSY, Level_High, 0);  // pad high wakeup deepsleep disable
        } else {
            cpu_set_gpio_wakeup(EPD_BUSY, Level_High, 1);  // pad high wakeup deepsleep enable
            bls_pm_setWakeupSource(PM_WAKEUP_PAD);  // gpio pad wakeup suspend/deepsleep
        }
    }
}

void display_sync_refresh(void)
{
    display_async_refresh();
    while (stage_lcd) {
        pm_wait_ms(10);
        display_async_refresh();
    }
}

_attribute_ram_code_  __attribute__((optimize("-Os"))) int task_lcd(void) {
    if (gpio_read(EPD_BUSY)) {
        switch (stage_lcd) {
        case 1: // Update/Init lcd, stage 1
            // send Charge Pump ON command
            transmit(0, POWER_ON);
            // EPD_BUSY: Low 32 ms from reset, 47.5 ms in refresh cycle
            stage_lcd = 2;
            break;
        case 2: // Update/Init lcd, stage 2
            if (epd_updated == 0) {
                transmit(0, PANEL_SETTING);
                transmit(1, 0x0F);

                transmit(0, POWER_SETTING);
                transmit(1, 0x32); // transmit(1, 0x32);
                transmit(1, 0x32); // transmit(1, 0x32);
                transmit(0, POWER_OFF_SEQUENCE_SETTING);
                transmit(1, 0x00);
                // Frame Rate Control
                transmit(0, PLL_CONTROL);
                if (flg_lcd_init)
                    transmit(1, 0x03);
                else {
                    transmit(1, 0x07);
                    transmit(0, PARTIAL_DISPLAY_REFRESH);
                    transmit(1, 0x00);
                    transmit(1, 0x87);
                    transmit(1, 0x01);
                    transmit(0, POWER_OFF_SEQUENCE_SETTING);
                    transmit(1, 0x06);
                }
                // send the e-paper voltage settings (waves)
                transmit(0, LUT_FOR_VCOM);
                for (int i = 0; i < sizeof(T_LUTV_init); i++)
                    transmit(1, T_LUTV_init[i]);

                if (flg_lcd_init) {
                    flg_lcd_init = 0;
                    transmit(0, LUT_CMD_0x23);
                    for (int i = 0; i < sizeof(T_LUT_KK_init); i++)
                        transmit(1, T_LUT_KK_init[i]);
                    transmit(0, LUT_CMD_0x26);
                    for (int i = 0; i < sizeof(T_LUT_KW_init); i++)
                        transmit(1, T_LUT_KW_init[i]);
                    // start an initialization sequence (white - all 0x00)
                    stage_lcd = 2;
                } else {
                    transmit(0, LUT_CMD_0x23);
                    for (int i = 0; i < sizeof(T_LUTV_init); i++)
                        transmit(1, T_LUTV_init[i]);

                    transmit(0, LUT_CMD_0x24);
                    for (int i = 0; i < sizeof(T_LUT_KK_update); i++)
                        transmit(1, T_LUT_KK_update[i]);

                    transmit(0, LUT_CMD_0x25);
                    for (int i = 0; i < sizeof(T_LUT_KW_update); i++)
                        transmit(1, T_LUT_KW_update[i]);

                    transmit(0, LUT_CMD_0x26);
                    for (int i = 0; i < sizeof(T_LUTV_init); i++)
                        transmit(1, T_LUTV_init[i]);
                    // send the actual data
                    stage_lcd = 3;
                }
            } else {
                stage_lcd = 3;
            }
            // send the actual data
            transmit(0, DATA_START_TRANSMISSION_1);
            for (int i = 0; i < sizeof(display_buff); i++)
                transmit(1, display_buff[i]^0xFF);
            // Refresh
            transmit(0, DISPLAY_REFRESH);
            // EPD_BUSY: Low 1217 ms from reset, 608.5 ms in refresh cycle
            break;
        case 3: // Update lcd, stage 3
            // send Charge Pump OFF command
            transmit(0, POWER_OFF);
            transmit(1, 0x03);
            // EPD_BUSY: Low 9.82 ms in refresh cycle
            epd_updated = 1;
            stage_lcd = 0;
            break;
        default:
            stage_lcd = 0;
        }
    }
    return stage_lcd;
}

// Positions of other symbols in the LCD
enum symbol {
    SYM_TEMP_UNDERSCORE  = NBIT(1, 5), // Used for Celsius sign
    SYM_TEMP_MINUS       = NBIT(1, 4), // Used for Fahrenheit sign
    SYM_TEMP_COMMON      = NBIT(1, 3), // Used for both C/F signs
    SYM_TEMP_DEGREE      = NBIT(1, 2), // Used for the degree sign
    SYM_BATTERY          = NBIT(7, 4), // Empty battery case
    SYM_BATTERY_L1       = NBIT(5, 6), // First battery stripe
    SYM_BATTERY_L2       = NBIT(5, 5), // Second battery stripe
    SYM_BATTERY_L3       = NBIT(6, 7), // Third battery stripe
    SYM_BATTERY_L4       = NBIT(6, 1), // Forth battery stripe
    SYM_BATTERY_L5       = NBIT(7, 5), // Fifth battery stripe
    SYM_BIG_DECIMAL_DOT  = NBIT(3, 6), // Decimal dot for top row
    SYM_SMALL_DECIMAL_DOT= NBIT(0, 2), // Decimal dot for bottom row
    SYM_PERCENTAGE       = NBIT(5, 3), // Percentage sign in bottom row
    SYM_BLUETOOTH        = NBIT(9, 1), // Bluetooth sign
};

_attribute_ram_code_ void display_temp_symbol(uint8_t symbol)
{
    display_render_bit(SYM_TEMP_COMMON, symbol & 0x20);
    display_render_bit(SYM_TEMP_DEGREE, symbol & 0x20);
    display_render_bit(SYM_TEMP_MINUS, symbol & 0x40);
    display_render_bit(SYM_TEMP_UNDERSCORE, symbol & 0x80);
}

_attribute_ram_code_ void display_ble_symbol(bool state)
{
    display_render_bit(SYM_BLUETOOTH, state);
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

static _attribute_ram_code_ void render_top_cell(int num_cell, enum display_char c)
{
    static const struct display_13seg_cell cells[] = {
        {
            .segment_bits = {
                [SEG_5M] = NBIT(9,  4),
                [SEG_5R] = NBIT(8,  5),
                [SEG_4R] = NBIT(8,  6),
                [SEG_3R] = NBIT(8,  7),
                [SEG_1R] = NBIT(7,  0),
                [SEG_1M] = NBIT(7,  1),
                [SEG_1L] = NBIT(9,  2),
                [SEG_1R] = NBIT(14, 4),
                [SEG_2R] = NBIT(14, 5),
                [SEG_3R] = NBIT(14, 6),
                [SEG_4R] = NBIT(14, 7),
                [SEG_5R] = NBIT(13, 0),
                [SEG_3M] = NBIT(9,  3),
            }
        }, {
            .segment_bits = {
                [SEG_5M] = NBIT(6, 3),
                [SEG_5R] = NBIT(5, 7),
                [SEG_4R] = NBIT(4, 0),
                [SEG_3R] = NBIT(4, 1),
                [SEG_1R] = NBIT(4, 6),
                [SEG_1M] = NBIT(4, 7),
                [SEG_1L] = NBIT(5, 4),
                [SEG_1R] = NBIT(5, 0),
                [SEG_2R] = NBIT(6, 6),
                [SEG_3R] = NBIT(6, 5),
                [SEG_4R] = NBIT(6, 4),
                [SEG_5R] = NBIT(6, 2),
                [SEG_3M] = NBIT(4, 5),
            }
        }, {
            .segment_bits = {
                [SEG_5M] = NBIT(1, 6),
                [SEG_5R] = NBIT(1, 7),
                [SEG_4R] = NBIT(1, 0),
                [SEG_3R] = NBIT(2, 6),
                [SEG_1R] = NBIT(2, 3),
                [SEG_1M] = NBIT(2, 1),
                [SEG_1L] = NBIT(2, 0),
                [SEG_1R] = NBIT(3, 7),
                [SEG_2R] = NBIT(2, 2),
                [SEG_3R] = NBIT(2, 4),
                [SEG_4R] = NBIT(2, 7),
                [SEG_5R] = NBIT(1, 1),
                [SEG_3M] = NBIT(2, 5),
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
    const uint8_t mask = BIT(1)|BIT(2)|BIT(3)|BIT(4)|BIT(5);
    uint8_t *byte = &display_buff[15];
    if (on) {
        *byte |= mask;
    } else {
        *byte &= ~mask;
    }
}

_attribute_ram_code_ void display_big_number_x10(int16_t number)
{
    const static struct display_3cell_line line = {
        .render_cell = render_top_cell,
        .render_decimal_dot = render_top_decimal_dot,
        .render_leftmost_one = render_top_leftmost_one,
    };
    display_render_3cell_number(&line, number);
}


static _attribute_ram_code_ void render_bottom_cell(int num_cell, enum display_char c)
{
    static const struct display_13seg_cell cells[] = {
        {
            .segment_bits = {
                [SEG_5M] = NBIT(13, 1),
                [SEG_5R] = NBIT(13, 2),
                [SEG_4R] = NBIT(14, 2),
                [SEG_3R] = NBIT(14, 0),
                [SEG_1R] = NBIT(15, 0),
                [SEG_1M] = NBIT(16, 6),
                [SEG_1L] = NBIT(16, 5),
                [SEG_1R] = NBIT(16, 4),
                [SEG_2R] = NBIT(16, 7),
                [SEG_3R] = NBIT(15, 6),
                [SEG_4R] = NBIT(14, 1),
                [SEG_5R] = NBIT(14, 3),
                [SEG_3M] = NBIT(15, 7),
            }
        }, {
            .segment_bits = {
                [SEG_5M] = NBIT(9,  6),
                [SEG_5R] = NBIT(8,  3),
                [SEG_4R] = NBIT(8,  2),
                [SEG_3R] = NBIT(8,  1),
                [SEG_1R] = NBIT(8,  4),
                [SEG_1M] = NBIT(7,  3),
                [SEG_1L] = NBIT(10, 5),
                [SEG_1R] = NBIT(10, 4),
                [SEG_2R] = NBIT(10, 6),
                [SEG_3R] = NBIT(9,  5),
                [SEG_4R] = NBIT(9,  0),
                [SEG_5R] = NBIT(10, 7),
                [SEG_3M] = NBIT(9,  7),
            }
        }, {
            .segment_bits = {
                [SEG_5M] = NBIT(5, 1),
                [SEG_5R] = NBIT(5, 2),
                [SEG_4R] = NBIT(7, 7),
                [SEG_3R] = NBIT(3, 2),
                [SEG_1R] = NBIT(3, 4),
                [SEG_1M] = NBIT(0, 1),
                [SEG_1L] = NBIT(0, 0),
                [SEG_1R] = NBIT(3, 5),
                [SEG_2R] = NBIT(3, 3),
                [SEG_3R] = NBIT(3, 0),
                [SEG_4R] = NBIT(7, 6),
                [SEG_5R] = NBIT(6, 0),
                [SEG_3M] = NBIT(3, 1),
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

#endif // DEVICE_TYPE == DEVICE_CGG1
