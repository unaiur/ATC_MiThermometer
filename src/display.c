#include <stdint.h>
#include "tl_common.h"
#include "battery.h"
#include "ble.h"
#include "drivers.h"
#include "lcd.h"
#include "app.h"

RAM uint32_t chow_tick_clk; // count show validity time, in clock
RAM uint32_t chow_tick_sec; // count show validity time, in sec

// There can be up to 4 different display layouts to display
enum display_layout {
	DISPLAY_EXT_DATA  = BIT(0), // Display debug data received in BLE cmd
	DISPLAY_TEMP_BAT  = BIT(1), // Display temperature and battery
	DISPLAY_TEMP_HUMI = BIT(2), // Display temperature and humitity
	DISPLAY_CLOCK     = BIT(3), // Display clock or blink the smiley
};

static inline bool should_display_ext_data_layout() {
	while (chow_tick_sec && clock_time() - chow_tick_clk
			> CLOCK_16M_SYS_TIMER_CLK_1S) {
		chow_tick_clk += CLOCK_16M_SYS_TIMER_CLK_1S;
		chow_tick_sec--;
	}
	return chow_tick_sec > 0;
}

static inline bool should_display_temp_bat_layout() {
#if	DISPLAY_BATTERY_LEVELS > 0
	return cfg.flg.show_batt_enabled;
#else
	return cfg.flg.show_batt_enabled || battery_level < 15;
#endif
}

static inline bool should_display_clock_layout() {
	return cfg.flg.blinking_time_smile;
}

static inline int get_display_layouts() {
	return DISPLAY_TEMP_HUMI
		| (should_display_ext_data_layout() ? DISPLAY_EXT_DATA : 0)
		| (should_display_temp_bat_layout() ? DISPLAY_TEMP_BAT : 0)
		| (should_display_clock_layout() ? DISPLAY_CLOCK : 0);

}

static inline void display_ext_data_layout(void) {
	show_battery_symbol(ext.flg.battery);
	show_temp_symbol(*((uint8_t *) &ext.flg));
	show_smiley(*((uint8_t *) &ext.flg));
	show_big_number_x10(ext.big_number);
#if	DISPLAY_SMALL_DECIMALS > 0
	show_small_number_x10(ext.small_number, ext.flg.percent_on);
#else
	show_small_number(ext.small_number, ext.flg.percent_on);
#endif
}

// Show temperature in Fahrenheit or Celsius degrees in the big number
static _attribute_ram_code_ void display_temperature() {
	if (cfg.flg.temp_F_or_C) {
		show_temp_symbol(TMP_SYM_F);
		show_big_number_x10((((measured_data.temp / 5) * 9) + 3200) / 10); // convert C to F
	} else {
		show_temp_symbol(TMP_SYM_C);
		show_big_number_x10(last_temp);
	}
}


static inline void display_temperature_battery_layout() {
	display_temperature();
	show_battery_symbol(1);
	show_smiley(0);

#if	DISPLAY_SMALL_DECIMALS == 0
	show_small_number((battery_level >= 100) ? 99 : battery_level, DISPLAY_BATTERY_LEVELS == 0);
#else
	uint16_t battery_level = 0;
	if(measured_data.battery_mv > MIN_VBAT_MV) {
		battery_level = ((measured_data.battery_mv - MIN_VBAT_MV)*10)/((MAX_VBAT_MV - MIN_VBAT_MV)/100);
		if (battery_level > 999) {
			battery_level = 999;
		}
	}
	show_small_number_x10(battery_level, DISPLAY_BATTERY_LEVELS == 0);
#endif
}

static inline uint8_t get_smiley(int16_t t, int16_t h) {
	if (!cfg.flg.comfort_smiley) {
		return cfg.flg2.smiley;
	}
	if (t >= cmf.t[0] && t <= cmf.t[1] && h >= cmf.h[0] && h <= cmf.h[1]) {
		return SMILE_HAPPY;
	}
	return SMILE_SAD;
}

static inline void display_temperature_humidity_layout() {
	display_temperature();
	show_battery_symbol(DISPLAY_BATTERY_LEVELS > 0);
	show_smiley(get_smiley(measured_data.temp, measured_data.humi));

#if	DISPLAY_SMALL_DECIMALS == 0
	show_small_number(last_humi, 1);
#else
	show_small_number_x10((measured_data.humi + 5) / 10, 1);
#endif
}

 _attribute_ram_code_ void lcd(void) {
	static RAM uint8_t available_layouts = 0;
	if (available_layouts == 0) {
		available_layouts = get_display_layouts();
	}

	show_ble_symbol(ble_connected);
	if (available_layouts & DISPLAY_EXT_DATA) {
		available_layouts ^= DISPLAY_EXT_DATA;
		display_ext_data_layout();
	} else if (available_layouts & DISPLAY_TEMP_BAT) {
		available_layouts ^= DISPLAY_TEMP_BAT;
		display_temperature_battery_layout();
	} else if (available_layouts & DISPLAY_TEMP_HUMI) {
		available_layouts ^= DISPLAY_TEMP_HUMI;
		display_temperature_humidity_layout();
	} else if (available_layouts & DISPLAY_CLOCK) {
		available_layouts ^= DISPLAY_CLOCK;
#if	USE_CLOCK
		show_clock();
#else
		show_smiley(0);
#endif
	}
}

