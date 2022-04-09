#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"
#include "cmd_parser.h"
#include "flash_eep.h"
#include "battery.h"
#include "ble.h"
#include "display.h"
#include "display_drv.h"
#include "sensor.h"
#include "button.h"
#include "app.h"
#include "i2c.h"
#include "uclock.h"
#if	USE_TRIGGER_OUT
#include "trigger.h"
#endif
#if USE_FLASH_MEMO
#include "logger.h"
#endif
#if USE_MIHOME_BEACON
#include "mi_beacon.h"
#endif

void app_enter_ota_mode(void);

RAM lcd_flg_t lcd_flg;

RAM measured_data_t measured_data;
RAM uint16_t last_reported_measure_count;
RAM int16_t last_temp; // x0.1 C
RAM uint16_t last_humi; // x1 %
RAM uint8_t battery_level; // 0..100%

RAM volatile uint8_t tx_measures;

RAM uint32_t adv_interval; // adv interval in 0.625 ms // = cfg.advertising_interval * 100
RAM uint32_t connection_timeout; // connection timeout in 10 ms, Tdefault = connection_latency_ms * 4 = 2000 * 4 = 8000 ms
RAM uint32_t min_step_time_update_lcd; // = cfg.min_step_time_update_lcd * 0.05 sec

RAM uint32_t utc_time_sec;	// clock in sec (= 0 1970-01-01 00:00:00)
RAM uint32_t utc_time_sec_tick;
#if USE_TIME_ADJUST
RAM uint32_t utc_time_tick_step = CLOCK_16M_SYS_TIMER_CLK_1S; // adjust time clock (in 1/16 us for 1 sec)
#else
#define utc_time_tick_step CLOCK_16M_SYS_TIMER_CLK_1S
#endif

RAM scomfort_t cmf;
const scomfort_t def_cmf = {
		.t = {2100,2600}, // x0.01 C
		.h = {3000,6000}  // x0.01 %
};

// Settings
const cfg_t def_cfg = {
		.flg.temp_F_or_C = false,
		.flg.comfort_smiley = true,
		.flg2.smiley = 0, // 0 = "     " off
		.flg.blinking_time_smile = false,
		.flg.show_batt_enabled = false,
		.flg.advertising_type = ADV_TYPE_DEFAULT,
		.flg.tx_measures = false,
		.advertising_interval = 40, // multiply by 62.5 ms = 2.5 sec
#if DISPLAY_TYPE == EPD
		.measure_interval = 8, // * advertising_interval = 20 sec
		.min_step_time_update_lcd = 199, //x0.05 sec,   9.95 sec
#else
		.measure_interval = 4, // * advertising_interval = 10 sec
		.min_step_time_update_lcd = 49, //x0.05 sec,   2.45 sec
#endif

#if DEVICE_TYPE == DEVICE_CGG1
		.hw_cfg.hwver = 2,
#elif DEVICE_TYPE == DEVICE_CGDK2
		.hw_cfg.hwver = 6,
#endif
#if USE_FLASH_MEMO
		.hw_cfg.clock = 1,
#endif
#if USE_FLASH_MEMO
		.hw_cfg.memo = 1,
#if DISPLAY_TYPE == EPD
		.averaging_measurements = 30, // * measure_interval = 20 * 30 = 600 sec = 10 minutes
#else
		.averaging_measurements = 60, // * measure_interval = 10 * 60 = 600 sec = 10 minutes
#endif
#endif
		.rf_tx_power = RF_POWER_P0p04dBm, // RF_POWER_P3p01dBm,
		.connect_latency = 124 // (124+1)*1.25*16 = 2500 ms
		};
RAM cfg_t cfg;
static const external_data_t def_ext = {
		.big_number = 0,
		.small_number = 0,
		.vtime_sec = 60 * 10, // 10 minutes
		.flg.smiley = 7, // 7 = "(ooo)"
		.flg.percent_on = true,
		.flg.battery = false,
		.flg.temp_symbol = 5 // 5 = "°C", ... app.h
		};
RAM external_data_t ext;
#if BLE_SECURITY_ENABLE
RAM uint32_t pincode;
#endif

__attribute__((optimize("-Os"))) void test_config(void) {
	if(cfg.rf_tx_power &BIT(7)) {
		if (cfg.rf_tx_power < RF_POWER_N25p18dBm)
			cfg.rf_tx_power = RF_POWER_N25p18dBm;
		else if (cfg.rf_tx_power > RF_POWER_P3p01dBm)
			cfg.rf_tx_power = RF_POWER_P3p01dBm;
	} else { if (cfg.rf_tx_power < RF_POWER_P3p23dBm)
		cfg.rf_tx_power = RF_POWER_P3p23dBm;
	else if (cfg.rf_tx_power > RF_POWER_P10p46dBm)
		cfg.rf_tx_power = RF_POWER_P10p46dBm;
	}
	if (cfg.measure_interval == 0)
		cfg.measure_interval = 1; // T = cfg.measure_interval * advertising_interval_ms (ms),  Tmin = 1 * 1*62.5 = 62.5 ms / 1 * 160 * 62.5 = 10000 ms
	else if (cfg.measure_interval > 25) // max = (0x100000000-1.5*10000000*16)/(10000000*16) = 25.3435456
		cfg.measure_interval = 25; // T = cfg.measure_interval * advertising_interval_ms (ms),  Tmax = 25 * 160*62.5 = 250000 ms = 250 sec
	if (cfg.flg.tx_measures)
		tx_measures = 0xff; // always notify
	if (cfg.advertising_interval == 0) // 0 ?
		cfg.advertising_interval = 1; // 1*62.5 = 62.5 ms
	else if (cfg.advertising_interval > 160) // max 160 : 160*62.5 = 10000 ms
		cfg.advertising_interval = 160; // 160*62.5 = 10000 ms
	adv_interval = cfg.advertising_interval * 100; // Tadv_interval = adv_interval * 62.5 ms

	// From IOS Accessory Design Guidelines requires that:
	// * Connection Latency <= 30
	// * Intervals are a positive multiple of 15.
	// * Either:
	//   - intervalMax >= intervalMin + 15
	//   - both interval min and max are set to 15ms
	// * IntervalMax * (Connection Latency + 1) <= 2 sec
	// * Connection Timeout from 2 to 6 seconds.
	// * Connection Timeout greater than IntervalMax * (Connection Latency + 1) * 3
	if (cfg.connect_latency) {
		my_periConnParameters.intervalMin = 12; // 12*1.25 = 15 ms
		my_periConnParameters.intervalMax = 12; // 12*1.25 = 15 ms
		my_periConnParameters.latency = cfg.connect_latency;
		if (my_periConnParameters.latency > 30) {
			my_periConnParameters.latency = 30;
		}
	} else {
		u32 interval = cfg.advertising_interval * 50; // Interval unit: 1.25ms
		interval -= interval % 12;  // Ensure it is a multiple of 15ms
		if (interval > 1588) {      // Maximum: 1.985 seconds
			interval = 1588;
		}
		my_periConnParameters.intervalMin = interval;
		my_periConnParameters.intervalMax = interval + 12;
		my_periConnParameters.latency = 0;
	}
	// The connection timeout is 4 times the connection latency, expressed in 10ms units
	connection_timeout = my_periConnParameters.intervalMax / 2 * (my_periConnParameters.latency + 1);
	if (connection_timeout < 200) {
		connection_timeout = 200;
	} else if (connection_timeout > 600) {
		connection_timeout = 600;
	}
	my_periConnParameters.timeout = connection_timeout;

	if(cfg.min_step_time_update_lcd < 10)
		cfg.min_step_time_update_lcd = 10; // min 10*0.05 = 0.5 sec
	min_step_time_update_lcd = cfg.min_step_time_update_lcd * (100 * CLOCK_16M_SYS_TIMER_CLK_1MS);

	cfg.hw_cfg.hwver = def_cfg.hw_cfg.hwver;
	cfg.hw_cfg.clock = 0;
	cfg.hw_cfg.memo = USE_FLASH_MEMO;
	cfg.hw_cfg.trg = USE_TRIGGER_OUT;
	cfg.hw_cfg.mi_beacon = USE_MIHOME_BEACON;
	cfg.hw_cfg.shtc3 = sensor_is_shtc3();

	my_RxTx_Data[0] = CMD_ID_CFG;
	my_RxTx_Data[1] = VERSION;
	memcpy(&my_RxTx_Data[2], &cfg, sizeof(cfg));
}

//------------------ user_init_normal -------------------
void user_init_normal(void) {//this will get executed one time after power up
	if (get_battery_mv() < MIN_VBAT_MV) // 2.2V
		cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_TIMER,
				clock_time() + 120 * CLOCK_16M_SYS_TIMER_CLK_1S); // go deep-sleep 2 minutes
	random_generator_init(); //must
	// Read config
	if (flash_supported_eep_ver(EEP_SUP_VER, VERSION)) {
		if(flash_read_cfg(&cfg, EEP_ID_CFG, sizeof(cfg)) != sizeof(cfg))
			memcpy(&cfg, &def_cfg, sizeof(cfg));
		if(flash_read_cfg(&cmf, EEP_ID_CMF, sizeof(cmf)) != sizeof(cmf))
			memcpy(&cmf, &def_cmf, sizeof(cmf));
#if USE_TIME_ADJUST
		if(flash_read_cfg(&utc_time_tick_step, EEP_ID_TIM, sizeof(utc_time_tick_step)) != sizeof(utc_time_tick_step))
			utc_time_tick_step = CLOCK_16M_SYS_TIMER_CLK_1S;
#endif
#if BLE_SECURITY_ENABLE
		if(flash_read_cfg(&pincode, EEP_ID_PCD, sizeof(pincode)) != sizeof(pincode))
			pincode = 0;
#endif
#if	USE_TRIGGER_OUT
		if(flash_read_cfg(&trg, EEP_ID_TRG, FEEP_SAVE_SIZE_TRG) != FEEP_SAVE_SIZE_TRG)
			memcpy(&trg, &def_trg, FEEP_SAVE_SIZE_TRG);
#endif
	} else {
		memcpy(&cfg, &def_cfg, sizeof(cfg));
		memcpy(&cmf, &def_cmf, sizeof(cmf));
#if BLE_SECURITY_ENABLE
		pincode = 0;
#endif
#if	USE_TRIGGER_OUT
		memcpy(&trg, &def_trg, FEEP_SAVE_SIZE_TRG);
#endif
	}
	test_config();
	memcpy(&ext, &def_ext, sizeof(ext));
	button_init();
	init_ble();
	sensor_init();
#if USE_FLASH_MEMO
	memo_init();
#endif
	display_init();
}

//------------------ user_init_deepRetn -------------------
_attribute_ram_code_ void user_init_deepRetn(void) {//after sleep this will get executed
//	adv_mi_count++;
	blc_ll_initBasicMCU();
	rf_set_power_level_index(cfg.rf_tx_power);
	blc_ll_recoverDeepRetention();
	bls_ota_registerStartCmdCb(app_enter_ota_mode);
}

//----------------------- main_loop()
_attribute_ram_code_ void main_loop(void) {
	blt_sdk_main_loop();
	uclock_after_sleep();

	// Keep the second counter
	while(clock_time() -  utc_time_sec_tick > utc_time_tick_step) {
		utc_time_sec_tick += utc_time_tick_step;
		utc_time_sec++; // + 1 sec
	}

	// Do not do anything else if we are upgrading the firmware
	if (ota_is_working) {
		bls_pm_setSuspendMask(SUSPEND_ADV | SUSPEND_CONN); // SUSPEND_DISABLE
		bls_pm_setManualLatency(0);
		return;
	}

	button_handle();
	if (sensor_read()) {
		last_temp = (measured_data.temp + 5)/ 10;
		last_humi = (measured_data.humi + 50)/ 100;

#if USE_FLASH_MEMO
		if(cfg.averaging_measurements)
			write_memo();
#endif

#if	USE_MIHOME_BEACON
		if((cfg.flg.advertising_type & ADV_TYPE_MASK_REF) && cfg.flg2.mi_beacon)
			mi_beacon_summ();
#endif
		set_adv_data();
		display_update();
		uclock_awake_after(0); // Ensure that we do not sleep after measuring new data
	} else if (sensor_is_idle()) {
		if ((blc_ll_getCurrentState() & BLS_LINK_STATE_CONN)) {
			if (blc_ll_getTxFifoNumber() < 9) {
				// If we are connected and we have space in the TX FIFO...
				if (last_reported_measure_count != measured_data.count) {
					last_reported_measure_count = measured_data.count;
					if (RxTxValueInCCC[0] | RxTxValueInCCC[1]) {
						if (tx_measures) {
							if (tx_measures != 0xff)
								tx_measures--;
							ble_send_measures();
						}
						if (lcd_flg.b.new_update) {
							lcd_flg.b.new_update = 0;
							ble_send_lcd();
						}
					}
					if (batteryValueInCCC[0] | batteryValueInCCC[1])
						ble_send_battery();
					if (tempValueInCCC[0] | tempValueInCCC[1])
						ble_send_temp();
					if (temp2ValueInCCC[0] | temp2ValueInCCC[1])
						ble_send_temp2();
					if (humiValueInCCC[0] | humiValueInCCC[1])
						ble_send_humi();
				} else if (mi_key_stage) {
					mi_key_stage = get_mi_keys(mi_key_stage);
		#if USE_FLASH_MEMO
				} else if (rd_memo.cnt) {
					send_memo_blk();
		#endif
				}
			}
			if (last_reported_measure_count != measured_data.count || mi_key_stage || rd_memo.cnt) {
				uclock_awake_after(62500); // If there are updates pendings, sleep for 62.5 ms
			}
		}
					
		display_async_refresh();
	}
	uclock_before_sleep();
}
