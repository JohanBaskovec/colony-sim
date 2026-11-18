#include "core/occupancy.h"

#include <stdlib.h>
#include <string.h>

static bool valid_being(BeingId being)
{
    return being != OCCUPANCY_NOBODY && being <= OCCUPANCY_BEINGS;
}

static bool in_bounds(const Occupancy *occupancy, TilePos tile)
{
    return tile.x >= 0 && tile.y >= 0 && tile.z >= 0 && tile.x < occupancy->width &&
           tile.y < occupancy->height && tile.z < occupancy->depth;
}

static size_t index_of(const Occupancy *occupancy, TilePos tile)
{
    return ((size_t)tile.z * (size_t)occupancy->height + (size_t)tile.y) *
               (size_t)occupancy->width +
           (size_t)tile.x;
}

static bool same_tile(TilePos a, TilePos b)
{
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

bool occupancy_init(Occupancy *occupancy, int width, int height, int depth)
{
    memset(occupancy, 0, sizeof(*occupancy));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t tiles = (size_t)width * (size_t)height * (size_t)depth;
    occupancy->claims = calloc(tiles, sizeof(*occupancy->claims));
    occupancy->present = calloc(tiles, sizeof(*occupancy->present));
    if (occupancy->claims == NULL || occupancy->present == NULL) {
        occupancy_free(occupancy);
        return false;
    }
    occupancy->width = width;
    occupancy->height = height;
    occupancy->depth = depth;
    return true;
}

void occupancy_free(Occupancy *occupancy)
{
    free(occupancy->claims);
    free(occupancy->present);
    memset(occupancy, 0, sizeof(*occupancy));
}

static void unplace(Occupancy *occupancy, OccupancyBeing *state)
{
    if (state->placed) {
        uint8_t *count = &occupancy->present[index_of(occupancy, state->at)];
        if (*count > 0) {
            (*count)--;
        }
        state->placed = false;
    }
}

void occupancy_place(Occupancy *occupancy, BeingId being, TilePos at)
{
    if (!valid_being(being) || occupancy->claims == NULL) {
        return;
    }
    OccupancyBeing *state = &occupancy->beings[being];
    if (state->placed && same_tile(state->at, at)) {
        return;
    }
    unplace(occupancy, state);
    if (in_bounds(occupancy, at)) {
        uint8_t *count = &occupancy->present[index_of(occupancy, at)];
        if (*count < UINT8_MAX) {
            (*count)++;
        }
        state->at = at;
        state->placed = true;
    }
}

void occupancy_release(Occupancy *occupancy, BeingId being)
{
    if (!valid_being(being) || occupancy->claims == NULL) {
        return;
    }
    OccupancyBeing *state = &occupancy->beings[being];
    if (state->claiming) {
        BeingId *holder = &occupancy->claims[index_of(occupancy, state->claim)];
        if (*holder == being) {
            *holder = OCCUPANCY_NOBODY;
        }
        state->claiming = false;
    }
    state->forced = false;
}

void occupancy_remove(Occupancy *occupancy, BeingId being)
{
    if (!valid_being(being) || occupancy->claims == NULL) {
        return;
    }
    occupancy_release(occupancy, being);
    unplace(occupancy, &occupancy->beings[being]);
    occupancy->beings[being].yielding = false;
}

void occupancy_set_yielding(Occupancy *occupancy, BeingId being, bool yielding)
{
    if (valid_being(being)) {
        occupancy->beings[being].yielding = yielding;
    }
}

BeingId occupancy_holder(const Occupancy *occupancy, TilePos tile)
{
    if (occupancy->claims == NULL || !in_bounds(occupancy, tile)) {
        return OCCUPANCY_NOBODY;
    }
    return occupancy->claims[index_of(occupancy, tile)];
}

bool occupancy_holds(const Occupancy *occupancy, BeingId being, TilePos tile)
{
    return valid_being(being) && occupancy_holder(occupancy, tile) == being;
}

/* Whether `being` may stop on the tile, as if it were (not) yielding. */
static bool may_stop(const Occupancy *occupancy, BeingId being, TilePos tile, bool yielding)
{
    if (!valid_being(being) || occupancy->claims == NULL || !in_bounds(occupancy, tile)) {
        return false;
    }
    BeingId holder = occupancy->claims[index_of(occupancy, tile)];
    return holder == OCCUPANCY_NOBODY || holder == being ||
           (!yielding && occupancy->beings[holder].yielding);
}

bool occupancy_can_stop(const Occupancy *occupancy, BeingId being, TilePos tile)
{
    return valid_being(being) &&
           may_stop(occupancy, being, tile, occupancy->beings[being].yielding);
}

/* Moves the claim of `being` to the tile, whoever held it losing it. */
static void take(Occupancy *occupancy, BeingId being, TilePos tile)
{
    OccupancyBeing *state = &occupancy->beings[being];
    BeingId *holder = &occupancy->claims[index_of(occupancy, tile)];
    if (*holder == being) {
        return;
    }
    occupancy_release(occupancy, being);
    if (*holder != OCCUPANCY_NOBODY) {
        occupancy->beings[*holder].claiming = false; /* Displaced. */
    }
    *holder = being;
    state->claim = tile;
    state->claiming = true;
}

bool occupancy_claim(Occupancy *occupancy, BeingId being, TilePos tile)
{
    if (!occupancy_can_stop(occupancy, being, tile)) {
        return false;
    }
    take(occupancy, being, tile);
    occupancy->beings[being].forced = false;
    return true;
}

bool occupancy_force(Occupancy *occupancy, BeingId being, TilePos tile)
{
    if (!valid_being(being) || occupancy->claims == NULL || !in_bounds(occupancy, tile)) {
        return false;
    }
    BeingId holder = occupancy->claims[index_of(occupancy, tile)];
    if (holder != OCCUPANCY_NOBODY && holder != being && occupancy->beings[holder].forced) {
        return false; /* Someone else lies there already. */
    }
    take(occupancy, being, tile);
    occupancy->beings[being].forced = true;
    return true;
}

int occupancy_others_on(const Occupancy *occupancy, BeingId being, TilePos tile)
{
    if (occupancy->claims == NULL || !in_bounds(occupancy, tile)) {
        return 0;
    }
    int count = occupancy->present[index_of(occupancy, tile)];
    const OccupancyBeing *self = valid_being(being) ? &occupancy->beings[being] : NULL;
    if (self != NULL && self->placed && same_tile(self->at, tile) && count > 0) {
        count--;
    }
    return count;
}

bool occupancy_stop_filter(const Map *map, int x, int y, int z, void *userdata)
{
    (void)map;
    const OccupancyStopper *stopper = userdata;
    return may_stop(stopper->occupancy, stopper->being,
                    (TilePos){(int16_t)x, (int16_t)y, (int16_t)z}, false);
}
