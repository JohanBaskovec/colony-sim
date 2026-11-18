#include "core/blood.h"

#include "core/dirt.h"
#include "core/util.h"

#include <math.h>

/* Below this intensity, nothing falls (a wound nearly clotted). */
#define BLOOD_MIN_INTENSITY 0.03f
/* Pools fed are searched among this many of the newest stains (a pool fed
 * moves to the newest place). */
#define BLOOD_POOL_SEARCH 256
/* Shares of a stain's life: drying, fading, shining. */
#define BLOOD_DRY_SHARE 0.6f
#define BLOOD_FADE_FROM 0.6f
#define BLOOD_SHINE_SHARE 0.35f
#define BLOOD_OPACITY 0.9f

float blood_intensity(const Health *health)
{
    if (health_is_dead(health) || health->injury_count == 0) {
        return 0.0f;
    }
    return clampf(health_bleeding(health) / BLOOD_HEAVY_BLEEDING, 0.0f, 1.0f);
}

static void push(BloodStains *stains, BloodStain stain)
{
    stains->list[stains->next] = stain;
    stains->next = (stains->next + 1) % BLOOD_STAIN_MAX;
    if (stains->count < BLOOD_STAIN_MAX) {
        stains->count++;
    }
}

const BloodStain *blood_recent(const BloodStains *stains, int index)
{
    if (index < 0 || index >= stains->count) {
        return NULL;
    }
    return &stains->list[(stains->next - 1 - index + BLOOD_STAIN_MAX) % BLOOD_STAIN_MAX];
}

static float rng_between(Rng *rng, float min, float max)
{
    return min + (max - min) * rng_next_float(rng);
}

static bool can_lie(const Map *map, float x, float y, int z)
{
    return dirt_can_lie(map, (int)floorf(x), (int)floorf(y), z);
}

/* Drops on a step: one or two, larger and more likely the worse it bleeds;
 * some drawn out along the walk. */
static int drip(BloodStains *stains, Rng *rng, const Map *map, const BloodSource *source,
                float intensity, uint64_t tick)
{
    float length = sqrtf(source->dir_x * source->dir_x + source->dir_y * source->dir_y);
    float dir_x = length > 0.0f ? source->dir_x / length : 0.0f;
    float dir_y = length > 0.0f ? source->dir_y / length : 0.0f;
    float chance = clampf(0.1f + 1.5f * intensity, 0.0f, 1.0f);
    int count = 1 + (rng_next_float(rng) < 0.6f * intensity);
    int fell = 0;
    for (int i = 0; i < count; i++) {
        if (rng_next_float(rng) >= chance) {
            continue;
        }
        BloodStain drop = {
            .x = source->x + rng_between(rng, -0.12f, 0.12f),
            .y = source->y + rng_between(rng, -0.10f, 0.04f),
            .z = (int16_t)source->z,
            .kind = BLOOD_DROP,
            .shape = (uint8_t)rng_range_u32(rng, 256),
            .size = rng_between(rng, 0.025f, 0.04f) * (0.8f + 1.4f * intensity),
            .smear = rng_next_float(rng) < 0.4f * intensity ? rng_between(rng, 0.3f, 0.9f) : 0.0f,
            .dir_x = dir_x,
            .dir_y = dir_y,
            .tick = tick,
        };
        if (can_lie(map, drop.x, drop.y, source->z)) {
            push(stains, drop);
            fell++;
        }
    }
    return fell;
}

/* The pool under someone still: the nearest one grows (and moves to the
 * newest place), else a new one starts. */
static int pool(BloodStains *stains, Rng *rng, const Map *map, const BloodSource *source,
                float intensity, uint64_t tick)
{
    if (!can_lie(map, source->x, source->y, source->z)) {
        return 0;
    }
    float cap = BLOOD_POOL_MAX * (0.4f + 0.6f * intensity);
    for (int i = 0; i < stains->count && i < BLOOD_POOL_SEARCH; i++) {
        BloodStain *stain =
            &stains->list[(stains->next - 1 - i + BLOOD_STAIN_MAX) % BLOOD_STAIN_MAX];
        float dx = stain->x - source->x, dy = stain->y - source->y;
        if (stain->kind != BLOOD_POOL || stain->size <= 0.0f || stain->z != source->z ||
            dx * dx + dy * dy > BLOOD_POOL_REACH * BLOOD_POOL_REACH ||
            tick - stain->tick >= BLOOD_STAIN_LIFE_TICKS) {
            continue;
        }
        BloodStain fed = *stain;
        stain->size = 0.0f; /* Moved to the newest place. */
        fed.size = fed.size < cap ? fminf(cap, fed.size + BLOOD_POOL_GROWTH * intensity) : fed.size;
        fed.tick = tick;
        push(stains, fed);
        return 1;
    }
    push(stains, (BloodStain){
                     .x = source->x,
                     .y = source->y,
                     .z = (int16_t)source->z,
                     .kind = BLOOD_POOL,
                     .shape = (uint8_t)rng_range_u32(rng, 256),
                     .size = BLOOD_POOL_START,
                     .tick = tick,
                 });
    return 1;
}

int blood_bleed(BloodStains *stains, Rng *rng, const Map *map, const Health *health,
                const BloodSource *source, uint64_t tick)
{
    float intensity = blood_intensity(health);
    if (intensity < BLOOD_MIN_INTENSITY) {
        return 0;
    }
    uint64_t phase = tick + (uint64_t)source->slot * 7u;
    if (source->moving) {
        return phase % BLOOD_STEP_TICKS == 0 ? drip(stains, rng, map, source, intensity, tick) : 0;
    }
    return phase % BLOOD_POOL_FEED_TICKS == 0 ? pool(stains, rng, map, source, intensity, tick) : 0;
}

bool blood_look(const BloodStain *stain, uint64_t now, BloodLook *look)
{
    if (stain->size <= 0.0f || now < stain->tick || now - stain->tick >= BLOOD_STAIN_LIFE_TICKS) {
        return false;
    }
    float age = (float)(now - stain->tick) / (float)BLOOD_STAIN_LIFE_TICKS;
    look->dryness = clampf(age / BLOOD_DRY_SHARE, 0.0f, 1.0f);
    look->opacity =
        age < BLOOD_FADE_FROM
            ? BLOOD_OPACITY
            : BLOOD_OPACITY * (1.0f - (age - BLOOD_FADE_FROM) / (1.0f - BLOOD_FADE_FROM));
    look->shine = age < BLOOD_SHINE_SHARE ? 1.0f - age / BLOOD_SHINE_SHARE : 0.0f;
    return true;
}
