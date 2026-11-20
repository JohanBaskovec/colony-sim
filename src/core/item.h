/*
 * Items lying on the ground (wood, stone...), owned by the Game.
 *
 * Every item lying on a tile is a record of its own (Item, in a pool), with
 * its own quality, hit points, rot and spot: items are never merged into
 * averages (docs/ITEMS.md). The items of a tile form a list, newest first.
 * A tile holds items of any types, as long as there is room (ITEM_TILE_BULK
 * in all). The items of a type (and, for bodies, of the same dead one) form
 * a group, seen as a stack: its count and average condition. For the
 * callers that think in one stack per tile, each tile has a summary
 * (Items.stacks): the group of its newest item; the *_of() functions look
 * for the newest group among some types, items_groups() lists them all.
 *
 * Dropping more items than a tile can take spreads them over nearby reachable
 * tiles.
 *
 * The player may forbid items, one by one or a whole tile at once: settlers
 * leave them alone (neither picked up, eaten, used nor added to), only an
 * item move the player ordered carries them (core/item_move.h). Forbidden
 * items never share a group with allowed ones; the functions about what
 * settlers may take (*_allowed*, items_pick_up_of(), the chosen items) skip
 * them.
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

#include "core/pool.h"

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
    /* Fallen from the trees (docs/TREES.md): sweet chestnuts, wild apples and pears. */
    ITEM_CHESTNUTS,
    ITEM_APPLES,
    ITEM_PEARS,
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

/* Wood species (core/wood.h): a bit per tree species, 0 for none. */
typedef uint16_t WoodMask;

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

/* How an item holds together in a grip (docs/CARRYING.md §2): what places it
 * can be carried in. */
typedef enum ItemForm {
    ITEM_FORM_NONE,   /* Not an item. */
    ITEM_FORM_RIGID,  /* One solid piece: a cobble, a log, a spear. */
    ITEM_FORM_BUNDLE, /* Many stems or sticks gathered: twigs, grass, reeds. */
    ITEM_FORM_LOOSE,  /* Small pieces that run between the fingers: berries, nuts. */
    ITEM_FORM_LIMP,   /* Drapes over what holds it: carcasses, bodies. */
    ITEM_FORM_COUNT
} ItemForm;

/* An item this long or longer (centimeters) goes under an arm or on the
 * shoulders rather than in a fist (docs/CARRYING.md §2). */
#define ITEM_LONG_CM 60

typedef struct ItemDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    ItemCategory category;
    float weight;  /* Kilograms per item. */
    float bulk;    /* Room taken per item (see ITEM_TILE_BULK). */
    int length_cm; /* Its longest side; 0 for loose items (a heap has none). */
    ItemForm form;
    bool strap;         /* Has a strap or a cord to sling it (none yet). */
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
    /* Made of wood: carries the species of its trees (ItemStack.woods,
     * docs/TREES.md §6), named by them with `wood_name_key` ("{0} branches",
     * "Branches de {0}": wood_item_name()). */
    bool wooden;
    const char *wood_name_key;
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
    /* Non-zero for items the player forbade the settlers: they never share a
     * stack with allowed ones. */
    uint8_t forbidden;
    /* Wooden items (ItemDef.wooden): the species of their wood, every one
     * they were made from (core/wood.h); 0 for other types, or wood of no
     * known species. Items of different species never share a stack. */
    WoodMask woods;
    /* When the item came to be, as a tick + 1 (0: unknown: items_add() stamps
     * the tick it is added at), and who made it (ItemMaker, 1-based index into
     * Items.makers, 0: nobody: found in the wild). Never part of what makes
     * stacks match. */
    uint32_t made;
    uint16_t maker;
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
    uint8_t forbidden;   /* Non-zero: the player forbade it (see items_forbidden()). */
    WoodMask woods;      /* As ItemStack.woods. */
    uint32_t made;       /* As ItemStack.made. */
    uint16_t maker;      /* As ItemStack.maker. */
    uint32_t next;       /* Next item of the same tile: slot + 1, 0 for none. */
    uint32_t tile;       /* Tile index, ITEM_TILE_HELD for an item in a load. */
} Item;

#define ITEM_TILE_HELD UINT32_MAX

/* An item named across ticks: its slot in the pool and the slot's
 * generation (an item gone is told apart from the one in its slot now). */
typedef struct ItemId {
    uint32_t slot; /* UINT32_MAX: none. */
    uint16_t generation;
} ItemId;

#define ITEM_ID_NONE ((ItemId){UINT32_MAX, 0})

/* Items.forbidden values. */
#define ITEM_TILE_SOME_FORBIDDEN 1
#define ITEM_TILE_ALL_FORBIDDEN 2

struct Items;

/* Whether items dropped may land on a tile (items_set_drop_filter()). */
typedef bool (*ItemDropFilter)(const void *data, const struct Items *items, int x, int y, int z,
                               const ItemStack *load);

/* A settler who made items: kept by name for as long as the items last, however
 * long the settler does. */
#define ITEM_MAKERS_MAX 255
typedef struct ItemMaker {
    EntityId id;
    char name[32]; /* Proper noun: never translated. */
} ItemMaker;

typedef struct Items {
    int width;
    int height;
    int depth;
    /* Summary of each tile (see the top of this file): the group of its
     * newest item, its count and average condition. Derived from
     * the items: never written but by this module. */
    ItemStack *stacks;
    /* Per tile: non-zero when it holds other groups than its summary. */
    uint8_t *mixed;
    long totals[ITEM_TYPE_COUNT]; /* Items of each type on the whole map. */
    uint32_t *level_stacks;       /* Tiles with items on each level (searches skip empty levels). */
    uint32_t additions;           /* Incremented whenever items are added (haulers look again). */
    /* Per tile, derived from its items: ITEM_TILE_SOME_FORBIDDEN when some
     * of them are forbidden, ITEM_TILE_ALL_FORBIDDEN when all are, 0 when
     * none (see items_forbidden()). And the count of tiles with some. */
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
    /* Items of each type that rotted away on the ground (statistics: not in
     * the state hash). */
    long rotted[ITEM_TYPE_COUNT];
    /* Tiles that dropped items keep off when they can (items_set_drop_filter()),
     * and the data passed to it. */
    ItemDropFilter drop_filter;
    const void *drop_data;
    /* The tick items are stamped with (items_set_now(): the game's clock; not in
     * the state hash). */
    uint32_t now;
    /* Who made items (items_maker_register()), not in the state hash. */
    int maker_count;
    ItemMaker makers[ITEM_MAKERS_MAX];
} Items;

/* Items dropped by a single action spread at most this far (in steps). */
#define ITEM_DROP_RADIUS 4

const ItemDef *item_def(ItemType type);

/* Whether an item of this type is at least ITEM_LONG_CM long. */
bool item_is_long(ItemType type);

/* The translation key of a form's name ("Rigid"), NULL for none. */
const char *item_form_name_key(ItemForm form);

/* Translation key of a category name, NULL if out of range. */
const char *item_category_name_key(ItemCategory category);

/* Every real item type (ITEM_NONE excluded). */
ItemTypeMask item_types_all(void);

/* Item types belonging to a category. */
ItemTypeMask item_types_in_category(ItemCategory category);

bool item_is_food(ItemType type);

/* Every type that is food (item_is_food()). */
ItemTypeMask item_types_food(void);

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

/* What the stack weighs, kilograms: its whole weight (by its wood species,
 * core/wood.h) less what was eaten. */
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
 * dead one), both forbidden or both allowed. */
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

/* The fewest hit points an item of `type` keeps `hours` with at rot ×1 (at
 * least 1, at most its maximum: the maximum for a type that never rots). */
int item_hit_points_lasting(ItemType type, float hours);

/* Hit points of the stack's items in percent of the maximum, rounded down
 * (100 only when undamaged); 0 for an empty stack. */
int item_stack_hit_points_percent(ItemStack stack);

bool items_init(Items *items, int width, int height, int depth);
void items_free(Items *items);

/* The summary of a tile: the group of its newest item. Empty stack when out
 * of bounds. */
ItemStack items_get(const Items *items, int x, int y, int z);

/* The newest group of a tile among `types` (an empty stack for none). */
ItemStack items_get_of(const Items *items, int x, int y, int z, ItemTypeMask types);

/* items_get_of(), but only of allowed items. */
ItemStack items_get_allowed_of(const Items *items, int x, int y, int z, ItemTypeMask types);

/* The group of a tile that `group` would belong with (same type, same dead
 * one, forbidden or not alike), an empty stack for none. */
ItemStack items_get_group(const Items *items, int x, int y, int z, ItemStack group);

/* Whether a tile holds more than one group. */
bool items_mixed(const Items *items, int x, int y, int z);

/* The groups of a tile, the newest first: up to `capacity` into `out`.
 * Returns how many were written. */
int items_groups(const Items *items, int x, int y, int z, ItemStack *out, int capacity);

/* How many items of `batch` a tile still has room for (items_add() would
 * take): its type's stack limit less the room taken by what lies there. */
int items_room(const Items *items, int x, int y, int z, ItemStack batch);

/* Copies the items lying on the tile at `index` (and whether they are
 * forbidden) from `src` to `dst` (same size), keeping dst's totals and
 * counters right: dst is a picture of src (see core/known.h). Nothing to do
 * when the tile's stamps are equal (the picture is up to date). */
void items_copy_tile(Items *dst, const Items *src, size_t index);

/* What settlers see of a tile: its newest group of allowed items (an empty
 * stack for none). */
ItemStack items_get_allowed(const Items *items, int x, int y, int z);

/* Whether every item on a tile is forbidden (false without any). */
bool items_forbidden(const Items *items, int x, int y, int z);

/* Whether some item on a tile is forbidden: nothing is added to the tile. */
bool items_any_forbidden(const Items *items, int x, int y, int z);

/* Forbids or allows every item on a tile (nothing without any). Allowing
 * counts as an addition (Items.additions: haulers look again). Returns
 * whether something changed. */
bool items_set_forbidden(Items *items, int x, int y, int z, bool forbidden);

/* Forbids or allows one item lying on the ground, as items_set_forbidden()
 * does. Returns whether it changed. */
bool items_set_item_forbidden(Items *items, ItemId id, bool forbidden);

/* Adds items to a tile, as many as it has room for (items_room()), each
 * with the batch's condition. Returns the number actually added. */
int items_add(Items *items, int x, int y, int z, ItemStack batch);

/* Removes up to `count` items of the tile's summary group (the newest
 * first). Returns the number removed. */
int items_remove(Items *items, int x, int y, int z, int count);

/* items_remove(), but returns what was taken: the count, and the average
 * condition of the very items removed (the newest of the group).
 * An empty stack when nothing was taken. */
ItemStack items_take(Items *items, int x, int y, int z, int count);

/* items_take() and items_remove() from the newest group among `types`,
 * forbidden or not. */
ItemStack items_take_of(Items *items, int x, int y, int z, ItemTypeMask types, int count);
int items_remove_of(Items *items, int x, int y, int z, ItemTypeMask types, int count);

/* items_take() from the group of the tile `group` belongs with
 * (items_get_group()). */
ItemStack items_take_group(Items *items, int x, int y, int z, ItemStack group, int count);

/* Removes every item lying on a tile: gone. */
void items_clear(Items *items, int x, int y, int z);

/* Groups items_lift_all() takes at most at once (more stay on the tile). */
#define ITEM_LIFT_MAX 64

/* Picks up every item lying on a tile: each group into a
 * load of `loads` (up to `capacity`), the newest first, to be put down
 * elsewhere (items_drop_load()) or used up. Returns how many loads. */
int items_lift_all(Items *items, int x, int y, int z, ItemStack *loads, int capacity);

/* Moves every item lying on a tile around another (as
 * items_drop_load() does), each as it was. What finds no room stays where
 * it lay when `keep`, else is lost. Returns how many were moved. */
int items_move_all(Items *items, const struct Map *map, int x, int y, int z, int to_x, int to_y,
                   int to_z, bool keep);

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

/* Picks up to `count` items (newest first) into `load`: of its group when
 * it holds some, else of the tile's summary group. Returns how many. */
int items_pick_up(Items *items, int x, int y, int z, int count, ItemStack *load);

/* items_pick_up() from the newest group of allowed items among `types` (of
 * the load's group when it holds some, if its type is among them). */
int items_pick_up_of(Items *items, int x, int y, int z, ItemTypeMask types, int count,
                     ItemStack *load);

/* items_pick_up() from the group of the tile `group` belongs with
 * (items_get_group()), forbidden or not. */
int items_pick_up_group(Items *items, int x, int y, int z, ItemStack group, int count,
                        ItemStack *load);

/* Forbids or allows every item of a load (they keep it when put down). */
void items_load_set_forbidden(Items *items, ItemStack *load, bool forbidden);

/* Puts the items of `load` on a tile, as items_add() would (what fits),
 * each at a spot of its own; the load keeps the rest. Returns how many. */
int items_put(Items *items, int x, int y, int z, ItemStack *load);

/* items_drop() for a load: puts all of it down around (x, y); what finds no
 * room is lost. The load is empty afterwards. Returns how many found room. */
int items_drop_load(Items *items, const struct Map *map, int x, int y, int z, ItemStack *load);

/* Items dropped (items_drop(), items_drop_load(), items_move_all()) land on
 * the tiles `filter` accepts when there is room on some within reach, on
 * any tile otherwise: storage that would get mixed is kept clear (the game
 * sets haul_drop_filter()). NULL: any tile. */
void items_set_drop_filter(Items *items, ItemDropFilter filter, const void *data);

/* `count` items of a load are used up (eaten, burnt, built in): gone. */
void items_use_up(Items *items, ItemStack *load, int count);

/* Moves up to `count` items from the load `from` to the load `to` (empty, or
 * of the same group), each as it is: a settler's load handed over to a site
 * or a workstation. Loads that are only numbers become items first.
 * Returns how many moved. */
int items_load_move(Items *items, ItemStack *from, ItemStack *to, int count);

/* Moves the item at `index` of a load (newest first, as items_load_first())
 * into the empty load `to`, as it is. A load that is only numbers becomes
 * items first. Returns 1, or 0 when there is no such item or `to` holds
 * something. */
int items_load_take(Items *items, ItemStack *from, int index, ItemStack *to);

/* The lowest quality among the items of a load (its own quality when it is
 * only numbers); ITEM_QUALITY_WORST for an empty one. */
int items_load_lowest_quality(const Items *items, ItemStack load);

/* The items of a load, newest first: as items_first() and items_next(). */
const Item *items_load_first(const Items *items, ItemStack load);

/* How wide an item is drawn, in tiles: by its bulk, from 0.4 (a flake) to 1
 * (a body). */
float item_drawn_size(ItemType type);

/* Tells the items what tick it is: those added from now on are stamped with it
 * (Item.made). */
void items_set_now(Items *items, uint64_t tick);

/* The record of a settler who makes items (reusing the one it has), to put
 * in ItemStack.maker; 0 when there is no room for another one. */
uint16_t items_maker_register(Items *items, EntityId id, const char *name);

/* The name of a maker (ItemStack.maker), NULL for none. */
const char *items_maker_name(const Items *items, uint16_t maker);

/* The settler a maker is (ItemStack.maker): ENTITY_NONE for none. */
EntityId items_maker_id(const Items *items, uint16_t maker);

/* The id of an item of the pool. */
ItemId items_id(const Items *items, const Item *item);

/* Picks one item lying on the ground into `load` (empty, or of its group);
 * it keeps its id. Returns 1, or 0 when it is gone, held or does not belong
 * with the load. */
int items_pick_up_item(Items *items, ItemId id, ItemStack *load);

/* The item lying on the ground with that id, NULL when gone or in a load. */
const Item *items_by_id(const Items *items, ItemId id);

/* Whether the item with that id is in a load (held by someone). */
bool items_id_held(const Items *items, ItemId id);

/* The item drawn at the point (x, y) of level z, in tiles (the top left of
 * tile 0 at 0): the newest one whose picture (item_drawn_size(), turned
 * any way: a circle) holds the point, on its tile or a neighbor; the
 * nearest center among those of the same tile. ITEM_ID_NONE when none. */
ItemId items_at_point(const Items *items, float x, float y, int z);

/* Items of a tile drawn at most (its newest): the others lie under them,
 * out of sight and out of reach of a click. */
#define ITEM_DRAWN_PER_TILE 10

/* The item of a tile that looks like `like` (the same type, spot and angle,
 * its copy in a picture of the items, or the real one of a
 * copy), ITEM_ID_NONE when none. */
ItemId items_find_like(const Items *items, int x, int y, int z, const Item *like);

/* Moves an item lying on the ground to a tile (it becomes its newest item),
 * at the spot given in 1/256 of a tile, whatever lies there: its condition
 * stays. Returns false when it is not on the
 * ground or the tile is out of bounds. */
bool items_move(Items *items, ItemId id, int x, int y, int z, int spot_x, int spot_y);

/* Turns an item lying on the ground by `steps` of 1/256 of a turn. */
bool items_turn(Items *items, ItemId id, int steps);

/* Orders of the items of a tile (the list of a heap). */
typedef enum ItemOrder {
    ITEM_ORDER_NEWEST,    /* As they lie: the newest first. */
    ITEM_ORDER_QUALITY,   /* The best first. */
    ITEM_ORDER_CONDITION, /* The least damaged first (hit points in percent). */
    ITEM_ORDER_ROT,       /* What rots away soonest first; what never rots last. */
    ITEM_ORDER_COUNT
} ItemOrder;

/* An item as a stack of one (its type, quality, hit points...). */
ItemStack item_as_stack(const Item *item);

/* Rot an item has left before it is gone, in 1/ITEM_ROT_STEP of a hit point
 * (item_stack_rot_hours() turns it into hours); -1 for one that never rots. */
long item_rot_left(const Item *item);

/* The items lying on the tile at `index` in an order: the first `capacity`
 * of them into `out` (ties: the newest first). Returns how many lie there. */
int items_list(const Items *items, size_t index, ItemOrder order, const Item **out, int capacity);

/* What of a tile's group of a type is wanted (a bill's ingredients): a
 * quality range, the items that rot away soonest first, and for wooden
 * items the species allowed (item_woods_allowed()). */
typedef struct ItemChoice {
    int min_quality;
    int max_quality;
    bool soonest_rot;
    WoodMask woods; /* 0: any. */
} ItemChoice;

/* Any item of the stack, the newest first. */
#define ITEM_CHOICE_ANY ((ItemChoice){ITEM_QUALITY_WORST, ITEM_QUALITY_BEST, false, 0})

/* Whether items of a stack are of the species `allowed` (0: any): always for
 * types not of wood; wooden ones of known species, all among them. */
bool item_woods_allowed(ItemStack stack, WoodMask allowed);

/* The newest group of allowed items of a tile among `types` whose species
 * are allowed (item_woods_allowed()); an empty stack for none. */
ItemStack items_get_allowed_of_woods(const Items *items, int x, int y, int z, ItemTypeMask types,
                                     WoodMask allowed);

/* Gives every item of a load the species `woods` (a load of wood delivered
 * to a site that holds another species: the structure is of both). */
void items_load_set_woods(Items *items, ItemStack *load, WoodMask woods);

/* Items of the tile's newest group of allowed items of `type` of allowed
 * species whose quality lies in the choice's range. */
int items_count_chosen(const Items *items, int x, int y, int z, ItemType type, ItemChoice choice);

/* Hours until the first item of the tile's newest group of allowed items of
 * `type` (of allowed species) in the choice's range rots away, at rot ×1 (item_stack_rot_hours());
 * negative when none rots. */
float items_soonest_rot_hours(const Items *items, int x, int y, int z, ItemType type,
                              ItemChoice choice);

/* The allowed item of a tile among `types` (not a body) that rots away
 * soonest: its type, and in `hours` (when not NULL) the hours it has left at
 * rot ×1 (item_stack_rot_hours()). ITEM_NONE (hours negative) when none of
 * them rots. */
ItemType items_soonest_rotting(const Items *items, int x, int y, int z, ItemTypeMask types,
                               float *hours);

/* items_pick_up_of() for one type, only items in the choice's range, those
 * that rot away soonest first when it says so. */
int items_pick_up_chosen(Items *items, int x, int y, int z, ItemType type, int count,
                         ItemChoice choice, ItemStack *load);

/* Natural items */

/* Lays up to `count` items of a type on a tile as nature puts them there (a
 * twig shed by a tree, a stone in the grass), as much as the tile has room
 * for: ordinary items, each of its own quality and condition (60 to 100 %,
 * from a hash of the tile, not a random stream). Returns how many. */
int items_add_natural(Items *items, int x, int y, int z, ItemType type, int count);

/* The same, of wood of the species `woods` (wooden types only: ignored for
 * the others). */
int items_add_natural_wood(Items *items, int x, int y, int z, ItemType type, int count,
                           WoodMask woods);

/* Items of a type lying on a tile (bodies left out). */
int items_count_type(const Items *items, int x, int y, int z, ItemType type);

/*
 * Puts items on (x, y) and, when it is full or holds forbidden items, on the
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

/* What items_check() found: 0 problems when everything is consistent. */
typedef struct ItemsCheck {
    int problems;
    const char *first; /* What the first problem was (English, for developers), NULL for none. */
    long first_tile;   /* Its tile index, -1 when it is not about one tile. */
} ItemsCheck;

/*
 * Checks that the items are consistent (tests and benchmarks): every item
 * listed lies on its own tile, the summaries, mixed marks, counts per level,
 * totals and forbidden tiles match the items, and every slot of the pool is
 * lying on a tile, in a load or free, exactly once. Visits every tile.
 */
ItemsCheck items_check(const Items *items);

/* Whether a load's list of items matches it: its count, type and dead one,
 * every item held (a load that is only numbers passes). */
bool items_check_load(const Items *items, ItemStack load);

/* Heaps on the map (statistics): tiles with items, those
 * with several groups, their groups in all, the most groups on one tile. */
typedef struct ItemHeaps {
    long tiles;
    long mixed;
    long mixed_groups;
    int most_groups;
} ItemHeaps;

ItemHeaps items_heaps(const Items *items);

#endif /* COLONY_CORE_ITEM_H */
