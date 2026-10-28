#pragma once
#include "sdkconfig.h"
#ifdef CONFIG_SATELLITE_OTA
void satellite_ota_start(void);
#else
static inline void satellite_ota_start(void) {}
#endif
