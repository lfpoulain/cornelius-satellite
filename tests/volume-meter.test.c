#include "volume-meter.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    for(int count=1;count<=16;count++) {
        int before=-1;
        for(int volume=0;volume<=100;volume++) {
            int sum=0;
            for(int led=0;led<count;led++) {
                int level=volume_meter_level(volume,led,count);
                if(volume==0) assert(level==0);
                if(volume==100) assert(level==255);
                sum+=level;
            }
            assert(sum>=before); before=sum;
            assert(sum>=volume*count*255/100-1 && sum<=volume*count*255/100);
        }
    }
    assert(volume_meter_level(50,0,7)==255);
    assert(volume_meter_level(50,3,7)==127);
    assert(volume_meter_level(50,4,7)==0);
    assert(volume_meter_level(-10,0,7)==0);
    assert(volume_meter_level(110,6,7)==255);
    assert(volume_meter_level(70,7,7)==0);
    assert(volume_meter_level(70,0,0)==0);
    puts("VOLUME_METER_VALIDATED: all levels 0..100, 1..16 LEDs, partial boundary, monotonicity, clamping");
    return 0;
}
