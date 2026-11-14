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

    /* A bed (two tiles, facing south): no topology change, slower to cross,
     * the grass is gone. */
    map_set_structure(&map, 2, 2, STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure(&map, 2, 2), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure(&map, 2, 3), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map.plants[map_index(&map, 2, 2)].type, PLANT_NONE);
    CHECK_EQ_UINT(map.topology_revision, revision);
    CHECK(map_is_passable(&map, 2, 2));
    CHECK_NEAR(map_move_cost(&map, 2, 2), 1.2f, 0.001f);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 2);
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
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_NONE], 254);

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
    CHECK_EQ_INT(constructions_place(&constructions, &map, STRUCTURE_NONE, 2, 2, DIRECTION_SOUTH),
                 CONSTRUCTION_NONE);

    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, DIRECTION_SOUTH);
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
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, DIRECTION_SOUTH);
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
    CHECK(!construction_add_work(&constructions, &map, &items, index, 1.0e6f, 60.0, NULL));
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
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 8, 8, DIRECTION_SOUTH);
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_BRANCHES, 5));
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 24));
    items_add(&items, 8, 8, item_stack_pristine(ITEM_COBBLE, 3)); /* In the way. */
    float work = structure_def(STRUCTURE_WALL)->build_work;
    CHECK(!construction_add_work(&constructions, &map, &items, index, work * 0.5f, 60.0, NULL));
    CHECK_NEAR(construction_work_fraction(construction_get(&constructions, index)), 0.5f, 0.001f);
    CHECK(map_is_passable(&map, 8, 8)); /* Not built yet. */
    CHECK(construction_add_work(&constructions, &map, &items, index, work * 0.5f, 60.0, NULL));
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
    int index = constructions_place(&a, &map, STRUCTURE_DOOR, 1, 1, DIRECTION_SOUTH);
    CHECK(constructions_hash(&a, 1) != constructions_hash(&b, 1));
    constructions_place(&b, &map, STRUCTURE_DOOR, 1, 1, DIRECTION_SOUTH);
    CHECK(constructions_hash(&a, 1) == constructions_hash(&b, 1));
    construction_deliver(&a, index, item_stack_pristine(ITEM_TWIGS, 2));
    CHECK(constructions_hash(&a, 1) != constructions_hash(&b, 1));
    constructions_free(&b);
    free_world(&map, &a, &items);
}

static void test_grades_and_hit_points(void)
{
    /* Hit points by grade: ×0.6 to ×1.5 of the standard ones. */
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_STANDARD), 60);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_CRUDE), 36);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_SUPERB), 90);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_GRASS_BED, ITEM_QUALITY_STANDARD), 20);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_NONE, ITEM_QUALITY_STANDARD), 0);
    /* Materials cap the grade: their average + 1; grass and twigs do not. */
    ItemStack delivered[STRUCTURE_MATERIAL_SLOTS] = {
        item_stack_make(ITEM_BRANCHES, 5, ITEM_QUALITY_CRUDE, 0),
        item_stack_pristine(ITEM_GRASS, 24)};
    CHECK_EQ_INT(structure_quality_cap(STRUCTURE_WALL, delivered), ITEM_QUALITY_ROUGH);
    delivered[0].quality = ITEM_QUALITY_FINE;
    CHECK_EQ_INT(structure_quality_cap(STRUCTURE_WALL, delivered), ITEM_QUALITY_SUPERB);
    ItemStack grass[STRUCTURE_MATERIAL_SLOTS] = {item_stack_pristine(ITEM_GRASS, 12),
                                                 {.type = ITEM_NONE}};
    CHECK_EQ_INT(structure_quality_cap(STRUCTURE_GRASS_BED, grass), ITEM_QUALITY_SUPERB);

    /* The map layer: grade and hit points. */
    Map map;
    REQUIRE(map_init(&map, 16, 16));
    map_set_structure(&map, 2, 2, STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure_quality(&map, 2, 2), ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2), 60);
    uint64_t hash = map_hash(&map);
    map_place_structure(&map, 2, 2, STRUCTURE_WALL, DIRECTION_SOUTH, ITEM_QUALITY_FINE);
    CHECK_EQ_INT(map_structure_quality(&map, 2, 2), ITEM_QUALITY_FINE);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2), 72);
    CHECK(map_hash(&map) != hash);
    map_set_structure_hit_points(&map, 2, 2, 500);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2), 72);
    map_set_structure_hit_points(&map, 2, 2, -4);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2), 1);
    map_set_structure_hit_points(&map, 3, 3, 10); /* No structure: nothing. */
    CHECK_EQ_INT(map_structure_hit_points(&map, 3, 3), 0);
    map_set_structure(&map, 2, 2, STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2), 0);
    CHECK_EQ_INT(map_structure_hit_points(&map, -1, 2), 0);
    map_free(&map);
}

static void test_built_grade(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    /* A master builder, crude branches: rough at best. */
    int wall = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, DIRECTION_SOUTH);
    construction_deliver(&constructions, wall,
                         item_stack_make(ITEM_BRANCHES, 5, ITEM_QUALITY_CRUDE, 0));
    construction_deliver(&constructions, wall, item_stack_pristine(ITEM_GRASS, 24));
    CHECK(construction_add_work(&constructions, &map, &items, wall, 1.0e6f,
                                KNOWLEDGE_LEVEL_MAX, NULL));
    CHECK_EQ_INT(map_structure_quality(&map, 2, 2), ITEM_QUALITY_ROUGH);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2),
                 structure_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_ROUGH));
    /* Grass does not cap: the builder's level does. */
    int bed = constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 3, 3, DIRECTION_SOUTH);
    construction_deliver(&constructions, bed, item_stack_pristine(ITEM_GRASS, 12));
    CHECK(construction_add_work(&constructions, &map, &items, bed, 1.0e6f, KNOWLEDGE_LEVEL_MAX,
                                NULL));
    CHECK_EQ_INT(map_structure_quality(&map, 3, 3), ITEM_QUALITY_SUPERB);
    int poor =
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 4, 4, DIRECTION_SOUTH);
    construction_deliver(&constructions, poor, item_stack_pristine(ITEM_GRASS, 12));
    CHECK(construction_add_work(&constructions, &map, &items, poor, 1.0e6f, 50.0, NULL));
    CHECK_EQ_INT(map_structure_quality(&map, 4, 4), ITEM_QUALITY_ROUGH);
    free_world(&map, &constructions, &items);
}

static void test_refreshing_a_bed(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    map_place_structure(&map, 2, 2, STRUCTURE_GRASS_BED, DIRECTION_SOUTH, ITEM_QUALITY_FINE);
    map_set_structure(&map, 3, 3, STRUCTURE_WALL);
    /* Only on a structure that can be refreshed, once. */
    CHECK_EQ_INT(constructions_place_refresh(&constructions, &map, 3, 3), CONSTRUCTION_NONE);
    CHECK_EQ_INT(constructions_place_refresh(&constructions, &map, 4, 4), CONSTRUCTION_NONE);
    int index = constructions_place_refresh(&constructions, &map, 2, 2);
    REQUIRE(index != CONSTRUCTION_NONE);
    CHECK_EQ_INT(constructions_place_refresh(&constructions, &map, 2, 2), CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&constructions, index);
    CHECK(site->refresh);
    CHECK_EQ_INT(construction_material(site, 0), ITEM_GRASS);
    CHECK_EQ_INT(construction_material_count(site, 0), 4);
    CHECK_EQ_INT(construction_material(site, 1), ITEM_NONE);
    CHECK_NEAR(construction_work_needed(site), 125.0, 1e-4);
    map_set_structure_hit_points(&map, 2, 2, 5);
    CHECK(!construction_add_work(&constructions, &map, &items, index, 1.0e6f, 60.0, NULL));
    CHECK_EQ_INT(construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 9)),
                 4);
    CHECK(!construction_add_work(&constructions, &map, &items, index, 100.0f, 60.0, NULL));
    CHECK(construction_add_work(&constructions, &map, &items, index, 25.0f, 60.0, NULL));
    /* Back to full hit points, the grade kept. */
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2), 24);
    CHECK_EQ_INT(map_structure_quality(&map, 2, 2), ITEM_QUALITY_FINE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2), CONSTRUCTION_NONE);
    /* A refresh of a structure that is gone does nothing. */
    index = constructions_place_refresh(&constructions, &map, 2, 2);
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 4));
    map_set_structure(&map, 2, 2, STRUCTURE_NONE);
    CHECK(!construction_add_work(&constructions, &map, &items, index, 1.0e6f, 60.0, NULL));
    free_world(&map, &constructions, &items);
}

static void test_beds_wear_by_the_day(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 0); /* Nothing to wear. */
    map_set_structure(&map, 2, 2, STRUCTURE_GRASS_BED);
    map_set_structure(&map, 3, 3, STRUCTURE_WALL);
    /* 20 hit points, one a day: a refresh site once below 10. */
    for (int day = 1; day <= 10; day++) {
        CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 0);
        CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2), 20 - day);
        CHECK_EQ_INT(construction_at(&constructions, 2, 2) != CONSTRUCTION_NONE, day >= 11);
    }
    CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 0);
    int site = construction_at(&constructions, 2, 2);
    REQUIRE(site != CONSTRUCTION_NONE);
    CHECK(construction_get(&constructions, site)->refresh);
    CHECK_EQ_INT(construction_at(&constructions, 2, 3), site); /* On the foot too. */
    CHECK_EQ_INT(map_structure_hit_points(&map, 3, 3), 60); /* Walls do not wear. */
    /* Nobody refreshes it: it falls apart, the grass brought is dropped. */
    construction_deliver(&constructions, site, item_stack_pristine(ITEM_GRASS, 2));
    for (int day = 0; day < 8; day++) {
        CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 0);
    }
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2), 1);
    CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 1);
    CHECK_EQ_INT(map_structure(&map, 2, 2), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&map, 2, 3), STRUCTURE_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2), CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 3), CONSTRUCTION_NONE);
    CHECK_EQ_INT((int)items.totals[ITEM_GRASS], 2);
    free_world(&map, &constructions, &items);
}

static void test_long_structures(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16));
    CHECK_EQ_INT(structure_length(STRUCTURE_GRASS_BED), 2);
    CHECK_EQ_INT(structure_length(STRUCTURE_WALL), 1);
    CHECK_EQ_INT(structure_length(STRUCTURE_NONE), 0);
    CHECK_EQ_INT(direction_turn(DIRECTION_WEST), DIRECTION_NORTH);
    CHECK_EQ_INT(direction_dx(DIRECTION_EAST) + direction_dy(DIRECTION_NORTH), 0);

    /* A bed facing east: its foot east of its head. */
    REQUIRE(map_place_structure(&map, 5, 5, STRUCTURE_GRASS_BED, DIRECTION_EAST,
                                ITEM_QUALITY_FINE));
    CHECK_EQ_INT(map_structure(&map, 6, 5), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure(&map, 4, 5), STRUCTURE_NONE);
    int x = -1, y = -1;
    CHECK(map_structure_origin(&map, 6, 5, &x, &y) && x == 5 && y == 5);
    CHECK(map_structure_is_origin(&map, 5, 5));
    CHECK(!map_structure_is_origin(&map, 6, 5));
    CHECK_EQ_INT(map_structure_direction(&map, 6, 5), DIRECTION_EAST);
    /* Grade and hit points are the head's, read or written by any tile. */
    CHECK_EQ_INT(map_structure_quality(&map, 6, 5), ITEM_QUALITY_FINE);
    map_set_structure_hit_points(&map, 6, 5, 7);
    CHECK_EQ_INT(map_structure_hit_points(&map, 5, 5), 7);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 2);

    /* Removed whole by any of its tiles. */
    map_remove_structure(&map, 6, 5);
    CHECK_EQ_INT(map_structure(&map, 5, 5), STRUCTURE_NONE);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 0);
    CHECK(!map_structure_origin(&map, 5, 5, &x, &y));

    /* A wall built on the foot of a bed replaces the whole bed. */
    map_place_structure(&map, 5, 5, STRUCTURE_GRASS_BED, DIRECTION_WEST, ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(map_structure(&map, 4, 5), STRUCTURE_GRASS_BED);
    map_set_structure(&map, 4, 5, STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure(&map, 5, 5), STRUCTURE_NONE);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 0);

    /* It does not fit across the edge. */
    CHECK(!map_place_structure(&map, 0, 0, STRUCTURE_GRASS_BED, DIRECTION_NORTH,
                               ITEM_QUALITY_STANDARD));
    CHECK_EQ_INT(map_structure(&map, 0, 0), STRUCTURE_NONE);
    CHECK(map_place_structure(&map, 0, 0, STRUCTURE_GRASS_BED, DIRECTION_SOUTH,
                              ITEM_QUALITY_STANDARD));
    map_free(&map);
}

static void test_bed_sites(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    /* Both tiles must qualify: not with its foot on rock or across the edge. */
    CHECK_EQ_INT(constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 5, 6,
                                     DIRECTION_NORTH),
                 CONSTRUCTION_NONE);
    CHECK_EQ_INT(constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 15, 2,
                                     DIRECTION_EAST),
                 CONSTRUCTION_NONE);
    int index =
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 2, 3, DIRECTION_NORTH);
    REQUIRE(index != CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2), index);
    int x, y;
    construction_tile(construction_get(&constructions, index), 1, &x, &y);
    CHECK(x == 2 && y == 2);
    CHECK(!construction_can_place(&constructions, &map, 2, 2)); /* Its foot is taken. */
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 12));
    CHECK(construction_add_work(&constructions, &map, &items, index, 1.0e6f, 60.0, NULL));
    CHECK(map_structure_is_origin(&map, 2, 3));
    CHECK_EQ_INT(map_structure(&map, 2, 2), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure_direction(&map, 2, 2), DIRECTION_NORTH);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2), CONSTRUCTION_NONE);

    /* A refresh site placed by the foot sits on the head, over both tiles;
     * removing it frees both. */
    index = constructions_place_refresh(&constructions, &map, 2, 2);
    REQUIRE(index != CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&constructions, index);
    CHECK(site->x == 2 && site->y == 3 && site->direction == DIRECTION_NORTH);
    CHECK_EQ_INT(construction_at(&constructions, 2, 3), index);
    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
    constructions_remove(&constructions, index, returned);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2), CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 3), CONSTRUCTION_NONE);
    free_world(&map, &constructions, &items);
}

static void test_promises(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, DIRECTION_SOUTH);
    REQUIRE(index != CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&constructions, index);
    uint64_t hash = constructions_hash(&constructions, 0);
    CHECK_EQ_INT(construction_unpromised(site, 1), 24);
    /* At most what nobody promised yet. */
    CHECK_EQ_INT(construction_promise(&constructions, index, 1, 20), 20);
    CHECK_EQ_INT(construction_promise(&constructions, index, 1, 20), 4);
    CHECK_EQ_INT(construction_promise(&constructions, index, 1, 1), 0);
    CHECK_EQ_INT(construction_unpromised(site, 1), 0);
    CHECK_EQ_INT(construction_unpromised(site, 0), 5);
    CHECK_EQ_INT(construction_promise(&constructions, index, 5, 1), 0); /* No such slot. */
    CHECK(constructions_hash(&constructions, 0) != hash);
    /* Delivered items are no longer missing; a released promise frees the rest. */
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 20));
    construction_release_promise(&constructions, index, site->number, 1, 20);
    CHECK_EQ_INT(construction_unpromised(site, 1), 0); /* The other 4 are still coming. */
    construction_release_promise(&constructions, index, site->number + 1, 1, 4); /* Not it. */
    CHECK_EQ_INT(site->promised[1], 4);
    construction_release_promise(&constructions, index, site->number, 1, 10);
    CHECK_EQ_INT(site->promised[1], 0);
    CHECK_EQ_INT(construction_unpromised(site, 1), 4);
    free_world(&map, &constructions, &items);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_map_layer);
    RUN_TEST(test_placing_and_removing);
    RUN_TEST(test_delivering);
    RUN_TEST(test_building);
    RUN_TEST(test_hash);
    RUN_TEST(test_grades_and_hit_points);
    RUN_TEST(test_built_grade);
    RUN_TEST(test_refreshing_a_bed);
    RUN_TEST(test_beds_wear_by_the_day);
    RUN_TEST(test_long_structures);
    RUN_TEST(test_bed_sites);
    RUN_TEST(test_promises);
    return test_summary();
}
