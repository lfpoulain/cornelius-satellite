#pragma once
#include "sdkconfig.h"
#include "driver/i2c_master.h"
#ifdef CONFIG_SATELLITE_GESTURES
void gestures_init(i2c_master_bus_handle_t bus);
void gestures_start(void);
bool gestures_ready(void);
#else
static inline void gestures_init(i2c_master_bus_handle_t bus) { (void)bus; }
static inline void gestures_start(void) {}
static inline bool gestures_ready(void) { return false; }
#endif
