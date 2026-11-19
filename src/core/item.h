/*
 * Items lying on the ground (wood, stone...), owned by the Game.
 *
 * Every item lying on a tile is a record of its own (Item, in a pool), with
 * its own quality, hit points, rot and spot: items are never merged into
 * averages (docs/ITEMS.md). The items of a tile form a list, newest first.
 * For the callers that think in stacks, each tile also has a summary
 * (Items.stacks): the type of its newest item, how many items of that type
 * lie there and their average condition. A tile still takes one type at a
 * time through items_add(); items that dry or get wet change type one by one,
 * so a tile may hold two types for a while.
 *
 * Dropping more items than a tile can take spreads them over nearby reachable
 * tiles.
 *
 * Every item has a quality, a value from 0 to 100 (how good the material is,
 * which carries over to what is made from it), and hit points (its
 * condition). Some types are always perfect: stone blocks are always of the
 * best quality and cannot be damaged.
 *
 * Food rots while it lies on a tile: it loses hit points, faster when warm,
 * not at all when frozen, a little slower under a roof, faster in the rain,
 * and is gone when they run out (see item_rot_rate()). Bodies rot the same
 * way; they are not food. Fresh grass does not dry in the rain. Items in
 * hands do not rot.
 */
#ifndef COLONY_CORE_ITEM_H
#define COLONY_CORE_ITEM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct Map; /* core/map.h includes this header. */

typedef enum ItemType {
    ITEM_NONE = 0,
    ITEM_WOOD,
    ITEM_STONE_BLOCK,
    ITEM_BLACKBERRIES,
    ITEM_COBBLE,
    ITEM_BRANCHES,
    ITEM_TWIGS,
    ITEM_WET_TWIGS, /* Picked up from wet ground, or rained on: they dry into twigs. */
    ITEM_FRESH_GRASS,
    ITEM_GRASS,
    ITEM_FLINT,
    ITEM_CHOPPER,
    ITEM_FLAKE,
    ITEM_HAND_AXE,
    ITEM_THROWING_SPEAR,
    ITEM_THRUSTING_SPEAR,
    ITEM_THROWING_SPEAR_HARDENED, /* Its point dried and charred in a fire. */
    ITEM_THRUSTING_SPEAR_HARDENED,
    ITEM_HAZELNUTS,
    ITEM_MUSHROOMS,
    ITEM_WILD_GARLIC,
    ITEM_NETTLES,
    ITEM_PLANTAIN,
    ITEM_WILD_CARROTS,
    ITEM_CATTAIL_RHIZOMES,
    ITEM_DIGGING_STICK,
    ITEM_DIGGING_STICK_HARDENED,
    ITEM_ROASTED_RHIZOMES,
    ITEM_ROASTED_CARROTS,
    ITEM_WOOD_SORREL,
    ITEM_WILD_STRAWBERRIES,
    ITEM_BILBERRIES,
    ITEM_ROSE_HIPS,
    /* Fibres cut from low plants: bedding, thatch and fuel (docs/PLANTS.md §10). */
    ITEM_MOSS,
    ITEM_BRACKEN,
    ITEM_HEATHER,
    ITEM_REEDS,
    /* Bodies (docs/HEALTH.md §8): a settler's, an animal's carcass by species. */
    ITEM_SETTLER_BODY,
    ITEM_ROE_DEER_CARCASS,
    ITEM_RED_DEER_CARCASS,
    ITEM_BOAR_CARCASS,
    ITEM_HARE_CARCASS,
    ITEM_WOLF_CARCASS,
    ITEM_FOX_CARCASS,
    ITEM_TYPE_COUNT
} ItemType;

/* Groups of item types, e.g. for storage filters. */
typedef enum ItemCategory {
    ITEM_CATEGORY_FOOD,
    ITEM_CATEGORY_RESOURCES,
    ITEM_CATEGORY_TOOLS,
    ITEM_CATEGORY_WEAPONS,
    ITEM_CATEGORY_BODIES, /* Not stored unless the player allows it. */
    ITEM_CATEGORY_COUNT
} ItemCategory;

/* Bit mask with one bit per ItemType. */
typedef uint64_t ItemTypeMask;
_Static_assert(ITEM_TYPE_COUNT <= 64, "item types must fit in an ItemTypeMask");

#define ITEM_TYPE_BIT(type) ((ItemTypeMask)1u << (unsigned)(type))

/* What takes a spark: dry grass, bracken and moss. */
#define ITEM_TINDER_TYPES                                                                          \
    (ITEM_TYPE_BIT(ITEM_GRASS) | ITEM_TYPE_BIT(ITEM_BRACKEN) | ITEM_TYPE_BIT(ITEM_MOSS))

/*
 * Bulk is how much room an item takes, packing gaps included, in units of
 * about half a liter. A tile holds ITEM_TILE_BULK of it (a heap of about
 * 1 m² × 50 cm), so the stack limit of a type is ITEM_TILE_BULK / bulk.
 */
#define ITEM_TILE_BULK 1000.0f

/* Quality: a value from ITEM_QUALITY_WORST to ITEM_QUALITY_BEST. The
 * ordinary item (a tool that works at ×1, a structure of the usual hit points)
 * is ITEM_QUALITY_STANDARD. */
typedef int ItemQuality;
#define ITEM_QUALITY_WORST 0
#define ITEM_QUALITY_STANDARD 50
#define ITEM_QUALITY_BEST 100
/* What is made from materials is at best this many points above their average
 * quality (a master with a coarse flint makes at best a rough hand axe). */
#define ITEM_QUALITY_CAP_BONUS 20

typedef struct ItemDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    ItemCategory category;
    float weight;       /* Kilograms per item. */
    float bulk;         /* Room taken per item (see ITEM_TILE_BULK). */
    float nutrition;    /* Food need restored by eating one item; 0 = not food. */
    int max_hit_points; /* Hit points of one item in perfect condition. */
    bool has_quality;   /* false: always ITEM_QUALITY_BEST. */
    bool damageable;    /* false: always at max_hit_points. */
    /* Items lying on a tile turn into `transforms_into` after
     * `transform_hours` out of the rain (fresh grass and wet twigs dry);
     * ITEM_NONE: never. */
    ItemType transforms_into;
    int transform_hours;
    /* Items lying out in the rain turn into `wets_into` (dry twigs get wet);
     * ITEM_NONE: never. */
    ItemType wets_into;
    /* Days a pristine item lasts lying in the open at
     * ITEM_ROT_REFERENCE_CELSIUS before it has rotted away; 0: never rots.
     * A type never both rots and transforms. */
    float rot_days;
} ItemDef;

/* Rot runs at ×1 at this temperature in the open, twice as fast every
 * ITEM_ROT_DOUBLING_CELSIUS warmer (at most ×ITEM_ROT_RATE_MAX), half as
 * fast every as many degrees cooler, and stops at or below
 * ITEM_ROT_FROZEN_CELSIUS; under a roof (built or natural) it runs at
 * ×ITEM_ROT_SHELTERED, out of sun and rain (tuning). */
#define ITEM_ROT_REFERENCE_CELSIUS 15.0f
#define ITEM_ROT_DOUBLING_CELSIUS 10.0f
#define ITEM_ROT_FROZEN_CELSIUS 0.0f
#define ITEM_ROT_RATE_MAX 4.0f
#define ITEM_ROT_SHELTERED 0.75f
/* Rot of food out in the rain (×), wet through. */
#define ITEM_ROT_RAINED_ON 1.5f
/* Rot units per hit point (see ItemStack.progress). */
#define ITEM_ROT_STEP 1000

/* Items of one type: on a tile, in a settler's hands, or about to be added. */
typedef struct ItemStack {
    uint8_t type;    /* ItemType, ITEM_NONE when empty. */
    uint8_t quality; /* ItemQuality, average of the items. */
    uint16_t count;
    uint16_t hit_points; /* Per item, 1..max_hit_points, average of the items. */
    /* Types that transform: hours spent lying on a tile toward the
     * transformation. Types that rot: rot toward the next hit point lost, in
     * 1/ITEM_ROT_STEP of one. Average of the items; 0 for other types. */
    uint16_t progress;
    /* Bodies: the share of its weight eaten, in 1/ITEM_EATEN_FULL, average
     * of the items; 0 for a whole one and for other types. Eating takes
     * weight, never hit points (those are what rot takes). */
    uint16_t eaten;
    /* A body: whose it was (Corpses, core/corpse.h), 0 for one nobody kept the
     * record of. Bodies of different beings never share a stack. */
    uint16_t corpse;
    /* A load in hand (see items_pick_up()): the very items, a list in the
     * item pool (slot + 1 of the first), the fields above their summary. 0
     * for a stack that is only numbers (what to add, what a tile holds):
     * functions taking a stack by value never use this field. */
    uint32_t held;
} ItemStack;

#define ITEM_EATEN_FULL 10000

/* One item lying on a tile. Its spot in the tile and its angle are for the
 * eye only: no rule reads them. */
typedef struct Item {
    uint8_t type;    /* ItemType, ITEM_NONE for a free slot. */
    uint8_t quality; /* ItemQuality. */
    uint8_t spot_x;  /* Where it lies in its tile, in 1/256 of a tile from the left... */
    uint8_t spot_y;  /* ...and from the top. */
    uint16_t hit_points;
    uint16_t progress;   /* As ItemStack.progress, for this item. */
    uint16_t eaten;      /* As ItemStack.eaten, for this item. */
    uint16_t corpse;     /* As ItemStack.corpse. */
    uint16_t generation; /* Bumped whenever the slot is freed. */
    uint8_t angle;       /* How it is turned, in 1/256 of a turn. */
    uint8_t flags;       /* Reserved. */
    uint32_t next;       /* Next item of the same tile: slot + 1, 0 for none. */
    uint32_t tile;       /* Tile index, ITEM_TILE_HELD for an item in a load. */
} Item;

#define ITEM_TILE_HELD UINT32_MAX

typedef struct Items {
    int width;
    int height;
    int depth;
    /* Summary of each tile (see the top of this file): the type of its
     * newest item, the count and average condition of its items of that
     * type. Derived from the items: never written but by this module. */
    ItemStack *stacks;
    long totals[ITEM_TYPE_COUNT]; /* Items of each type on the whole map. */
    uint32_t *level_stacks;       /* Tiles with items on each level (searches skip empty levels). */
    uint32_t additions;           /* Incremented whenever items are added (haulers look again). */
    /* Stacks the player forbade the settlers (non-zero per tile): neither
     * picked up, eaten, used nor added to. Cleared when the stack is gone. */
    uint8_t *forbidden;
    int forbidden_count;
    /* The picture of these items in the known world (core/known.h), and the
     * tiles in view as a bit per tile: while both are set, a stack that is
     * added to, taken from, forbidden or lost on a tile in view is copied
     * into the picture at once (a settler acting on what the one before it just did).
     * NULL: none. */
    struct Items *mirror;
    const uint64_t *mirror_when;
    /* The items: a pool of slots, free ones chained through Item.next. */
    Item *pool;
    uint32_t pool_capacity;
    uint32_t pool_used;  /* Slots ever used: the others were never touched. */
    uint32_t free_slots; /* First free slot + 1, 0 for none. */
    long item_count;     /* Items lying on the whole map. */
    long held_count;     /* Items in loads (ItemStack.held), not on the map. */
    uint32_t *heads;     /* Per tile: its newest item, slot + 1, 0 for none. */
    /* Per tile: when its items last changed (a picture copied from it keeps
     * the same stamp: equal stamps, same items). */
    uint32_t *stamps;
    uint32_t stamp_clock;
    /* A picture of other items (core/known.h): a tile changed here rather
     * than copied gets a stamp no copy has, so that it is copied again. */
    bool picture;
} Items;

/* Items dropped by a single action spread at most this far (in steps). */
#define ITEM_DROP_RADIUS 4

const ItemDef *item_def(ItemType type);

/* Translation key of a category name, NULL if out of range. */
const char *item_category_name_key(ItemCategory category);

/* Every real item type (ITEM_NONE excluded). */
ItemTypeMask item_types_all(void);

/* Item types belonging to a category. */
ItemTypeMask item_types_in_category(ItemCategory category);

bool item_is_food(ItemType type);

/* Whether items of a type rot (ItemDef.rot_days > 0). */
bool item_rots(ItemType type);

/* Speed of rot (×1 at ITEM_ROT_REFERENCE_CELSIUS in the open) at a
 * temperature, under a roof or not; 0 when frozen. */
float item_rot_rate(float celsius, bool sheltered);

/* Hours until a stack rots away at `rate` (item_rot_rate()); a negative
 * value when it never does (a type that does not rot, a rate of 0). */
float item_stack_rot_hours(ItemStack stack, float rate);

/* Maximum number of items of a type in one stack (one tile); 0 for ITEM_NONE. */
int item_stack_limit(ItemType type);

/* Kilograms of `count` items of a type. */
float item_stack_weight(ItemType type, int count);

/* What the stack weighs, kilograms: its whole weight less what was eaten. */
float item_stack_mass(ItemStack stack);

/* An animal eats up to `kg` from one item of the stack (the stack's count is
 * not touched: the caller splits one off): the item loses that much weight,
 * not hit points, and is emptied (type ITEM_NONE) when none is left.
 * Returns the kilograms taken. */
float item_stack_eat(ItemStack *stack, float kg);

/*
 * A stack with the given condition, made valid for its type: quality clamped
 * to 0..100 (always the best for types without quality), hit points
 * clamped to 1..max_hit_points (always the maximum for types that cannot be
 * damaged). An invalid type or a count <= 0 gives an empty stack.
 */
ItemStack item_stack_make(ItemType type, int count, int quality, int hit_points);

/* Whether two stacks may share a tile: same type (and, for bodies, the same
 * dead one). */
bool item_stacks_match(ItemStack a, ItemStack b);

/* Adds `from` to `into` (empty, or matching): counts add up (capped at
 * UINT16_MAX), quality, hit points and transformation progress are averaged by
 * count.
 * Fails, changing nothing, when the stacks do not match. */
bool item_stack_merge(ItemStack *into, ItemStack from);

/* `count` items in perfect condition. */
ItemStack item_stack_pristine(ItemType type, int count);

/* Hit points of an item of `type` at `percent` of its maximum (at least 1). */
int item_hit_points_at_percent(ItemType type, int percent);

/* Hit points of the stack's items in percent of the maximum, rounded down
 * (100 only when undamaged); 0 for an empty stack. */
int item_stack_hit_points_percent(ItemStack stack);

bool items_init(Items *items, int width, int height, int depth);
void items_free(Items *items);

/* Empty stack when out of bounds. */
ItemStack items_get(const Items *items, int x, int y, int z);

/* Copies the items lying on the tile at `index` (and whether they are
 * forbidden) from `src` to `dst` (same size), keeping dst's totals and
 * counters right: dst is a picture of src (see core/known.h). Nothing to do
 * when the tile's stamps are equal (the picture is up to date). */
void items_copy_tile(Items *dst, const Items *src, size_t index);

/* What settlers see of a tile: items_get(), but an empty stack where it is
 * forbidden. */
ItemStack items_get_allowed(const Items *items, int x, int y, int z);

/* Whether the stack on a tile is forbidden (false without a stack). */
bool items_forbidden(const Items *items, int x, int y, int z);

/* Forbids or allows the stack on a tile (nothing without one). Allowing
 * counts as an addition (Items.additions: haulers look again). Returns
 * whether it changed. */
bool items_set_forbidden(Items *items, int x, int y, int z, bool forbidden);

/* Adds items to a tile that is empty or holds a matching stack (same type), up
 * to the stack limit; the tile's stack takes the average quality and hit
 * points. Returns the number actually added. */
int items_add(Items *items, int x, int y, int z, ItemStack batch);

/* Removes up to `count` items from a tile (they have the quality and hit points
 * of the stack). Returns the number removed. */
int items_remove(Items *items, int x, int y, int z, int count);

/* items_remove(), but returns what was taken: the count, and the average
 * condition of the very items removed (the newest of the tile's type).
 * An empty stack when nothing was taken. */
ItemStack items_take(Items *items, int x, int y, int z, int count);

/* The newest item lying on the tile at `index`, NULL for none; then the next
 * one (older), NULL after the last. Valid until the items change. */
const Item *items_first(const Items *items, size_t index);
const Item *items_next(const Items *items, const Item *item);

/* Items of a type lying on a tile (any type: ITEM_NONE). */
int items_count_at(const Items *items, int x, int y, int z, ItemType type);

/*
 * Loads: items taken off the ground stay themselves while held (in a
 * settler's hands), as a list in the pool that ItemStack.held names, and go
 * back to the ground as they were. A load that is only numbers (held 0, a
 * stack made by hand) is turned into items of its condition first. A load
 * holds one type.
 */

/* Picks up to `count` items of the tile's stack (newest first) into `load`,
 * which is empty or matches (item_stacks_match()). Returns how many. */
int items_pick_up(Items *items, int x, int y, int z, int count, ItemStack *load);

/* Puts the items of `load` on a tile, as items_add() would (what fits),
 * each at a spot of its own; the load keeps the rest. Returns how many. */
int items_put(Items *items, int x, int y, int z, ItemStack *load);

/* items_drop() for a load: puts all of it down around (x, y); what finds no
 * room is lost. The load is empty afterwards. Returns how many found room. */
int items_drop_load(Items *items, const struct Map *map, int x, int y, int z, ItemStack *load);

/* `count` items of a load are used up (eaten, burnt, built in): gone. */
void items_use_up(Items *items, ItemStack *load, int count);

/* The items of a load, newest first: as items_first() and items_next(). */
const Item *items_load_first(const Items *items, ItemStack load);

/*
 * Puts items on (x, y) and, when it is full, forbidden or holds something
 * else (another type), on the
 * nearest passable tiles reachable from it (breadth-first, fixed order,
 * within ITEM_DROP_RADIUS). Returns the number placed: items that find no
 * room are lost.
 */
int items_drop(Items *items, const struct Map *map, int x, int y, int z, ItemStack batch);

/* What items lying on a tile go through: the speed of rot (item_rot_rate()),
 * and whether what dries can dry (not out in the rain). */
typedef struct ItemConditions {
    float rot_rate;
    bool dries;
} ItemConditions;

/* Conditions on a tile, asked of the caller of items_advance(). */
typedef ItemConditions (*ItemConditionsFn)(const void *context, size_t tile);

/*
 * Lets `hours` pass for the stacks of tiles begin..end-1 (tile indices), in
 * the conditions `conditions` gives for each tile (NULL: rot at ×1, drying):
 * stacks of types that transform progress where they dry, and those that
 * reach their type's transform_hours turn into the new type (same count,
 * same hit points in percent, quality kept when both types have one); where
 * nothing dries (out in the rain), types that get wet turn into their
 * `wets_into` type at once (dry twigs into wet ones); stacks
 * of types that rot lose hit points and are gone when none are left. Returns
 * the number of stacks that transformed or rotted away.
 */
int items_advance(Items *items, size_t begin, size_t end, int hours, ItemConditionsFn conditions,
                  const void *context);

uint64_t items_hash(const Items *items, uint64_t hash);

#endif /* COLONY_CORE_ITEM_H */
