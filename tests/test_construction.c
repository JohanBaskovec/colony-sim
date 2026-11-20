#include "core/construction.h"
#include "core/i18n.h"
#include "core/knowledge.h"
#include "core/structure.h"
#include "core/tree.h"

#include "test_framework.h"

/* 16 x 16 soil map with a granite wall at (5, 5), water at (6, 6), a tree at
 * (7, 7) and grass at (8, 8). */
/* 16 x 16: soil on level 0, open air over it (for roofs). */
static bool init_world(Map *map, Constructions *constructions, Items *items)
{
    if (!map_init(map, 16, 16, 2) || !constructions_init(constructions, 16, 16, 2) ||
        !items_init(items, 16, 16, 2)) {
        return false;
    }
    map->rock[map_index(map, 5, 5, 0)] = ROCK_GRANITE;
    map->terrain[map_index(map, 6, 6, 0)] = TERRAIN_DEEP_WATER;
    map->plants[map_index(map, 7, 7, 0)] = (Plant){.type = PLANT_TREE, .growth = 1.0f};
    map->plants[map_index(map, 8, 8, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
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
        /* Everything takes material but the ground bedding, which is only work. */
        CHECK((def->materials[0].type != ITEM_NONE && def->materials[0].count > 0) ||
              type == STRUCTURE_GROUND_BEDDING);
        /* ... and it is built at once: no work to do either. */
        CHECK(def->build_work > 0.0f || type == STRUCTURE_GROUND_BEDDING);
        CHECK_EQ_INT(structure_needs_construction((StructureType)type),
                     type != STRUCTURE_GROUND_BEDDING);
        CHECK(def->length >= 1 && def->length <= STRUCTURE_MAX_LENGTH);
        CHECK(def->knowledge == KNOWLEDGE_BASIC_SHELTERS);
        CHECK(structure_deconstruct_work((StructureType)type) <= def->build_work);
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
    REQUIRE(map_init(&map, 16, 16, 1));
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_NONE], 256);
    map.plants[map_index(&map, 2, 2, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    uint32_t revision = map.topology_revision;
    uint64_t hash = map_hash(&map);

    /* A bed (two tiles, facing south): no topology change, slower to cross,
     * the grass is gone. */
    map_set_structure(&map, 2, 2, 0, STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure(&map, 2, 2, 0), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure(&map, 2, 3, 0), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map.plants[map_index(&map, 2, 2, 0)].type, PLANT_NONE);
    CHECK_EQ_UINT(map.topology_revision, revision);
    CHECK(map_is_passable(&map, 2, 2, 0));
    CHECK_NEAR(map_move_cost(&map, 2, 2, 0), 1.2f, 0.001f);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 2);
    CHECK(map_hash(&map) != hash);

    /* A wall blocks: a topology change. */
    map_set_structure(&map, 3, 2, 0, STRUCTURE_WALL);
    CHECK(!map_is_passable(&map, 3, 2, 0));
    CHECK_EQ_UINT(map.topology_revision, revision + 1);
    map_set_structure(&map, 3, 2, 0, STRUCTURE_WALL); /* No change. */
    CHECK_EQ_UINT(map.topology_revision, revision + 1);
    map_set_structure(&map, 3, 2, 0, STRUCTURE_NONE);
    CHECK(map_is_passable(&map, 3, 2, 0));
    CHECK_EQ_UINT(map.topology_revision, revision + 2);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_WALL], 0);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_NONE], 254);

    /* Out of bounds and invalid types are ignored. */
    map_set_structure(&map, -1, 0, 0, STRUCTURE_WALL);
    map_set_structure(&map, 1, 1, 0, STRUCTURE_TYPE_COUNT);
    CHECK_EQ_INT(map_structure(&map, -1, 0, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&map, 1, 1, 0), STRUCTURE_NONE);
    map_free(&map);
}

static void test_placing_and_removing(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    CHECK(!construction_can_place(&constructions, &map, 5, 5, 0)); /* Rock. */
    CHECK(!construction_can_place(&constructions, &map, 6, 6, 0)); /* Water. */
    CHECK(!construction_can_place(&constructions, &map, 7, 7, 0)); /* Tree. */
    CHECK(!construction_can_place(&constructions, &map, 16, 0, 0));
    CHECK(construction_can_place(&constructions, &map, 8, 8, 0)); /* Grass is cleared. */
    map.plants[map_index(&map, 10, 10, 0)] = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    CHECK(construction_can_place(&constructions, &map, 10, 10, 0)); /* So is a shrub. */
    map_set_structure(&map, 9, 9, 0, STRUCTURE_GRASS_BED);
    CHECK(!construction_can_place(&constructions, &map, 9, 9, 0)); /* Already built. */
    CHECK_EQ_INT(
        constructions_place(&constructions, &map, STRUCTURE_NONE, 2, 2, 0, DIRECTION_SOUTH),
        CONSTRUCTION_NONE);

    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, 0, DIRECTION_SOUTH);
    REQUIRE(index != CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2, 0), index);
    CHECK_EQ_INT(constructions.used_count, 1);
    CHECK(!construction_can_place(&constructions, &map, 2, 2, 0));
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
    CHECK_EQ_INT(construction_at(&constructions, 2, 2, 0), CONSTRUCTION_NONE);
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
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, 0, DIRECTION_SOUTH);
    const ConstructionSite *site = construction_get(&constructions, index);
    uint32_t revision = constructions.revision;
    /* Only what is missing is taken; qualities mix, the lowest one stays. */
    ItemStack fine = item_stack_make(ITEM_BRANCHES, 3, TEST_QUALITY_FINE, 20);
    ItemStack crude = item_stack_make(ITEM_BRANCHES, 4, TEST_QUALITY_CRUDE, 20);
    CHECK_EQ_INT(construction_deliver(&constructions, index, fine), 3);
    CHECK_EQ_INT(construction_deliver(&constructions, index, crude), 2);
    CHECK_EQ_INT(site->delivered[0].count, 5);
    CHECK_EQ_INT(site->delivered[0].quality, TEST_QUALITY_CRUDE);
    CHECK_EQ_INT(construction_missing(site, 0), 0);
    CHECK(constructions.revision != revision);
    CHECK_EQ_INT(construction_deliver(&constructions, index, crude), 0); /* Full. */
    /* No work before every material is in. */
    CHECK(!construction_add_work(&constructions, &map, &items, NULL, index, 1.0e6f, 60.0, NULL));
    CHECK_NEAR(site->progress, 0.0f, 0.0001f);
    CHECK_EQ_INT(construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 30)),
                 24);
    CHECK(construction_materials_complete(site));
    free_world(&map, &constructions, &items);
}

/* A load of `count` items picked up from a heap added at (x, y). */
static ItemStack load_of(Items *items, int x, int y, ItemStack stack)
{
    ItemStack load = {.type = ITEM_NONE};
    items_add(items, x, y, 1, stack);
    items_pick_up(items, x, y, 1, stack.count, &load);
    return load;
}

static void test_delivering_loads(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, 0, DIRECTION_SOUTH);
    const ConstructionSite *site = construction_get(&constructions, index);
    /* The items of the loads move into the site, each as it was. */
    ItemStack fine =
        load_of(&items, 1, 1, item_stack_make(ITEM_BRANCHES, 3, TEST_QUALITY_FINE, 20));
    ItemStack crude =
        load_of(&items, 1, 2, item_stack_make(ITEM_BRANCHES, 4, TEST_QUALITY_CRUDE, 10));
    REQUIRE(fine.held != 0 && crude.held != 0);
    CHECK_EQ_INT(construction_deliver_load(&constructions, &items, index, &fine), 3);
    CHECK_EQ_INT(fine.count, 0);
    CHECK_EQ_INT(construction_deliver_load(&constructions, &items, index, &crude), 2);
    CHECK_EQ_INT(crude.count, 2); /* Only what is missing. */
    CHECK_EQ_INT(site->delivered[0].count, 5);
    CHECK(site->delivered[0].held != 0);
    CHECK(items_check_load(&items, site->delivered[0]));
    CHECK_EQ_INT(items_load_lowest_quality(&items, site->delivered[0]), TEST_QUALITY_CRUDE);
    CHECK_EQ_INT(construction_deliver_load(&constructions, &items, index, &crude), 0); /* Full. */
    CHECK_EQ_INT(construction_deliver(&constructions, index, item_stack_pristine(ITEM_BRANCHES, 1)),
                 0);
    CHECK_EQ_INT(items.held_count, 7);

    /* Removed: given back as they were (not all at the lowest quality). */
    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
    REQUIRE(constructions_remove(&constructions, index, returned) == 1);
    items_drop_load(&items, &map, 3, 3, 0, &returned[0]);
    ItemChoice fine_only = {TEST_QUALITY_FINE, TEST_QUALITY_FINE, false};
    ItemChoice crude_only = {TEST_QUALITY_CRUDE, TEST_QUALITY_CRUDE, false};
    CHECK_EQ_INT(items_count_chosen(&items, 3, 3, 0, ITEM_BRANCHES, fine_only), 3);
    CHECK_EQ_INT(items_count_chosen(&items, 3, 3, 0, ITEM_BRANCHES, crude_only), 2);
    CHECK_EQ_INT(items.held_count, 2);

    /* Built: the lowest quality caps the wall, the items are used up. */
    int wall = constructions_place(&constructions, &map, STRUCTURE_WALL, 6, 2, 0, DIRECTION_SOUTH);
    items_pick_up(&items, 3, 3, 0, 2, &crude); /* The 2 crude left in hand + 2 more. */
    ItemStack rest = {.type = ITEM_NONE};
    items_pick_up(&items, 3, 3, 0, 1, &rest);
    CHECK_EQ_INT(construction_deliver_load(&constructions, &items, wall, &crude), 4);
    CHECK_EQ_INT(construction_deliver_load(&constructions, &items, wall, &rest), 1);
    ItemStack grass = load_of(&items, 1, 3, item_stack_pristine(ITEM_GRASS, 24));
    CHECK_EQ_INT(construction_deliver_load(&constructions, &items, wall, &grass), 24);
    CHECK(construction_add_work(&constructions, &map, &items, NULL, wall, 1.0e6f,
                                KNOWLEDGE_LEVEL_MAX, NULL));
    CHECK_EQ_INT(map_structure_quality(&map, 6, 2, 0), TEST_QUALITY_ROUGH);
    CHECK_EQ_INT(items.held_count, 0);
    CHECK_EQ_INT(items.totals[ITEM_BRANCHES], 2); /* Left lying. */
    CHECK_EQ_INT(items_check(&items).problems, 0);

    /* Finished at once (debug): the missing materials are made, then used up. */
    int bed =
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 9, 9, 0, DIRECTION_SOUTH);
    ItemStack some = load_of(&items, 1, 4, item_stack_pristine(ITEM_GRASS, 5));
    CHECK_EQ_INT(construction_deliver_load(&constructions, &items, bed, &some), 5);
    CHECK(construction_finish_now(&constructions, &map, &items, NULL, bed, KNOWLEDGE_LEVEL_MAX,
                                  NULL));
    CHECK_EQ_INT(items.held_count, 0);
    CHECK_EQ_INT(items_check(&items).problems, 0);
    free_world(&map, &constructions, &items);
}

static void test_building(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 8, 8, 0, DIRECTION_SOUTH);
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_BRANCHES, 5));
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 24));
    items_add(&items, 8, 8, 0, item_stack_pristine(ITEM_COBBLE, 3)); /* In the way. */
    float work = structure_def(STRUCTURE_WALL)->build_work;
    CHECK(
        !construction_add_work(&constructions, &map, &items, NULL, index, work * 0.5f, 60.0, NULL));
    CHECK_NEAR(construction_work_fraction(construction_get(&constructions, index)), 0.5f, 0.001f);
    CHECK(map_is_passable(&map, 8, 8, 0)); /* Not built yet. */
    CHECK(
        construction_add_work(&constructions, &map, &items, NULL, index, work * 0.5f, 60.0, NULL));
    /* Built: the wall blocks, the grass is gone, the site is gone, the
     * stones were pushed next door, the materials are used up. */
    CHECK_EQ_INT(map_structure(&map, 8, 8, 0), STRUCTURE_WALL);
    CHECK(!map_is_passable(&map, 8, 8, 0));
    CHECK_EQ_INT(map.plants[map_index(&map, 8, 8, 0)].type, PLANT_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 8, 8, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(constructions.used_count, 0);
    CHECK_EQ_INT(items_get(&items, 8, 8, 0).count, 0);
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
    REQUIRE(constructions_init(&b, 16, 16, 1));
    CHECK(constructions_hash(&a, 1) == constructions_hash(&b, 1));
    int index = constructions_place(&a, &map, STRUCTURE_DOOR, 1, 1, 0, DIRECTION_SOUTH);
    CHECK(constructions_hash(&a, 1) != constructions_hash(&b, 1));
    constructions_place(&b, &map, STRUCTURE_DOOR, 1, 1, 0, DIRECTION_SOUTH);
    CHECK(constructions_hash(&a, 1) == constructions_hash(&b, 1));
    construction_deliver(&a, index, item_stack_pristine(ITEM_TWIGS, 2));
    CHECK(constructions_hash(&a, 1) != constructions_hash(&b, 1));
    constructions_free(&b);
    free_world(&map, &a, &items);
}

static void test_quality_and_hit_points(void)
{
    /* Hit points by quality: ×0.6 at 0, ×1 at 50, ×1.5 at 100, linear between. */
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_STANDARD), 60);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_WORST), 36);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_WALL, ITEM_QUALITY_BEST), 90);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_WALL, 25), 48);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_WALL, 75), 75);
    int last = 0;
    for (int quality = 0; quality <= 100; quality++) {
        int hit_points = structure_max_hit_points(STRUCTURE_WALL, quality);
        CHECK(hit_points >= last);
        last = hit_points;
    }
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_GRASS_BED, ITEM_QUALITY_STANDARD), 20);
    CHECK_EQ_INT(structure_max_hit_points(STRUCTURE_NONE, ITEM_QUALITY_STANDARD), 0);
    /* Materials cap the quality: their average + 20; grass and twigs do not. */
    ItemStack delivered[STRUCTURE_MATERIAL_SLOTS] = {
        item_stack_make(ITEM_BRANCHES, 5, TEST_QUALITY_CRUDE, 0),
        item_stack_pristine(ITEM_GRASS, 24)};
    CHECK_EQ_INT(structure_quality_cap(STRUCTURE_WALL, delivered), TEST_QUALITY_ROUGH);
    delivered[0].quality = TEST_QUALITY_FINE;
    CHECK_EQ_INT(structure_quality_cap(STRUCTURE_WALL, delivered), TEST_QUALITY_SUPERB);
    ItemStack grass[STRUCTURE_MATERIAL_SLOTS] = {item_stack_pristine(ITEM_GRASS, 12),
                                                 {.type = ITEM_NONE}};
    CHECK_EQ_INT(structure_quality_cap(STRUCTURE_GRASS_BED, grass), ITEM_QUALITY_BEST);

    /* The map layer: quality and hit points. */
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    map_set_structure(&map, 2, 2, 0, STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure_quality(&map, 2, 2, 0), ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0), 60);
    uint64_t hash = map_hash(&map);
    map_place_structure(&map, 2, 2, 0, STRUCTURE_WALL, DIRECTION_SOUTH, TEST_QUALITY_FINE);
    CHECK_EQ_INT(map_structure_quality(&map, 2, 2, 0), TEST_QUALITY_FINE);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0), 72);
    CHECK(map_hash(&map) != hash);
    map_set_structure_hit_points(&map, 2, 2, 0, 500);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0), 72);
    map_set_structure_hit_points(&map, 2, 2, 0, -4);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0), 1);
    map_set_structure_hit_points(&map, 3, 3, 0, 10); /* No structure: nothing. */
    CHECK_EQ_INT(map_structure_hit_points(&map, 3, 3, 0), 0);
    map_set_structure(&map, 2, 2, 0, STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0), 0);
    CHECK_EQ_INT(map_structure_hit_points(&map, -1, 2, 0), 0);
    map_free(&map);
}

static void test_built_quality(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    /* A master builder, crude branches: rough at best. */
    int wall = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, 0, DIRECTION_SOUTH);
    construction_deliver(&constructions, wall,
                         item_stack_make(ITEM_BRANCHES, 5, TEST_QUALITY_CRUDE, 0));
    construction_deliver(&constructions, wall, item_stack_pristine(ITEM_GRASS, 24));
    CHECK(construction_add_work(&constructions, &map, &items, NULL, wall, 1.0e6f,
                                KNOWLEDGE_LEVEL_MAX, NULL));
    CHECK_EQ_INT(map_structure_quality(&map, 2, 2, 0), TEST_QUALITY_ROUGH);
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0),
                 structure_max_hit_points(STRUCTURE_WALL, TEST_QUALITY_ROUGH));
    /* Grass does not cap: the builder's level does. */
    int bed =
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 3, 3, 0, DIRECTION_SOUTH);
    construction_deliver(&constructions, bed, item_stack_pristine(ITEM_GRASS, 12));
    CHECK(construction_add_work(&constructions, &map, &items, NULL, bed, 1.0e6f,
                                KNOWLEDGE_LEVEL_MAX, NULL));
    CHECK_EQ_INT(map_structure_quality(&map, 3, 3, 0),
                 knowledge_product_score(KNOWLEDGE_LEVEL_MAX, NULL));
    int poor =
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 4, 4, 0, DIRECTION_SOUTH);
    construction_deliver(&constructions, poor, item_stack_pristine(ITEM_GRASS, 12));
    CHECK(construction_add_work(&constructions, &map, &items, NULL, poor, 1.0e6f, 50.0, NULL));
    CHECK_EQ_INT(map_structure_quality(&map, 4, 4, 0), knowledge_product_score(50.0, NULL));
    free_world(&map, &constructions, &items);
}

static void test_refreshing_a_bed(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    map_place_structure(&map, 2, 2, 0, STRUCTURE_GRASS_BED, DIRECTION_SOUTH, TEST_QUALITY_FINE);
    map_set_structure(&map, 3, 3, 0, STRUCTURE_WALL);
    /* Only on a structure that can be refreshed, once. */
    CHECK_EQ_INT(constructions_place_refresh(&constructions, &map, 3, 3, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(constructions_place_refresh(&constructions, &map, 4, 4, 0), CONSTRUCTION_NONE);
    int index = constructions_place_refresh(&constructions, &map, 2, 2, 0);
    REQUIRE(index != CONSTRUCTION_NONE);
    CHECK_EQ_INT(constructions_place_refresh(&constructions, &map, 2, 2, 0), CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&constructions, index);
    CHECK(site->refresh);
    CHECK_EQ_INT(construction_material(site, 0), ITEM_GRASS);
    CHECK_EQ_INT(construction_material_count(site, 0), 4);
    CHECK_EQ_INT(construction_material(site, 1), ITEM_NONE);
    CHECK_NEAR(construction_work_needed(site), 125.0, 1e-4);
    map_set_structure_hit_points(&map, 2, 2, 0, 5);
    CHECK(!construction_add_work(&constructions, &map, &items, NULL, index, 1.0e6f, 60.0, NULL));
    CHECK_EQ_INT(construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 9)),
                 4);
    CHECK(!construction_add_work(&constructions, &map, &items, NULL, index, 100.0f, 60.0, NULL));
    CHECK(construction_add_work(&constructions, &map, &items, NULL, index, 25.0f, 60.0, NULL));
    /* Back to full hit points, the quality kept. */
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0), 24);
    CHECK_EQ_INT(map_structure_quality(&map, 2, 2, 0), TEST_QUALITY_FINE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2, 0), CONSTRUCTION_NONE);
    /* A refresh of a structure that is gone does nothing. */
    index = constructions_place_refresh(&constructions, &map, 2, 2, 0);
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 4));
    map_set_structure(&map, 2, 2, 0, STRUCTURE_NONE);
    CHECK(!construction_add_work(&constructions, &map, &items, NULL, index, 1.0e6f, 60.0, NULL));
    free_world(&map, &constructions, &items);
}

static void test_beds_wear_by_the_day(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 0); /* Nothing to wear. */
    map_set_structure(&map, 2, 2, 0, STRUCTURE_GRASS_BED);
    map_set_structure(&map, 3, 3, 0, STRUCTURE_WALL);
    /* 20 hit points, one a day: a refresh site once below 10. */
    for (int day = 1; day <= 10; day++) {
        CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 0);
        CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0), 20 - day);
        CHECK_EQ_INT(construction_at(&constructions, 2, 2, 0) != CONSTRUCTION_NONE, day >= 11);
    }
    CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 0);
    int site = construction_at(&constructions, 2, 2, 0);
    REQUIRE(site != CONSTRUCTION_NONE);
    CHECK(construction_get(&constructions, site)->refresh);
    CHECK_EQ_INT(construction_at(&constructions, 2, 3, 0), site); /* On the foot too. */
    CHECK_EQ_INT(map_structure_hit_points(&map, 3, 3, 0), 60);    /* Walls do not wear. */
    /* Nobody refreshes it: it falls apart, the grass brought is dropped. */
    construction_deliver(&constructions, site, item_stack_pristine(ITEM_GRASS, 2));
    for (int day = 0; day < 8; day++) {
        CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 0);
    }
    CHECK_EQ_INT(map_structure_hit_points(&map, 2, 2, 0), 1);
    CHECK_EQ_INT(constructions_wear_day(&constructions, &map, &items), 1);
    CHECK_EQ_INT(map_structure(&map, 2, 2, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&map, 2, 3, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 3, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT((int)items.totals[ITEM_GRASS], 2);
    free_world(&map, &constructions, &items);
}

static void test_long_structures(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    CHECK_EQ_INT(structure_length(STRUCTURE_GRASS_BED), 2);
    CHECK_EQ_INT(structure_length(STRUCTURE_WALL), 1);
    CHECK_EQ_INT(structure_length(STRUCTURE_NONE), 0);
    CHECK_EQ_INT(direction_turn(DIRECTION_WEST), DIRECTION_NORTH);
    CHECK_EQ_INT(direction_dx(DIRECTION_EAST) + direction_dy(DIRECTION_NORTH), 0);

    /* A bed facing east: its foot east of its head. */
    REQUIRE(
        map_place_structure(&map, 5, 5, 0, STRUCTURE_GRASS_BED, DIRECTION_EAST, TEST_QUALITY_FINE));
    CHECK_EQ_INT(map_structure(&map, 6, 5, 0), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure(&map, 4, 5, 0), STRUCTURE_NONE);
    int x = -1, y = -1;
    CHECK(map_structure_origin(&map, 6, 5, 0, &x, &y) && x == 5 && y == 5);
    CHECK(map_structure_is_origin(&map, 5, 5, 0));
    CHECK(!map_structure_is_origin(&map, 6, 5, 0));
    CHECK_EQ_INT(map_structure_direction(&map, 6, 5, 0), DIRECTION_EAST);
    /* Grade and hit points are the head's, read or written by any tile. */
    CHECK_EQ_INT(map_structure_quality(&map, 6, 5, 0), TEST_QUALITY_FINE);
    map_set_structure_hit_points(&map, 6, 5, 0, 7);
    CHECK_EQ_INT(map_structure_hit_points(&map, 5, 5, 0), 7);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 2);

    /* Removed whole by any of its tiles. */
    map_remove_structure(&map, 6, 5, 0);
    CHECK_EQ_INT(map_structure(&map, 5, 5, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 0);
    CHECK(!map_structure_origin(&map, 5, 5, 0, &x, &y));

    /* A wall built on the foot of a bed replaces the whole bed. */
    map_place_structure(&map, 5, 5, 0, STRUCTURE_GRASS_BED, DIRECTION_WEST, ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(map_structure(&map, 4, 5, 0), STRUCTURE_GRASS_BED);
    map_set_structure(&map, 4, 5, 0, STRUCTURE_WALL);
    CHECK_EQ_INT(map_structure(&map, 5, 5, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map.structure_counts[STRUCTURE_GRASS_BED], 0);

    /* It does not fit across the edge. */
    CHECK(!map_place_structure(&map, 0, 0, 0, STRUCTURE_GRASS_BED, DIRECTION_NORTH,
                               ITEM_QUALITY_STANDARD));
    CHECK_EQ_INT(map_structure(&map, 0, 0, 0), STRUCTURE_NONE);
    CHECK(map_place_structure(&map, 0, 0, 0, STRUCTURE_GRASS_BED, DIRECTION_SOUTH,
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
    CHECK_EQ_INT(
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 5, 6, 0, DIRECTION_NORTH),
        CONSTRUCTION_NONE);
    CHECK_EQ_INT(
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 15, 2, 0, DIRECTION_EAST),
        CONSTRUCTION_NONE);
    int index =
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 2, 3, 0, DIRECTION_NORTH);
    REQUIRE(index != CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2, 0), index);
    int x, y, z;
    construction_tile(construction_get(&constructions, index), 1, &x, &y, &z);
    CHECK(x == 2 && y == 2);
    CHECK(!construction_can_place(&constructions, &map, 2, 2, 0)); /* Its foot is taken. */
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 12));
    CHECK(construction_add_work(&constructions, &map, &items, NULL, index, 1.0e6f, 60.0, NULL));
    CHECK(map_structure_is_origin(&map, 2, 3, 0));
    CHECK_EQ_INT(map_structure(&map, 2, 2, 0), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map_structure_direction(&map, 2, 2, 0), DIRECTION_NORTH);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2, 0), CONSTRUCTION_NONE);

    /* A refresh site placed by the foot sits on the head, over both tiles;
     * removing it frees both. */
    index = constructions_place_refresh(&constructions, &map, 2, 2, 0);
    REQUIRE(index != CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&constructions, index);
    CHECK(site->x == 2 && site->y == 3 && site->direction == DIRECTION_NORTH);
    CHECK_EQ_INT(construction_at(&constructions, 2, 3, 0), index);
    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
    constructions_remove(&constructions, index, returned);
    CHECK_EQ_INT(construction_at(&constructions, 2, 2, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_at(&constructions, 2, 3, 0), CONSTRUCTION_NONE);
    free_world(&map, &constructions, &items);
}

static void test_promises(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    int index = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, 0, DIRECTION_SOUTH);
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

static void test_workstation_site(void)
{
    Map map;
    Constructions constructions;
    Items items;
    Workstations stations;
    REQUIRE(init_world(&map, &constructions, &items));
    REQUIRE(workstations_init(&stations, 16, 16, 1));
    CHECK_EQ_INT(
        constructions_place_workstation(&constructions, &map, WORKSTATION_TYPE_COUNT, 3, 4, 0),
        CONSTRUCTION_NONE);
    CHECK_EQ_INT(
        constructions_place_workstation(&constructions, &map, WORKSTATION_KNAPPING_SPOT, 5, 5, 0),
        CONSTRUCTION_NONE); /* Rock. */
    int index =
        constructions_place_workstation(&constructions, &map, WORKSTATION_KNAPPING_SPOT, 3, 4, 0);
    REQUIRE(index != CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&constructions, index);
    CHECK_EQ_INT(construction_workstation(site), WORKSTATION_KNAPPING_SPOT);
    CHECK_EQ_INT(site->type, STRUCTURE_NONE);
    CHECK_EQ_INT(construction_length(site), 1);
    CHECK_EQ_INT(construction_knowledge(site), KNOWLEDGE_BASIC_SHELTERS);
    CHECK_STR_EQ(construction_name_key(site), "workstation.knapping_spot");
    CHECK_EQ_INT(construction_material(site, 0), ITEM_COBBLE);
    CHECK_EQ_INT(construction_material_count(site, 0), 1);
    CHECK_EQ_INT(construction_material(site, 1), ITEM_NONE);
    CHECK_NEAR(construction_work_needed(site),
               workstation_def(WORKSTATION_KNAPPING_SPOT)->build_work, 1e-6);
    CHECK_EQ_INT(constructions.workstation_count, 1);
    CHECK_EQ_INT(constructions.workstation_tiles[map_index(&map, 3, 4, 0)], 1);
    CHECK(!construction_can_place(&constructions, &map, 3, 4, 0)); /* Taken. */

    /* A structure site is not a workstation's. */
    int wall =
        constructions_place(&constructions, &map, STRUCTURE_WALL, 10, 10, 0, DIRECTION_SOUTH);
    const ConstructionSite *wall_site = construction_get(&constructions, wall);
    CHECK_EQ_INT(construction_workstation(wall_site), WORKSTATION_TYPE_COUNT);
    CHECK_STR_EQ(construction_name_key(wall_site), structure_def(STRUCTURE_WALL)->name_key);
    CHECK_EQ_INT(constructions.workstation_count, 1);
    CHECK_EQ_INT(constructions.workstation_tiles[map_index(&map, 10, 10, 0)], 0);

    /* Built like a structure; once done, the station stands there. */
    CHECK(!construction_add_work(&constructions, &map, &items, &stations, index, 1.0e6f, 60.0,
                                 NULL)); /* Materials first. */
    CHECK_EQ_INT(construction_deliver(&constructions, index, item_stack_pristine(ITEM_COBBLE, 3)),
                 1);
    float work = construction_work_needed(site);
    CHECK(!construction_add_work(&constructions, &map, &items, &stations, index, work * 0.5f, 60.0,
                                 NULL));
    /* A tree grown on the tile meanwhile: the station waits, the site stays. */
    map.plants[map_index(&map, 3, 4, 0)] = (Plant){.type = PLANT_TREE, .growth = 1.0f};
    CHECK(!construction_add_work(&constructions, &map, &items, &stations, index, work, 60.0, NULL));
    CHECK(construction_get(&constructions, index) != NULL);
    CHECK_NEAR(construction_work_fraction(site), 1.0, 1e-6);
    map.plants[map_index(&map, 3, 4, 0)] = (Plant){0};
    CHECK(construction_add_work(&constructions, &map, &items, &stations, index, 1.0f, 60.0, NULL));
    CHECK_EQ_INT(construction_at(&constructions, 3, 4, 0), CONSTRUCTION_NONE);
    CHECK_EQ_INT(constructions.workstation_count, 0);
    CHECK_EQ_INT(constructions.workstation_tiles[map_index(&map, 3, 4, 0)], 0);
    const Workstation *station =
        workstation_get_const(&stations, workstation_at(&stations, 3, 4, 0));
    REQUIRE(station != NULL);
    CHECK_EQ_INT(station->type, WORKSTATION_KNAPPING_SPOT);
    CHECK_EQ_INT((int)items.totals[ITEM_COBBLE], 0); /* The anvil stone. */

    /* Removing a site gives its materials back and frees its tile. */
    index =
        constructions_place_workstation(&constructions, &map, WORKSTATION_KNAPPING_SPOT, 9, 4, 0);
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_COBBLE, 1));
    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
    CHECK_EQ_INT(constructions_remove(&constructions, index, returned), 1);
    CHECK_EQ_INT(returned[0].type, ITEM_COBBLE);
    CHECK_EQ_INT(constructions.workstation_count, 0);
    CHECK_EQ_INT(constructions.workstation_tiles[map_index(&map, 9, 4, 0)], 0);

    /* A crafting spot site (a bush to clear) needs no material nor work. */
    index =
        constructions_place_workstation(&constructions, &map, WORKSTATION_CRAFTING_SPOT, 9, 4, 0);
    CHECK(construction_materials_complete(construction_get(&constructions, index)));
    CHECK(construction_add_work(&constructions, &map, &items, &stations, index, 0.0f, 60.0, NULL));
    CHECK(workstation_at(&stations, 9, 4, 0) != WORKSTATION_NONE);
    workstations_free(&stations);
    free_world(&map, &constructions, &items);
}

/* Items lying where a workstation or a wall is to stand are pushed next door as the work
 * begins, not when it is done; a floor-like site leaves them alone. */
static void test_the_builder_clears_the_items(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    int station =
        constructions_place_workstation(&constructions, &map, WORKSTATION_KNAPPING_SPOT, 3, 4, 0);
    int wall =
        constructions_place(&constructions, &map, STRUCTURE_WALL, 10, 10, 0, DIRECTION_SOUTH);
    int bed =
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 12, 3, 0, DIRECTION_SOUTH);
    REQUIRE(station != CONSTRUCTION_NONE && wall != CONSTRUCTION_NONE && bed != CONSTRUCTION_NONE);
    CHECK(construction_takes_tiles(construction_get(&constructions, station)));
    CHECK(construction_takes_tiles(construction_get(&constructions, wall)));
    CHECK(!construction_takes_tiles(construction_get(&constructions, bed)));
    items_add(&items, 3, 4, 0, item_stack_pristine(ITEM_COBBLE, 3));
    items_add(&items, 10, 10, 0, item_stack_pristine(ITEM_BRANCHES, 2));
    items_add(&items, 12, 3, 0, item_stack_pristine(ITEM_GRASS, 4));

    construction_clear_items(construction_get(&constructions, station), &map, &items);
    construction_clear_items(construction_get(&constructions, wall), &map, &items);
    construction_clear_items(construction_get(&constructions, bed), &map, &items);
    CHECK_EQ_INT(items_get(&items, 3, 4, 0).count, 0);
    CHECK_EQ_INT(items_get(&items, 10, 10, 0).count, 0);
    CHECK_EQ_INT(items_get(&items, 12, 3, 0).count, 4); /* Nothing is built on it. */
    /* Nothing lost, and it lies next to the site. */
    CHECK_EQ_INT(items.totals[ITEM_COBBLE], 3);
    CHECK_EQ_INT(items.totals[ITEM_BRANCHES], 2);
    int cobbles = 0;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            ItemStack stack = items_get(&items, 3 + dx, 4 + dy, 0);
            cobbles += stack.type == ITEM_COBBLE ? stack.count : 0;
        }
    }
    CHECK_EQ_INT(cobbles, 3);
    CHECK_EQ_INT(items_check(&items).problems, 0);
    free_world(&map, &constructions, &items);
}

static void test_roof_layer(void)
{
    const StructureDef *def = structure_def(STRUCTURE_ROOF);
    CHECK(structure_is_roof(STRUCTURE_ROOF));
    CHECK(!structure_is_roof(STRUCTURE_WALL));
    CHECK(!structure_is_roof(STRUCTURE_TYPE_COUNT));
    CHECK(!structure_blocks(STRUCTURE_ROOF));
    CHECK_EQ_INT(def->materials[0].type, ITEM_BRANCHES);
    CHECK_EQ_INT(def->materials[1].type, ITEM_GRASS);

    Map map;
    REQUIRE(map_init(&map, 16, 16, 2));
    uint64_t bare = map_hash(&map);
    /* Not a structure of the structure layer. */
    CHECK(!map_place_structure(&map, 3, 3, 0, STRUCTURE_ROOF, DIRECTION_SOUTH,
                               ITEM_QUALITY_STANDARD));
    CHECK_EQ_INT(map_structure(&map, 3, 3, 0), STRUCTURE_NONE);
    /* The floor of the open tile over a bed, which stays. */
    map_set_structure(&map, 3, 3, 0, STRUCTURE_GRASS_BED);
    uint32_t revision = map.build_revision;
    uint32_t topology = map.topology_revision;
    map_set_roof(&map, 3, 3, 1, STRUCTURE_ROOF, TEST_QUALITY_FINE);
    CHECK(map.build_revision != revision);
    CHECK(map.topology_revision != topology);
    CHECK_EQ_INT(map_roof(&map, 3, 3, 1), STRUCTURE_ROOF);
    CHECK_EQ_INT(map.terrain[map_index(&map, 3, 3, 1)], TERRAIN_GRASS_ROOF);
    CHECK_EQ_INT(terrain_def(TERRAIN_GRASS_ROOF)->built, STRUCTURE_ROOF);
    CHECK_EQ_INT(map_roof_quality(&map, 3, 3, 1), TEST_QUALITY_FINE);
    CHECK_EQ_INT(map_roof(&map, 3, 3, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map_roof_over(&map, 3, 3, 0), STRUCTURE_ROOF);
    CHECK_EQ_INT(map_roof_over(&map, 3, 3, 1), STRUCTURE_NONE);
    CHECK_EQ_INT(map_structure(&map, 3, 3, 0), STRUCTURE_GRASS_BED);
    CHECK_EQ_INT(map.roof_count, 1);
    /* A floor nobody stands on, that covers what is under it. */
    CHECK(!map_is_open(&map, 3, 3, 1));
    CHECK(map_has_floor(&map, 3, 3, 1));
    CHECK(!map_is_passable(&map, 3, 3, 1));
    CHECK(!map_is_buildable(&map, 3, 3, 1));
    CHECK(map_is_roofed(&map, 3, 3, 0));
    CHECK(!map_natural_roof(&map, 3, 3, 0));
    CHECK_EQ_INT(map_ground_level(&map, 3, 3), 0);
    map_set_roof(&map, 3, 3, 1, STRUCTURE_ROOF, TEST_QUALITY_CRUDE); /* Rebuilt: no new tile. */
    CHECK_EQ_INT(map.roof_count, 1);
    map_set_roof(&map, 4, 3, 1, STRUCTURE_WALL, ITEM_QUALITY_STANDARD); /* Not a roof. */
    map_set_roof(&map, -1, 3, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    map_set_roof(&map, 5, 3, 0, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD); /* Not open. */
    map_set_roof(&map, 6, 3, 1, STRUCTURE_NONE, ITEM_QUALITY_STANDARD); /* No roof there. */
    CHECK_EQ_INT(map_roof(&map, 4, 3, 1), STRUCTURE_NONE);
    CHECK_EQ_INT(map_roof(&map, -1, 3, 1), STRUCTURE_NONE);
    CHECK_EQ_INT(map_roof(&map, 5, 3, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(map.terrain[map_index(&map, 5, 3, 0)], TERRAIN_SOIL);
    CHECK(map_is_open(&map, 6, 3, 1));
    CHECK_EQ_INT(map.roof_count, 1);
    uint64_t roofed = map_hash(&map);
    map_set_roof(&map, 3, 3, 1, STRUCTURE_NONE, ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(map.roof_count, 0);
    CHECK(map_is_open(&map, 3, 3, 1));
    map_set_structure(&map, 3, 3, 0, STRUCTURE_NONE);
    /* The hash sees roofs, and stays as it was without any. */
    CHECK(roofed != bare);
    CHECK_EQ_UINT(map_hash(&map), bare);

    /* Support: a wall or rock within ROOF_SUPPORT_RANGE tiles, on the level
     * below. */
    CHECK(!map_roof_supported(&map, 8, 8, 1));
    map_set_structure(&map, 8 + ROOF_SUPPORT_RANGE + 1, 8, 0, STRUCTURE_WALL);
    CHECK(!map_roof_supported(&map, 8, 8, 1));
    map_set_structure(&map, 8 + ROOF_SUPPORT_RANGE, 8 + ROOF_SUPPORT_RANGE, 0, STRUCTURE_WALL);
    CHECK(map_roof_supported(&map, 8, 8, 1));
    CHECK(!map_roof_supported(&map, 8, 8, 0)); /* Nothing under level 0. */
    map_set_structure(&map, 8 + ROOF_SUPPORT_RANGE, 8 + ROOF_SUPPORT_RANGE, 0, STRUCTURE_DOOR);
    CHECK(!map_roof_supported(&map, 8, 8, 1)); /* A door holds nothing up. */
    map_set_rock(&map, 8 - ROOF_SUPPORT_RANGE, 8, 0, ROCK_GRANITE);
    CHECK(map_roof_supported(&map, 8, 8, 1));
    map_free(&map);
}

static void test_roof_sites(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    /* Granite at (5, 5), water at (6, 6), a tree at (7, 7), grass at (8, 8),
     * all on level 0: roofs go on level 1. */
    CHECK(!construction_can_place_roof(&constructions, &map, 4, 4, 0)); /* Not open. */
    CHECK(!construction_can_place_roof(&constructions, &map, 5, 5, 1)); /* Over rock. */
    CHECK(construction_can_place_roof(&constructions, &map, 6, 6, 1));  /* Over water. */
    /* Over a tree: its trunk (core/tree.h) is in the way, once it has grown
     * into the map; over a sapling, a roof goes (the sapling then grows no
     * higher). */
    tree_build(&map, 7, 7, 0);
    CHECK(!construction_can_place_roof(&constructions, &map, 7, 7, 1));
    CHECK(!construction_can_place_roof(&constructions, &map, 8, 7, 1)); /* Its foliage. */
    tree_clear(&map, NULL, 7, 7, 0);
    map.plants[map_index(&map, 7, 7, 0)].growth = 0.2f;
    CHECK(construction_can_place_roof(&constructions, &map, 7, 7, 1));
    CHECK(construction_can_place_roof(&constructions, &map, 4, 4, 1));  /* Next to the rock. */
    CHECK(!construction_can_place_roof(&constructions, &map, 8, 8, 1)); /* 3 tiles from the rock. */
    map_set_structure(&map, 9, 10, 0, STRUCTURE_WALL);
    CHECK(construction_can_place_roof(&constructions, &map, 8, 8, 1));    /* Over grass. */
    CHECK(!construction_can_place_roof(&constructions, &map, 12, 13, 1)); /* Nothing to rest on. */
    CHECK(!construction_can_place_roof(&constructions, &map, -1, 0, 1));
    map_set_structure(&map, 12, 10, 0, STRUCTURE_WALL);
    CHECK(construction_can_place_roof(&constructions, &map, 12, 12, 1));
    CHECK(construction_can_place_roof(&constructions, &map, 12, 10, 1)); /* Over the wall. */
    CHECK(construction_can_place_structure(&constructions, &map, STRUCTURE_ROOF, 12, 12, 1,
                                           DIRECTION_SOUTH));
    /* Not over the way up of a slope. */
    map_set_slope(&map, 12, 11, 0, true);
    CHECK(!construction_can_place_roof(&constructions, &map, 12, 11, 1));
    map_set_slope(&map, 12, 11, 0, false);

    /* Over a bed: the roof is built on the level above, the bed stays. */
    map_set_structure(&map, 11, 11, 0, STRUCTURE_GRASS_BED);
    int index =
        constructions_place(&constructions, &map, STRUCTURE_ROOF, 11, 11, 1, DIRECTION_SOUTH);
    REQUIRE(index != CONSTRUCTION_NONE);
    CHECK(!construction_can_place_roof(&constructions, &map, 11, 11, 1)); /* A site there. */
    CHECK_EQ_INT(construction_at(&constructions, 11, 11, 1), index);
    CHECK_EQ_INT(construction_at(&constructions, 11, 11, 0), CONSTRUCTION_NONE);
    /* Worked on from beside the tile under it. */
    CHECK_EQ_INT(construction_worked_from(&constructions, 11, 11, 0, true), index);
    CHECK_EQ_INT(construction_worked_from(&constructions, 11, 11, 0, false), CONSTRUCTION_NONE);
    CHECK_EQ_INT(construction_worked_from(&constructions, 11, 11, 1, false), CONSTRUCTION_NONE);
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_BRANCHES, 3));
    construction_deliver(&constructions, index, item_stack_pristine(ITEM_GRASS, 20));
    CHECK(construction_add_work(&constructions, &map, &items, NULL, index, 1.0e6f, 60.0, NULL));
    CHECK_EQ_INT(map_roof(&map, 11, 11, 1), STRUCTURE_ROOF);
    /* The quality of the builder's level, as for any structure. */
    CHECK_EQ_INT(map_roof_quality(&map, 11, 11, 1), knowledge_product_score(60.0, NULL));
    CHECK_EQ_INT(map_structure(&map, 11, 11, 0), STRUCTURE_GRASS_BED);
    CHECK(!construction_can_place_roof(&constructions, &map, 11, 11, 1)); /* Roofed already. */
    CHECK(!construction_can_place_roof(&constructions, &map, 11, 11, 2)); /* Out of the map. */
    /* Planned next to a planned wall: built once the wall stands. */
    CHECK(!construction_can_place_roof(&constructions, &map, 3, 12, 1));
    int wall = constructions_place(&constructions, &map, STRUCTURE_WALL, 3, 14, 0, DIRECTION_SOUTH);
    REQUIRE(wall != CONSTRUCTION_NONE);
    CHECK(construction_can_place_roof(&constructions, &map, 3, 12, 1));
    int roof = constructions_place(&constructions, &map, STRUCTURE_ROOF, 3, 12, 1, DIRECTION_SOUTH);
    REQUIRE(roof != CONSTRUCTION_NONE);
    construction_deliver(&constructions, roof, item_stack_pristine(ITEM_BRANCHES, 3));
    construction_deliver(&constructions, roof, item_stack_pristine(ITEM_GRASS, 20));
    const ConstructionSite *roof_site = construction_get(&constructions, roof);
    CHECK(construction_materials_complete(roof_site));
    CHECK(!construction_can_work(roof_site, &map));
    CHECK(!construction_add_work(&constructions, &map, &items, NULL, roof, 1.0e6f, 60.0, NULL));
    CHECK_NEAR(roof_site->progress, 0.0, 1e-6);
    ItemStack unused[STRUCTURE_MATERIAL_SLOTS];
    constructions_remove(&constructions, wall, unused);
    map_set_structure(&map, 3, 14, 0, STRUCTURE_WALL); /* Built. */
    CHECK(construction_can_work(roof_site, &map));
    CHECK(construction_add_work(&constructions, &map, &items, NULL, roof, 1.0e6f, 60.0, NULL));
    CHECK_EQ_INT(map_roof(&map, 3, 12, 1), STRUCTURE_ROOF);
    /* A bed can still be planned under a roof; nothing on it. */
    CHECK(construction_can_place_structure(&constructions, &map, STRUCTURE_GRASS_BED, 13, 11, 0,
                                           DIRECTION_SOUTH));
    CHECK(!construction_can_place(&constructions, &map, 3, 12, 1));
    free_world(&map, &constructions, &items);
}

/* A ladder is only planned where it leads up: open space over it, the
 * ground of a neighbor on the level above. */
static void test_ladders_need_a_cliff(void)
{
    Map map;
    Constructions constructions;
    REQUIRE(map_init(&map, 16, 16, 2));
    REQUIRE(constructions_init(&constructions, 16, 16, 2));
    for (int y = 0; y < 16; y++) { /* A hill over x >= 8. */
        for (int x = 8; x < 16; x++) {
            map_set_rock(&map, x, y, 0, ROCK_GRANITE);
            map_set_terrain(&map, x, y, 1, TERRAIN_SOIL);
        }
    }
    CHECK(construction_can_place_structure(&constructions, &map, STRUCTURE_LADDER, 7, 4, 0,
                                           DIRECTION_SOUTH));
    CHECK(!construction_can_place_structure(&constructions, &map, STRUCTURE_LADDER, 3, 4, 0,
                                            DIRECTION_SOUTH));
    CHECK(construction_can_place_structure(&constructions, &map, STRUCTURE_PROP, 3, 4, 0,
                                           DIRECTION_SOUTH));
    constructions_free(&constructions);
    map_free(&map);
}

/* Dry grass can be replaced by the other fibres cut from plants: bracken, heather,
 * reeds, and moss in a bed; once a fibre is in, the slot takes only that one. */
static void test_fibres_stand_in_for_grass(void)
{
    Map map;
    Constructions constructions;
    Items items;
    REQUIRE(init_world(&map, &constructions, &items));
    int wall = constructions_place(&constructions, &map, STRUCTURE_WALL, 2, 2, 0, DIRECTION_SOUTH);
    int bed =
        constructions_place(&constructions, &map, STRUCTURE_GRASS_BED, 4, 4, 0, DIRECTION_SOUTH);
    REQUIRE(wall != CONSTRUCTION_NONE && bed != CONSTRUCTION_NONE);
    const ConstructionSite *wall_site = construction_get(&constructions, wall);
    const ConstructionSite *bed_site = construction_get(&constructions, bed);
    ItemTypeMask wall_grass = construction_material_types(wall_site, 1);
    ItemTypeMask bed_grass = construction_material_types(bed_site, 0);
    CHECK(wall_grass & ITEM_TYPE_BIT(ITEM_GRASS));
    CHECK(wall_grass & ITEM_TYPE_BIT(ITEM_REEDS));
    CHECK(wall_grass & ITEM_TYPE_BIT(ITEM_HEATHER));
    CHECK(wall_grass & ITEM_TYPE_BIT(ITEM_BRACKEN));
    CHECK(!(wall_grass & ITEM_TYPE_BIT(ITEM_MOSS))); /* No wall of moss. */
    CHECK(bed_grass & ITEM_TYPE_BIT(ITEM_MOSS));
    CHECK(!(wall_grass & ITEM_TYPE_BIT(ITEM_WOOD)));
    CHECK_EQ_UINT(construction_material_types(wall_site, 0), ITEM_TYPE_BIT(ITEM_BRANCHES));

    CHECK_EQ_INT(construction_deliver(&constructions, wall, item_stack_pristine(ITEM_REEDS, 10)),
                 10);
    CHECK_EQ_UINT(construction_material_types(wall_site, 1), ITEM_TYPE_BIT(ITEM_REEDS));
    CHECK_EQ_INT(construction_deliver(&constructions, wall, item_stack_pristine(ITEM_GRASS, 4)),
                 0); /* Not mixed with the reeds in. */
    CHECK_EQ_INT(construction_deliver(&constructions, wall, item_stack_pristine(ITEM_REEDS, 20)),
                 14);
    CHECK_EQ_INT(construction_missing(wall_site, 1), 0);
    CHECK_EQ_INT(construction_deliver(&constructions, bed, item_stack_pristine(ITEM_MOSS, 12)), 12);
    CHECK(construction_materials_complete(bed_site));
    /* Taken apart or cancelled, the reeds come back as reeds. */
    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
    CHECK_EQ_INT(constructions_remove(&constructions, wall, returned), 1);
    CHECK_EQ_INT(returned[0].type, ITEM_REEDS);
    CHECK_EQ_INT(returned[0].count, 24);
    free_world(&map, &constructions, &items);
}

int main(void)
{
    RUN_TEST(test_definitions);
    RUN_TEST(test_fibres_stand_in_for_grass);
    RUN_TEST(test_map_layer);
    RUN_TEST(test_placing_and_removing);
    RUN_TEST(test_delivering);
    RUN_TEST(test_delivering_loads);
    RUN_TEST(test_building);
    RUN_TEST(test_hash);
    RUN_TEST(test_quality_and_hit_points);
    RUN_TEST(test_built_quality);
    RUN_TEST(test_refreshing_a_bed);
    RUN_TEST(test_beds_wear_by_the_day);
    RUN_TEST(test_long_structures);
    RUN_TEST(test_bed_sites);
    RUN_TEST(test_promises);
    RUN_TEST(test_workstation_site);
    RUN_TEST(test_the_builder_clears_the_items);
    RUN_TEST(test_fibres_stand_in_for_grass);
    RUN_TEST(test_roof_layer);
    RUN_TEST(test_roof_sites);
    RUN_TEST(test_ladders_need_a_cliff);
    return test_summary();
}
