/*
 * What hangs in the crowns of the trees (docs/TREES.md §7): fruit and nuts
 * ripe on the tree, dead branches and twigs not yet fallen. They are not
 * items of the ground (core/item.h): nobody hauls or eats them, they do not
 * rot, and they hang in foliage of any density, up to a room by its density
 * (CROWN_ROOM_*, in item bulk). They become ordinary items when they fall to
 * the ground below (now and then, sooner in a storm, at once when their
 * foliage goes) or are picked by a gatherer (core/gather_zone.h).
 *
 * Each foliage tile has a list of what hangs there, one entry per item, in a
 * pool of slots (like the items' pool). The game hangs fruit in its season
 * and the dead wood its trees shed (tree_shed()); a running game reaches the
 * crowns through Map.crowns. The colony's known world (core/known.h) has a
 * picture of them of its own: what hung in a crown when it was last seen.
 */
#ifndef COLONY_CORE_CROWN_H
#define COLONY_CORE_CROWN_H

#include "core/item.h"
#include "core/map.h"

#include <stdbool.h>
#include <stdint.h>

/* Room in a block of foliage, in item bulk (ITEM_TILE_BULK on the ground),
 * by density (tuning). */
#define CROWN_ROOM_DENSE 200.0f
#define CROWN_ROOM_MEDIUM 100.0f
#define CROWN_ROOM_SPARSE 40.0f

/* Chance per hour that a ripe fruit falls, × (1 + days it has hung /
 * CROWN_FRUIT_RIPENING_DAYS): the longer it hangs, the sooner it falls
 * (tuning: about half fall within four days). */
#define CROWN_FRUIT_FALL_CHANCE 0.005f
#define CROWN_FRUIT_RIPENING_DAYS 5.0f
/* Chance per hour that a dead branch or twig falls (about two days). */
#define CROWN_DEADWOOD_FALL_CHANCE 0.02f
/* × these chances in a storm: fruit, and dead wood (most comes down). */
#define CROWN_STORM_FRUIT 4.0f
#define CROWN_STORM_DEADWOOD 10.0f

/* One item hanging in a crown. */
typedef struct CrownItem {
    uint8_t type;    /* ItemType; ITEM_NONE for a free slot. */
    uint8_t quality; /* ItemQuality. */
    uint16_t hit_points;
    WoodMask woods; /* As ItemStack.woods (dead branches: their tree's). */
    uint16_t day;   /* The game day (tick / TICKS_PER_DAY) it came to hang there. */
    uint32_t next;  /* Next item of the same tile: slot + 1, 0 for none. */
    uint32_t tile;  /* Tile index. */
} CrownItem;

typedef struct Crowns {
    int width;
    int height;
    int depth;
    uint32_t *heads; /* Per tile: its newest item, slot + 1, 0 for none. */
    CrownItem *pool;
    uint32_t pool_capacity;
    uint32_t pool_used; /* Slots ever used. */
    uint32_t free_head; /* First free slot + 1, chained through CrownItem.next. */
    long count;         /* Items hanging. */
    /* Today (game day, set by the game): what comes to hang is dated with it
     * (CrownItem.day). */
    int day;
    long totals[ITEM_TYPE_COUNT];
    /* Incremented whenever something comes to hang or goes: gatherers who
     * found nothing look again. */
    uint32_t revision;
} Crowns;

bool crowns_init(Crowns *crowns, int width, int height, int depth);
void crowns_free(Crowns *crowns);

/* Room of a block of foliage of a terrain, in bulk (0 for what is not
 * foliage). */
float crown_room(Terrain terrain);

/* How many items of `type` may still hang on a tile (its foliage's room less
 * what hangs there; 0 where there is no foliage). */
int crowns_room(const Crowns *crowns, const Map *map, int x, int y, int z, ItemType type);

/* Hangs an item on a tile when there is room (crowns_room()). Returns
 * whether it hangs. */
bool crowns_add(Crowns *crowns, const Map *map, int x, int y, int z, CrownItem item);

/* Number of items of `types` hanging on a tile. */
int crowns_count(const Crowns *crowns, int x, int y, int z, ItemTypeMask types);

/* What hangs on a tile, newest first: up to `max` items in `out`. Returns
 * how many hang there (maybe more than `max`). */
int crowns_list(const Crowns *crowns, int x, int y, int z, CrownItem *out, int max);

/* The item as an ordinary item of the ground (one of it). */
ItemStack crown_item_stack(CrownItem item);

/* Takes up to `max` items of `types` off a tile (newest first) into `out`
 * (room for `max`). Returns how many were taken. */
int crowns_take(Crowns *crowns, int x, int y, int z, ItemTypeMask types, CrownItem *out, int max);

/* Makes the list of the tile at `index` of `dst` the same as `src`'s (the
 * known world's picture of the crowns, core/known.h); nothing changes when
 * they already agree. False when out of memory. */
bool crowns_copy_tile(Crowns *dst, const Crowns *src, size_t index);

/* Lets an item that hung on (x, y, z) fall to the ground below
 * (tree_landing_level(), items_drop() there: lost over water or in a
 * wall). Returns whether it
 * found room. */
bool crown_drop(const Map *map, Items *items, int x, int y, int z, CrownItem item);

/* Everything hanging on a tile falls (its foliage goes). Returns how many
 * fell. */
int crowns_drop_tile(Crowns *crowns, const Map *map, Items *items, int x, int y, int z);

/* What a pass over the crowns needs: the hour's rolls, the day, the
 * weather, which fruit is in season. */
typedef struct CrownHour {
    uint64_t seed; /* With the slot and `hour`: each item's roll. */
    uint64_t hour; /* The game hour (tick / TICKS_PER_HOUR). */
    int day;       /* The game day (tick / TICKS_PER_DAY). */
    int day_of_year;
    bool storm;
} CrownHour;

/* Whether a fruit of `type` is still in its season on a day of the year (out
 * of it, what is left on the trees falls); true for what is not fruit. */
bool crown_fruit_in_season(ItemType type, int day_of_year);

/* Chance that an item hanging falls within the hour: fruit sooner the longer
 * it hung, dead wood about evenly, more in a storm; 1 for fruit out of its
 * season. */
float crown_fall_chance(CrownItem item, const CrownHour *hour);

/* An hour for the slots begin..end-1 of the pool: what falls falls
 * (crown_fall_chance()), and so does what hangs where there is no foliage
 * any more. Returns how many fell. */
int crowns_hour(Crowns *crowns, const Map *map, Items *items, uint32_t begin, uint32_t end,
                const CrownHour *hour);

uint64_t crowns_hash(const Crowns *crowns, uint64_t hash);

/* Whether the lists, totals and count agree (tests, debug). */
bool crowns_check(const Crowns *crowns);

#endif /* COLONY_CORE_CROWN_H */
