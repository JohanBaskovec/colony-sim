/* Gathering zones: tiles, orders, what they want, the jobs tiles offer and
 * what they give. */
#include "core/gather_zone.h"
#include "core/i18n.h"
#include "core/util.h"

#include "test_framework.h"

#include <stdlib.h>

#define ALL_JOBS ((GatherJobMask)((1u << GATHER_JOB_COUNT) - 1u))

/* 16 x 16 soil map with a granite wall at (5, 5), sand at (10, 8), water at
 * (11, 8) and gravel at (9, 8). */
static bool init_world(Map *map, GatherZones *zones, Items *items)
{
    if (!map_init(map, 16, 16) || !gather_zones_init(zones, 16, 16) ||
        !items_init(items, 16, 16)) {
        return false;
    }
    map->rock[map_index(map, 5, 5)] = ROCK_GRANITE;
    map->terrain[map_index(map, 9, 8)] = TERRAIN_GRAVEL;
    map->terrain[map_index(map, 10, 8)] = TERRAIN_SAND;
    map->terrain[map_index(map, 11, 8)] = TERRAIN_SHALLOW_WATER;
    return true;
}

static void free_world(Map *map, GatherZones *zones, Items *items)
{
    items_free(items);
    gather_zones_free(zones);
    map_free(map);
}

static Plant *put_plant(Map *map, int x, int y, PlantType type, float growth)
{
    Plant *plant = &map->plants[map_index(map, x, y)];
    plant->type = (uint8_t)type;
    plant->growth = growth;
    plant_fill_stocks(plant);
    return plant;
}

/* Enables an order in `mode` and updates what the zones want. */
static void order(GatherZones *zones, const Items *items, int index, GatherResource resource,
                  GatherMode mode)
{
    gather_zone_set_enabled(zones, index, resource, true);
    gather_zone_set_mode(zones, index, resource, mode);
    gather_zones_update(zones, items->totals);
}

static void test_init_rejects_invalid_sizes(void)
{
    GatherZones zones;
    CHECK(!gather_zones_init(&zones, 0, 10));
    CHECK(zones.zone == NULL && zones.width == 0);
}

static void test_definitions(void)
{
    GatherResourceMask seen = 0;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        const GatherResourceDef *def = gather_resource_def((GatherResource)r);
        REQUIRE(def != NULL);
        CHECK(def->id != NULL);
        CHECK(i18n_is_valid_key(def->name_key));
        CHECK(def->counted & ITEM_TYPE_BIT(def->item));
        /* Each resource comes from the job that says it gives it. */
        CHECK(gather_job_def(def->job)->resources & GATHER_RESOURCE_BIT(r));
        seen |= GATHER_RESOURCE_BIT(r);
    }
    for (int j = 0; j < GATHER_JOB_COUNT; j++) {
        const GatherJobDef *def = gather_job_def((GatherJob)j);
        REQUIRE(def != NULL);
        CHECK(i18n_is_valid_key(def->activity_key));
        CHECK(work_type_def(def->work_type) != NULL);
        CHECK(def->resources != 0 && (def->resources & ~seen) == 0);
    }
    CHECK(gather_resource_def(GATHER_RESOURCE_COUNT) == NULL);
    CHECK(gather_job_def(GATHER_JOB_NONE) == NULL);
    /* Only wood and berries may wait for full growth. */
    CHECK(gather_resource_def(GATHER_WOOD)->ripe_option);
    CHECK(gather_resource_def(GATHER_BERRIES)->ripe_option);
    CHECK(!gather_resource_def(GATHER_TWIGS)->ripe_option);
    /* Gathering from 50 on; nobody can fell trees yet. */
    CHECK(gather_job_doable_at(GATHER_JOB_HARVEST, KNOWLEDGE_MASTERY_LEVEL));
    CHECK(!gather_job_doable_at(GATHER_JOB_STONES, KNOWLEDGE_MASTERY_LEVEL - 0.1));
    CHECK(!gather_job_doable_at(GATHER_JOB_CHOP, KNOWLEDGE_LEVEL_MAX));
    CHECK(gather_job_def(GATHER_JOB_CHOP)->work_type == WORK_WOODCUTTING);
}

static void test_where_zones_and_stones_lie(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    /* Zones cover open ground, sand included, but not water nor rock. */
    CHECK(gather_zone_can_cover(&map, 8, 8));
    CHECK(gather_zone_can_cover(&map, 10, 8));
    CHECK(!gather_zone_can_cover(&map, 11, 8));
    CHECK(!gather_zone_can_cover(&map, 5, 5));
    CHECK(!gather_zone_can_cover(&map, -1, 0));
    /* Stones lie on soil, gravel and rough stone, not on sand. */
    CHECK(gather_stone_ground(&map, 8, 8));
    CHECK(gather_stone_ground(&map, 9, 8));
    CHECK(!gather_stone_ground(&map, 10, 8));
    CHECK(!gather_stone_ground(&map, 5, 5));
    int index = gather_zone_create(&zones, &map, 4, 8, 11, 8);
    REQUIRE(index != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_get(&zones, index)->cell_count, 7);
    CHECK_EQ_INT(gather_zone_at(&zones, 11, 8), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(&zones, 10, 8), index);
    CHECK_EQ_INT(gather_zone_create(&zones, &map, 11, 8, 11, 8), GATHER_ZONE_NONE);
    free_world(&map, &zones, &items);
}

static void test_zone_tiles(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int a = gather_zone_create(&zones, &map, 0, 0, 2, 2);
    REQUIRE(a != GATHER_ZONE_NONE);
    const GatherZone *zone = gather_zone_get(&zones, a);
    CHECK_EQ_INT(zone->number, 1);
    /* Every order is off at first. */
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        CHECK(!zone->orders[r].enabled);
        CHECK_EQ_INT(zone->orders[r].mode, GATHER_UNTIL);
        CHECK_EQ_INT(zone->orders[r].target, GATHER_TARGET_DEFAULT);
        CHECK(zone->orders[r].ripe_only == gather_resource_def((GatherResource)r)->ripe_option);
    }
    gather_zones_update(&zones, items.totals);
    CHECK_EQ_INT(zone->wanted, 0);
    CHECK_EQ_INT(zones.wanting_cells, 0);
    /* Tiles of another zone are not taken. */
    int b = gather_zone_create(&zones, &map, 2, 2, 3, 3);
    REQUIRE(b != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_get(&zones, b)->cell_count, 3);
    CHECK_EQ_INT(gather_zone_get(&zones, b)->number, 2);
    CHECK_EQ_INT(gather_zone_extend(&zones, &map, a, 0, 3, 1, 3), 2);
    CHECK_EQ_INT(zone->cell_count, 11);
    CHECK_EQ_INT(zones.used_count, 2);
    /* Removing tiles; a zone left empty is deleted. */
    CHECK_EQ_INT(gather_zones_remove_rect(&zones, 2, 2, 3, 3), 4);
    CHECK(gather_zone_get(&zones, b) == NULL);
    CHECK_EQ_INT(zone->cell_count, 10);
    CHECK_EQ_INT(zones.used_count, 1);
    gather_zone_delete(&zones, a);
    CHECK(gather_zone_get(&zones, a) == NULL);
    CHECK_EQ_INT(gather_zone_at(&zones, 0, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(zones.used_count, 0);
    /* Numbers are never reused. */
    int c = gather_zone_create(&zones, &map, 0, 0, 0, 0);
    CHECK_EQ_INT(gather_zone_get(&zones, c)->number, 3);
    free_world(&map, &zones, &items);
}

static void test_order_settings(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int index = gather_zone_create(&zones, &map, 0, 0, 2, 2);
    const GatherZone *zone = gather_zone_get(&zones, index);
    const GatherOrder *twigs = &zone->orders[GATHER_TWIGS];
    uint32_t revision = zones.revision;
    gather_zone_set_enabled(&zones, index, GATHER_TWIGS, true);
    CHECK(twigs->enabled);
    CHECK(zones.revision != revision);
    gather_zone_set_target(&zones, index, GATHER_TWIGS, 0);
    CHECK_EQ_INT(twigs->target, GATHER_TARGET_MIN);
    gather_zone_set_target(&zones, index, GATHER_TWIGS, 100000);
    CHECK_EQ_INT(twigs->target, GATHER_TARGET_MAX);
    gather_zone_set_mode(&zones, index, GATHER_TWIGS, GATHER_MODE_COUNT);
    CHECK_EQ_INT(twigs->mode, GATHER_UNTIL);
    /* Twigs never wait for full growth. */
    gather_zone_set_ripe_only(&zones, index, GATHER_TWIGS, true);
    CHECK(!twigs->ripe_only);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    CHECK(!zone->orders[GATHER_WOOD].ripe_only);
    /* Invalid zones and resources are ignored. */
    gather_zone_set_enabled(&zones, 7, GATHER_TWIGS, true);
    gather_zone_set_enabled(&zones, index, GATHER_RESOURCE_COUNT, true);
    gather_zone_set_target(&zones, -1, GATHER_TWIGS, 5);
    free_world(&map, &zones, &items);
}

static void test_wanted_resources(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int index = gather_zone_create(&zones, &map, 0, 0, 2, 2);
    const GatherZone *zone = gather_zone_get(&zones, index);
    /* Until the colony has 10 armfuls of grass: fresh and dry both count. */
    order(&zones, &items, index, GATHER_GRASS, GATHER_UNTIL);
    gather_zone_set_target(&zones, index, GATHER_GRASS, 10);
    items_add(&items, 12, 12, item_stack_pristine(ITEM_FRESH_GRASS, 6));
    gather_zones_update(&zones, items.totals);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_GRASS));
    CHECK_EQ_INT(zones.wanting_cells, 9);
    CHECK_EQ_INT((int)gather_resource_count(GATHER_GRASS, items.totals), 6);
    items_add(&items, 13, 12, item_stack_pristine(ITEM_GRASS, 4));
    gather_zones_update(&zones, items.totals);
    CHECK_EQ_INT(zone->wanted, 0);
    CHECK_EQ_INT(zones.wanting_cells, 0);
    /* Forever: whatever lies in the colony. */
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    items_add(&items, 14, 12, item_stack_pristine(ITEM_COBBLE, 50));
    gather_zones_update(&zones, items.totals);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_COBBLES));
    /* A fixed count: what this order gathered, not what lies about. */
    order(&zones, &items, index, GATHER_FLINT, GATHER_DO_COUNT);
    items_add(&items, 15, 12, item_stack_pristine(ITEM_FLINT, 50));
    gather_zones_update(&zones, items.totals);
    CHECK(zone->wanted & GATHER_RESOURCE_BIT(GATHER_FLINT));
    /* Turned off: never wanted. */
    gather_zone_set_enabled(&zones, index, GATHER_COBBLES, false);
    gather_zones_update(&zones, items.totals);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_FLINT));
    free_world(&map, &zones, &items);
}

static void test_tile_jobs_in_order(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 2, 2, PLANT_TREE, 1.0f);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, ALL_JOBS), GATHER_JOB_NONE);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_TWIGS, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_BRANCHES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    /* The tree's stocks before felling it, and stones last. */
    GatherJobMask no_branches = ALL_JOBS & ~GATHER_JOB_BIT(GATHER_JOB_BRANCHES);
    GatherJobMask chop_or_stones = GATHER_JOB_BIT(GATHER_JOB_CHOP) |
                                   GATHER_JOB_BIT(GATHER_JOB_STONES);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, ALL_JOBS), GATHER_JOB_BRANCHES);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, no_branches), GATHER_JOB_TWIGS);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, chop_or_stones), GATHER_JOB_CHOP);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, GATHER_JOB_BIT(GATHER_JOB_STONES)),
                 GATHER_JOB_STONES);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 1, ALL_JOBS), GATHER_JOB_STONES);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 1, GATHER_JOB_BIT(GATHER_JOB_CHOP)),
                 GATHER_JOB_NONE);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 8, 8, ALL_JOBS), GATHER_JOB_NONE);
    CHECK(gather_job_available(&zones, &map, 2, 2, GATHER_JOB_CHOP));
    CHECK(!gather_job_available(&zones, &map, 2, 2, GATHER_JOB_HARVEST));
    CHECK(!gather_job_available(&zones, &map, 2, 2, GATHER_JOB_NONE));
    free_world(&map, &zones, &items);
}

static void test_fully_grown_plants_only(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 1, 1, PLANT_TREE, 0.4f);
    Plant *bush = put_plant(&map, 2, 1, PLANT_BUSH, 0.8f);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_BERRIES, GATHER_FOREVER);
    /* By default, young trees and bushes not fully grown are left alone. */
    CHECK(!gather_job_available(&zones, &map, 1, 1, GATHER_JOB_CHOP));
    CHECK(!gather_job_available(&zones, &map, 2, 1, GATHER_JOB_HARVEST));
    bush->growth = 1.0f;
    CHECK(gather_job_available(&zones, &map, 2, 1, GATHER_JOB_HARVEST));
    /* Without the option, any tree, and any bush with ripe fruit. */
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    gather_zone_set_ripe_only(&zones, index, GATHER_BERRIES, false);
    CHECK(gather_job_available(&zones, &map, 1, 1, GATHER_JOB_CHOP));
    bush->growth = plant_def(PLANT_BUSH)->min_edible_growth;
    CHECK(gather_job_available(&zones, &map, 2, 1, GATHER_JOB_HARVEST));
    bush->growth = plant_def(PLANT_BUSH)->min_edible_growth - 0.01f;
    CHECK(!gather_job_available(&zones, &map, 2, 1, GATHER_JOB_HARVEST));
    free_world(&map, &zones, &items);
}

static void test_felling_a_tree(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 3, 3, PLANT_TREE, 1.0f);
    put_plant(&map, 8, 8, PLANT_TREE, 0.5f);
    int index = gather_zone_create(&zones, &map, 0, 0, 9, 9);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    /* Bigger trees take longer. */
    float full = gather_job_work_needed(&map, 3, 3, GATHER_JOB_CHOP);
    CHECK(full > gather_job_work_needed(&map, 8, 8, GATHER_JOB_CHOP));
    CHECK(!gather_zone_add_work(&zones, &map, &items, 3, 3, GATHER_JOB_CHOP, full * 0.5f, 60.0,
                                NULL));
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, 3, 3), 0.5, 1e-4);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 3, GATHER_JOB_CHOP, full * 0.5f, 60.0,
                               NULL));
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3)].type, PLANT_NONE);
    ItemStack wood = items_get(&items, 3, 3);
    CHECK_EQ_INT(wood.type, ITEM_WOOD);
    /* A full tile, the rest spread around. */
    CHECK_EQ_INT(wood.count, item_stack_limit(ITEM_WOOD));
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], plant_def(PLANT_TREE)->cut_yield);
    CHECK(!gather_job_available(&zones, &map, 3, 3, GATHER_JOB_CHOP));
    /* A half-grown tree gives half the wood. */
    CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, GATHER_JOB_CHOP, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD],
                 plant_def(PLANT_TREE)->cut_yield + (plant_def(PLANT_TREE)->cut_yield + 1) / 2);
    CHECK_EQ_INT(gather_zone_get(&zones, index)->orders[GATHER_WOOD].done,
                 (int)items.totals[ITEM_WOOD]);
    free_world(&map, &zones, &items);
}

static void test_products_depend_on_level(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 3, 3, PLANT_TREE, 1.0f);
    put_plant(&map, 12, 12, PLANT_TREE, 1.0f);
    int index = gather_zone_create(&zones, &map, 0, 0, 15, 15);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    int wood_hp = item_def(ITEM_WOOD)->max_hit_points;
    const double beginner_level = KNOWLEDGE_MASTERY_LEVEL;
    /* A beginner brings back rough, damaged logs... */
    gather_zone_add_work(&zones, &map, &items, 3, 3, GATHER_JOB_CHOP, 1e6f, beginner_level, NULL);
    ItemStack beginner = items_get(&items, 3, 3);
    CHECK_EQ_INT(beginner.type, ITEM_WOOD);
    CHECK_EQ_INT(beginner.quality,
                 knowledge_quality_of_score(knowledge_product_score(beginner_level, NULL)));
    CHECK(beginner.hit_points < wood_hp);
    /* ...a master superb, intact ones. */
    gather_zone_add_work(&zones, &map, &items, 12, 12, GATHER_JOB_CHOP, 1e6f, KNOWLEDGE_LEVEL_MAX,
                         NULL);
    ItemStack master = items_get(&items, 12, 12);
    CHECK_EQ_INT(master.quality, ITEM_QUALITY_SUPERB);
    CHECK_EQ_INT(master.hit_points, wood_hp);
    free_world(&map, &zones, &items);
}

static void test_branches_twigs_grass_and_berries(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    Plant *tree = put_plant(&map, 1, 1, PLANT_TREE, 1.0f);
    Plant *bush = put_plant(&map, 3, 1, PLANT_BUSH, 1.0f);
    Plant *grass = put_plant(&map, 1, 3, PLANT_GRASS, 1.0f);
    Plant *short_grass = put_plant(&map, 3, 3, PLANT_GRASS, 0.4f);
    int index = gather_zone_create(&zones, &map, 0, 0, 4, 4);
    order(&zones, &items, index, GATHER_BRANCHES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_TWIGS, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_GRASS, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_BERRIES, GATHER_FOREVER);

    /* Branches: all at once, the work growing with their number; the tree stays. */
    int branches = plant_stock_available(tree, PLANT_STOCK_BRANCHES);
    REQUIRE(branches > 1);
    float one_branch = gather_job_work_needed(&map, 1, 1, GATHER_JOB_BRANCHES) / (float)branches;
    CHECK(one_branch > 0.0f);
    CHECK(gather_zone_add_work(&zones, &map, &items, 1, 1, GATHER_JOB_BRANCHES, 1e6f, 60.0,
                               NULL));
    CHECK_EQ_INT(tree->type, PLANT_TREE);
    CHECK_EQ_INT(plant_stock_available(tree, PLANT_STOCK_BRANCHES), 0);
    CHECK_EQ_INT((int)items.totals[ITEM_BRANCHES], branches);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 1, ALL_JOBS), GATHER_JOB_TWIGS);

    /* The bush's twigs come before its berries: picking them leaves a
     * young bush, too small to hold twigs. */
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, ALL_JOBS), GATHER_JOB_TWIGS);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 1, GATHER_JOB_TWIGS, 1e6f, 60.0, NULL));
    CHECK(items.totals[ITEM_TWIGS] > 0);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, ALL_JOBS), GATHER_JOB_HARVEST);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 1, GATHER_JOB_HARVEST, 1e6f, 60.0,
                               NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_BERRIES], plant_def(PLANT_BUSH)->harvest_yield);
    CHECK_NEAR(bush->growth, PLANT_GROWTH_AFTER_PICKING, 1e-6);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, ALL_JOBS), GATHER_JOB_NONE);

    /* Grass: tall enough only; it grows back from nothing. */
    CHECK(gather_job_available(&zones, &map, 1, 3, GATHER_JOB_CUT_GRASS));
    CHECK(!gather_job_available(&zones, &map, 3, 3, GATHER_JOB_CUT_GRASS));
    CHECK(gather_zone_add_work(&zones, &map, &items, 1, 3, GATHER_JOB_CUT_GRASS, 1e6f, 60.0,
                               NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_FRESH_GRASS], 3);
    CHECK_EQ_INT(grass->type, PLANT_GRASS);
    CHECK_NEAR(grass->growth, 0.0, 1e-6);
    CHECK(!gather_zone_add_work(&zones, &map, &items, 3, 3, GATHER_JOB_CUT_GRASS, 1e6f, 60.0,
                                NULL));
    CHECK_NEAR(short_grass->growth, 0.4, 1e-6);
    free_world(&map, &zones, &items);
}

static void test_fixed_count_stops(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int index = gather_zone_create(&zones, &map, 0, 0, 1, 0);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_DO_COUNT);
    gather_zone_set_target(&zones, index, GATHER_COBBLES, 3);
    const GatherZone *zone = gather_zone_get(&zones, index);
    float stone = gather_job_work_needed(&map, 0, 0, GATHER_JOB_STONES);
    for (int i = 0; i < 3; i++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, 0, 0, GATHER_JOB_STONES, stone, 60.0,
                                   NULL));
    }
    /* Done at once, whatever lies in the colony. */
    CHECK_EQ_INT(zone->orders[GATHER_COBBLES].done, 3);
    CHECK_EQ_INT(zone->wanted, 0);
    CHECK_EQ_INT(zones.wanting_cells, 0);
    CHECK(!gather_zone_add_work(&zones, &map, &items, 0, 0, GATHER_JOB_STONES, stone, 60.0,
                                NULL));
    gather_zones_update(&zones, items.totals);
    CHECK_EQ_INT(zone->wanted, 0);
    /* A higher target: more to do. Another mode starts over. */
    gather_zone_set_target(&zones, index, GATHER_COBBLES, 4);
    gather_zones_update(&zones, items.totals);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_COBBLES));
    gather_zone_set_mode(&zones, index, GATHER_COBBLES, GATHER_FOREVER);
    CHECK_EQ_INT(zone->orders[GATHER_COBBLES].done, 0);
    free_world(&map, &zones, &items);
}

static void test_progress_belongs_to_one_job(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 2, 2, PLANT_TREE, 1.0f);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3);
    order(&zones, &items, index, GATHER_TWIGS, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    CHECK(!gather_zone_add_work(&zones, &map, &items, 2, 2, GATHER_JOB_TWIGS, 100.0f, 60.0,
                                NULL));
    CHECK(gather_zone_progress_fraction(&zones, &map, 2, 2) > 0.0f);
    /* Work on another job there starts from scratch. */
    CHECK(!gather_zone_add_work(&zones, &map, &items, 2, 2, GATHER_JOB_STONES, 100.0f, 60.0,
                                NULL));
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, 2, 2), 0.5, 1e-4);
    CHECK_NEAR(zones.progress[map_index(&map, 2, 2)], 100.0, 1e-4);
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, -1, 2), 0.0, 1e-6);
    /* Removing the tile forgets the work. */
    gather_zones_remove_rect(&zones, 2, 2, 2, 2);
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, 2, 2), 0.0, 1e-6);
    free_world(&map, &zones, &items);
}

static void test_stones_again_and_again(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int index = gather_zone_create(&zones, &map, 8, 8, 10, 8);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    float needed = gather_job_work_needed(&map, 8, 8, GATHER_JOB_STONES);
    for (int i = 0; i < 10; i++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, GATHER_JOB_STONES, needed, 60.0,
                                   NULL));
    }
    CHECK_EQ_INT((int)items.totals[ITEM_COBBLE], 10);
    /* No stones on sand, nor outside a zone. */
    CHECK(!gather_job_available(&zones, &map, 10, 8, GATHER_JOB_STONES));
    CHECK(gather_job_available(&zones, &map, 9, 8, GATHER_JOB_STONES));
    CHECK(!gather_zone_add_work(&zones, &map, &items, 2, 2, GATHER_JOB_STONES, needed, 60.0,
                                NULL));
    /* A good eye picks good stones. */
    gather_zone_add_work(&zones, &map, &items, 9, 8, GATHER_JOB_STONES, needed,
                         KNOWLEDGE_LEVEL_MAX, NULL);
    CHECK_EQ_INT(items_get(&items, 9, 8).quality, ITEM_QUALITY_SUPERB);
    CHECK(items_get(&items, 8, 8).quality <= ITEM_QUALITY_STANDARD);
    free_world(&map, &zones, &items);
}

static void test_flint_patch_and_sorting(void)
{
    Map map;
    GatherZones zones;
    Items items;
    Rng rng;
    REQUIRE(init_world(&map, &zones, &items));
    rng_seed(&rng, 7, 1);
    map_set_flint_chance(&map, 8, 8, MAP_FLINT_MAX_CHANCE);
    map_set_flint_chance(&map, 9, 9, 200); /* Clamped. */
    CHECK_EQ_INT(map_flint_chance(&map, 9, 9), MAP_FLINT_MAX_CHANCE);
    int index = gather_zone_create(&zones, &map, 4, 4, 8, 8);
    CHECK_EQ_INT(gather_zone_flint_cells(&zones, &map, index), 1);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_FLINT, GATHER_FOREVER);
    float stone = gather_job_work_needed(&map, 8, 8, GATHER_JOB_STONES);
    for (int i = 0; i < 200; i++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, GATHER_JOB_STONES, stone, 60.0,
                                   &rng));
        CHECK(gather_zone_add_work(&zones, &map, &items, 4, 4, GATHER_JOB_STONES, stone, 60.0,
                                   &rng));
    }
    /* About 60% flint on the patch, none elsewhere: 400 stones in all. */
    long flint = items.totals[ITEM_FLINT];
    CHECK(flint > 90 && flint < 150);
    CHECK_EQ_INT((int)(items.totals[ITEM_COBBLE] + flint), 400);

    /* Only flint wanted: tiles without flint offer nothing, and the cobbles
     * found are left in the ground. */
    long cobbles = items.totals[ITEM_COBBLE];
    gather_zone_set_enabled(&zones, index, GATHER_COBBLES, false);
    gather_zones_update(&zones, items.totals);
    CHECK(!gather_job_available(&zones, &map, 4, 4, GATHER_JOB_STONES));
    CHECK(gather_job_available(&zones, &map, 8, 8, GATHER_JOB_STONES));
    for (int i = 0; i < 100; i++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, GATHER_JOB_STONES, stone, 60.0,
                                   &rng));
    }
    CHECK_EQ_INT((int)items.totals[ITEM_COBBLE], (int)cobbles);
    CHECK(items.totals[ITEM_FLINT] > flint + 40);
    CHECK_EQ_INT(gather_zone_get(&zones, index)->orders[GATHER_FLINT].done,
                 (int)items.totals[ITEM_FLINT]);
    free_world(&map, &zones, &items);
}

static void test_plants_growing_wake_settlers(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    uint32_t revision = zones.revision;
    gather_zones_plants_changed(&zones); /* No zone: nothing to look for. */
    CHECK_EQ_UINT(zones.revision, revision);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3);
    order(&zones, &items, index, GATHER_BERRIES, GATHER_FOREVER);
    revision = zones.revision;
    gather_zones_plants_changed(&zones);
    CHECK(zones.revision != revision);
    free_world(&map, &zones, &items);
}

static void test_hash(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    uint64_t empty = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    int index = gather_zone_create(&zones, &map, 0, 0, 2, 2);
    uint64_t created = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    CHECK(created != empty);
    gather_zone_set_target(&zones, index, GATHER_FLINT, 3);
    uint64_t target = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    CHECK(target != created);
    gather_zone_set_ripe_only(&zones, index, GATHER_BERRIES, false);
    CHECK(gather_zones_hash(&zones, HASH_FNV1A64_INIT) != target);
    free_world(&map, &zones, &items);
}

int main(void)
{
    RUN_TEST(test_init_rejects_invalid_sizes);
    RUN_TEST(test_definitions);
    RUN_TEST(test_where_zones_and_stones_lie);
    RUN_TEST(test_zone_tiles);
    RUN_TEST(test_order_settings);
    RUN_TEST(test_wanted_resources);
    RUN_TEST(test_tile_jobs_in_order);
    RUN_TEST(test_fully_grown_plants_only);
    RUN_TEST(test_felling_a_tree);
    RUN_TEST(test_products_depend_on_level);
    RUN_TEST(test_branches_twigs_grass_and_berries);
    RUN_TEST(test_fixed_count_stops);
    RUN_TEST(test_progress_belongs_to_one_job);
    RUN_TEST(test_stones_again_and_again);
    RUN_TEST(test_flint_patch_and_sorting);
    RUN_TEST(test_plants_growing_wake_settlers);
    RUN_TEST(test_hash);
    return test_summary();
}
