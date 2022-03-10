#include <stdint.h>
#include "tl_common.h"
#include "app_config.h"
#include "display_drv.h"
#include "drivers.h"

RAM uint8_t display_buff[DISPLAY_BUFF_LEN];

// Sets a bit of the LCD buffer on or off
_attribute_ram_code_ void display_render_bit(uint8_t bit, bool on)
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
