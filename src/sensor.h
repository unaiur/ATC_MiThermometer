#pragma once

void sensor_init(void);
void sensor_turn_off(void);
bool sensor_is_idle(void);
bool sensor_is_shtc3(void);
bool sensor_read(void);
