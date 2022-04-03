
#include "tl_common.h"
#include "drivers.h"
#include "app_config.h"
#include "uclock.h"
#include "button.h"
#include "ble.h"
#include "display.h"
#include <stdint.h>

#define DEBOUNCE_MS   50000
#define LONG_PRESS_MS 500000

enum state {
    IDLE,
    DEBOUNCING,
    PRESSED,
};

RAM static enum state button_state;
RAM static bool button_pressed_on_boot;
RAM static uint16_t num_clicks;
RAM static uint32_t debounce_timer;
RAM static uint32_t long_press_timer;

static inline bool is_button_pressed()
{
    return gpio_read(GPIO_KEY) == 0 ? true : false;
}

void button_init()
{
    button_state = IDLE;
    button_pressed_on_boot = is_button_pressed();
    cpu_set_gpio_wakeup(GPIO_KEY, Level_Low, 1);
}

static _attribute_ram_code_ void button_handle_click(bool long_press, int repetitions)
{
    ble_connected ^= 0x10;
    display_update();
}

_attribute_ram_code_ void button_handle()
{
    bool pressed = is_button_pressed();
    if (button_state == DEBOUNCING) {
        if (uclock_should_awake(debounce_timer)) {
            if (pressed) {
                button_state = PRESSED;
                long_press_timer = uclock_awake_after(LONG_PRESS_MS);
                ++num_clicks;
            } else {
                button_state = IDLE;
            }
        }
    } else {
        enum state new_state = pressed ? PRESSED: IDLE;
        if (new_state != button_state) {
            button_state = DEBOUNCING;
            debounce_timer = uclock_awake_after(DEBOUNCE_MS);
        }
    }

    if (num_clicks && uclock_should_awake(long_press_timer)) {
        button_handle_click(button_state == PRESSED, num_clicks);
        num_clicks = 0;
    }

    if (button_state == DEBOUNCING) {
        cpu_set_gpio_wakeup(GPIO_KEY, 0, 0);
    } else {
        cpu_set_gpio_wakeup(GPIO_KEY, button_state == PRESSED ? Level_High : Level_Low, 1);
        bls_pm_setWakeupSource(PM_WAKEUP_PAD);
    }
}
