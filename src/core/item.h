/*
 * Items lying on the ground (wood, stone...): one stack per tile, stored as a
 * dense layer the size of the map and owned by the Game.
 *
 * Stacks hold a single item type, up to the type's stack limit. Dropping more
 * items than a tile can take spreads them over nearby reachable tiles.
 *
 * Every item has a quality grade (how good the material is, which carries
 * over to what is made from it) and hit points (its condition). Some types
 * are always perfect: stone blocks are always of the best grade and cannot be
 * damaged. Only items of the same type and grade stack together; a stack
 * keeps the count-weighted average hit points of its items.
 *
 * Food rots while it lies on a tile: it loses hit points, faster when warm,
 * not at all when frozen, a little slower under a roof, faster in the rain,
 * and is gone when they run out (see item_rot_rate()). Fresh grass does not
 * dry in the rain. Items in hands do not rot.
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
    ITEM_TYPE_COUNT
} ItemType;

/* Groups of item types, e.g. for storage filters. */
typedef enum ItemCategory {
    ITEM_CATEGORY_FOOD,
    ITEM_CATEGORY_RESOURCES,
    ITEM_CATEGORY_TOOLS,
    ITEM_CATEGORY_WEAPONS,
    ITEM_CATEGORY_COUNT
} ItemCategory;

/* Bit mask with one bit per ItemType. */
typedef uint32_t ItemTypeMask;
_Static_assert(ITEM_TYPE_COUNT <= 32, "item types must fit in an ItemTypeMask");

#define ITEM_TYPE_BIT(type) ((ItemTypeMask)1u << (unsigned)(type))

/*
 * Bulk is how much room an item takes, packing gaps included, in units of
 * about half a liter. A tile holds ITEM_TILE_BULK of it (a heap of about
 * 1 m² × 50 cm), so the stack limit of a type is ITEM_TILE_BULK / bulk.
 */
#define ITEM_TILE_BULK 1000.0f

/* Quality grades, worst to best. Grades are few on purpose: items of
 * different grades never share a tile. */
typedef enum ItemQuality {
    ITEM_QUALITY_CRUDE,
    ITEM_QUALITY_ROUGH,
    ITEM_QUALITY_STANDARD,
    ITEM_QUALITY_FINE,
    ITEM_QUALITY_SUPERB,
    ITEM_QUALITY_COUNT
} ItemQuality;

#define ITEM_QUALITY_BEST (ITEM_QUALITY_COUNT - 1)

typedef struct ItemDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    ItemCategory category;
    float weight;    /* Kilograms per item. */
    float bulk;      /* Room taken per item (see ITEM_TILE_BULK). */
    float nutrition; /* Food need restored by eating one item; 0 = not food. */
    int max_hit_points; /* Hit points of one item in perfect condition. */
    bool has_quality; /* false: always ITEM_QUALITY_BEST. */
    bool damageable;  /* false: always at max_hit_points. */
    /* Items lying on a tile turn into `transforms_into` after
     * `transform_hours` (fresh grass dries); ITEM_NONE: never. */
    ItemType transforms_into;
    int transform_hours;
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
    uint8_t quality; /* ItemQuality, the same for every item of the stack. */
    uint16_t count;
    uint16_t hit_points; /* Per item, 1..max_hit_points, average of the items. */
    /* Types that transform: hours spent lying on a tile toward the
     * transformation. Types that rot: rot toward the next hit point lost, in
     * 1/ITEM_ROT_STEP of one. Average of the items; 0 for other types. */
    uint16_t progress;
} ItemStack;

typedef struct Items {
    int width;
    int height;
    ItemStack *stacks;
    long totals[ITEM_TYPE_COUNT]; /* Items of each type on the whole map. */
    uint32_t additions; /* Incremented whenever items are added (haulers look again). */
} Items;

/* Items dropped by a single action spread at most this far (in steps). */
#define ITEM_DROP_RADIUS 4

const ItemDef *item_def(ItemType type);

/* Translation key of a quality grade, NULL if out of range. */
const char *item_quality_name_key(ItemQuality quality);

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

/*
 * A stack with the given condition, made valid for its type: quality clamped
 * to the grades (always the best for types without quality), hit points
 * clamped to 1..max_hit_points (always the maximum for types that cannot be
 * damaged). An invalid type or a count <= 0 gives an empty stack.
 */
ItemStack item_stack_make(ItemType type, int count, int quality, int hit_points);

/* Whether two stacks may share a tile: same type and same grade. */
bool item_stacks_match(ItemStack a, ItemStack b);

/* Adds `from` to `into` (empty, or matching): counts add up (capped at
 * UINT16_MAX), hit points and transformation progress are averaged by count.
 * Fails, changing nothing, when the stacks do not match. */
bool item_stack_merge(ItemStack *into, ItemStack from);

/* `count` items in perfect condition. */
ItemStack item_stack_pristine(ItemType type, int count);

/* Hit points of an item of `type` at `percent` of its maximum (at least 1). */
int item_hit_points_at_percent(ItemType type, int percent);

/* Hit points of the stack's items in percent of the maximum, rounded down
 * (100 only when undamaged); 0 for an empty stack. */
int item_stack_hit_points_percent(ItemStack stack);

bool items_init(Items *items, int width, int height);
void items_free(Items *items);

/* Empty stack when out of bounds. */
ItemStack items_get(const Items *items, int x, int y);

/* Adds items to a tile that is empty or holds a matching stack (same type and
 * grade), up to the stack limit; the tile's stack takes the average hit
 * points. Returns the number actually added. */
int items_add(Items *items, int x, int y, ItemStack batch);

/* Removes up to `count` items from a tile (they have the grade and hit points
 * of the stack). Returns the number removed. */
int items_remove(Items *items, int x, int y, int count);

/*
 * Puts items on (x, y) and, when it is full or holds something else (another
 * type or grade), on the
 * nearest passable tiles reachable from it (breadth-first, fixed order,
 * within ITEM_DROP_RADIUS). Returns the number placed: items that find no
 * room are lost.
 */
int items_drop(Items *items, const struct Map *map, int x, int y, ItemStack batch);

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
 * same hit points in percent, quality kept when both types have one); stacks
 * of types that rot lose hit points and are gone when none are left. Returns
 * the number of stacks that transformed or rotted away.
 */
int items_advance(Items *items, size_t begin, size_t end, int hours,
                  ItemConditionsFn conditions, const void *context);

uint64_t items_hash(const Items *items, uint64_t hash);

#endif /* COLONY_CORE_ITEM_H */
