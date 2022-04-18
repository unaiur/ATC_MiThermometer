#include "tl_common.h"
#include "drivers.h"
#include "vendor/common/user_config.h"
#include "app_config.h"
#include "drivers/8258/gpio_8258.h"
#include "i2c.h"

static struct {
    int pending_bytes: 2;
    int started: 1;
} state;

static const uint8_t cmd_by_len[] = {
     FLD_I2C_CMD_ADDR,
     FLD_I2C_CMD_ADDR | FLD_I2C_CMD_DO,
     FLD_I2C_CMD_ADDR | FLD_I2C_CMD_DO | FLD_I2C_CMD_DI
};

typedef volatile unsigned char *reg8_ptr_t;

static reg8_ptr_t const data_ptr[] = {
    &reg_i2c_adr,
    &reg_i2c_do,
    &reg_i2c_di,
};

_attribute_ram_code_ void i2c_send_byte(uint8_t byte, int is_last)
{
    int pending_bytes = state.pending_bytes;
    *data_ptr[pending_bytes] = byte;
    if (pending_bytes < 2 && !is_last) {
        ++state.pending_bytes;
    } else {
        uint8_t cmd = cmd_by_len[pending_bytes];
        int bits = (1 + pending_bytes) * 9; // includes ACK bits
        if (!state.started) {
            cmd |= FLD_I2C_CMD_START | FLD_I2C_CMD_ID;
            bits += 10;
            state.started = 1;
        }
        if (is_last) {
            cmd |= FLD_I2C_CMD_STOP;
            bits += 1;
            state.started = 0;
        }
        reg_i2c_ctrl = cmd;
        //cpu_stall_wakeup_by_timer0(bits * 4 * reg_i2c_speed);
        while (reg_i2c_status & FLD_I2C_CMD_BUSY);
        state.pending_bytes = 0;
    }
}

_attribute_ram_code_ void i2c_send_buff(const uint8_t *buff, unsigned len, int is_last)
{
    if (len > 0) {
        while (len-- > 1) {
            i2c_send_byte(*buff++, false);
        }
        i2c_send_byte(*buff++, is_last);
    }
}

void i2c_send_abort() {
    reg_i2c_ctrl = FLD_I2C_CMD_STOP;
    state.pending_bytes = 0;
    state.started = 0;
    while (reg_i2c_status & FLD_I2C_CMD_BUSY);
}

