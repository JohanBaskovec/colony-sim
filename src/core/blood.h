/*
 * Blood on the ground (docs/HEALTH.md §9.1): those who bleed leave drops
 * behind them as they walk, more and larger the worse they bleed, and a pool
 * grows under one who bleeds standing or lying still. Stains dry (darken)
 * and fade within BLOOD_STAIN_LIFE_TICKS.
 *
 * For the eye only, for now: nothing in the simulation reads stains, they
 * have a random stream of their own and game_hash() leaves them out. The
 * newest BLOOD_STAIN_MAX are kept (a ring: the oldest go first).
 */
#ifndef COLONY_CORE_BLOOD_H
#define COLONY_CORE_BLOOD_H

#include "core/game_clock.h"
#include "core/health.h"
#include "core/map.h"
#include "core/rng.h"

#include <stdbool.h>
#include <stdint.h>

#define BLOOD_STAIN_MAX 2048
/* How long a stain lasts: it dries over the first 60 %, fades over the rest. */
#define BLOOD_STAIN_LIFE_TICKS (2 * TICKS_PER_HOUR)
/* Blood lost per hour (share of the whole) at which drops are the largest
 * and most frequent: a serious wound. */
#define BLOOD_HEAVY_BLEEDING 0.08f
/* Who walks takes a step every this many ticks (as drawn); drops fall on steps. */
#define BLOOD_STEP_TICKS 16
/* Who stands still feeds the pool under it every this many ticks. */
#define BLOOD_POOL_FEED_TICKS 10
/* A pool's radius (tiles): where it starts, how much it grows at each feeding
 * at the heaviest bleeding, and how far it may spread (also at the heaviest). */
#define BLOOD_POOL_START 0.06f
#define BLOOD_POOL_GROWTH 0.015f
#define BLOOD_POOL_MAX 0.32f
/* A pool within this distance (tiles) of where one bleeds is fed. */
#define BLOOD_POOL_REACH 0.3f

typedef enum BloodStainKind {
    BLOOD_DROP,
    BLOOD_POOL,
} BloodStainKind;

typedef struct BloodStain {
    float x, y; /* World coordinates (tile centers at +0.5). */
    int16_t z;
    uint8_t kind;  /* BloodStainKind */
    uint8_t shape; /* Where its droplets lie, for the eye. */
    float size;    /* Radius, tiles. */
    float smear;   /* Drawn out along the walk, 0..1 (drops). */
    float dir_x;   /* Which way it was walking (a unit vector; 0 when still). */
    float dir_y;
    uint64_t tick; /* When it fell (a pool: when it was last fed). */
} BloodStain;

typedef struct BloodStains {
    BloodStain list[BLOOD_STAIN_MAX];
    int next;  /* Where the next stain goes. */
    int count; /* Up to BLOOD_STAIN_MAX. */
} BloodStains;

/* 0..1: how hard a body bleeds, against BLOOD_HEAVY_BLEEDING; 0 for the dead. */
float blood_intensity(const Health *health);

/* Someone that may bleed, at a tick. */
typedef struct BloodSource {
    float x, y; /* Where it is now (world coordinates). */
    int z;
    float dir_x, dir_y; /* Which way it walks (any length; 0 when still). */
    bool moving;
    uint32_t slot; /* Its slot: those side by side do not step in time. */
} BloodSource;

/*
 * Lets `health`'s bleeding fall at a tick: drops on its steps while it moves
 * (where blood can lie: not on water, rock or walls), a pool fed while it
 * stays still. Returns how many stains fell or grew.
 */
int blood_bleed(BloodStains *stains, Rng *rng, const Map *map, const Health *health,
                const BloodSource *source, uint64_t tick);

/* The `index`-th most recent stain (0: the last one), NULL past the count. */
const BloodStain *blood_recent(const BloodStains *stains, int index);

/* How a stain looks at a tick: how dry (0 fresh .. 1), how opaque, how much
 * it shines (fresh blood). False once it is gone. */
typedef struct BloodLook {
    float dryness;
    float opacity;
    float shine;
} BloodLook;

bool blood_look(const BloodStain *stain, uint64_t now, BloodLook *look);

#endif /* COLONY_CORE_BLOOD_H */
