#include <stdint.h>
#include "tl_common.h"
#include "drivers.h"
#include "vendor/common/user_config.h"
#include "app_config.h"
#include "drivers/8258/gpio_8258.h"
#include "i2c.h"

#define TX_STATE_BUF_LEN_MASK 0x0F
#define TX_STATE_OPEN         0x80
#define TX_BUF_CAPACITY       4

static RAM uint8_t tx_state = 0;

static _attribute_ram_code_ void wait_until_idle(void)
{
    while (reg_i2c_status & FLD_I2C_CMD_BUSY);
}

_attribute_ram_code_ void i2c_start(uint8_t address)
{
    if ((reg_clk_en0 & FLD_CLK0_I2C_EN) == 0) {
        i2c_gpio_set(I2C_GROUP); // I2C_GPIO_GROUP_C0C1, I2C_GPIO_GROUP_C2C3, I2C_GPIO_GROUP_B6D7, I2C_GPIO_GROUP_A3A4
        reg_i2c_speed = (uint8_t)(CLOCK_SYS_CLOCK_HZ/(4*750000)); //i2c clock = system_clock/(4*DivClock)
        reg_i2c_mode |= FLD_I2C_MASTER_EN; //enable master mode
        reg_i2c_mode &= ~FLD_I2C_HOLD_MASTER; // Disable clock stretching for Sensor
        reg_clk_en0 |= FLD_CLK0_I2C_EN;    //enable i2c clock
        reg_spi_sp  &= ~FLD_SPI_ENABLE;   //force PADs act as I2C; i2c and spi share the hardware of IC
    }
    reg_i2c_id = address;
    tx_state = 1;
}

static const uint8_t cmd_by_len[] = {
     FLD_I2C_CMD_ID,
     FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR,
     FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_DO,
     FLD_I2C_CMD_ID | FLD_I2C_CMD_ADDR | FLD_I2C_CMD_DO | FLD_I2C_CMD_DI,
};
_attribute_ram_code_ static void i2c_flush(bool stop)
{
    int len = tx_state & TX_STATE_BUF_LEN_MASK;
    uint8_t cmd = len ? cmd_by_len[len - 1] : 0;
    int bits = len * 9; // include ACK bits
    if (!(tx_state & TX_STATE_OPEN)) {
        cmd |= FLD_I2C_CMD_START;
        bits += 1;
    }
    if (stop) {
        cmd |= FLD_I2C_CMD_STOP;
        bits += 1;
    }
    reg_i2c_ctrl = cmd;
    cpu_stall_wakeup_by_timer0(bits * 4 * reg_i2c_speed);
    wait_until_idle();
}

_attribute_ram_code_ void i2c_stop()
{
    i2c_flush(true);
    tx_state = 0;
}

_attribute_ram_code_ void i2c_abort() {
    reg_i2c_ctrl = FLD_I2C_CMD_STOP;
    tx_state = 0;
    wait_until_idle();
}

bool i2c_check_address(int address)
{
    i2c_start((uint8_t) address);
    i2c_stop();
    return (reg_i2c_status & FLD_I2C_NAK) ? false : true;
}

typedef volatile unsigned char *reg8_ptr_t;
static const reg8_ptr_t data_ptr[] = {
    &reg_i2c_id,
    &reg_i2c_adr,
    &reg_i2c_do,
    &reg_i2c_di,
};
_attribute_ram_code_ void i2c_send_byte(uint8_t b)
{
    int len = tx_state & TX_STATE_BUF_LEN_MASK;
    if (len == TX_BUF_CAPACITY) {
        i2c_flush(false);
        len = 0;
        tx_state = TX_STATE_OPEN;
    }
    *data_ptr[len] = b;
    ++tx_state;
}

_attribute_ram_code_ void i2c_send_buff(const uint8_t *buff, unsigned len)
{
    while (len--) {
        i2c_send_byte(*buff++);
    }
}

_attribute_ram_code_ void i2c_write_tx_1byte(uint8_t address, uint8_t data)
{
    i2c_start(address);
    reg_i2c_adr = data;
    tx_state = 2; // address + data = 2 bytes
    i2c_stop();
}

_attribute_ram_code_ void i2c_write_tx_1word(uint8_t address, uint16_t data)
{
    i2c_start(address);
    reg_i2c_adr_dat = data;
    tx_state = 3; // address + data = 3 bytes
    i2c_stop();
}
