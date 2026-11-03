#pragma once
#include <stdint.h>
/* Filled ring with one partially lit boundary LED: 0=dark, 100=full ring. */
static inline uint8_t volume_meter_level(int volume,int led,int count) {
    if (count<=0 || led<0 || led>=count) return 0;
    if (volume<0) volume=0;
    if (volume>100) volume=100;
    int units=volume*count-led*100;
    if (units<=0) return 0;
    if (units>=100) return 255;
    return (uint8_t)(units*255/100);
}
