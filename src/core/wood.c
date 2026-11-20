#include "core/wood.h"

#include "core/i18n.h"

#include <string.h>

/* docs/TREES.md §6.2: oak and the fruit trees hard; beech the best firewood
 * but rotting outdoors; chestnut light, lasting outdoors; ash tough and
 * springy, the wood of shafts; birch rotting fast; hazel light straight
 * rods. */
static const WoodDef WOOD_DEFS[WOOD_SPECIES_COUNT] = {
    [PLANT_OAK - PLANT_TREE_FIRST] = {N_("wood.oak"), 1.10f, 1.10f, 0, 0.6f, 1.20f},
    [PLANT_BEECH - PLANT_TREE_FIRST] = {N_("wood.beech"), 1.10f, 1.00f, 0, 1.5f, 1.25f},
    [PLANT_CHESTNUT - PLANT_TREE_FIRST] = {N_("wood.chestnut"), 0.85f, 0.85f, -5, 0.6f, 0.85f},
    [PLANT_ASH - PLANT_TREE_FIRST] = {N_("wood.ash"), 1.05f, 1.30f, 10, 1.4f, 1.20f},
    [PLANT_BIRCH - PLANT_TREE_FIRST] = {N_("wood.birch"), 1.00f, 0.85f, -5, 1.6f, 1.00f},
    [PLANT_WILD_APPLE - PLANT_TREE_FIRST] = {N_("wood.wild_apple"), 1.05f, 1.10f, 0, 1.0f, 1.10f},
    [PLANT_WILD_PEAR - PLANT_TREE_FIRST] = {N_("wood.wild_pear"), 1.05f, 1.05f, 0, 1.0f, 1.10f},
    [PLANT_HAZEL - PLANT_TREE_FIRST] = {N_("wood.hazel"), 0.85f, 1.15f, 8, 1.0f, 0.90f},
};

PlantType wood_tree(WoodMask bit)
{
    for (int i = 0; i < WOOD_SPECIES_COUNT; i++) {
        if (bit == (WoodMask)(1u << (unsigned)i)) {
            return (PlantType)(PLANT_TREE_FIRST + i);
        }
    }
    return PLANT_NONE;
}

const WoodDef *wood_def(PlantType tree)
{
    return plant_is_tree((uint8_t)tree) ? &WOOD_DEFS[tree - PLANT_TREE_FIRST] : NULL;
}

int wood_count(WoodMask woods)
{
    int count = 0;
    for (int i = 0; i < WOOD_SPECIES_COUNT; i++) {
        count += (woods >> i) & 1u;
    }
    return count;
}

/* Which value of a WoodDef wood_mean() takes. */
typedef enum WoodValue {
    WOOD_VALUE_WEIGHT,
    WOOD_VALUE_TOUGHNESS,
    WOOD_VALUE_SHAFTS,
    WOOD_VALUE_WEATHERING,
    WOOD_VALUE_FUEL
} WoodValue;

static float value_of(const WoodDef *def, WoodValue value)
{
    switch (value) {
    case WOOD_VALUE_WEIGHT: return def->weight;
    case WOOD_VALUE_TOUGHNESS: return def->toughness;
    case WOOD_VALUE_SHAFTS: return (float)def->shafts;
    case WOOD_VALUE_WEATHERING: return def->weathering;
    case WOOD_VALUE_FUEL: return def->fuel;
    }
    return 0.0f;
}

/* The mean of a value over the species of a mask; `none` without any. */
static float wood_mean(WoodMask woods, WoodValue value, float none)
{
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < WOOD_SPECIES_COUNT; i++) {
        if ((woods >> i) & 1u) {
            sum += value_of(&WOOD_DEFS[i], value);
            count++;
        }
    }
    return count > 0 ? sum / (float)count : none;
}

float wood_weight(WoodMask woods)
{
    return wood_mean(woods, WOOD_VALUE_WEIGHT, 1.0f);
}

float wood_toughness(WoodMask woods)
{
    return wood_mean(woods, WOOD_VALUE_TOUGHNESS, 1.0f);
}

int wood_shafts(WoodMask woods)
{
    float mean = wood_mean(woods, WOOD_VALUE_SHAFTS, 0.0f);
    return (int)(mean < 0.0f ? mean - 0.5f : mean + 0.5f);
}

float wood_weathering(WoodMask woods)
{
    return wood_mean(woods, WOOD_VALUE_WEATHERING, 1.0f);
}

float wood_fuel(WoodMask woods)
{
    return wood_mean(woods, WOOD_VALUE_FUEL, 1.0f);
}

size_t wood_list_name(WoodMask woods, char *out, size_t size)
{
    char list[256] = "", part[256];
    int left = wood_count(woods);
    size_t length = 0;
    for (int i = 0; i < WOOD_SPECIES_COUNT && left > 0; i++) {
        if (!((woods >> i) & 1u)) {
            continue;
        }
        const char *name = tr(WOOD_DEFS[i].name_key);
        if (list[0] == '\0') {
            length = tr_format(part, sizeof(part), "{0}", 1, (const char *const[]){name});
        } else {
            /* The last one joins with "and", the others with a comma. */
            const char *pattern = tr(left == 1 ? "wood.list.two" : "wood.list.more");
            length = TR_FORMAT(part, sizeof(part), pattern, list, name);
        }
        memcpy(list, part, sizeof(list));
        left--;
    }
    if (size > 0) {
        size_t copy = strlen(list) < size - 1 ? strlen(list) : size - 1;
        memcpy(out, list, copy);
        out[copy] = '\0';
    }
    return length;
}

int wood_count_lying(const Items *items, ItemType type, int counts[WOOD_SPECIES_COUNT])
{
    memset(counts, 0, sizeof(int) * WOOD_SPECIES_COUNT);
    int unknown = 0;
    for (uint32_t slot = 0; slot < items->pool_used; slot++) {
        const Item *item = &items->pool[slot];
        if (item->type != (uint8_t)type || item->tile == ITEM_TILE_HELD || item->forbidden) {
            continue;
        }
        unknown += item->woods == 0;
        for (int i = 0; i < WOOD_SPECIES_COUNT; i++) {
            counts[i] += (item->woods >> i) & 1u;
        }
    }
    return unknown;
}

size_t wood_item_name(ItemType type, WoodMask woods, char *out, size_t size)
{
    const ItemDef *def = item_def(type);
    if (def == NULL || def->name_key == NULL) {
        if (size > 0) {
            out[0] = '\0';
        }
        return 0;
    }
    size_t length;
    if (!def->wooden || def->wood_name_key == NULL || (woods & WOOD_ALL) == 0) {
        length = tr_format(out, size, tr(def->name_key), 0, NULL);
    } else {
        char list[256];
        wood_list_name(woods, list, sizeof(list));
        length = TR_FORMAT(out, size, tr(def->wood_name_key), list);
    }
    if (size > 0 && out[0] >= 'a' && out[0] <= 'z') {
        out[0] = (char)(out[0] - 'a' + 'A'); /* "{0} branches" with "oak". */
    }
    return length;
}
