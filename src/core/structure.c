#include "core/structure.h"

#include "core/i18n.h"
#include "core/util.h"

/*
 * Materials, for a tile of about 1 m × 1 m (branches: 1.5 kg, dry grass: an
 * armful of 1 kg, twigs: a handful of 0.2 kg; see item.c):
 * - grass bed: a litter of 2 m × 0.8 m, about 10 cm thick once slept on, of
 *   dry grass at about 80 kg/m³: 0.16 m³, 12 kg;
 * - wall, 1 m long and 2 m high: 3 posts of 2.4 m (40 cm in the ground), 4 cm
 *   thick, 2 kg each, and 8 rails of 1 m, 2 cm thick, every 25 cm, 0.2 kg
 *   each: 7.6 kg of wood, 5 branches; then 2 m² of thatch 15 cm thick, tied
 *   to the rails: 0.3 m³ of dry grass, 24 kg;
 * - branch screen, 1 m × 2 m: a frame of 2 uprights and 3 cross bars (3 kg)
 *   woven with thinner branches (4.5 kg), gaps stuffed with twigs;
 * - roof, 1 m² of ground, about 1.2 m² of slope: rafters 4 cm thick every
 *   50 cm (2.4 m, 2 kg) and thin battens every 25 cm (5 m, 1 kg): 3 branches;
 *   thatch 20 cm thick to shed rain: 0.24 m³ of dry grass, 20 kg.
 * Work: about 15 minutes to spread a bed; 2 hours for a wall segment (dig
 * the post holes with a stick, tie the rails, thatch); 1.5 hours to weave a
 * screen or to roof a tile, working overhead (TICKS_PER_HOUR = 2,500).
 * Hit points: a bed 20 (it packs down, a hit point a day, and is refreshed
 * with 4 armfuls of dry grass in 125 ticks), a wall 60, a screen 40, a roof
 * 50.
 */
static const StructureDef STRUCTURE_DEFS[STRUCTURE_TYPE_COUNT] = {
    [STRUCTURE_NONE] = {"none", NULL, NULL, {{ITEM_NONE, 0}, {ITEM_NONE, 0}}, 0.0f,
                        KNOWLEDGE_NONE, 0.0f, 1.0f, 0.0f, false, 0, 0, 0, {ITEM_NONE, 0}, 0.0f,
                        false},
    [STRUCTURE_GRASS_BED] = {"grass_bed", N_("structure.grass_bed"),
                             N_("structure.grass_bed.description"),
                             {{ITEM_GRASS, 12}, {ITEM_NONE, 0}}, 625.0f, KNOWLEDGE_BASIC_SHELTERS,
                             50.0f, 1.2f, 1.0f, false, 2, 20, 1, {ITEM_GRASS, 4}, 125.0f,
                             false},
    [STRUCTURE_WALL] = {"grass_wall", N_("structure.wall"), N_("structure.wall.description"),
                        {{ITEM_BRANCHES, 5}, {ITEM_GRASS, 24}}, 5000.0f, KNOWLEDGE_BASIC_SHELTERS,
                        50.0f, 0.0f, 0.0f, true, 1, 60, 0, {ITEM_NONE, 0}, 0.0f, false},
    [STRUCTURE_DOOR] = {"branch_screen", N_("structure.door"), N_("structure.door.description"),
                        {{ITEM_BRANCHES, 5}, {ITEM_TWIGS, 5}}, 3750.0f, KNOWLEDGE_BASIC_SHELTERS,
                        55.0f, 1.5f, 0.0f, false, 1, 40, 0, {ITEM_NONE, 0}, 0.0f, false},
    /* Over the tile: no effect on movement. */
    [STRUCTURE_ROOF] = {"grass_roof", N_("structure.roof"), N_("structure.roof.description"),
                        {{ITEM_BRANCHES, 3}, {ITEM_GRASS, 20}}, 3750.0f, KNOWLEDGE_BASIC_SHELTERS,
                        50.0f, 1.0f, 0.0f, false, 1, 50, 0, {ITEM_NONE, 0}, 0.0f, true},
};

/* Hit points by grade, crude to superb. */
static const float GRADE_HIT_POINTS[ITEM_QUALITY_COUNT] = {0.6f, 0.8f, 1.0f, 1.2f, 1.5f};

int structure_max_hit_points(StructureType type, ItemQuality quality)
{
    const StructureDef *def = structure_def(type);
    if (def == NULL || type == STRUCTURE_NONE) {
        return 0;
    }
    int grade = clampi((int)quality, ITEM_QUALITY_CRUDE, ITEM_QUALITY_BEST);
    int hit_points = (int)((float)def->max_hit_points * GRADE_HIT_POINTS[grade] + 0.5f);
    return hit_points > 1 ? hit_points : 1;
}

ItemQuality structure_quality_cap(StructureType type,
                                  const ItemStack delivered[STRUCTURE_MATERIAL_SLOTS])
{
    const StructureDef *def = structure_def(type);
    int sum = 0, graded = 0;
    for (int slot = 0; def != NULL && slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        const ItemDef *material = item_def(def->materials[slot].type);
        if (def->materials[slot].type == ITEM_NONE || material == NULL ||
            !material->has_quality || delivered[slot].count == 0) {
            continue;
        }
        sum += delivered[slot].quality;
        graded++;
    }
    if (graded == 0) {
        return ITEM_QUALITY_BEST;
    }
    /* The average rounded down, + 1. */
    return (ItemQuality)clampi(sum / graded + 1, ITEM_QUALITY_CRUDE, ITEM_QUALITY_BEST);
}

int direction_dx(Direction direction)
{
    return direction == DIRECTION_EAST ? 1 : direction == DIRECTION_WEST ? -1 : 0;
}

int direction_dy(Direction direction)
{
    return direction == DIRECTION_SOUTH ? 1 : direction == DIRECTION_NORTH ? -1 : 0;
}

Direction direction_turn(Direction direction)
{
    return (Direction)(((unsigned)direction + 1) % DIRECTION_COUNT);
}

int structure_length(StructureType type)
{
    return (unsigned)type < STRUCTURE_TYPE_COUNT ? STRUCTURE_DEFS[type].length : 0;
}

/* Taking apart is quicker than building. */
#define DECONSTRUCT_WORK_SHARE 0.25f

const StructureDef *structure_def(StructureType type)
{
    return (unsigned)type < STRUCTURE_TYPE_COUNT ? &STRUCTURE_DEFS[type] : NULL;
}

bool structure_blocks(StructureType type)
{
    return type != STRUCTURE_NONE && (unsigned)type < STRUCTURE_TYPE_COUNT &&
           STRUCTURE_DEFS[type].move_cost <= 0.0f;
}

bool structure_is_bed(StructureType type)
{
    return (unsigned)type < STRUCTURE_TYPE_COUNT && STRUCTURE_DEFS[type].rest_factor > 0.0f;
}

bool structure_is_door(StructureType type)
{
    return type == STRUCTURE_DOOR;
}

bool structure_is_roof(StructureType type)
{
    return (unsigned)type < STRUCTURE_TYPE_COUNT && STRUCTURE_DEFS[type].roof;
}

float structure_deconstruct_work(StructureType type)
{
    const StructureDef *def = structure_def(type);
    return def != NULL ? def->build_work * DECONSTRUCT_WORK_SHARE : 0.0f;
}

int structure_salvage_count(StructureType type, int slot)
{
    const StructureDef *def = structure_def(type);
    if (def == NULL || slot < 0 || slot >= STRUCTURE_MATERIAL_SLOTS ||
        def->materials[slot].type == ITEM_NONE) {
        return 0;
    }
    return def->materials[slot].count / 2;
}
