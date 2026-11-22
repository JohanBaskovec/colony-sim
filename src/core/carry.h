/*
 * Carrying by hand (docs/CARRYING.md): the places of a body where things are
 * carried (a hand, both hands, under an arm, the shoulders, slung), what each
 * place takes by an item's form, length, room and weight, the containers
 * kept there (baskets in a hand, a pack basket on the back), and the
 * arrangement of what a settler carries among them.
 *
 * Pure: an arrangement follows from what is carried (the kept items and a
 * load of one type), the item definitions and the body (the weight carried
 * without slowing down and each arm's efficiency), with no state of its own.
 * Kept items go first where they leave the hands free; the load takes the
 * arrangement of the places left that carries the most of it (kept items
 * move to make room), at equal count the one with fewer places, then the one
 * leaving a hand free.
 */
#ifndef COLONY_CORE_CARRY_H
#define COLONY_CORE_CARRY_H

#include "core/item.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum CarryPlace {
    CARRY_RIGHT_HAND,
    CARRY_LEFT_HAND,
    CARRY_BOTH_HANDS, /* An armful, cupped hands, or one item dragged. */
    CARRY_UNDER_RIGHT_ARM,
    CARRY_UNDER_LEFT_ARM,
    CARRY_SHOULDERS,
    CARRY_SLUNG, /* Items with a strap. */
    /* On the back by its straps: a pack basket (ItemDef.on_back), nothing beside
     * it on the shoulders. */
    CARRY_BACK,
    /* In a container carried in a hand, slung or on the back (a basket, a tray:
     * ItemDef.container_litres, docs/CONTAINERS.md §7.2): a place of its own, filled with what is
     * left of the load once the body's places are; never a place of the body. */
    CARRY_IN_CONTAINER,
    CARRY_PLACE_COUNT
} CarryPlace;

typedef enum CarrySide { CARRY_RIGHT, CARRY_LEFT, CARRY_SIDE_COUNT } CarrySide;

/* What the player asked of an item a settler carries (docs/CARRYING.md
 * §7.1). */
typedef enum CarryLock {
    CARRY_LOCK_NONE,    /* Free: the settler may set it down, store it, use it up. */
    CARRY_LOCK_KEPT,    /* Always carried, wherever it fits best. */
    CARRY_LOCK_IN_HAND, /* Always carried, in a hand, ready. */
    CARRY_LOCK_COUNT
} CarryLock;

/* What both hands do. */
typedef enum CarryHold {
    CARRY_HOLD_NONE,
    CARRY_HOLD_ARMFUL, /* Both arms around a load: the places under the arms are taken too. */
    CARRY_HOLD_CUPPED, /* Loose items. */
    CARRY_HOLD_DRAGGED,
    CARRY_HOLD_SHOULDERS, /* Holding a heavy limp load on the shoulders by its legs. */
    CARRY_HOLD_GRIP,      /* Both hands on one item wielded (a two-handed weapon). */
} CarryHold;

/* Places' room and weight (docs/CARRYING.md §3, tuning). Room is a share of
 * a place: bulk over the place's room, or one over the most long rigid items
 * it takes. Weights are shares of the weight carried without slowing down. */
#define CARRY_HAND_ROOM 1.5f
#define CARRY_HAND_LOOSE_ROOM 0.5f
#define CARRY_HAND_LONG 2
#define CARRY_HAND_WEIGHT 0.25f
#define CARRY_ARMFUL_ROOM 60.0f
#define CARRY_ARMFUL_PIECES_ROOM 12.0f /* Short rigid pieces roll out. */
#define CARRY_ARMFUL_LONG 6
#define CARRY_CUPPED_ROOM 2.0f
#define CARRY_BOTH_WEIGHT 0.75f
#define CARRY_UNDER_ROOM 20.0f
#define CARRY_UNDER_LONG 2
#define CARRY_UNDER_WEIGHT 0.2f
#define CARRY_SHOULDERS_ROOM 60.0f
#define CARRY_SHOULDERS_LONG 3
#define CARRY_SHOULDERS_WEIGHT 1.0f
#define CARRY_SHOULDERS_RIGID_KG 10.0f /* A rigid item this heavy goes on the shoulders. */
#define CARRY_SLUNG_COUNT 2
#define CARRY_SLUNG_WEIGHT 0.25f
#define CARRY_BACK_WEIGHT 1.0f
/* A load on the shoulders over this share, or long rigid items there, need
 * a hand steadying it; a limp one over CARRY_HELD_WEIGHT, both hands. */
#define CARRY_STEADY_WEIGHT 0.25f
#define CARRY_HELD_WEIGHT 0.5f
/* A single limp item over every place goes on the shoulders, held with both
 * hands, up to this share; heavier ones (or without both arms) are dragged. */
#define CARRY_HEAVY_LIMP 2.0f
/* An arm below this efficiency holds nothing. */
#define CARRY_ARM_MIN 0.2f
/* What goes in a container (carry_container_takes()): items shorter than
 * ITEM_LONG_CM and no heavier than this, a container's room being its litres
 * in bulk (two a litre, ITEM_TILE_BULK). */
#define CARRY_CONTAINER_ITEM_KG 2.0f
#define CARRY_CONTAINER_BULK_PER_LITRE 2.0f

/* Kept items an arrangement takes at most, and its entries. */
#define CARRY_KEPT_MAX 16
#define CARRY_ENTRY_MAX 24

typedef struct CarryBody {
    float weight;                /* Kilograms carried without slowing down. */
    float arm[CARRY_SIDE_COUNT]; /* Each arm's efficiency, 0..1. */
} CarryBody;

/* Items of one type in a place: kept ones, or the load. */
typedef struct CarryEntry {
    uint8_t place; /* CarryPlace. */
    uint8_t type;  /* ItemType. */
    uint16_t count;
    bool kept;
} CarryEntry;

typedef struct CarryArrangement {
    CarryEntry entries[CARRY_ENTRY_MAX];
    int entry_count;
    int load;                          /* Items of the load placed. */
    uint32_t left_out;                 /* Bit i: kept item i fits nowhere. */
    int8_t kept_place[CARRY_KEPT_MAX]; /* Kept item i's CarryPlace, -1 when left out. */
    CarryHold hold;                    /* What both hands do. */
    int steadying;                     /* The hand (CarryPlace) steadying the shoulders, or -1. */
    unsigned working;                  /* Bit per hand (CarryPlace) kept empty for the work. */
} CarryArrangement;

/* Whether the body can use a place: a hand or under an arm needs that arm
 * at CARRY_ARM_MIN or more, both hands need both. */
bool carry_place_usable(const CarryBody *body, CarryPlace place);

/* The most weight a place bears, kilograms (an arm's places times its
 * efficiency, both hands times the weaker one). */
float carry_place_weight_limit(const CarryBody *body, CarryPlace place);

/* Whether a place takes items of a type at all (by its form and length). */
bool carry_place_takes(CarryPlace place, ItemType type);

/* Whether a type is a container (ItemDef.container_litres), and whether a
 * container takes items of a type: small ones (berries, nuts, roots, twigs,
 * cord, stones), not long nor heavy ones. */
bool carry_is_container(ItemType type);
bool carry_container_takes(ItemType type);

/* Whether a container holds items of a type: what containers take
 * (carry_container_takes()), and no soft fruit in a net (ItemDef.net, soft). */
bool carry_container_holds(ItemType container, ItemType type);

/*
 * Arranges `kept` (kept_count items, at most CARRY_KEPT_MAX) and up to
 * `load_count` items of `load` (ITEM_NONE: none). Returns how many of the
 * load it places: the most that fit within the places and the weight the
 * body carries (the kept items' included); one at least when load_count is
 * positive (a single item over every place: on the shoulders, held, if limp
 * up to CARRY_HEAVY_LIMP of the weight and with both arms, else dragged).
 * `out` may be NULL.
 */
int carry_arrange(const CarryBody *body, const ItemType *kept, int kept_count, ItemType load,
                  int load_count, CarryArrangement *out);

/* How many items of `type` the body carries beside `kept` (carry_arrange()). */
int carry_capacity(const CarryBody *body, const ItemType *kept, int kept_count, ItemType type);

/* The same, with the kept items of the mask `in_hand` (bit i: kept[i]) kept
 * in a hand (docs/CARRYING.md §7.1): placed first, in the hand of the better
 * arm, else the other, else both hands. */
int carry_arrange_held(const CarryBody *body, const ItemType *kept, int kept_count,
                       uint32_t in_hand, ItemType load, int load_count, CarryArrangement *out);
int carry_capacity_held(const CarryBody *body, const ItemType *kept, int kept_count,
                        uint32_t in_hand, ItemType type);

/* The same, with the kept items of the mask `grip` (a part of `in_hand`)
 * held in both hands, as a weapon wielded with both (docs/COMBAT.md §7):
 * placed first, alone in both hands, which leaves the places under the arms
 * free (unlike an armful) but no hand to steady the shoulders. */
int carry_arrange_gripped(const CarryBody *body, const ItemType *kept, int kept_count,
                          uint32_t in_hand, uint32_t grip, ItemType load, int load_count,
                          CarryArrangement *out);
int carry_capacity_gripped(const CarryBody *body, const ItemType *kept, int kept_count,
                           uint32_t in_hand, uint32_t grip, ItemType type);

/* The arrangement while an action needs `hands` hands (0 to 2,
 * docs/CARRYING.md §6.3), without a load (it is set aside): the items held
 * with both hands and those kept in hand first, then the tools in use (the
 * mask `work`) in a hand, then as many hands as the action needs beside
 * theirs kept empty (`working`, the better arm's first), then the others
 * around them. The kept items left out are set aside: the action may still
 * lack hands when items kept in hand take them. */
void carry_arrange_working(const CarryBody *body, const ItemType *kept, int kept_count,
                           uint32_t in_hand, uint32_t grip, uint32_t work, int hands,
                           CarryArrangement *out);

/* How many hands (0 to 2) the items kept in hand take in an arrangement
 * (an item in both hands takes two). Items of the same type as one kept in
 * hand count with it. */
int carry_hands_taken(const CarryArrangement *arrangement, const ItemType *kept, int kept_count,
                      uint32_t in_hand);

#endif /* COLONY_CORE_CARRY_H */
