/*
 * Workstations: places where settlers make things (crafting spot, knapping
 * spot, open hearth), and the bills the player gives them. A hearth also
 * holds a fire: its fuel, whether it burns, what the player wants of it (see
 * core/hearth.h); it is worked from a neighboring tile.
 *
 * A workstation is built, not placed: its blueprint is a construction site
 * (core/construction.h), where builders and haulers bring its materials and
 * a builder does the work; the station appears once the site is finished. A
 * station made of nothing (crafting spot) is ready as soon as it is placed,
 * unless a plant must be cleared first. A station carries a list of bills:
 * a recipe (core/recipe.h), a repeat mode (do N times, until N in stock,
 * forever), the input types allowed, how far from the station inputs are
 * searched for, who may work on it (anyone, settlers whose level in the
 * recipe's knowledge lies in a range, or one settler), and whether it is
 * paused. Settlers take the first bill
 * that wants doing. A craft in progress keeps its input
 * and its work on the station, so that interrupted work resumes.
 * Design: docs/KNOWLEDGE.md §2.5, §6.3, §8.2.
 */
#ifndef COLONY_CORE_WORKSTATION_H
#define COLONY_CORE_WORKSTATION_H

#include "core/item.h"
#include "core/knowledge.h"
#include "core/map.h"
#include "core/pool.h"
#include "core/recipe.h"
#include "core/rng.h"
#include "core/structure.h"
#include "core/work.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum WorkstationType {
    WORKSTATION_CRAFTING_SPOT,
    WORKSTATION_KNAPPING_SPOT,
    WORKSTATION_OPEN_HEARTH,
    WORKSTATION_TYPE_COUNT
} WorkstationType;

#define WORKSTATION_BIT(type) (1u << (unsigned)(type))

/* What a hearth burns (core/hearth.h): branches, twigs and logs, wet twigs
 * (they burn on a fire, though they catch no flame), dry bracken and heather;
 * only branches, dry twigs and logs are brought by default (wet twigs are
 * left to dry into kindling, bracken and heather are what thatch and bedding
 * are made of). */
#define HEARTH_DEFAULT_FUEL_TYPES                                                                  \
    (ITEM_TYPE_BIT(ITEM_BRANCHES) | ITEM_TYPE_BIT(ITEM_TWIGS) | ITEM_TYPE_BIT(ITEM_WOOD))
#define HEARTH_FUEL_TYPES                                                                          \
    (HEARTH_DEFAULT_FUEL_TYPES | ITEM_TYPE_BIT(ITEM_WET_TWIGS) | ITEM_TYPE_BIT(ITEM_BRACKEN) |     \
     ITEM_TYPE_BIT(ITEM_HEATHER))

typedef struct WorkstationDef {
    const char *id; /* Stable identifier (saves), never shown. */
    const char *name_key;
    const char *description_key; /* One short sentence for the build menu. */
    /* Brought to its construction site; unused slots: ITEM_NONE. */
    StructureMaterial materials[STRUCTURE_MATERIAL_SLOTS];
    /* Work ticks at speed ×1. A station with no material and no work is
     * placed ready. */
    float build_work;
    /* Knowledge needed to build it; KNOWLEDGE_NONE: anyone can. */
    KnowledgeType knowledge;
    float min_level;
    /* Holds a fire (core/hearth.h), worked from a neighboring tile rather
     * than standing on it. */
    bool hearth;
} WorkstationDef;

typedef enum BillMode {
    BILL_DO_TIMES,    /* Do it `target` times. */
    BILL_UNTIL_STOCK, /* Until `target` products are in stock (see Game.stock). */
    BILL_FOREVER,     /* Again and again. */
    BILL_MODE_COUNT
} BillMode;

#define BILL_TARGET_MIN 1
#define BILL_TARGET_MAX 999
#define BILL_TARGET_DEFAULT 1

/* How far from the station a bill's inputs are searched for, in travel cost
 * (about one per tile on open ground); 0: nowhere, nothing is made. The slider offers the
 * radii of a stockpile's (stockpile_radius_of_step()), up to STOCKPILE_RADIUS_MAX. */
#define BILL_INPUT_RADIUS_MIN 0
#define BILL_INPUT_RADIUS_MAX 160
#define BILL_INPUT_RADIUS_DEFAULT 80

/* Worker levels a bill accepts by default (BILL_WORKER_LEVEL): everyone who
 * knows the recipe. */
#define BILL_LEVEL_MIN ((int)KNOWLEDGE_LEVEL_MIN)
#define BILL_LEVEL_MAX ((int)KNOWLEDGE_LEVEL_MAX)

/* Who may take a bill (settlers must also know the recipe well enough). */
typedef enum BillWorker {
    BILL_WORKER_ANYONE,
    BILL_WORKER_LEVEL,    /* Settlers whose level lies in min_level..max_level. */
    BILL_WORKER_SPECIFIC, /* One settler: Bill.colonist. */
    BILL_WORKER_COUNT
} BillWorker;

typedef struct Bill {
    uint8_t recipe; /* RecipeType */
    uint8_t mode;   /* BillMode */
    uint16_t target;
    uint16_t done;       /* Crafts completed (counted by BILL_DO_TIMES). */
    ItemTypeMask inputs; /* Input types allowed, among the recipe's. */
    uint8_t worker;      /* BillWorker. */
    /* BILL_WORKER_LEVEL: settlers whose level in the recipe's knowledge lies
     * in this inclusive range may take the bill (beginners for cheap things,
     * masters for the best ones). The range is kept while another worker
     * mode is chosen. */
    uint8_t min_level;
    uint8_t max_level;
    /* BILL_WORKER_SPECIFIC: the only settler who may take it (a settler who
     * left is never matched again: nobody takes the bill). */
    EntityId colonist;
    /* A paused bill starts no craft (one under way on the station is
     * finished, so that the station is not blocked). */
    bool paused;
    uint16_t input_radius; /* BILL_INPUT_RADIUS_MIN..BILL_INPUT_RADIUS_MAX. */
    /* Only ingredients whose quality lies in this inclusive range are used
     * (each item's own: docs/ITEMS.md); ITEM_QUALITY_WORST..BEST: any. */
    uint8_t min_quality;
    uint8_t max_quality;
    /* Wooden ingredients only of these species (docs/TREES.md §6.4); 0: any. */
    WoodMask woods;
} Bill;

#define WORKSTATION_MAX 64
#define WORKSTATION_NONE (-1)
#define WORKSTATION_BILL_MAX 8

typedef struct Workstation {
    bool used;
    uint8_t type; /* WorkstationType */
    int16_t x, y, z;
    int number; /* Unique, never reused: tells a station from a later one in the same slot. */
    Bill bills[WORKSTATION_BILL_MAX];
    int bill_count;
    /* Craft in progress: its recipe (RECIPE_COUNT: none), the input put down
     * on the station (a settler's load: its items, each as it was, used up
     * when the craft is done) and the work done. */
    uint8_t work_recipe;
    ItemStack work_input;
    float work_progress;
    /* The input came out of nothing (workstation_start_free_craft()): it is
     * not given back when the craft is called off. */
    bool free_input;
    /* Hearths only (core/hearth.h): kilograms of fuel in it, whether it
     * burns, whether settlers keep it burning (refuel and light it), the fuel
     * types allowed, the kilograms haulers are bringing, and how soaked
     * the fire is by rain (it goes out at HEARTH_SOAKED_OUT). */
    float fuel;
    bool burning;
    bool keep_burning;
    ItemTypeMask fuel_types;
    float fuel_promised;
    float soaked;
    /* What a lighter laid in it for the next fire, HEARTH_LAID_* bits
     * (core/hearth.h): it burns when the fire takes. */
    uint8_t laid;
    /* The player forbade it: nobody crafts at it, refuels, lights it nor
     * tells stories there. */
    bool forbidden;
    /* Crafters take the bills of the stations of the highest priority first
     * (JOB_PRIORITY_HIGHEST..JOB_PRIORITY_LOWEST, core/work.h). */
    uint8_t priority;
} Workstation;

typedef struct Workstations {
    int width;
    int height;
    int depth;
    uint8_t *at; /* Workstation index + 1 per tile, 0 when none. */
    Workstation list[WORKSTATION_MAX];
    int used_count;
    int next_number;
    /* Incremented whenever stations or bills change: settlers who found
     * nothing to do look again. */
    uint32_t revision;
} Workstations;

/* NULL when out of range. */
const WorkstationDef *workstation_def(WorkstationType type);

bool workstations_init(Workstations *workstations, int width, int height, int depth);
void workstations_free(Workstations *workstations);

/* Index of the station on a tile, or WORKSTATION_NONE. */
int workstation_at(const Workstations *workstations, int x, int y, int z);

/* NULL for an unused or invalid index. */
Workstation *workstation_get(Workstations *workstations, int index);
const Workstation *workstation_get_const(const Workstations *workstations, int index);

/* Whether a station may be placed here: passable, buildable ground (not a
 * slope, see map_is_buildable()), no tree (grass and bushes are cleared by
 * the builder first), no station yet. */
bool workstation_can_place(const Workstations *workstations, const Map *map, int x, int y, int z);

/* Whether a station of this type is built on a construction site (it needs
 * materials or work), rather than placed ready. */
bool workstation_needs_construction(WorkstationType type);

/* Places a station, ready for bills (its construction, if any, is done).
 * Returns its index, or WORKSTATION_NONE when the tile does not qualify or
 * the list is full. */
int workstations_place(Workstations *workstations, const Map *map, WorkstationType type, int x,
                       int y, int z);

/*
 * Removes a station. The input of a craft in progress (unless free) is given back in
 * `returned`; returns the number of stacks (0 or 1). The caller drops them
 * (items_drop_load(): a load, its items as they were).
 */
int workstations_remove(Workstations *workstations, int index, ItemStack returned[1]);

/* Forbids or allows a station (see Workstation.forbidden); a change counts
 * in Workstations.revision. Returns whether it changed. */
bool workstation_set_forbidden(Workstations *workstations, int index, bool forbidden);

/* Sets a station's priority (clamped, see Workstation.priority); a change
 * counts in Workstations.revision. Returns whether it changed. */
bool workstation_set_priority(Workstations *workstations, int index, int priority);

/* Adds a bill for a recipe made at this kind of station (do once, every
 * input allowed). Returns its index, or -1. */
int workstation_add_bill(Workstations *workstations, int index, RecipeType recipe);

/* Removes a bill; later bills move up. The input of a craft in progress for
 * this recipe is given back in `returned` when no other bill makes it
 * (returns whether it was; a free input is not given back: the craft is
 * just called off). */
bool workstation_remove_bill(Workstations *workstations, int index, int bill, ItemStack *returned);

/* Changing a bill. Invalid values are refused (targets are clamped to
 * BILL_TARGET_MIN..BILL_TARGET_MAX). */
bool workstation_set_bill_mode(Workstations *workstations, int index, int bill, BillMode mode);
bool workstation_set_bill_target(Workstations *workstations, int index, int bill, int target);
/* Allows or forbids one of the recipe's input types. */
bool workstation_set_bill_input(Workstations *workstations, int index, int bill, ItemType type,
                                bool allowed);
/* Who may take a bill. BILL_WORKER_LEVEL is refused for a recipe that needs
 * no knowledge, BILL_WORKER_SPECIFIC without a settler (`colonist` is only
 * kept in that mode). */
bool workstation_set_bill_worker(Workstations *workstations, int index, int bill, BillWorker worker,
                                 EntityId colonist);
/* Worker level range (BILL_WORKER_LEVEL): each bound is clamped between
 * BILL_LEVEL_MIN, the other bound and BILL_LEVEL_MAX. */
bool workstation_set_bill_min_level(Workstations *workstations, int index, int bill, int level);
bool workstation_set_bill_max_level(Workstations *workstations, int index, int bill, int level);
bool workstation_set_bill_paused(Workstations *workstations, int index, int bill, bool paused);
/* Clamped to BILL_INPUT_RADIUS_MIN..BILL_INPUT_RADIUS_MAX. */
bool workstation_set_bill_input_radius(Workstations *workstations, int index, int bill, int radius);
/* Quality range of the ingredients: both bounds clamped to
 * ITEM_QUALITY_WORST..ITEM_QUALITY_BEST, swapped when crossed. */
bool workstation_set_bill_quality_range(Workstations *workstations, int index, int bill, int low,
                                        int high);

/* Sets the species a bill's wooden ingredients may be of (0: any). */
bool workstation_set_bill_woods(Workstations *workstations, int index, int bill, WoodMask woods);

/* Whether the settler `who`, at `level` in the recipe's knowledge, may take
 * the bill as far as the player's choice of workers goes (the level only
 * counts for BILL_WORKER_LEVEL, and not for a recipe that needs no
 * knowledge). */
bool bill_accepts_worker(const Bill *bill, double level, EntityId who);

/* Whether a bill still wants doing, given how many of its product are in
 * stock (only used by BILL_UNTIL_STOCK). Paused or not: see Bill.paused. */
bool bill_wants_more(const Bill *bill, int stored);

/*
 * Whether enough of an input type the bill allows lies in the colony
 * (`totals`: items lying per type), once the tools still to fetch (`tools`:
 * one item of each non-empty set, 0 for none) have taken theirs: knapping
 * with a cobble as a hammer needs a second stone to knap.
 */
bool bill_has_input(const Bill *bill, const long totals[ITEM_TYPE_COUNT],
                    const ItemTypeMask tools[RECIPE_TOOL_MAX]);

/*
 * Starts a craft of `recipe` with `input` (the station must hold no other
 * input): a load, whose items the station holds from then on (the caller
 * empties its own copy), or numbers. Returns false, changing nothing, if one
 * is already in progress.
 */
bool workstation_start_craft(Workstations *workstations, int index, RecipeType recipe,
                             ItemStack input);

/* The input a free craft of a bill gets (the debug cheat
 * GameDebug.free_crafts): the recipe's count of the first input type the
 * bill allows, of standard quality (or the least the recipe takes), in perfect
 * condition. Empty when the bill allows none. */
ItemStack bill_free_input(const Bill *bill);

/* Starts a craft of a bill's recipe with its free input (bill_free_input()),
 * which is not given back if the craft is called off. Fails like
 * workstation_start_craft(), or when the bill allows no input. */
bool workstation_start_free_craft(Workstations *workstations, int index, int bill);

/*
 * Adds work to the craft in progress. When it is complete, the products are
 * dropped on the station's tile, the bill `bill` (or the first one of the
 * same recipe if that one changed) counts it, and true is returned. Products
 * come from the worker's `level` (see recipe_craft()).
 */
bool workstation_add_craft_work(Workstations *workstations, int index, int bill, Items *items,
                                const Map *map, float work, double level, Rng *rng);

uint64_t workstations_hash(const Workstations *workstations, uint64_t hash);

#endif /* COLONY_CORE_WORKSTATION_H */
