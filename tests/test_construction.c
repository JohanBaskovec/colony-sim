#include "core/construction.h"
#include "core/i18n.h"
#include "core/structure.h"

#include "test_framework.h"

/* 16 x 16 soil map with a granite wall at (5, 5), water at (6, 6), a tree at
 * (7, 7) and grass at (8, 8). */
static bool init_world(Map *map, Constructions *constructions, Items *items)
{
    if (!map_init(map, 16, 16) || !constructions_init(constructions, 16, 16) ||
        !items_init(items, 16, 16)) {
        return false;
    }
    map->rock[map_index(map, 5, 5)] = ROCK_GRANITE;
    map->terrain[map_index(map, 6, 6)] = TERRAIN_DEEP_WATER;
    map->plants[map_index(map, 7, 7)] = (Plant){.type = PLANT_TREE, .growth = 1.0f};
    map->plants[map_index(map, 8, 8)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    return true;
}

static void free_world(Map *map, Constructions *constructions, Items *items)
{
    items_free(items);
    constructions_free(constructions);
    map_free(map);
}

static void test_definitions(void)
{
    for (int type = 1; type < STRUCTURE_TYPE_COUNT; type++) {
        const StructureDef *def = structure_def((StructureType)type);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL && i18n_is_valid_key(def->name_key));
        CHECK(i18n_is_valid_key(def->description_key));
        CHECK(def->materials[0].type != ITEM_NONE && def->materials[0].count > 0);
        CHECK(def->build_work > 0.0f);
        CHECK(def->knowledge == KNOWLEDGE_BASIC_SHELTERS);
        CHECK(structure_deconstruct_work((StructureType)type) < def->build_work);
    }
    CHECK(structure_def(STRUCTURE_TYPE_COUNT) == NULL);
    /* Beds and doors can be walked through, walls cannot. */
    CHECK(!structure_blocks(STRUCTURE_NONE));
    CHECK(!structure_blocks(STRUCTURE_GRASS_BED));
    CHECK(structure_blocks(STRUCTURE_WALL));
    CHECK(!structure_blocks(STRUCTURE_DOOR));
    CHECK(structure_def(STRUCTURE_DOOR)->move_cost > 1.0f);
    CHECK(structure_is_bed(STRUCTURE_GRASS_BED));
    CHECK(!structure_is_bed(STRUCTURE_WALL));
    CHECK(!structure_is_bed(STRUCTURE_NONE));
    /* Dry grass only (fresh grass would mould). */
    for (int type = 1; type < STRUCTURE_TYPE_COUNT; type++) {
        const StructureDef *def = structure_def((StructureType)type);
        for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            CHECK(def->materials[slot].type != ITEM_FRESH_GRASS);
        }
    }
    /* A wall segment of 1 m × 2 m: about 7.5 kg of wood and 24 kg of thatch. */
    const StructureDef *wall = structure_def(STRUCTURE_WALL);
    CHECK_EQ_INT(wall->materials[0].type, ITEM_BRANCHES);
    CHECK_EQ_INT(wall->materials[1].type, ITEM_GRASS);
    CHECK_NEAR(item_stack_weight(ITEM_BRANCHES, wall->materials[0].count), 7.5f, 0.01f);
    CHECK_NEAR(item_stack_weight(ITEM_GRASS, wall->materials[1].count), 24.0f, 0.01f);
    CHECK(wall->drawn_as_outline);
    CHECK_EQ_INT(structure_salvage_count(STRUCTURE_WALL, 1), 12);
    CHECK_EQ_INT(structure_salvage_count(STRUCTURE_GRASS_BED, 1), 0); /* Unused slot. */
}

static void test_map_layer(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16));
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_NONE], 256);
    map.plants[map_index(&map, 2, 2)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    uint32_t revision = map.topology_revision;
    uint64_t hash = map_hash(&map);

    /* A bed: no topology change, slower to cross, the grass is gone. */
    map_set_structure(&map, 2, 2, STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure(&map, 2, 2), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map.plants[map_index(&map, 2, 2)].type, PLANT_NONE);
    CHECK_EQ_UINT(map.topology_revision, revision);
    CHECK(map_is_passable(&map, 2, 2));
    CHECK_NEAR(map_move_cost(&map, 2, 2), 1.2f, 0.001f);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 1);
    CHECK(map_hash(&map) != hash);

    /* A wall blocks: a topology change. */
    map_set_structure(&map, 3, 2, STRUCTURE_WALL);
    CHECK(!map_is_passable(&map, 3, 2));
    CHECK_EQ_UINT(map.topology_revision, revision + 1);
    map_set_structure(&map, 3, 2, STRUCTURE_WALL); /* No change. */
    CHECK_EQ_UINT(map.topology_revision, revision + 1);
    map_set_structure(&map, 3, 2, STRUCTURE_NONE);
    CHECK(map_is_passable(&map, 3, 2));
    CHECK_EQ_UINT(map.topology_revision, revision + 2);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_WALL], 0);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_NONE], 255);

    /* Out of bounds and invalid types are ignored. */
    map_set_structure(&map, -1, 0, STRUCTURE_WALL);
    map_set_structure(&map, 1, 1, STRUCTURE_TYPE_COUNT);
    CHECK_EQ_INT(map_structure(&map, -1, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&map, 1, 1), STRUCTURE_NONE);
    map_free(&map);
}

static void test_placing_and_removing(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    CHECK(!construction_can_place(&constructions, &map, 5, 5)); /* Rock. */
    CHECK(!construction_can_place(&constructions, &map, 6, 6)); /* Water. */
    CHECK(!construction_can_place(&constructions, &map, 7, 7)); /* Tree. */
    CHECK(!construction_can_place(&constructions, &map, 16, 0));
    CHECK(construction_can_place(&constructions, &map, 8, 8)); /* Grass is cleared. */
    map.plants[map_index(&map, 10, 10)] = (Plant){.type = PLANT_BUSH, .growth = 1.0f};
    CHECK(construction_can_place(&constructions, &map, 10, 10)); /* So is a bush. */
    map_set_structure(&map, 9, 9, STRUCTURE_GRASS_BED);
    CHECK(!construction_can_place(&constructions, &map, 9, 9)); /* Already built. */
    CHECK_EQ_INT(constructions_place(&constructions, &map, STRUCTURE_NONE, 2, 2),
                 CONSTRUCTION_NONE);

    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2);
    REQUIRE(index != CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2), index);
    CHECK_EQ_INT(constructions.used_count, 1);
    CHECK(!construction_can_place(&constructions, &map, 2, 2));
    const ConstructionSite *site = construction_get(&constructions, index);
    REQUIRE(site != NULL);
    CHECK_EQ_INT(construction_missing(site, 0), 5);
    CHECK_EQ_INT(construction_missing(site, 1), 24);
    CHECK(!construction_materials_complete(site));

    /* Delivered materials come back when the site is removed. */
    CHECK_EQ_INT(construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 10)),
                 10);
    CHECK_EQ_INT(construction_deliver(&constructions, index, item_stack_pristine(ITEM_WOOD, 3)), 0);
    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
    CHECK_EQ_INT(constructions_remove(&constructions, index, returned), 1);
    CHECK_EQ_INT(returned[0].type, ITEM_GRASS);
    CHECK_EQ_INT(returned[0].count, 10);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2), CONSTRUCTION_NONE);
    CHECK(construction_get(&constructions, index) == NULL);
    CHECK_EQ_INT(constructions.used_count, 0);
    CHECK_EQ_INT(constructions_remove(&constructions, index, returned), 0);
    free_world(&map, &constructions, &items);
}

static void test_delivering(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2);
    const ConstructionSite *site = construction_get(&constructions, index);
    uint32_t revision = constructions.revision;
    /* Only what is missing is taken; grades mix, the lowest one stays. */
    ItemStack fine = item_stack_make(ITEM_BRANCHES, 3, ITEM_QUALITY_FINE, 20);
    ItemStack crude = item_stack_make(ITEM_BRANCHES, 4, ITEM_QUALITY_CRUDE, 20);
    CHECK_EQ_INT(construction_deliver(&constructions, index, fine), 3);
    CHECK_EQ_INT(construction_deliver(&constructions, index, crude), 2);
    CHECK_EQ_INT(site->delivered[0].count, 5);
    CHECK_EQ_INT(site->delivered[0].quality, ITEM_QUALITY_CRUDE);
    CHECK_EQ_INT(construction_missing(site, 0), 0);
    CHECK(constructions.revision != revision);
    CHECK_EQ_INT(construction_deliver(&constructions, index, crude), 0); /* Full. */
    /* No work before every material is in. */
    CHECK(!construction_add_work(&constructions, &map, &items, index, 1.0e6f));
    CHECK_NEAR(site->progress, 0.0f, 0.0001f);
    CHECK_EQ_INT(construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 30)),
                 24);
    CHECK(construction_materials_complete(site));
    free_world(&map, &constructions, &items);
}

static void test_building(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 8, 8);
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_BRANCHES, 5));
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 24));
    items_add(&items, 8, 8, item_stack_pristine(ITEM_COBBLE, 3)); /* In the way. */
    float work = structure_def(STRUCTURE_WALL)->build_work;
    CHECK(!construction_add_work(&constructions, &map, &items, index, work * 0.5f));
    CHECK_NEAR(construction_work_fraction(construction_get(&constructions, index)), 0.5f, 0.001f);
    CHECK(map_is_passable(&map, 8, 8)); /* Not built yet. */
    CHECK(construction_add_work(&constructions, &map, &items, index, work * 0.5f));
    /* Built: the wall blocks, the grass is gone, the site is gone, the
     * stones were pushed next door, the materials are used up. */
    CHECK_EQ_INT(map_structure(&map, 8, 8), STRUCTURE_WALL);
    CHECK(!map_is_passable(&map, 8, 8));
    CHECK_EQ_INT(map.plants[map_index(&map, 8, 8)].type, PLANT_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 8, 8), CONSTRUCTION_NONE);
    CHECK_EQ_INT(constructions.used_count, 0);
    CHECK_EQ_INT(items_get(&items, 8, 8).count, 0);
    CHECK_EQ_INT(items.totals[ITEM_COBBLE], 3);
    CHECK_EQ_INT(items.totals[ITEM_GRASS], 0);
    free_world(&map, &constructions, &items);
}

static void test_hash(void)
{
    Map map;
    Constructions a, b;
    Items items;
    REQUIRE(init_world(&map, &a, &items));
    REQUIRE(constructions_init(&b, 16, 16));
    CHECK(constructions_hash(&a, 1) == constructions_hash(&b, 1));
    int index = constructions_place(&a, &map, STRUCTURE_DOOR, 1, 1);
    CHECK(constructions_hash(&a, 1) != constructions_hash(&b, 1));
    constructions_place(&b, &map, STRUCTURE_DOOR, 1, 1);
    CHECK(constructions_hash(&a, 1) == constructions_hash(&b, 1));
    construction_deliver(&a, index, item_stack_pristine(ITEM_TWIGS, 2));
    CHECK(constructions_hash(&a, 1) != constructions_hash(&b, 1));
    constructions_free(&b);
    free_world(&map, &a, &items);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_map_layer);
    RUN_TEST(test_placing_and_removing);
    RUN_TEST(test_delivering);
    RUN_TEST(test_building);
    RUN_TEST(test_hash);
    return test_summary();
}
