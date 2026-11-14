#include "core/structure.h"

#include "core/i18n.h"

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
 *   woven with thinner branches (4.5 kg), gaps stuffed with twigs.
 * Work: about 15 minutes to spread a bed; 2 hours for a wall segment (dig
 * the post holes with a stick, tie the rails, thatch); 1.5 hours to weave a
 * screen (TICKS_PER_HOUR = 2,500).
 */
static const StructureDef STRUCTURE_DEFS[STRUCTURE_TYPE_COUNT] = {
    [STRUCTURE_NONE] = {"none", NULL, NULL, {{ITEM_NONE, 0}, {ITEM_NONE, 0}}, 0.0f,
                        KNOWLEDGE_NONE, 0.0f, 1.0f, 0.0f, false},
    [STRUCTURE_GRASS_BED] = {"grass_bed", N_("structure.grass_bed"),
                             N_("structure.grass_bed.description"),
                             {{ITEM_GRASS, 12}, {ITEM_NONE, 0}}, 625.0f, KNOWLEDGE_BASIC_SHELTERS,
                             50.0f, 1.2f, 1.0f, false},
    [STRUCTURE_WALL] = {"grass_wall", N_("structure.wall"), N_("structure.wall.description"),
                        {{ITEM_BRANCHES, 5}, {ITEM_GRASS, 24}}, 5000.0f, KNOWLEDGE_BASIC_SHELTERS,
                        50.0f, 0.0f, 0.0f, true},
    [STRUCTURE_DOOR] = {"branch_screen", N_("structure.door"), N_("structure.door.description"),
                        {{ITEM_BRANCHES, 5}, {ITEM_TWIGS, 5}}, 3750.0f, KNOWLEDGE_BASIC_SHELTERS,
                        55.0f, 1.5f, 0.0f, false},
};

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
