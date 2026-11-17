/* Gathering zones: tiles, orders, what they want, the jobs tiles offer and
 * what they give. */
#include "core/colonist.h"
#include "core/gather_zone.h"
#include "core/i18n.h"
#include "core/util.h"

#include "test_framework.h"

#include <stdlib.h>

#define ALL_JOBS ((GatherJobMask)((1u << GATHER_JOB_COUNT) - 1u))

/* 16 x 16 soil map with a granite wall at (5, 5), sand at (10, 8), water at
 * (11, 8) and gravel at (9, 8); MAP_STONES_MAX cobbles on every tile (only
 * found where gather_stone_ground() allows). */
static bool init_world(Map *map, GatherZones *zones, Items *items)
{
    if (!map_init(map, 16, 16, 1) || !gather_zones_init(zones, 16, 16, 1) ||
        !items_init(items, 16, 16, 1)) {
        return false;
    }
    map->rock[map_index(map, 5, 5, 0)] = ROCK_GRANITE;
    map->terrain[map_index(map, 9, 8, 0)] = TERRAIN_GRAVEL;
    map->terrain[map_index(map, 10, 8, 0)] = TERRAIN_SAND;
    map->terrain[map_index(map, 11, 8, 0)] = TERRAIN_SHALLOW_WATER;
    for (int y = 0; y < 16; y++) {
        for (int x = 0; x < 16; x++) {
            map_set_stones(map, x, y, 0, MAP_STONES_MAX, 0);
        }
    }
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
    Plant *plant = &map->plants[map_index(map, x, y, 0)];
    plant->type = (uint8_t)type;
    plant->growth = growth;
    plant_settle(plant, GAME_START_DAY);
    return plant;
}

/* Enables an order in `mode` and updates what the zones want. */
static void order(GatherZones *zones, const Items *items, int index, GatherResource resource,
                  GatherMode mode)
{
    gather_zone_set_enabled(zones, index, resource, true);
    gather_zone_set_mode(zones, index, resource, mode);
    gather_zones_update(zones, items->totals, 0.0f);
}

static void test_init_rejects_invalid_sizes(void)
{
    GatherZones zones;
    CHECK(!gather_zones_init(&zones, 0, 10, 1));
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
    /* Plants may wait for the full harvest, stones not. */
    CHECK(gather_resource_def(GATHER_WOOD)->ripe_option);
    CHECK(gather_resource_def(GATHER_BLACKBERRIES)->ripe_option);
    CHECK(gather_resource_def(GATHER_TWIGS)->ripe_option);
    CHECK(gather_resource_def(GATHER_GRASS)->ripe_option);
    CHECK(!gather_resource_def(GATHER_COBBLES)->ripe_option);
    CHECK(
        !(gather_ripe_options() & GATHER_RESOURCE_BIT(GATHER_HAZELNUTS))); /* Lie on the ground. */
    CHECK(!(gather_ripe_options() & GATHER_RESOURCE_BIT(GATHER_FLINT)));
    /* Groups: each resource in the group listing it, grass on its own. */
    GatherResourceMask grouped = 0;
    for (int g = 0; g < GATHER_GROUP_COUNT; g++) {
        const GatherGroupDef *group = gather_group_def((GatherGroup)g);
        REQUIRE(group != NULL);
        CHECK(group->id != NULL);
        CHECK(i18n_is_valid_key(group->name_key));
        CHECK(group->members != 0 && (group->members & grouped) == 0);
        grouped |= group->members;
        for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
            bool member = (group->members & GATHER_RESOURCE_BIT(r)) != 0;
            CHECK(member == (gather_resource_group((GatherResource)r) == (GatherGroup)g));
        }
    }
    CHECK(gather_group_def(GATHER_GROUP_NONE) == NULL);
    CHECK_EQ_INT(gather_resource_group(GATHER_GRASS), GATHER_GROUP_NONE);
    CHECK_EQ_INT(gather_resource_group(GATHER_TWIGS), GATHER_GROUP_WOOD);
    CHECK_EQ_INT(gather_resource_group(GATHER_FLINT), GATHER_GROUP_STONES);
    CHECK(gather_resource_is_food(GATHER_MUSHROOMS));
    CHECK(!gather_resource_is_food(GATHER_WOOD) && !gather_resource_is_food(GATHER_GRASS));
    /* Each wild food resource gives food. */
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        CHECK(gather_resource_is_food((GatherResource)r) ==
              item_is_food(gather_resource_def((GatherResource)r)->item));
    }
    /* Every wild food is a resource, gathered by the job that suits the
     * plant: dug or picked, carefully when easily mistaken for poison. */
    for (int p = PLANT_NONE + 1; p < PLANT_COUNT; p++) {
        const PlantDef *plant = plant_def((PlantType)p);
        if (plant->food_item == ITEM_NONE) {
            continue;
        }
        GatherResource resource = GATHER_RESOURCE_COUNT;
        for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
            if (gather_resource_def((GatherResource)r)->item == plant->food_item) {
                resource = (GatherResource)r;
            }
        }
        REQUIRE(resource != GATHER_RESOURCE_COUNT);
        GatherJob job = gather_resource_def(resource)->job;
        GatherJob expected =
            plant->food_dug
                ? (plant->food_confusable ? GATHER_JOB_DIG_CAREFULLY : GATHER_JOB_DIG)
                : (plant->food_confusable ? GATHER_JOB_HARVEST_CAREFULLY : GATHER_JOB_HARVEST);
        CHECK_EQ_INT(job, expected);
    }
    /* Gathering from 50 on, food that looks like poison from higher up;
     * nobody can fell trees yet. */
    CHECK(gather_job_doable_at(GATHER_JOB_HARVEST, KNOWLEDGE_MASTERY_LEVEL));
    CHECK(gather_job_doable_at(GATHER_JOB_DIG, KNOWLEDGE_MASTERY_LEVEL));
    CHECK(!gather_job_doable_at(GATHER_JOB_HARVEST_CAREFULLY, GATHER_CAREFUL_MIN_LEVEL - 0.1));
    CHECK(gather_job_doable_at(GATHER_JOB_HARVEST_CAREFULLY, GATHER_CAREFUL_MIN_LEVEL));
    CHECK(!gather_job_doable_at(GATHER_JOB_DIG_CAREFULLY, GATHER_CAREFUL_MIN_LEVEL - 0.1));
    CHECK(gather_job_def(GATHER_JOB_DIG)->tool_use == TOOL_USE_DIGGING);
    CHECK(gather_job_def(GATHER_JOB_DIG)->fetches_tool);
    CHECK(!gather_job_def(GATHER_JOB_CUT_GRASS)->fetches_tool);
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
    /* Zones cover open ground, sand and shallow water (cattails) included,
     * but not deep water nor rock. */
    CHECK(gather_zone_can_cover(&map, 8, 8, 0));
    CHECK(gather_zone_can_cover(&map, 10, 8, 0));
    CHECK(gather_zone_can_cover(&map, 11, 8, 0));
    map.terrain[map_index(&map, 12, 8, 0)] = TERRAIN_DEEP_WATER;
    CHECK(!gather_zone_can_cover(&map, 12, 8, 0));
    CHECK(!gather_zone_can_cover(&map, 5, 5, 0));
    CHECK(!gather_zone_can_cover(&map, -1, 0, 0));
    map.terrain[map_index(&map, 13, 8, 0)] = TERRAIN_OPEN; /* No floor. */
    CHECK(!gather_zone_can_cover(&map, 13, 8, 0));
    map.terrain[map_index(&map, 13, 8, 0)] = TERRAIN_SOIL;
    /* Stones lie on soil, gravel and rough stone, not on sand. */
    CHECK(gather_stone_ground(&map, 8, 8, 0));
    CHECK(gather_stone_ground(&map, 9, 8, 0));
    CHECK(!gather_stone_ground(&map, 10, 8, 0));
    CHECK(!gather_stone_ground(&map, 5, 5, 0));
    /* A built wall is like rock; no stones are searched for under a bed. */
    map_set_structure(&map, 7, 9, 0, STRUCTURE_WALL);
    map_set_structure(&map, 8, 9, 0, STRUCTURE_GRASS_BED);
    CHECK(!gather_zone_can_cover(&map, 7, 9, 0));
    CHECK(gather_zone_can_cover(&map, 8, 9, 0));
    CHECK(!gather_stone_ground(&map, 8, 9, 0));
    int index = gather_zone_create(&zones, &map, 4, 8, 12, 8, 0);
    REQUIRE(index != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_get(&zones, index)->cell_count, 8);
    CHECK_EQ_INT(gather_zone_at(&zones, 12, 8, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(&zones, 11, 8, 0), index);
    CHECK_EQ_INT(gather_zone_create(&zones, &map, 12, 8, 12, 8, 0), GATHER_ZONE_NONE);
    free_world(&map, &zones, &items);
}

static void test_zone_tiles(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int a = gather_zone_create(&zones, &map, 0, 0, 2, 2, 0);
    REQUIRE(a != GATHER_ZONE_NONE);
    const GatherZone *zone = gather_zone_get(&zones, a);
    CHECK_EQ_INT(zone->number, 1);
    /* Every order is off at first. */
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        CHECK(!zone->orders[r].enabled);
        CHECK_EQ_INT(zone->orders[r].mode, GATHER_UNTIL);
        CHECK_EQ_INT(zone->orders[r].target, GATHER_TARGET_DEFAULT);
        CHECK_EQ_INT(zone->orders[r].unit, GATHER_UNIT_ITEMS);
        /* Only wood waits for the full harvest: young trees give little. */
        CHECK(zone->orders[r].ripe_only == (r == GATHER_WOOD));
    }
    /* Food until 5 days of food, the other groups forever. */
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_FOOD].mode, GATHER_UNTIL);
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_FOOD].unit, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_FOOD].target, GATHER_FOOD_DAYS_DEFAULT);
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_WOOD].mode, GATHER_FOREVER);
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_STONES].mode, GATHER_FOREVER);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    CHECK_EQ_INT(zones.wanting_cells, 0);
    /* Tiles of another zone are not taken. */
    int b = gather_zone_create(&zones, &map, 2, 2, 3, 3, 0);
    REQUIRE(b != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_get(&zones, b)->cell_count, 3);
    CHECK_EQ_INT(gather_zone_get(&zones, b)->number, 2);
    CHECK_EQ_INT(gather_zone_extend(&zones, &map, a, 0, 3, 1, 3, 0), 2);
    CHECK_EQ_INT(zone->cell_count, 11);
    CHECK_EQ_INT(zones.used_count, 2);
    /* Removing tiles; a zone left empty is deleted. */
    CHECK_EQ_INT(gather_zones_remove_rect(&zones, 2, 2, 3, 3, 0), 4);
    CHECK(gather_zone_get(&zones, b) == NULL);
    CHECK_EQ_INT(zone->cell_count, 10);
    CHECK_EQ_INT(zones.used_count, 1);
    gather_zone_delete(&zones, a);
    CHECK(gather_zone_get(&zones, a) == NULL);
    CHECK_EQ_INT(gather_zone_at(&zones, 0, 0, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(zones.used_count, 0);
    /* Numbers are never reused. */
    int c = gather_zone_create(&zones, &map, 0, 0, 0, 0, 0);
    CHECK_EQ_INT(gather_zone_get(&zones, c)->number, 3);
    free_world(&map, &zones, &items);
}

static void test_order_settings(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int index = gather_zone_create(&zones, &map, 0, 0, 2, 2, 0);
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
    /* Stones never wait for the full harvest. */
    gather_zone_set_ripe_only(&zones, index, GATHER_TWIGS, true);
    CHECK(twigs->ripe_only);
    gather_zone_set_ripe_only(&zones, index, GATHER_COBBLES, true);
    CHECK(!zone->orders[GATHER_COBBLES].ripe_only);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    CHECK(!zone->orders[GATHER_WOOD].ripe_only);
    /* Days of food for food only; a new unit comes with its default target. */
    gather_zone_set_unit(&zones, index, GATHER_TWIGS, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(twigs->unit, GATHER_UNIT_ITEMS);
    gather_zone_set_unit(&zones, index, GATHER_HAZELNUTS, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(zone->orders[GATHER_HAZELNUTS].unit, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(zone->orders[GATHER_HAZELNUTS].target, GATHER_FOOD_DAYS_DEFAULT);
    gather_zone_set_unit(&zones, index, GATHER_HAZELNUTS, GATHER_UNIT_COUNT);
    CHECK_EQ_INT(zone->orders[GATHER_HAZELNUTS].unit, GATHER_UNIT_FOOD_DAYS);
    gather_zone_set_unit(&zones, index, GATHER_HAZELNUTS, GATHER_UNIT_ITEMS);
    CHECK_EQ_INT(zone->orders[GATHER_HAZELNUTS].target, GATHER_TARGET_DEFAULT);
    /* Days of food that become items to gather: the items' default. */
    const GatherGroupOrder *food = &zone->groups[GATHER_GROUP_FOOD];
    gather_zone_set_group_target(&zones, index, GATHER_GROUP_FOOD, 8);
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_FOOD, GATHER_FOREVER);
    CHECK_EQ_INT(food->target, GATHER_TARGET_DEFAULT);
    gather_zone_set_group_target(&zones, index, GATHER_GROUP_FOOD, 30);
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_FOOD, GATHER_DO_COUNT);
    CHECK_EQ_INT(food->target, 30);
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_FOOD, GATHER_UNTIL);
    CHECK_EQ_INT(food->unit, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(food->target, GATHER_FOOD_DAYS_DEFAULT);
    /* Invalid zones and resources are ignored. */
    gather_zone_set_enabled(&zones, 7, GATHER_TWIGS, true);
    gather_zone_set_enabled(&zones, index, GATHER_RESOURCE_COUNT, true);
    gather_zone_set_target(&zones, -1, GATHER_TWIGS, 5);
    free_world(&map, &zones, &items);
}

static void test_zone_priority(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int a = gather_zone_create(&zones, &map, 0, 0, 2, 2, 0);
    int b = gather_zone_create(&zones, &map, 0, 10, 2, 12, 0);
    const GatherZone *zone = gather_zone_get(&zones, a);
    CHECK_EQ_INT(zone->priority, GATHER_ZONE_PRIORITY_DEFAULT);
    CHECK_EQ_INT(gather_zone_priority_at(&zones, 1, 1, 0), GATHER_ZONE_PRIORITY_DEFAULT);
    CHECK_EQ_INT(gather_zone_priority_at(&zones, 8, 8, 0), 0);
    CHECK_EQ_INT(gather_zone_priority_at(&zones, -1, 8, 0), 0);
    uint32_t revision = zones.revision;
    gather_zone_set_priority(&zones, a, 2);
    CHECK_EQ_INT(zone->priority, 2);
    CHECK(zones.revision != revision); /* Settlers look again. */
    revision = zones.revision;
    gather_zone_set_priority(&zones, a, 2);
    CHECK_EQ_INT(zones.revision, revision);
    gather_zone_set_priority(&zones, a, 0);
    CHECK_EQ_INT(zone->priority, GATHER_ZONE_PRIORITY_HIGHEST);
    gather_zone_set_priority(&zones, a, 99);
    CHECK_EQ_INT(zone->priority, GATHER_ZONE_PRIORITY_LOWEST);
    gather_zone_set_priority(&zones, 9, 3); /* Ignored. */

    /* Only zones that want something count. */
    CHECK_EQ_INT(gather_zones_wanting_priorities(&zones), 0);
    order(&zones, &items, a, GATHER_TWIGS, GATHER_FOREVER);
    CHECK_EQ_INT(gather_zones_wanting_priorities(&zones), 1u << GATHER_ZONE_PRIORITY_LOWEST);
    order(&zones, &items, b, GATHER_TWIGS, GATHER_FOREVER);
    CHECK_EQ_INT(gather_zones_wanting_priorities(&zones),
                 (1u << GATHER_ZONE_PRIORITY_LOWEST) | (1u << GATHER_ZONE_PRIORITY_DEFAULT));

    /* A merged zone keeps the priority of the one it keeps the orders of. */
    CHECK_EQ_INT(gather_zones_add_rect(&zones, &map, 1, 2, 1, 10, 0, a, NULL), a);
    CHECK_EQ_INT(gather_zone_priority_at(&zones, 1, 11, 0), GATHER_ZONE_PRIORITY_LOWEST);
    free_world(&map, &zones, &items);
}

static void test_wanted_resources(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int index = gather_zone_create(&zones, &map, 0, 0, 2, 2, 0);
    const GatherZone *zone = gather_zone_get(&zones, index);
    /* Until the colony has 10 armfuls of grass: fresh and dry both count. */
    order(&zones, &items, index, GATHER_GRASS, GATHER_UNTIL);
    gather_zone_set_target(&zones, index, GATHER_GRASS, 10);
    items_add(&items, 12, 12, 0, item_stack_pristine(ITEM_FRESH_GRASS, 6));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_GRASS));
    CHECK_EQ_INT(zones.wanting_cells, 9);
    CHECK_EQ_INT((int)gather_resource_count(GATHER_GRASS, items.totals), 6);
    items_add(&items, 13, 12, 0, item_stack_pristine(ITEM_GRASS, 4));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    CHECK_EQ_INT(zones.wanting_cells, 0);
    /* Forever: whatever lies in the colony. */
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    items_add(&items, 14, 12, 0, item_stack_pristine(ITEM_COBBLE, 50));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_COBBLES));
    /* A fixed count: what this order gathered, not what lies about. */
    order(&zones, &items, index, GATHER_FLINT, GATHER_DO_COUNT);
    items_add(&items, 15, 12, 0, item_stack_pristine(ITEM_FLINT, 50));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK(zone->wanted & GATHER_RESOURCE_BIT(GATHER_FLINT));
    /* Turned off: never wanted. */
    gather_zone_set_enabled(&zones, index, GATHER_COBBLES, false);
    gather_zones_update(&zones, items.totals, 0.0f);
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
    map_add_litter(&map, 2, 2, 0, ITEM_BRANCHES);
    map_add_litter(&map, 2, 2, 0, ITEM_TWIGS);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3, 0);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, 0, ALL_JOBS), GATHER_JOB_NONE);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_TWIGS, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_BRANCHES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    /* What lies under the tree before felling it, and stones last. */
    GatherJobMask no_branches = ALL_JOBS & ~GATHER_JOB_BIT(GATHER_JOB_BRANCHES);
    GatherJobMask chop_or_stones =
        GATHER_JOB_BIT(GATHER_JOB_CHOP) | GATHER_JOB_BIT(GATHER_JOB_STONES);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, 0, ALL_JOBS), GATHER_JOB_BRANCHES);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, 0, no_branches), GATHER_JOB_TWIGS);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, 0, chop_or_stones), GATHER_JOB_CHOP);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, 0, GATHER_JOB_BIT(GATHER_JOB_STONES)),
                 GATHER_JOB_STONES);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 1, 0, ALL_JOBS), GATHER_JOB_STONES);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 1, 0, GATHER_JOB_BIT(GATHER_JOB_CHOP)),
                 GATHER_JOB_NONE);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 8, 8, 0, ALL_JOBS), GATHER_JOB_NONE);
    CHECK(gather_job_available(&zones, &map, 2, 2, 0, GATHER_JOB_CHOP));
    CHECK(!gather_job_available(&zones, &map, 2, 2, 0, GATHER_JOB_HARVEST));
    CHECK(!gather_job_available(&zones, &map, 2, 2, 0, GATHER_JOB_NONE));
    free_world(&map, &zones, &items);
}

static void test_fully_grown_plants_only(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 1, 1, PLANT_TREE, 0.4f);
    const PlantDef *def = plant_def(PLANT_BRAMBLE);
    Plant *young = put_plant(&map, 2, 1, PLANT_BRAMBLE, def->min_food_growth - 0.01f);
    put_plant(&map, 3, 1, PLANT_BRAMBLE, def->min_food_growth);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3, 0);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    /* By default, young trees not fully grown are left alone. */
    CHECK(!gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_CHOP));
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    CHECK(gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_CHOP));
    /* A bramble too young bears no fruit; an older one does. */
    CHECK(!gather_job_available(&zones, &map, 2, 1, 0, GATHER_JOB_HARVEST));
    CHECK(plant_stock_available(young, PLANT_STOCK_FOOD) == 0);
    CHECK(gather_job_available(&zones, &map, 3, 1, 0, GATHER_JOB_HARVEST));
    free_world(&map, &zones, &items);
}

/* Wild food in its season only; the job depends on the plant. */
static void test_food_in_season(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int autumn = SEASON_DAY(SEASON_AUTUMN, 4);
    Plant *bramble = put_plant(&map, 1, 1, PLANT_BRAMBLE, 1.0f);
    plant_take_stock(bramble, PLANT_STOCK_FOOD, 1000); /* Not in fruit yet. */
    Plant *cattail = put_plant(&map, 2, 1, PLANT_CATTAIL, 1.0f);
    Plant *carrot = put_plant(&map, 3, 1, PLANT_WILD_CARROT, 1.0f);
    Plant *mushrooms = put_plant(&map, 1, 3, PLANT_MUSHROOMS, 1.0f);
    map.terrain[map_index(&map, 2, 1, 0)] = TERRAIN_SHALLOW_WATER; /* Cattails stand in water. */
    int index = gather_zone_create(&zones, &map, 0, 0, 4, 4, 0);
    for (int r = GATHER_BLACKBERRIES; r <= GATHER_CATTAIL_RHIZOMES; r++) {
        order(&zones, &items, index, (GatherResource)r, GATHER_FOREVER);
    }
    /* Brambles not in fruit yet, rhizomes all year, no roots yet. */
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 1, 0, GATHER_JOB_BIT(GATHER_JOB_HARVEST)),
                 GATHER_JOB_NONE);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 1, 0, ALL_JOBS), GATHER_JOB_DIG);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, 0, ALL_JOBS), GATHER_JOB_NONE);
    /* Autumn. */
    plant_settle(bramble, autumn);
    plant_settle(carrot, autumn);
    plant_settle(mushrooms, autumn);
    CHECK(gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_HARVEST));
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, 0, ALL_JOBS), GATHER_JOB_DIG_CAREFULLY);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 3, 0, ALL_JOBS), GATHER_JOB_HARVEST_CAREFULLY);
    /* Not for the settlers who cannot tell them from poisonous ones. */
    GatherJobMask novice = ALL_JOBS & ~GATHER_JOB_BIT(GATHER_JOB_DIG_CAREFULLY) &
                           ~GATHER_JOB_BIT(GATHER_JOB_HARVEST_CAREFULLY);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, 0, novice), GATHER_JOB_NONE);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 3, 0, novice), GATHER_JOB_NONE);

    /* Berries: the work grows with their number; the bramble stays. */
    int berries = plant_stock_available(bramble, PLANT_STOCK_FOOD);
    REQUIRE(berries == plant_def(PLANT_BRAMBLE)->stock_capacity[PLANT_STOCK_FOOD]);
    CHECK(gather_job_work_needed(&map, 1, 1, 0, GATHER_JOB_HARVEST) >
          gather_job_work_needed(&map, 1, 3, 0, GATHER_JOB_HARVEST_CAREFULLY));
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 1, 1, 0, GATHER_JOB_HARVEST, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_BLACKBERRIES], berries);
    CHECK_EQ_INT(bramble->type, PLANT_BRAMBLE);
    CHECK_NEAR(bramble->growth, 1.0f, 1e-6);
    /* Rhizomes: part of the rootstock, the cattail grows on. */
    int rhizomes = plant_stock_available(cattail, PLANT_STOCK_FOOD);
    CHECK(gather_zone_add_work(&zones, &map, &items, 2, 1, 0, GATHER_JOB_DIG, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_CATTAIL_RHIZOMES], rhizomes);
    CHECK_NEAR(cattail->growth, 1.0f, 1e-6);
    /* A wild carrot dug up is gone, but grows again from what is left. */
    int roots = plant_stock_available(carrot, PLANT_STOCK_FOOD);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 1, 0, GATHER_JOB_DIG_CAREFULLY, 1e6f, 60.0,
                               NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_WILD_CARROTS], roots);
    CHECK_EQ_INT(carrot->type, PLANT_WILD_CARROT);
    CHECK_NEAR(carrot->growth, 0.0f, 1e-6);
    /* Mushrooms picked: the patch is gone. */
    CHECK(gather_zone_add_work(&zones, &map, &items, 1, 3, 0, GATHER_JOB_HARVEST_CAREFULLY, 1e6f,
                               60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_MUSHROOMS],
                 plant_def(PLANT_MUSHROOMS)->stock_capacity[PLANT_STOCK_FOOD]);
    CHECK_EQ_INT(mushrooms->type, PLANT_NONE);
    free_world(&map, &zones, &items);
}

/* Waiting for the full harvest: a full stock (every fruit ripe, not a grown
 * plant), trees and grass fully grown. */
static void test_full_harvest(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    const PlantDef *bramble_def = plant_def(PLANT_BRAMBLE);
    Plant *young = put_plant(&map, 3, 1, PLANT_BRAMBLE, bramble_def->min_food_growth);
    Plant *bramble = put_plant(&map, 5, 1, PLANT_BRAMBLE, 1.0f);
    put_plant(&map, 1, 3, PLANT_GRASS, 0.8f);
    put_plant(&map, 3, 3, PLANT_GRASS, 1.0f);
    int index = gather_zone_create(&zones, &map, 0, 0, 6, 4, 0);
    order(&zones, &items, index, GATHER_BRANCHES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_GRASS, GATHER_FOREVER);
    gather_zone_set_all_ripe_only(&zones, index, true);
    const GatherZone *zone = gather_zone_get(&zones, index);
    CHECK_EQ_INT(gather_zone_ripe_mask(zone), gather_ripe_options());
    /* Full stocks: the young bramble's few berries are all ripe. */
    REQUIRE(plant_stock_available(young, PLANT_STOCK_FOOD) > 0);
    CHECK(gather_job_available(&zones, &map, 3, 1, 0, GATHER_JOB_HARVEST));
    CHECK(gather_job_available(&zones, &map, 5, 1, 0, GATHER_JOB_HARVEST));
    /* Some taken: what is left waits until the stock is full again. */
    CHECK_EQ_INT(plant_take_stock(bramble, PLANT_STOCK_FOOD, 1), 1);
    CHECK(!gather_job_available(&zones, &map, 5, 1, 0, GATHER_JOB_HARVEST));
    long available[GATHER_RESOURCE_COUNT];
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_BLACKBERRIES],
                 plant_stock_available(young, PLANT_STOCK_FOOD));
    CHECK_EQ_INT((int)available[GATHER_BRANCHES], 0);
    /* Grass fully grown only. */
    CHECK(!gather_job_available(&zones, &map, 1, 3, 0, GATHER_JOB_CUT_GRASS));
    CHECK(gather_job_available(&zones, &map, 3, 3, 0, GATHER_JOB_CUT_GRASS));
    /* Without the option, whatever there is. */
    gather_zone_set_group_ripe_only(&zones, index, GATHER_GROUP_FOOD, false);
    CHECK(gather_job_available(&zones, &map, 5, 1, 0, GATHER_JOB_HARVEST));
    CHECK(!gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_BRANCHES));
    CHECK(!(gather_zone_ripe_mask(zone) & GATHER_RESOURCE_BIT(GATHER_HAZELNUTS)));
    CHECK(gather_zone_ripe_mask(zone) & GATHER_RESOURCE_BIT(GATHER_TWIGS));
    /* What lies on the ground is always ready: no waiting for a full stock. */
    map_add_litter(&map, 1, 1, 0, ITEM_BRANCHES);
    CHECK(gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_BRANCHES));
    map_take_litter(&map, 1, 1, 0, ITEM_BRANCHES);
    CHECK(!gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_BRANCHES));
    gather_zone_set_all_ripe_only(&zones, index, false);
    CHECK_EQ_INT(gather_zone_ripe_mask(zone), 0);
    CHECK(gather_job_available(&zones, &map, 1, 3, 0, GATHER_JOB_CUT_GRASS));
    free_world(&map, &zones, &items);
}

/* A resource is gathered while its own order and its group's both want it. */
static void test_group_orders(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int index = gather_zone_create(&zones, &map, 0, 0, 1, 0, 0);
    const GatherZone *zone = gather_zone_get(&zones, index);
    GatherResourceMask stones = gather_group_def(GATHER_GROUP_STONES)->members;
    /* Turning a group on turns each of its resources on. */
    gather_zone_set_group_enabled(&zones, index, GATHER_GROUP_STONES, true);
    CHECK_EQ_INT(gather_zone_enabled_mask(zone), stones);
    gather_zone_set_mode(&zones, index, GATHER_COBBLES, GATHER_FOREVER);
    gather_zone_set_mode(&zones, index, GATHER_FLINT, GATHER_FOREVER);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, stones);
    /* The group until 3 stones in the colony: cobbles and flint both count. */
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_STONES, GATHER_UNTIL);
    gather_zone_set_group_target(&zones, index, GATHER_GROUP_STONES, 3);
    items_add(&items, 10, 10, 0, item_stack_pristine(ITEM_COBBLE, 2));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, stones);
    items_add(&items, 11, 10, 0, item_stack_pristine(ITEM_FLINT, 1));
    CHECK_EQ_INT((int)gather_group_count(GATHER_GROUP_STONES, items.totals), 3);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    /* The group gathers 2 stones, then stops, whatever its resources say. */
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_STONES, GATHER_DO_COUNT);
    gather_zone_set_group_target(&zones, index, GATHER_GROUP_STONES, 2);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, stones);
    float stone = gather_job_work_needed(&map, 0, 0, 0, GATHER_JOB_STONES);
    for (int i = 0; i < 2; i++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, 0, 0, 0, GATHER_JOB_STONES, stone, 60.0,
                                   NULL));
    }
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_STONES].done, 2);
    CHECK_EQ_INT(zone->wanted, 0);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    /* Its own order limits a resource as well. */
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_STONES, GATHER_FOREVER);
    gather_zone_set_mode(&zones, index, GATHER_FLINT, GATHER_UNTIL);
    gather_zone_set_target(&zones, index, GATHER_FLINT, 1);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_COBBLES));
    /* Turning the group off turns each of them off; grass has no group. */
    gather_zone_set_group_enabled(&zones, index, GATHER_GROUP_STONES, false);
    CHECK_EQ_INT(gather_zone_enabled_mask(zone), 0);
    gather_zone_set_group_unit(&zones, index, GATHER_GROUP_STONES, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_STONES].unit, GATHER_UNIT_ITEMS);
    order(&zones, &items, index, GATHER_GRASS, GATHER_FOREVER);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_GRASS));
    /* Invalid groups are ignored. */
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_NONE, GATHER_DO_COUNT);
    gather_zone_set_group_enabled(&zones, 9, GATHER_GROUP_WOOD, true);
    free_world(&map, &zones, &items);
}

/* Food until a number of days of food for all the settlers. */
static void test_days_of_food(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    float daily = gather_daily_food(2);
    CHECK_NEAR(daily, 2.0 * NEED_FOOD_DECAY_PER_TICK * TICKS_PER_DAY, 1e-5);
    CHECK_NEAR(gather_daily_food(0), 0.0, 1e-9);
    long totals[ITEM_TYPE_COUNT] = {0};
    float berry = item_def(ITEM_BLACKBERRIES)->nutrition;
    float roasted = item_def(ITEM_ROASTED_CARROTS)->nutrition;
    /* Berries for 3 days and a half: 3 whole days. */
    long three_days = (long)(3.5f * daily / berry);
    totals[ITEM_BLACKBERRIES] = three_days;
    totals[ITEM_TWIGS] = 1000; /* Not food. */
    CHECK_EQ_INT((int)gather_food_days(ITEM_TYPE_BIT(ITEM_BLACKBERRIES), totals, daily), 3);
    CHECK_EQ_INT((int)gather_resource_food_days(GATHER_BLACKBERRIES, totals, daily), 3);
    CHECK_EQ_INT((int)gather_resource_food_days(GATHER_TWIGS, totals, daily), 0);
    CHECK_EQ_INT((int)gather_group_food_days(GATHER_GROUP_FOOD, totals, daily), 3);
    CHECK_EQ_INT((int)gather_group_food_days(GATHER_GROUP_WOOD, totals, daily), 0);
    CHECK_EQ_INT((int)gather_food_days(ITEM_TYPE_BIT(ITEM_BLACKBERRIES), totals, 0.0f), 0);
    /* Cooked food counts for the group, not for a resource. */
    totals[ITEM_ROASTED_CARROTS] = (long)(2.0f * daily / roasted) + 1;
    CHECK_EQ_INT((int)gather_group_food_days(GATHER_GROUP_FOOD, totals, daily), 5);
    CHECK_EQ_INT((int)gather_resource_food_days(GATHER_WILD_CARROTS, totals, daily), 0);

    int index = gather_zone_create(&zones, &map, 0, 0, 1, 0, 0);
    const GatherZone *zone = gather_zone_get(&zones, index);
    gather_zone_set_group_enabled(&zones, index, GATHER_GROUP_FOOD, true);
    gather_zone_set_mode(&zones, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    gather_zone_set_mode(&zones, index, GATHER_HAZELNUTS, GATHER_UNTIL);
    gather_zone_set_unit(&zones, index, GATHER_HAZELNUTS, GATHER_UNIT_FOOD_DAYS);
    gather_zone_set_target(&zones, index, GATHER_HAZELNUTS, 1);
    /* 5 days of food: the group (until 5 days by default) stops. */
    CHECK_EQ_INT(gather_zone_wanted(zone, totals, daily) & GATHER_RESOURCE_BIT(GATHER_BLACKBERRIES),
                 0);
    /* Below 5 days again: it starts over at once. */
    totals[ITEM_ROASTED_CARROTS] = 0;
    GatherResourceMask wanted = gather_zone_wanted(zone, totals, daily);
    CHECK(wanted & GATHER_RESOURCE_BIT(GATHER_BLACKBERRIES));
    /* No hazelnuts lying: less than a day of them. */
    CHECK(wanted & GATHER_RESOURCE_BIT(GATHER_HAZELNUTS));
    totals[ITEM_HAZELNUTS] = (long)(daily / item_def(ITEM_HAZELNUTS)->nutrition) + 1;
    wanted = gather_zone_wanted(zone, totals, daily);
    CHECK(!(wanted & GATHER_RESOURCE_BIT(GATHER_HAZELNUTS)));
    CHECK(wanted & GATHER_RESOURCE_BIT(GATHER_BLACKBERRIES));
    /* Twice the settlers: half the days. */
    CHECK(gather_zone_wanted(zone, totals, gather_daily_food(4)) &
          GATHER_RESOURCE_BIT(GATHER_HAZELNUTS));
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
    int index = gather_zone_create(&zones, &map, 0, 0, 9, 9, 0);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    /* Bigger trees take longer. */
    float full = gather_job_work_needed(&map, 3, 3, 0, GATHER_JOB_CHOP);
    CHECK(full > gather_job_work_needed(&map, 8, 8, 0, GATHER_JOB_CHOP));
    CHECK(!gather_zone_add_work(&zones, &map, &items, 3, 3, 0, GATHER_JOB_CHOP, full * 0.5f, 60.0,
                                NULL));
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, 3, 3, 0), 0.5, 1e-4);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 3, 0, GATHER_JOB_CHOP, full * 0.5f, 60.0,
                               NULL));
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3, 0)].type, PLANT_NONE);
    /* It fell full length (the map has room for its trunk only: one level,
     * see core/tree.h): its wood lies along the trunk. */
    ItemStack wood = items_get(&items, 3, 3, 0);
    CHECK_EQ_INT(wood.type, ITEM_WOOD);
    CHECK_EQ_INT(items_get(&items, 3, 4, 0).type, ITEM_WOOD);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], plant_def(PLANT_TREE)->cut_yield);
    CHECK(!gather_job_available(&zones, &map, 3, 3, 0, GATHER_JOB_CHOP));
    /* A half-grown tree gives half the wood. */
    CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, 0, GATHER_JOB_CHOP, 1e6f, 60.0, NULL));
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
    int index = gather_zone_create(&zones, &map, 0, 0, 15, 15, 0);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    int wood_hp = item_def(ITEM_WOOD)->max_hit_points;
    const double beginner_level = KNOWLEDGE_MASTERY_LEVEL;
    /* A beginner brings back rough, damaged logs... */
    gather_zone_add_work(&zones, &map, &items, 3, 3, 0, GATHER_JOB_CHOP, 1e6f, beginner_level,
                         NULL);
    ItemStack beginner = items_get(&items, 3, 3, 0);
    CHECK_EQ_INT(beginner.type, ITEM_WOOD);
    CHECK_EQ_INT(beginner.quality,
                 knowledge_quality_of_score(knowledge_product_score(beginner_level, NULL)));
    CHECK(beginner.hit_points < wood_hp);
    /* ...a master superb, intact ones. */
    gather_zone_add_work(&zones, &map, &items, 12, 12, 0, GATHER_JOB_CHOP, 1e6f,
                         KNOWLEDGE_LEVEL_MAX, NULL);
    ItemStack master = items_get(&items, 12, 12, 0);
    CHECK_EQ_INT(master.quality, ITEM_QUALITY_SUPERB);
    CHECK_EQ_INT(master.hit_points, wood_hp);
    free_world(&map, &zones, &items);
}

static void test_branches_twigs_grass_and_nuts(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    Plant *grass = put_plant(&map, 1, 3, PLANT_GRASS, 1.0f);
    Plant *short_grass = put_plant(&map, 3, 3, PLANT_GRASS, 0.4f);
    int index = gather_zone_create(&zones, &map, 0, 0, 4, 4, 0);
    order(&zones, &items, index, GATHER_BRANCHES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_TWIGS, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_GRASS, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_HAZELNUTS, GATHER_FOREVER);

    /* Branches lying on a tile (shed by a tree): all at once, the work growing
     * with their number. */
    int branches = MAP_BRANCHES_MAX;
    REQUIRE(branches > 1);
    for (int i = 0; i < branches; i++) {
        map_add_litter(&map, 1, 1, 0, ITEM_BRANCHES);
    }
    map_add_litter(&map, 1, 1, 0, ITEM_TWIGS);
    float one_branch = gather_job_work_needed(&map, 1, 1, 0, GATHER_JOB_BRANCHES) / (float)branches;
    CHECK(one_branch > 0.0f);
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 1, 1, 0, GATHER_JOB_BRANCHES, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(map_litter(&map, 1, 1, 0, ITEM_BRANCHES), 0);
    CHECK_EQ_INT((int)items.totals[ITEM_BRANCHES], branches);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 1, 0, ALL_JOBS), GATHER_JOB_TWIGS);

    /* Branches and twigs come before the nuts lying on the same tile; the nuts
     * are taken all at once too, the work growing with their number. */
    for (int i = 0; i < MAP_NUTS_MAX; i++) {
        map_add_litter(&map, 3, 1, 0, ITEM_HAZELNUTS);
    }
    map_add_litter(&map, 3, 1, 0, ITEM_BRANCHES);
    map_add_litter(&map, 3, 1, 0, ITEM_TWIGS);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, 0, ALL_JOBS), GATHER_JOB_BRANCHES);
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 3, 1, 0, GATHER_JOB_BRANCHES, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, 0, ALL_JOBS), GATHER_JOB_TWIGS);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 1, 0, GATHER_JOB_TWIGS, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, 0, ALL_JOBS), GATHER_JOB_NUTS);
    CHECK(gather_job_work_needed(&map, 3, 1, 0, GATHER_JOB_NUTS) > 0.0f);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 1, 0, GATHER_JOB_NUTS, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_HAZELNUTS], MAP_NUTS_MAX);
    CHECK_EQ_INT(map_litter(&map, 3, 1, 0, ITEM_HAZELNUTS), 0);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, 0, ALL_JOBS), GATHER_JOB_NONE);

    /* Grass: tall enough only; it grows back from nothing. */
    CHECK(gather_job_available(&zones, &map, 1, 3, 0, GATHER_JOB_CUT_GRASS));
    CHECK(!gather_job_available(&zones, &map, 3, 3, 0, GATHER_JOB_CUT_GRASS));
    CHECK(gather_zone_add_work(&zones, &map, &items, 1, 3, 0, GATHER_JOB_CUT_GRASS, 1e6f, 60.0,
                               NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_FRESH_GRASS], 3);
    CHECK_EQ_INT(grass->type, PLANT_GRASS);
    CHECK_NEAR(grass->growth, 0.0, 1e-6);
    CHECK(!gather_zone_add_work(&zones, &map, &items, 3, 3, 0, GATHER_JOB_CUT_GRASS, 1e6f, 60.0,
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
    int index = gather_zone_create(&zones, &map, 0, 0, 1, 0, 0);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_DO_COUNT);
    gather_zone_set_target(&zones, index, GATHER_COBBLES, 3);
    const GatherZone *zone = gather_zone_get(&zones, index);
    float stone = gather_job_work_needed(&map, 0, 0, 0, GATHER_JOB_STONES);
    for (int i = 0; i < 3; i++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, 0, 0, 0, GATHER_JOB_STONES, stone, 60.0,
                                   NULL));
    }
    /* Done at once, whatever lies in the colony. */
    CHECK_EQ_INT(zone->orders[GATHER_COBBLES].done, 3);
    CHECK_EQ_INT(zone->wanted, 0);
    CHECK_EQ_INT(zones.wanting_cells, 0);
    CHECK(
        !gather_zone_add_work(&zones, &map, &items, 0, 0, 0, GATHER_JOB_STONES, stone, 60.0, NULL));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    /* A higher target: more to do. Another mode starts over. */
    gather_zone_set_target(&zones, index, GATHER_COBBLES, 4);
    gather_zones_update(&zones, items.totals, 0.0f);
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
    map_add_litter(&map, 2, 2, 0, ITEM_TWIGS);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3, 0);
    order(&zones, &items, index, GATHER_TWIGS, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    CHECK(
        !gather_zone_add_work(&zones, &map, &items, 2, 2, 0, GATHER_JOB_TWIGS, 100.0f, 60.0, NULL));
    CHECK(gather_zone_progress_fraction(&zones, &map, 2, 2, 0) > 0.0f);
    /* Work on another job there starts from scratch. */
    CHECK(!gather_zone_add_work(&zones, &map, &items, 2, 2, 0, GATHER_JOB_STONES, 100.0f, 60.0,
                                NULL));
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, 2, 2, 0), 0.5, 1e-4);
    CHECK_NEAR(zones.progress[map_index(&map, 2, 2, 0)], 100.0, 1e-4);
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, -1, 2, 0), 0.0, 1e-6);
    /* Removing the tile forgets the work. */
    gather_zones_remove_rect(&zones, 2, 2, 2, 2, 0);
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, 2, 2, 0), 0.0, 1e-6);
    free_world(&map, &zones, &items);
}

static void test_stones_run_out(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    map_set_stones(&map, 8, 8, 0, 3, 0);
    int index = gather_zone_create(&zones, &map, 8, 8, 10, 8, 0);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    float needed = gather_job_work_needed(&map, 8, 8, 0, GATHER_JOB_STONES);
    for (int i = 0; i < 3; i++) {
        CHECK(gather_job_available(&zones, &map, 8, 8, 0, GATHER_JOB_STONES));
        CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, 0, GATHER_JOB_STONES, needed, 60.0,
                                   NULL));
    }
    CHECK_EQ_INT((int)items.totals[ITEM_COBBLE], 3);
    /* None left: the tile offers nothing more. */
    CHECK_EQ_INT(map_stones(&map, 8, 8, 0, ITEM_COBBLE), 0);
    CHECK(!gather_job_available(&zones, &map, 8, 8, 0, GATHER_JOB_STONES));
    CHECK(!gather_zone_add_work(&zones, &map, &items, 8, 8, 0, GATHER_JOB_STONES, needed, 60.0,
                                NULL));
    /* No stones on sand, nor outside a zone. */
    CHECK(!gather_job_available(&zones, &map, 10, 8, 0, GATHER_JOB_STONES));
    CHECK(gather_job_available(&zones, &map, 9, 8, 0, GATHER_JOB_STONES));
    CHECK(!gather_zone_add_work(&zones, &map, &items, 2, 2, 0, GATHER_JOB_STONES, needed, 60.0,
                                NULL));
    /* A good eye picks good stones. */
    gather_zone_add_work(&zones, &map, &items, 9, 8, 0, GATHER_JOB_STONES, needed,
                         KNOWLEDGE_LEVEL_MAX, NULL);
    CHECK_EQ_INT(items_get(&items, 9, 8, 0).quality, ITEM_QUALITY_SUPERB);
    CHECK(items_get(&items, 8, 8, 0).quality <= ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(map_stones(&map, 9, 8, 0, ITEM_COBBLE), MAP_STONES_MAX - 1);
    free_world(&map, &zones, &items);
}

static void test_map_stones(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 1));
    CHECK_EQ_INT(map_stones(&map, 3, 3, 0, ITEM_COBBLE), 0); /* None on a new map. */
    map_set_stones(&map, 3, 3, 0, 7, 2);
    CHECK_EQ_INT(map_stones(&map, 3, 3, 0, ITEM_COBBLE), 7);
    CHECK_EQ_INT(map_stones(&map, 3, 3, 0, ITEM_FLINT), 2);
    CHECK_EQ_INT(map_stones(&map, 3, 3, 0, ITEM_BRANCHES), 0);
    CHECK_EQ_INT(map_stones(&map, -1, 3, 0, ITEM_COBBLE), 0);
    /* MAP_STONES_MAX together at most: flint first. */
    map_set_stones(&map, 4, 4, 0, 30, 15);
    CHECK_EQ_INT(map_stones(&map, 4, 4, 0, ITEM_FLINT), 15);
    CHECK_EQ_INT(map_stones(&map, 4, 4, 0, ITEM_COBBLE), MAP_STONES_MAX - 15);
    map_set_stones(&map, 4, 4, 0, -3, 99);
    CHECK_EQ_INT(map_stones(&map, 4, 4, 0, ITEM_FLINT), MAP_STONES_MAX);
    CHECK_EQ_INT(map_stones(&map, 4, 4, 0, ITEM_COBBLE), 0);
    map_set_stones(&map, 16, 4, 0, 3, 3); /* Ignored. */
    uint64_t hash = map_hash(&map);
    CHECK(map_take_stone(&map, 3, 3, 0, ITEM_FLINT));
    CHECK(map_hash(&map) != hash);
    CHECK(map_take_stone(&map, 3, 3, 0, ITEM_FLINT));
    CHECK(!map_take_stone(&map, 3, 3, 0, ITEM_FLINT));
    CHECK(!map_take_stone(&map, 3, 3, 0, ITEM_TWIGS));
    CHECK(!map_take_stone(&map, 30, 3, 0, ITEM_COBBLE));
    CHECK_EQ_INT(map_stones(&map, 3, 3, 0, ITEM_COBBLE), 7);
    map_free(&map);
}

static void test_flint_and_cobbles(void)
{
    Map map;
    GatherZones zones;
    Items items;
    Rng rng;
    REQUIRE(init_world(&map, &zones, &items));
    rng_seed(&rng, 7, 1);
    map_set_stones(&map, 8, 8, 0, 10, 10);
    int index = gather_zone_create(&zones, &map, 4, 4, 8, 8, 0);
    order(&zones, &items, index, GATHER_COBBLES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_FLINT, GATHER_FOREVER);
    float stone = gather_job_work_needed(&map, 8, 8, 0, GATHER_JOB_STONES);
    /* Both wanted: drawn among what the tile holds, until it is empty. */
    for (int i = 0; i < 20; i++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, 0, GATHER_JOB_STONES, stone, 60.0,
                                   &rng));
    }
    CHECK_EQ_INT((int)items.totals[ITEM_FLINT], 10);
    CHECK_EQ_INT((int)items.totals[ITEM_COBBLE], 10);
    CHECK(!gather_job_available(&zones, &map, 8, 8, 0, GATHER_JOB_STONES));

    /* Only flint wanted: tiles without flint offer nothing, and the cobbles
     * stay in the ground. */
    map_set_stones(&map, 8, 8, 0, 5, 3);
    gather_zone_set_enabled(&zones, index, GATHER_COBBLES, false);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK(!gather_job_available(&zones, &map, 4, 4, 0, GATHER_JOB_STONES));
    CHECK(gather_job_available(&zones, &map, 8, 8, 0, GATHER_JOB_STONES));
    for (int i = 0; i < 3; i++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, 0, GATHER_JOB_STONES, stone, 60.0,
                                   &rng));
    }
    CHECK_EQ_INT((int)items.totals[ITEM_FLINT], 13);
    CHECK_EQ_INT((int)items.totals[ITEM_COBBLE], 10);
    CHECK_EQ_INT(map_stones(&map, 8, 8, 0, ITEM_COBBLE), 5);
    CHECK(!gather_job_available(&zones, &map, 8, 8, 0, GATHER_JOB_STONES));
    CHECK_EQ_INT(gather_zone_get(&zones, index)->orders[GATHER_FLINT].done, 13);
    /* Without randomness, a cobble is found before flint. */
    map_set_stones(&map, 8, 8, 0, 1, 1);
    gather_zone_set_enabled(&zones, index, GATHER_COBBLES, true);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 8, 8, 0, GATHER_JOB_STONES, stone, 60.0, NULL));
    CHECK_EQ_INT(map_stones(&map, 8, 8, 0, ITEM_COBBLE), 0);
    CHECK_EQ_INT(map_stones(&map, 8, 8, 0, ITEM_FLINT), 1);
    free_world(&map, &zones, &items);
}

static void test_available_resources(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    long available[GATHER_RESOURCE_COUNT];
    gather_zone_available(&zones, &map, 3, available); /* Invalid: all zeros. */
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        CHECK_EQ_INT((int)available[r], 0);
    }
    /* A 3 x 1 zone on soil, gravel and sand. */
    int index = gather_zone_create(&zones, &map, 8, 8, 10, 8, 0);
    map_set_stones(&map, 8, 8, 0, 4, 2);
    map_set_stones(&map, 9, 8, 0, 6, 0);
    map_set_stones(&map, 10, 8, 0, 9, 9); /* Sand: never found. */
    put_plant(&map, 8, 8, PLANT_TREE, 1.0f);
    put_plant(&map, 9, 8, PLANT_TREE, 0.5f);
    /* Branches and twigs lying on the tiles, a shrub's stock too. */
    map_add_litter(&map, 8, 8, 0, ITEM_BRANCHES);
    map_add_litter(&map, 8, 8, 0, ITEM_BRANCHES);
    map_add_litter(&map, 9, 8, 0, ITEM_BRANCHES);
    map_add_litter(&map, 9, 8, 0, ITEM_TWIGS);
    map_add_litter(&map, 10, 8, 0, ITEM_TWIGS);
    map_add_litter(&map, 10, 8, 0, ITEM_TWIGS);
    map_add_litter(&map, 11, 8, 0, ITEM_TWIGS); /* Out of the zone. */
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_COBBLES], 10);
    CHECK_EQ_INT((int)available[GATHER_FLINT], 2);
    CHECK_EQ_INT((int)available[GATHER_BRANCHES], 3);
    CHECK_EQ_INT((int)available[GATHER_TWIGS], 3);
    /* Wood: fully grown trees only by default, all of them without the option. */
    int full_wood = plant_def(PLANT_TREE)->cut_yield;
    int young_wood = (int)((float)full_wood * 0.5f + 0.5f);
    CHECK_EQ_INT((int)available[GATHER_WOOD], full_wood);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_WOOD], full_wood + young_wood);
    CHECK_EQ_INT((int)available[GATHER_BLACKBERRIES], 0);
    CHECK_EQ_INT((int)available[GATHER_GRASS], 0);
    /* What is gathered no longer counts. */
    order(&zones, &items, index, GATHER_FLINT, GATHER_FOREVER);
    float stone = gather_job_work_needed(&map, 8, 8, 0, GATHER_JOB_STONES);
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 8, 8, 0, GATHER_JOB_STONES, stone, 60.0, NULL));
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_FLINT], 1);
    CHECK_EQ_INT((int)available[GATHER_COBBLES], 10);
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
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3, 0);
    order(&zones, &items, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
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
    int index = gather_zone_create(&zones, &map, 0, 0, 2, 2, 0);
    uint64_t created = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    CHECK(created != empty);
    gather_zone_set_target(&zones, index, GATHER_FLINT, 3);
    uint64_t target = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    CHECK(target != created);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    uint64_t ripe = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    CHECK(ripe != target);
    gather_zone_set_priority(&zones, index, 1);
    CHECK(gather_zones_hash(&zones, HASH_FNV1A64_INIT) != ripe);
    free_world(&map, &zones, &items);
}

static void test_overlapping_rectangle_merges_zones(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int a = gather_zones_add_rect(&zones, &map, 0, 0, 2, 2, 0, GATHER_ZONE_NONE, NULL);
    int b = gather_zones_add_rect(&zones, &map, 6, 0, 8, 2, 0, GATHER_ZONE_NONE, NULL);
    REQUIRE(a != GATHER_ZONE_NONE && b != GATHER_ZONE_NONE && a != b);
    order(&zones, &items, a, GATHER_TWIGS, GATHER_FOREVER);
    order(&zones, &items, b, GATHER_COBBLES, GATHER_FOREVER);
    /* Work under way in the younger zone. */
    CHECK(!gather_zone_add_work(&zones, &map, &items, 7, 1, 0, GATHER_JOB_STONES, 100.0f, 60.0,
                                NULL));

    /* Over both: the oldest takes everything, with its orders. */
    CHECK_EQ_INT(gather_zones_add_rect(&zones, &map, 2, 1, 6, 1, 0, GATHER_ZONE_NONE, NULL), a);
    CHECK_EQ_INT(zones.used_count, 1);
    CHECK(gather_zone_get(&zones, b) == NULL);
    const GatherZone *merged = gather_zone_get(&zones, a);
    REQUIRE(merged != NULL);
    CHECK_EQ_INT(merged->cell_count, 9 + 9 + 3);
    CHECK(merged->orders[GATHER_TWIGS].enabled);
    CHECK(!merged->orders[GATHER_COBBLES].enabled);
    CHECK_EQ_INT(gather_zone_at(&zones, 8, 2, 0), a);
    CHECK_NEAR(zones.progress[map_index(&map, 7, 1, 0)], 100.0, 1e-4); /* Kept. */

    /* The selected zone wins and keeps its orders. */
    int c = gather_zones_add_rect(&zones, &map, 0, 12, 1, 13, 0, GATHER_ZONE_NONE, NULL);
    REQUIRE(c != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zones_add_rect(&zones, &map, 1, 2, 1, 12, 0, c, NULL), c);
    CHECK(gather_zone_get(&zones, a) == NULL);
    CHECK(!gather_zone_get(&zones, c)->orders[GATHER_TWIGS].enabled);
    CHECK_EQ_INT(gather_zone_get(&zones, c)->cell_count, 21 + 4 + 9);
    CHECK_EQ_INT(zones.used_count, 1);
    free_world(&map, &zones, &items);
}

static void test_blocked_tiles_are_left_out(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    uint8_t *blocked = calloc(map_tile_count(&map), 1);
    REQUIRE(blocked != NULL);
    blocked[map_index(&map, 1, 1, 0)] = 3;
    /* Several layers: any of them blocks. */
    uint8_t *other = calloc(map_tile_count(&map), 1);
    REQUIRE(other != NULL);
    other[map_index(&map, 2, 2, 0)] = 1;
    const uint8_t *layers[] = {blocked, other, NULL};
    int a = gather_zones_add_rect(&zones, &map, 0, 0, 2, 2, 0, GATHER_ZONE_NONE, layers);
    REQUIRE(a != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_get(&zones, a)->cell_count, 7);
    CHECK_EQ_INT(gather_zone_at(&zones, 1, 1, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(&zones, 2, 2, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zones_add_rect(&zones, &map, 1, 1, 1, 1, 0, GATHER_ZONE_NONE, layers),
                 GATHER_ZONE_NONE);
    CHECK_EQ_INT(zones.used_count, 1);
    free(other);
    free(blocked);
    free_world(&map, &zones, &items);
}

static void test_clearing_plants(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    CHECK_NEAR(gather_clear_work(&map, 1, 1, 0), 0.0, 1e-6); /* Nothing there. */
    CHECK_NEAR(gather_clear_work(&map, -1, 1, 0), 0.0, 1e-6);
    map.plants[map_index(&map, 1, 1, 0)] = (Plant){.type = PLANT_TREE, .growth = 1.0f};
    CHECK_NEAR(gather_clear_work(&map, 1, 1, 0), 0.0, 1e-6); /* Not by hand. */
    CHECK(map_plant_blocks_building(&map, 1, 1, 0));

    /* Grass is cut: fresh grass. */
    map.plants[map_index(&map, 3, 3, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    CHECK(!map_plant_blocks_building(&map, 3, 3, 0));
    CHECK(gather_clear_work(&map, 3, 3, 0) > 0.0f);
    gather_clear_plant(&map, &items, 3, 3, 0, 60.0, NULL);
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3, 0)].type, PLANT_NONE);
    CHECK_EQ_INT((int)items.totals[ITEM_FRESH_GRASS], 3);
    CHECK_NEAR(gather_clear_work(&map, 3, 3, 0), 0.0, 1e-6);

    /* A bramble is pulled up: its berries. */
    Plant *bramble = &map.plants[map_index(&map, 8, 3, 0)];
    *bramble = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    plant_settle(bramble, SEASON_DAY(SEASON_AUTUMN, 2));
    int berries = plant_stock_available(bramble, PLANT_STOCK_FOOD);
    REQUIRE(berries > 0);
    CHECK(gather_clear_work(&map, 8, 3, 0) > gather_clear_work(&map, 3, 3, 0));
    gather_clear_plant(&map, &items, 8, 3, 0, 60.0, NULL);
    CHECK_EQ_INT(bramble->type, PLANT_NONE);
    CHECK_EQ_INT((int)items.totals[ITEM_BLACKBERRIES], berries);
    /* A hazel is a tree: not cleared by hand. */
    map.plants[map_index(&map, 8, 5, 0)] = (Plant){.type = PLANT_HAZEL, .growth = 1.0f};
    CHECK_NEAR(gather_clear_work(&map, 8, 5, 0), 0.0, 1e-6);
    CHECK(map_plant_blocks_building(&map, 8, 5, 0));

    /* Wild garlic withered underground: nothing in the way. */
    Plant *garlic = &map.plants[map_index(&map, 9, 3, 0)];
    *garlic = (Plant){.type = PLANT_WILD_GARLIC, .growth = 1.0f};
    plant_settle(garlic, GAME_START_DAY);
    REQUIRE(plant_is_dormant(garlic));
    CHECK_NEAR(gather_clear_work(&map, 9, 3, 0), 0.0, 1e-6);
    free_world(&map, &zones, &items);
}

/* The box of the zone tiles holds them all, and empties with the last zone. */
static void test_zone_tiles_box(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    CHECK(tile_box_is_empty(&zones.box));
    int a = gather_zone_create(&zones, &map, 1, 1, 2, 2, 0);
    int b = gather_zone_create(&zones, &map, 10, 12, 12, 13, 0);
    REQUIRE(a != GATHER_ZONE_NONE && b != GATHER_ZONE_NONE);
    CHECK(tile_box_contains(&zones.box, 1, 1, 0));
    CHECK(tile_box_contains(&zones.box, 12, 13, 0));
    CHECK(!tile_box_contains(&zones.box, 0, 1, 0));
    CHECK(!tile_box_contains(&zones.box, 13, 13, 0));
    gather_zone_delete(&zones, a);
    CHECK(tile_box_contains(&zones.box, 12, 13, 0));
    CHECK_EQ_INT(gather_zones_remove_rect(&zones, 0, 0, 15, 15, 0), 6);
    CHECK(tile_box_is_empty(&zones.box));
    REQUIRE(gather_zone_create(&zones, &map, 7, 7, 7, 7, 0) != GATHER_ZONE_NONE);
    CHECK(tile_box_contains(&zones.box, 7, 7, 0));
    CHECK(!tile_box_contains(&zones.box, 12, 13, 0));
    free_world(&map, &zones, &items);
}

/* A single plant, for the Harvest and Uproot orders: what a harvest takes and
 * from which plants, and uprooting harvests before the plant goes. */
static void test_single_plant_harvest_and_uproot(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int autumn = SEASON_DAY(SEASON_AUTUMN, 4);
    Plant *bramble = put_plant(&map, 1, 1, PLANT_BRAMBLE, 1.0f);
    Plant *cattail = put_plant(&map, 2, 1, PLANT_CATTAIL, 1.0f);
    Plant *mushrooms = put_plant(&map, 3, 1, PLANT_MUSHROOMS, 1.0f);
    Plant *grass = put_plant(&map, 4, 1, PLANT_GRASS, 1.0f);
    put_plant(&map, 6, 6, PLANT_TREE, 1.0f);
    plant_settle(bramble, autumn);
    plant_settle(mushrooms, autumn);
    /* Which job, on which plant. */
    CHECK_EQ_INT(gather_plant_harvest_job(&map, 1, 1, 0), GATHER_JOB_HARVEST);
    CHECK_EQ_INT(gather_plant_harvest_job(&map, 2, 1, 0), GATHER_JOB_DIG);
    CHECK_EQ_INT(gather_plant_harvest_job(&map, 3, 1, 0), GATHER_JOB_HARVEST_CAREFULLY);
    CHECK_EQ_INT(gather_plant_harvest_job(&map, 4, 1, 0), GATHER_JOB_CUT_GRASS);
    CHECK_EQ_INT(gather_plant_harvest_job(&map, 6, 6, 0), GATHER_JOB_NONE); /* Felled instead. */
    CHECK_EQ_INT(gather_plant_harvest_job(&map, 8, 8, 0), GATHER_JOB_NONE);
    CHECK(gather_plant_uprootable(&map, 1, 1, 0));
    CHECK(gather_plant_uprootable(&map, 4, 1, 0));
    CHECK(!gather_plant_uprootable(&map, 6, 6, 0));
    CHECK(!gather_plant_uprootable(&map, 8, 8, 0));
    /* A harvest takes the food, the bramble stays and has nothing left. */
    int berries = plant_stock_available(bramble, PLANT_STOCK_FOOD);
    REQUIRE(berries > 0);
    CHECK(gather_plant_harvestable(&map, 1, 1, 0));
    CHECK(gather_harvest_plant(&map, &items, 1, 1, 0, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_BLACKBERRIES], berries);
    CHECK_EQ_INT(bramble->type, PLANT_BRAMBLE);
    CHECK(!gather_plant_harvestable(&map, 1, 1, 0));
    CHECK(!gather_harvest_plant(&map, &items, 1, 1, 0, 60.0, NULL));
    /* Grass cut grows again. */
    CHECK(gather_harvest_plant(&map, &items, 4, 1, 0, 60.0, NULL));
    CHECK(items.totals[ITEM_FRESH_GRASS] > 0);
    CHECK_EQ_INT(grass->type, PLANT_GRASS);
    CHECK(!gather_plant_harvestable(&map, 4, 1, 0));
    /* Uprooting the cattail: its rhizomes first, then it is gone. */
    int rhizomes = plant_stock_available(cattail, PLANT_STOCK_FOOD);
    REQUIRE(rhizomes > 0);
    CHECK(gather_uproot_work(&map, 2, 1, 0) > gather_clear_work(&map, 2, 1, 0));
    gather_clear_plant(&map, &items, 2, 1, 0, 60.0, NULL);
    CHECK_EQ_INT((int)items.totals[ITEM_CATTAIL_RHIZOMES], rhizomes);
    CHECK_EQ_INT(cattail->type, PLANT_NONE);
    /* Even a bare plant takes some work to pull up. */
    CHECK(gather_uproot_work(&map, 1, 1, 0) > 0.0f);
    CHECK_NEAR(gather_uproot_work(&map, 6, 6, 0), 0.0f, 1e-6);
    free_world(&map, &zones, &items);
}

int main(void)
{
    RUN_TEST(test_single_plant_harvest_and_uproot);
    RUN_TEST(test_init_rejects_invalid_sizes);
    RUN_TEST(test_definitions);
    RUN_TEST(test_where_zones_and_stones_lie);
    RUN_TEST(test_zone_tiles);
    RUN_TEST(test_order_settings);
    RUN_TEST(test_zone_priority);
    RUN_TEST(test_wanted_resources);
    RUN_TEST(test_tile_jobs_in_order);
    RUN_TEST(test_fully_grown_plants_only);
    RUN_TEST(test_full_harvest);
    RUN_TEST(test_group_orders);
    RUN_TEST(test_days_of_food);
    RUN_TEST(test_food_in_season);
    RUN_TEST(test_felling_a_tree);
    RUN_TEST(test_products_depend_on_level);
    RUN_TEST(test_branches_twigs_grass_and_nuts);
    RUN_TEST(test_fixed_count_stops);
    RUN_TEST(test_progress_belongs_to_one_job);
    RUN_TEST(test_stones_run_out);
    RUN_TEST(test_map_stones);
    RUN_TEST(test_flint_and_cobbles);
    RUN_TEST(test_available_resources);
    RUN_TEST(test_plants_growing_wake_settlers);
    RUN_TEST(test_hash);
    RUN_TEST(test_overlapping_rectangle_merges_zones);
    RUN_TEST(test_blocked_tiles_are_left_out);
    RUN_TEST(test_clearing_plants);
    RUN_TEST(test_zone_tiles_box);
    return test_summary();
}
