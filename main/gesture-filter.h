#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Portable temporal classifier. Never emit a direction before a complete motion.
 * Hardware WAVE takes priority. L-R-L / R-L-R without other axes is also a wave;
 * a single reversal is ambiguous and must not execute either lateral command. */
typedef struct {
    bool collecting, release_required, invalid;
    uint16_t seen, last_flags;
    unsigned last_horizontal, reversals, rejected;
    int64_t started, last_active, quiet_since, block_until;
} gesture_filter_t;

static inline uint16_t gesture_filter_feed(gesture_filter_t *s, uint16_t raw, int64_t now)
{
    const uint16_t horizontal=0x0c, wave=0x100;
    if (s->release_required) {
        if (raw) s->quiet_since=-1;
        else {
            if (s->quiet_since<0) s->quiet_since=now;
            if (now-s->quiet_since>=250 && now>=s->block_until) s->release_required=false;
        }
        return 0;
    }
    if (!s->collecting) {
        if (!raw) return 0;
        s->collecting=true; s->seen=0; s->invalid=false;
        s->last_horizontal=0; s->reversals=0; s->started=now;
    }
    if (raw) {
        s->seen|=raw; s->last_active=now;
        // Circular gestures are deliberately unsupported; reject the entire motion
        // instead of turning a circular flag into an unrelated directional command.
        if (raw & (~0x1ffu | 0xc0u)) s->invalid=true;
        unsigned direction=raw & horizontal;
        if (direction==4 || direction==8) {
            if (s->last_horizontal && direction!=s->last_horizontal) s->reversals++;
            s->last_horizontal=direction;
        }
    }
    bool quiet=now-s->last_active>=150;
    bool expired=now-s->started>=1800;
    if ((now-s->started<600 || !quiet) && !expired) return 0;
    uint16_t result=0;
    if (quiet && !s->invalid) {
        if (s->seen & wave) result=wave;
        else if ((s->seen & ~horizontal)==0 && s->reversals>=2) result=wave;
        else if (s->seen && !(s->seen & (s->seen-1))) result=s->seen;
    }
    s->last_flags=s->seen;
    if (!result) s->rejected++;
    s->collecting=false; s->release_required=true;
    s->quiet_since=-1; s->block_until=now+700;
    return result;
}
