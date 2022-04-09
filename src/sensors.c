#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "vendor/common/user_config.h"
#include "app_config.h"
#include "drivers/8258/gpio_8258.h"
#include "drivers/8258/pm.h"
#include "trigger.h"
#include "uclock.h"

#include "i2c.h"
#include "sensor.h"
#include "app.h"
#include "battery.h"

#define SENSOR_MEASURING_TIMEOUT_ms  11 // SHTV3 11 ms, SHT4x max 8.2 ms

// Sensor SHTC3 https://www.sensirion.com/fileadmin/user_upload/customers/sensirion/Dokumente/2_Humidity_Sensors/Datasheets/Sensirion_Humidity_Sensors_SHTC3_Datasheet.pdf
#define SHTC3_I2C_ADDR		0x70
#define SHTC3_WAKEUP		0x1735 // Wake-up command of the sensor
#define SHTC3_WAKEUP_us		240    // time us
#define SHTC3_SOFT_RESET	0x5d80 // Soft reset command
#define SHTC3_SOFT_RESET_us	240    // time us
#define SHTC3_GO_SLEEP		0x98b0 // Sleep command of the sensor
#define SHTC3_MEASURE		0x6678 // Measurement commands, Clock Stretching Disabled, Normal Mode, Read T First
#define SHTC3_LPMEASURE		0x9C60 // Measurement commands, Clock Stretching Disabled, Low Power Mode, Read T First

// Sensor SHT4x https://www.sensirion.com/fileadmin/user_upload/customers/sensirion/Dokumente/2_Humidity_Sensors/Datasheets/Sensirion_Humidity_Sensors_Datasheet.pdf
#define SHT4x_I2C_ADDR		0x44
#define SHT4x_SOFT_RESET	0x94 // Soft reset command
#define SHT4x_SOFT_RESET_us	900  // max 1 ms
#define SHT4x_MEASURE_HI	0xFD // Measurement commands, Clock Stretching Disabled, Normal Mode, Read T First
#define SHT4x_MEASURE_HI_us 7000 // 6.9..8.2 ms
#define SHT4x_MEASURE_LO	0xE0 // Measurement commands, Clock Stretching Disabled, Low Power Mode, Read T First
#define SHT4x_MEASURE_LO_us 1700 // 1.7 ms


#define CRC_POLYNOMIAL  0x131 // P(x) = x^8 + x^5 + x^4 + 1 = 100110001

static RAM uint8_t sensor_i2c_addr;
static RAM bool sensor_idle;
static RAM uint32_t next_awake;
static RAM uint32_t next_read;

static inline uint32_t get_read_interval_us()
{
	 // cfg.advertising_interval is configured in units of 62.5 ms
	return 62500U * cfg.advertising_interval * cfg.measure_interval;
}

static inline bool is_shtc3()
{
	return sensor_i2c_addr == (SHTC3_I2C_ADDR << 1);
}

static inline bool is_sht4x()
{
	return sensor_i2c_addr == (SHT4x_I2C_ADDR << 1);
}

static _attribute_ram_code_ void send_sensor_word(uint16_t cmd) {
	reg_i2c_id = sensor_i2c_addr;
	reg_i2c_adr_dat = cmd;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_DO | FLD_I2C_CMD_STOP;
	while(reg_i2c_status & FLD_I2C_CMD_BUSY);
}

static _attribute_ram_code_ void send_sensor_byte(uint8_t cmd) {
	reg_i2c_id = sensor_i2c_addr;
	reg_i2c_adr = cmd;
	reg_i2c_ctrl = FLD_I2C_CMD_START | FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_STOP;
	while(reg_i2c_status & FLD_I2C_CMD_BUSY);
}

static void sensor_reset(void)
{
	if (is_shtc3()) {
		send_sensor_word(SHTC3_SOFT_RESET); // Soft reset command
		sleep_us(SHTC3_SOFT_RESET_us); // 240 us
		send_sensor_word(SHTC3_GO_SLEEP); // Sleep command of the sensor
	} else if (is_sht4x()) {
		send_sensor_byte(SHT4x_SOFT_RESET); // Soft reset command
		sleep_us(SHT4x_SOFT_RESET_us); // max 1 ms
	}
}

void sensor_turn_off(void)
{
	if (!sensor_idle && is_shtc3()) {
		if ((reg_clk_en0 & FLD_CLK0_I2C_EN) == 0) {
				init_i2c();
		}
		sensor_reset();
	}
	sensor_idle = true;
}

bool sensor_is_shtc3()
{
	if (sensor_i2c_addr == 0) {
		sensor_i2c_addr = (uint8_t) scan_i2c_addr(SHTC3_I2C_ADDR << 1);
	}
	if (sensor_i2c_addr == 0) {
		sensor_i2c_addr = (uint8_t) scan_i2c_addr(SHT4x_I2C_ADDR << 1);
	}
	return is_shtc3();
}

void sensor_init()
{
	next_read = next_awake = uclock_awake_after(0);
	if (is_shtc3()) {
		send_sensor_word(SHTC3_WAKEUP); //	Wake-up command of the sensor
		sleep_us(SHTC3_WAKEUP_us);	// 240 us
	}
	sensor_reset();
	sensor_idle = true;
}

static _attribute_ram_code_ u8 update_crc8(u8 data, u8 crc)
{
	crc ^= data;
	for (int i = 8; i > 0; i--) {
		if(crc & 0x80)
			crc = (crc << 1) ^ (CRC_POLYNOMIAL & 0xff);
		else
			crc = (crc << 1);
	}
	return crc;
}

static _attribute_ram_code_ bool read_word(uint16_t *value)
{
	reg_i2c_ctrl = FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID;
	while(reg_i2c_status & FLD_I2C_CMD_BUSY);
	u8 firstByte = reg_i2c_di;

	reg_i2c_ctrl = FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID;
	u8 crc = update_crc8(firstByte, 0xff);
	while(reg_i2c_status & FLD_I2C_CMD_BUSY);
	u8 secondByte = reg_i2c_di;

	reg_i2c_ctrl = FLD_I2C_CMD_DI | FLD_I2C_CMD_READ_ID;
	crc = update_crc8(secondByte, crc);
	while(reg_i2c_status & FLD_I2C_CMD_BUSY);
	u8 received_crc = reg_i2c_di;

	if (crc == received_crc) {
		*value = (firstByte << 8) | secondByte;
		return true;
	}
	return false;
}

static _attribute_ram_code_ bool read_sensor_cb(void)
{
	reg_i2c_id = sensor_i2c_addr | FLD_I2C_WRITE_READ_BIT;

	for (int retries = 0; retries < 5; ++retries) {
		reg_i2c_ctrl = FLD_I2C_CMD_ID | FLD_I2C_CMD_START;
		while(reg_i2c_status & FLD_I2C_CMD_BUSY);

		uint16_t _temp;
		uint16_t _humi;
		bool valid = !(reg_i2c_status & FLD_I2C_NAK) && read_word(&_temp) && _temp != 0xffff && read_word(&_humi);

		reg_i2c_ctrl = FLD_I2C_CMD_STOP;
		while(reg_i2c_status & FLD_I2C_CMD_BUSY);

		if (valid) {
			measured_data.temp = ((int32_t)(17500*_temp) >> 16) - 4500 + cfg.temp_offset * 10; // x 0.01 C
			if (is_shtc3())
				measured_data.humi = ((uint32_t)(10000*_humi) >> 16) + cfg.humi_offset * 10; // x 0.01 %
			else
				measured_data.humi = ((uint32_t)(12500*_humi) >> 16) - 600 + cfg.humi_offset * 10; // x 0.01 %
			if (measured_data.humi < 0)
				measured_data.humi = 0;
			else if(measured_data.humi > 9999)
				measured_data.humi = 9999;
			measured_data.count++;
			if (is_shtc3())
				send_sensor_word(SHTC3_GO_SLEEP); // Sleep command of the sensor
			return true;
		}
	}
	sensor_reset();
	return false;
}

static _attribute_ram_code_ void start_measure_sensor_deep_sleep(void)
{
	if (is_shtc3()) {
		send_sensor_word(SHTC3_WAKEUP); //	Wake-up command of the sensor
		sleep_us(SHTC3_WAKEUP_us - 5);	// 240 us
		send_sensor_word(SHTC3_MEASURE);
	} else if (is_sht4x()) {
		send_sensor_byte(SHT4x_MEASURE_HI);
	}
	gpio_setup_up_down_resistor(I2C_SCL, PM_PIN_PULLUP_1M);
	gpio_setup_up_down_resistor(I2C_SDA, PM_PIN_PULLUP_1M);
}

_attribute_ram_code_ bool sensor_is_idle()
{
	return sensor_idle;
}

_attribute_ram_code_ bool sensor_read()
{
	if (!uclock_should_awake(next_awake)) {
		return false;
	}
	if ((reg_clk_en0 & FLD_CLK0_I2C_EN) == 0) {
		init_i2c();
	}
	bool result = false;
	if (sensor_idle) {
		sensor_idle = false;
		start_measure_sensor_deep_sleep();
		next_awake = uclock_awake_after(SENSOR_MEASURING_TIMEOUT_ms * 1000);
		check_battery();
	} else {
		sensor_idle = true;
#if USE_TRIGGER_OUT && defined(GPIO_RDS)
		rds_input_on();
#endif
		if (read_sensor_cb()) {
			result = true;
#if USE_TRIGGER_OUT
			set_trigger_out();
#endif
#if USE_TRIGGER_OUT && defined(GPIO_RDS)
			test_trg_input();
#endif
		}

#if USE_TRIGGER_OUT && defined(GPIO_RDS)
		rds_input_off();
#endif
		next_awake = uclock_awake_at(next_read += get_read_interval_us());
	}
	return result;
}
