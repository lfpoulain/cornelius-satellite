#include "gesture-filter.h"
#include <assert.h>
#include <stdio.h>

static uint16_t settle(gesture_filter_t *s,int64_t from,int64_t until) {
    uint16_t result=0;
    for(int64_t t=from;t<=until;t+=50) { uint16_t value=gesture_filter_feed(s,0,t); if(value) { assert(!result); result=value; } }
    return result;
}
int main(void) {
    for(unsigned bit=0;bit<9;bit++) {
        gesture_filter_t s={0};
        assert(!gesture_filter_feed(&s,(uint16_t)(1u<<bit),0));
        assert(!settle(&s,50,550));
        assert(gesture_filter_feed(&s,0,600)==((bit==6 || bit==7)?0:(1u<<bit)));
    }
    gesture_filter_t wave={0};
    assert(!gesture_filter_feed(&wave,4,0));
    assert(!gesture_filter_feed(&wave,8,150));
    assert(!gesture_filter_feed(&wave,4,300));
    assert(settle(&wave,350,650)==0x100);
    assert(!gesture_filter_feed(&wave,8,700)); // suppressed until hand withdrawn
    assert(!settle(&wave,750,1500));
    assert(!gesture_filter_feed(&wave,8,1550));
    assert(settle(&wave,1600,2200)==8);
    gesture_filter_t reverse={0};
    gesture_filter_feed(&reverse,4,0);gesture_filter_feed(&reverse,8,150);
    assert(!settle(&reverse,200,650)); assert(reverse.rejected==1);
    gesture_filter_t hardware={0};
    gesture_filter_feed(&hardware,4,0);gesture_filter_feed(&hardware,0x108,100);
    assert(settle(&hardware,150,650)==0x100);
    gesture_filter_t mixed={0};
    gesture_filter_feed(&mixed,4,0);gesture_filter_feed(&mixed,1,100);
    assert(!settle(&mixed,150,650));
    gesture_filter_t continuous={0};
    for(int t=0;t<=3000;t+=50) assert(!gesture_filter_feed(&continuous,4,t));
    assert(continuous.rejected==1);
    gesture_filter_t invalid={0};
    gesture_filter_feed(&invalid,0x200,0);assert(!settle(&invalid,50,650));
    gesture_filter_t circular={0};
    gesture_filter_feed(&circular,4,0);gesture_filter_feed(&circular,0x40,150);
    assert(!settle(&circular,200,650));
    puts("GESTURE_FILTER_VALIDATED: 7 gestures, both circles disabled, mixed circle rejected, wave priority, ambiguity, release and invalid flags");
    return 0;
}
