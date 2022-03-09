#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "stack/ble/ble.h"
#include "vendor/common/blt_common.h"
#include "cmd_parser.h"
#include "flash_eep.h"
#include "battery.h"
#include "ble.h"
#include "lcd.h"
#include "sensor.h"
#include "app.h"
#include "i2c.h"
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
RAM int16_t last_temp; // x0.1 C
RAM uint16_t last_humi; // x1 %
RAM uint8_t battery_level; // 0..100%

RAM volatile uint8_t tx_measures;

RAM volatile uint8_t start_measure; // start measure all
RAM volatile uint8_t wrk_measure;
RAM volatile uint8_t end_measure;
RAM uint32_t tim_last_chow; // timer show lcd >= 1.5 sec
RAM uint32_t tim_measure; // timer measurements >= 10 sec

RAM uint32_t adv_interval; // adv interval in 0.625 ms // = cfg.advertising_interval * 100
RAM uint32_t connection_timeout; // connection timeout in 10 ms, Tdefault = connection_latency_ms * 4 = 2000 * 4 = 8000 ms
RAM uint32_t measurement_step_time; // = adv_interval * measure_interval
RAM uint32_t min_step_time_update_lcd; // = cfg.min_step_time_update_lcd * 0.05 sec

RAM uint32_t utc_time_sec;	// clock in sec (= 0 1970-01-01 00:00:00)
RAM uint32_t utc_time_sec_tick;
#if USE_CLOCK && USE_TIME_ADJUST
RAM uint32_t utc_time_tick_step = CLOCK_16M_SYS_TIMER_CLK_1S; // adjust time clock (in 1/16 us for 1 sec)
#else
#define utc_time_tick_step CLOCK_16M_SYS_TIMER_CLK_1S
#endif

RAM scomfort_t cmf;
const scomfort_t def_cmf = {
		.t = {2100,2600}, // x0.01 C
		.h = {3000,6000}  // x0.01 %
};

void lcd(void);

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
		.hw_cfg.hwver = 7,
#endif
#if USE_FLASH_MEMO || USE_CLOCK
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

__attribute__((optimize("-Os")))
static void set_hw_version(void) {
	if(sensor_i2c_addr == (SHTC3_I2C_ADDR << 1))
		cfg.hw_cfg.shtc3 = 1; // = 1 - sensor SHTC3
	else
		cfg.hw_cfg.shtc3 = 0; // = 0 - sensor SHT4x or ?
}

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
	measurement_step_time = adv_interval * cfg.measure_interval * (625
			* sys_tick_per_us) - 250; // measurement_step_time = adv_interval * 62.5 * measure_interval, max 250 sec
	/* interval = 16;
	 * connection_interval_ms = (interval * 125) / 100;
	 * connection_latency_ms = (cfg.connect_latency + 1) * connection_interval_ms = (16*125/100)*(99+1) = 2000;
	 * connection_timeout_ms = connection_latency_ms * 4 = 2000 * 4 = 8000;
	 */
	connection_timeout = ((cfg.connect_latency + 1) * 4 * 16 * 125) / 1000; // = 800, default = 8 sec
	if (connection_timeout > 32 * 100)
		connection_timeout = 32 * 100; //x10 ms, max 32 sec?
	else if(connection_timeout < 100)
		connection_timeout = 100;	//x10 ms,  1 sec
	if(!cfg.connect_latency) {
		my_periConnParameters.intervalMin = (cfg.advertising_interval * 625 / 30) - 1; // Tmin = 20*1.25 = 25 ms, Tmax = 3333*1.25 = 4166.25 ms
		my_periConnParameters.intervalMax = my_periConnParameters.intervalMin + 5;
		my_periConnParameters.latency = 0;
	} else {
		my_periConnParameters.intervalMin = 16; // 10*1.25 = 12.5 ms
		my_periConnParameters.intervalMax = 16; // 60*1.25 = 75 ms
		my_periConnParameters.latency = cfg.connect_latency;
	}
	my_periConnParameters.timeout = connection_timeout;
	if(cfg.min_step_time_update_lcd < 10)
		cfg.min_step_time_update_lcd = 10; // min 10*0.05 = 0.5 sec
	min_step_time_update_lcd = cfg.min_step_time_update_lcd * (100 * CLOCK_16M_SYS_TIMER_CLK_1MS);

	cfg.hw_cfg.clock = USE_CLOCK;
	cfg.hw_cfg.memo = USE_FLASH_MEMO;
	cfg.hw_cfg.trg = USE_TRIGGER_OUT;
	my_RxTx_Data[0] = CMD_ID_CFG;
	my_RxTx_Data[1] = VERSION;
	memcpy(&my_RxTx_Data[2], &cfg, sizeof(cfg));
}

_attribute_ram_code_ void WakeupLowPowerCb(int par) {
	(void) par;
	if (wrk_measure) {
#if	USE_TRIGGER_OUT && defined(GPIO_RDS)
		rds_input_on();
#endif
#if (defined(GPIO_ADC1) || defined(GPIO_ADC2))
		if(1) {
#else
		if(read_sensor_cb()) {
#endif
#ifdef GPIO_ADC1 // Test only!
			measured_data.temp = get_adc_mv(8);
#endif
#ifdef GPIO_ADC2 // Test only!
			measured_data.humi = get_adc_mv(9);
#endif
			last_temp = (measured_data.temp + 5)/ 10;
			last_humi = (measured_data.humi + 50)/ 100;
#if	USE_TRIGGER_OUT
			set_trigger_out();
#endif
#if USE_FLASH_MEMO
			if(cfg.averaging_measurements)
				write_memo();
#endif
#if	USE_MIHOME_BEACON
			if((cfg.flg.advertising_type & ADV_TYPE_MASK_REF) && cfg.flg2.mi_beacon)
				mi_beacon_summ();
#endif
#if USE_TRIGGER_OUT && defined(GPIO_RDS)
			test_trg_input();
#endif
			set_adv_data();
			end_measure = 1;
		}
#if	USE_TRIGGER_OUT && defined(GPIO_RDS)
		rds_input_off();
#endif
		wrk_measure = 0;
	}	
	timer_measure_cb = 0;
}

_attribute_ram_code_ void suspend_exit_cb(u8 e, u8 *p, int n) {
	(void) e; (void) p; (void) n;
	if(timer_measure_cb)
		init_i2c();
}

_attribute_ram_code_ void suspend_enter_cb(u8 e, u8 *p, int n) {
	(void) e; (void) p; (void) n;
	if (wrk_measure
		&& timer_measure_cb
		&& clock_time() - timer_measure_cb > SENSOR_MEASURING_TIMEOUT - 3*CLOCK_16M_SYS_TIMER_CLK_1MS) {
			WakeupLowPowerCb(0);
			bls_pm_setAppWakeupLowPower(0, 0); // clear callback
	}
}

void low_vbat(void) {
	if(cfg.hw_cfg.shtc3 && wrk_measure)
		soft_reset_sensor();
	show_temp_symbol(0);
	show_big_number_x10(measured_data.battery_mv * 10);
	show_small_number_x10(-1023, 1); // "Lo"
	show_battery_symbol(1);
	update_lcd();
#if DISPLAY_TYPE == EPD
	while(task_lcd()) pm_wait_ms(10);
#endif
	cpu_sleep_wakeup(DEEPSLEEP_MODE, PM_WAKEUP_TIMER,
			clock_time() + 120 * CLOCK_16M_SYS_TIMER_CLK_1S); // go deep-sleep 2 minutes
}

//--- check battery
_attribute_ram_code_ void check_battery(void) {
	measured_data.battery_mv = get_battery_mv();
	if (measured_data.battery_mv < 2000) {
		low_vbat();
	}
	battery_level = get_battery_level(measured_data.battery_mv);
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
#if USE_CLOCK && USE_TIME_ADJUST
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
	init_ble();
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_EXIT, &suspend_exit_cb);
	bls_app_registerEventCallback(BLT_EV_FLAG_SUSPEND_ENTER, &suspend_enter_cb);
	init_sensor();
#if USE_FLASH_MEMO
	memo_init();
#endif
	init_lcd();
	set_hw_version();
	wrk_measure = 1;
#if defined(GPIO_ADC1) || defined(GPIO_ADC2)
	sensor_go_sleep();
#else
	start_measure_sensor_low_power();
#endif
	check_battery();
	WakeupLowPowerCb(0);
	lcd();
#if DISPLAY_TYPE == LCD
	update_lcd();
#endif
	start_measure = 1;
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
#if	USE_CLOCK || USE_FLASH_MEMO
	while(clock_time() -  utc_time_sec_tick > utc_time_tick_step) {
		utc_time_sec_tick += utc_time_tick_step;
		utc_time_sec++; // + 1 sec
	}
#endif
	if (wrk_measure
		&& timer_measure_cb
		&& clock_time() - timer_measure_cb > SENSOR_MEASURING_TIMEOUT) {
			bls_pm_setAppWakeupLowPower(0, 0); // clear callback
			WakeupLowPowerCb(0);
	}
	if (ota_is_working) {
		bls_pm_setSuspendMask (SUSPEND_ADV | SUSPEND_CONN); // SUSPEND_DISABLE
		bls_pm_setManualLatency(0);
	} else {
		if (!wrk_measure) {
			if (start_measure) {
				wrk_measure = 1;
				start_measure = 0;
#if defined(GPIO_ADC1) || defined(GPIO_ADC2)
				check_battery();
				WakeupLowPowerCb(0);
#else
				if (cfg.flg.lp_measures) {
					if (cfg.hw_cfg.shtc3) {
						start_measure_sensor_low_power();
						check_battery();
						WakeupLowPowerCb(0);
					} else { // sensor SHT4x
						// no callback, data read sensor is next cycle
						WakeupLowPowerCb(0);
						check_battery();
						start_measure_sensor_deep_sleep();
					}
				} else {
					start_measure_sensor_deep_sleep();
					check_battery();
					if (bls_pm_getSystemWakeupTick() - clock_time() > SENSOR_MEASURING_TIMEOUT + 5*CLOCK_16M_SYS_TIMER_CLK_1MS) {
						bls_pm_registerAppWakeupLowPowerCb(WakeupLowPowerCb);
						bls_pm_setAppWakeupLowPower(timer_measure_cb + SENSOR_MEASURING_TIMEOUT, 1);
					} else {
						bls_pm_setAppWakeupLowPower(0, 0); // clear callback
					}
				}
#endif
			} else {
				uint32_t new = clock_time();
				if ((blc_ll_getCurrentState() & BLS_LINK_STATE_CONN) && blc_ll_getTxFifoNumber() < 9) {
					if (end_measure) {
						end_measure = 0;
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
				if (new - tim_measure >= measurement_step_time) {
					tim_measure = new;
					start_measure = 1;
				}
#if DISPLAY_TYPE == EPD // Can be bypassed by measurements when there is no E-Ink update (https://github.com/pvvx/ATC_MiThermometer/issues/180)
				else
#endif
				if (new - tim_last_chow >= min_step_time_update_lcd) {
					if (!lcd_flg.b.ext_data) {
						lcd_flg.b.new_update = lcd_flg.b.notify_on;
						lcd();
					}
#if DISPLAY_TYPE == EPD
					if(!stage_lcd)
#endif
						update_lcd();
					tim_last_chow = new;
				}
				bls_pm_setAppWakeupLowPower(0, 0);
			}
		}
		if((cfg.flg.advertising_type & ADV_TYPE_MASK_REF) // type 2 - Mi and 3 - all
			&& blc_ll_getCurrentState() == BLS_LINK_STATE_ADV) {
			if(adv_old_count != adv_send_count)
				set_adv_data();
		}
#if DISPLAY_TYPE == EPD
		if(wrk_measure == 0 && stage_lcd) {
			if(gpio_read(EPD_BUSY) && (!task_lcd())) {
				cpu_set_gpio_wakeup(EPD_BUSY, Level_High, 0);  // pad high wakeup deepsleep disable
			} else if((bls_pm_getSystemWakeupTick() - clock_time()) > 25 * CLOCK_16M_SYS_TIMER_CLK_1MS) {
				cpu_set_gpio_wakeup(EPD_BUSY, Level_High, 1);  // pad high wakeup deepsleep enable
				bls_pm_setWakeupSource(PM_WAKEUP_PAD);  // gpio pad wakeup suspend/deepsleep
			}
		}
#endif
		bls_pm_setSuspendMask(
				SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN
						| DEEPSLEEP_RETENTION_CONN);
	}
}
