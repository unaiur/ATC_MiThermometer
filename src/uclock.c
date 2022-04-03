#include "tl_common.h"
#include "drivers.h"
#include "uclock.h"
#include "stack/ble/ll/ll_pm.h"
#include <limits.h>

#define MAX_TIMER_US (3600U * 1000000U) // 1 hour
#define SLEEP_SAFETY_MARGIN_US 4000000U // 4 seconds
#define MAX_SLEEP_US ((UINT_MAX - SLEEP_SAFETY_MARGIN_US * CLOCK_16M_SYS_TIMER_CLK_1US) / CLOCK_16M_SYS_TIMER_CLK_1US)

static RAM utime_t this_loop_start;
static RAM utime_t next_sleep_us;

static inline bool is_before(uint32_t p1, uint32_t p2)
{
    return p2 - p1 < MAX_TIMER_US;
}

_attribute_ram_code_ utime_t uclock_time()
{
    static RAM uint32_t last_clock;
    static RAM utime_t last_uclock;
    uint32_t elapsed_usec = (clock_time() - last_clock) / CLOCK_16M_SYS_TIMER_CLK_1US;
    last_clock += elapsed_usec * CLOCK_16M_SYS_TIMER_CLK_1US;
    return last_uclock += elapsed_usec;
}

_attribute_ram_code_ void uclock_after_sleep()
{
    this_loop_start = uclock_time();
    next_sleep_us = UINT_MAX;
}

_attribute_ram_code_ bool uclock_should_awake(utime_t t)
{
    utime_t now = uclock_time();
    if (is_before(now, t)) {
        uclock_awake_at(t);
        return false;
    }
    return true;
}

_attribute_ram_code_ utime_t uclock_awake_at(utime_t t)
{
    uint32_t sleep_us = t - this_loop_start;
    if (sleep_us < next_sleep_us) {
        if (sleep_us > MAX_TIMER_US) {
            next_sleep_us = MAX_TIMER_US;
        } else {
            next_sleep_us = sleep_us;
        }
    }
    return t;
}

_attribute_ram_code_ utime_t uclock_awake_after(uint32_t usecs)
{
    return uclock_awake_at(uclock_time() + usecs);
}


_attribute_ram_code_ void uclock_before_sleep()
{
    bls_pm_setSuspendMask(SUSPEND_ADV | DEEPSLEEP_RETENTION_ADV | SUSPEND_CONN | DEEPSLEEP_RETENTION_CONN);
    if (next_sleep_us == UINT_MAX) {
        bls_pm_setAppWakeupLowPower(0, 0);
    } else {
        utime_t loop_duration = uclock_time() - this_loop_start;
        // Adjust the next sleep duration taking into acount the clock difference
        if (next_sleep_us <= loop_duration + 128) {
            bls_pm_setSuspendMask(SUSPEND_DISABLE);
        } else {
            next_sleep_us -= loop_duration;
            bls_pm_setAppWakeupLowPower(clock_time() + next_sleep_us * CLOCK_16M_SYS_TIMER_CLK_1US, 1);
        }
    }
}
