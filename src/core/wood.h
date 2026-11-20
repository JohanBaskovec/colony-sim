/*
 * Wood species (docs/TREES.md §6): logs, branches and what is made from them
 * carry the species of the trees their wood came from, a bit per tree species
 * (WoodMask, ItemStack.woods). A species changes a little how the wood
 * weighs, wears, makes shafts, weathers out of doors and burns; an item of
 * several species takes the mean of their values.
 */
#ifndef COLONY_CORE_WOOD_H
#define COLONY_CORE_WOOD_H

#include "core/item.h"
#include "core/map.h"

#include <stdbool.h>

/* Species of wood: one per tree species, in the order of the plant types. */
#define WOOD_SPECIES_COUNT (PLANT_TREE_LAST - PLANT_TREE_FIRST + 1)
_Static_assert(WOOD_SPECIES_COUNT <= 16, "wood species must fit in a WoodMask");

/* The bit of a tree species' wood (0 for other plants). */
static inline WoodMask wood_bit(PlantType tree)
{
    return plant_is_tree((uint8_t)tree) ? (WoodMask)(1u << (unsigned)(tree - PLANT_TREE_FIRST)) : 0;
}

/* The tree species of a single bit's wood (PLANT_NONE for anything else). */
PlantType wood_tree(WoodMask bit);

/* How a species' wood differs from the plain one (tuning; ×1 or +0 for
 * none). */
typedef struct WoodDef {
    const char *name_key; /* Its name as a material: "oak", "chêne". */
    float weight;         /* × the weight of the items made of it. */
    /* ÷ the wear and damage the tools, weapons and structures made of it
     * take: a tougher wood lasts longer. */
    float toughness;
    int shafts;       /* Quality points added to spears and digging sticks. */
    float weathering; /* × the wear of a structure out of doors. */
    float fuel;       /* × the time a log or a branch burns. */
} WoodDef;

/* The species of the tree species `tree` (NULL for other plants). */
const WoodDef *wood_def(PlantType tree);

/* Number of species in a mask. */
int wood_count(WoodMask woods);

/* The values of a mix of species: the mean of theirs (×1 and +0 for an
 * empty mask, wood of no known species). */
float wood_weight(WoodMask woods);
float wood_toughness(WoodMask woods);
int wood_shafts(WoodMask woods);
float wood_weathering(WoodMask woods);
float wood_fuel(WoodMask woods);

/* The species of a mask by name, as a material: "oak", "ash and oak",
 * "beech, birch and oak" (translated, core/i18n.h); "" for none. Returns
 * the length the whole text would have, as tr_format() does. */
size_t wood_list_name(WoodMask woods, char *out, size_t size);

/* The name of an item of a type made of `woods`: "Oak branches", "Épieu en
 * frêne et chêne" (ItemDef.wood_name_key); the type's plain name for types
 * not of wood and for wood of no known species. Begins with a capital
 * letter. Returns the length the whole text would have. */
size_t wood_item_name(ItemType type, WoodMask woods, char *out, size_t size);

/* How many items of `type` lie about (not forbidden, not in a load) of each
 * species: counts[i] for the species of bit i (an item of several species
 * counts for each); returns how many there are of no known species. */
int wood_count_lying(const Items *items, ItemType type, int counts[WOOD_SPECIES_COUNT]);

/* Every species. */
#define WOOD_ALL ((WoodMask)((1u << WOOD_SPECIES_COUNT) - 1u))

#endif /* COLONY_CORE_WOOD_H */
