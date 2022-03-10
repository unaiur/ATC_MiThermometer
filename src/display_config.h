#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "app_config.h"

// SUPPORTED DISPLAY TYPES
#define EPD 1 /* Electronic Paper Display */
#define LCD 2 /* Liquid Cristal Display */

// DISPLAY CONFIGURATION
#if DEVICE_TYPE == DEVICE_CGG1
#define DISPLAY_TYPE           EPD
#define DISPLAY_BUFF_LEN       18

#elif DEVICE_TYPE == DEVICE_CGDK2
#define DISPLAY_TYPE           LCD
#define DISPLAY_BUFF_LEN       12

#else
#error "Set DEVICE_TYPE!"
#endif

