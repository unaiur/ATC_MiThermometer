#pragma once
#include <stdint.h>
#include <stdbool.h>

typedef uint32_t utime_t;

utime_t uclock_time();

void uclock_after_sleep();
void uclock_before_sleep();
bool uclock_should_awake(utime_t t);
utime_t uclock_awake_after(uint32_t usecs);
utime_t uclock_awake_at(utime_t t);
