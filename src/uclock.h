#pragma once
#include <stdint.h>
#include <stdbool.h>

/** Microsecond clock and timer functions
 *
 * Example that triggers a temperature read every second and collect the result 11 ms later:
 *
 *   bool adquiring_temperature;
 *   utime_t temperature_read_timer;
 *   utime_t adquisition_timer;
 *   void sensor_loop() {
 *     // Read temperature every second
 *     if (uclock_should_awake(temperature_read_timer)) {
 *       temperature_read_timer = uclock_awake_at(temperature_read_timer + ONE_MILLION);
 *       start_temperature_adquisition();
 *       adquiring_temperature = true;
 *       adquisition_timer = uclock_awake_after(11000); // Temperature adquisition requires 11 milliseconds to complete
 *     } else if (adquiring_temperature && uclock_should_awake(adquisition_timer)) {
 *       collect_temperature();
 *       adquiring_temperature = false;
 *     }
 *   }
 */
typedef uint32_t utime_t;

/** Returns a microsecond tick count
 *
 * It is like clock_time() but the unit is 1 microsecond
 * instead of 1/16 of a microsecond.
 *
 * This allows to setup timers of up to 1 hour (in constrast
 * with 260 seconds allowed by clock_time).
 */
utime_t uclock_time();

/** Schedules an awake timer after the given microseconds.
 *
 * Next loop will not sleep for longer than the given number of microseconds, up to one hour.
 * It can also awake earlier and does not have effect on subsequent loops; you need to
 * re-schedule the awake time on each loop.
 *
 * It returns the tick count when we should awake.
 */
utime_t uclock_awake_after(uint32_t usecs);

/** Schedules an awake timer at the given uclock ticks.
 *
 * Next loop will not sleep past the given tick count. It can also awake earlier and does not
 * have effect on subsequent loops; you need to re-schedule the awake timer on each loop.
 *
 * It returns the tick count when we should awake.
 */
utime_t uclock_awake_at(utime_t t);

/** Checks if the awake timer was expired, rescheduling if not
 *
 * It checks if the system tick count is past the given tick count and therefore, the timer is
 * expired. If not, it re-schedules the timer for the next loop.
 *
 * It returns true if the timer is expired, false if not.
 */
bool uclock_should_awake(utime_t t);

/** Performs time keeping duties on each loop, before calling the SDK main loop */
void uclock_before_sleep();

/** Performs time keeping duties on each loop, after calling the SDK main loop */
void uclock_after_sleep();
