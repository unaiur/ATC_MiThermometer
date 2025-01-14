#include <stdint.h>
#include "tl_common.h"
#include "battery.h"
#include "ble.h"
#include "drivers.h"
#include "display.h"
#include "display_drv.h"
#include "app.h"
#include "button.h"

RAM uint32_t chow_tick_clk; // count show validity time, in clock
RAM uint32_t chow_tick_sec; // count show validity time, in sec

static inline bool should_display_ext_data_layout()
{
	while (chow_tick_sec && clock_time() - chow_tick_clk
			> CLOCK_16M_SYS_TIMER_CLK_1S) {
		chow_tick_clk += CLOCK_16M_SYS_TIMER_CLK_1S;
		chow_tick_sec--;
	}
	return chow_tick_sec > 0;
}

static inline void display_ext_data_layout(void)
{
	display_battery_symbol(ext.flg.battery);
	display_temp_symbol(*((uint8_t *) &ext.flg));
	display_big_number_x10(ext.big_number);
	display_small_number_x10(ext.small_number, ext.flg.percent_on);
}

static inline void display_temperature_humidity_layout()
{
	// Show temperature in Fahrenheit or Celsius degrees in the big number
	bool btn_on_boot = button_was_pressed_on_boot();
	if (cfg.flg.temp_F_or_C) {
		display_temp_symbol(btn_on_boot ? TMP_SYM_EQ : TMP_SYM_F);
		display_big_number_x10((((measured_data.temp / 5) * 9) + 3200) / 10); // convert C to F
	} else {
		display_temp_symbol(btn_on_boot ? TMP_SYM_EQ : TMP_SYM_C);
		display_big_number_x10(last_temp);
	}
	display_battery_symbol(true);
	display_small_number_x10((measured_data.humi + 5) / 10, 1);
}

_attribute_ram_code_ void display_update(void)
{
	if (lcd_flg.b.ext_data)
		return;

	lcd_flg.b.new_update = lcd_flg.b.notify_on;
	display_ble_symbol(ble_connected & 0x10);
	if (should_display_ext_data_layout()) {
		display_ext_data_layout();
	} else {
		display_temperature_humidity_layout();
	}
}

void display_low_battery_voltage(int battery_mv)
{
	display_temp_symbol(0);
	display_big_number_x10(battery_mv * 10);
	display_small_number_x10(-1023, 1); // Force "Lo" display
	display_battery_symbol(1);
	display_sync_refresh();
}

