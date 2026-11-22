#include "core/stall.h"

#include <string.h>

void stall_watch_init(StallWatch *watch)
{
    memset(watch, 0, sizeof(*watch));
    watch->longest_slot = -1;
}

int stall_watch_update(StallWatch *watch, const Game *game)
{
    int stuck = 0;
    uint32_t slots = game_colonist_slot_count(game);
    for (uint32_t slot = 0; slot < COLONIST_MAX; slot++) {
        const Colonist *colonist =
            slot < slots ? game_colonist_const(game, game_colonist_id_at(game, slot)) : NULL;
        if (colonist == NULL) {
            watch->still[slot] = 0;
            continue;
        }
        TilePos here = {(int16_t)colonist->x, (int16_t)colonist->y, (int16_t)colonist->z};
        TilePos *last = &watch->tile[slot];
        bool same = here.x == last->x && here.y == last->y && here.z == last->z;
        *last = here;
        if (!same || !colonist_is_moving(colonist)) {
            watch->still[slot] = 0;
            continue;
        }
        uint32_t still = ++watch->still[slot];
        if (still == STALL_TICKS) {
            watch->stalls++;
        }
        stuck += still >= STALL_TICKS;
        if (still > watch->longest) {
            watch->longest = still;
            watch->longest_slot = (int)slot;
            watch->longest_tick = game->clock.tick;
        }
    }
    return stuck;
}
