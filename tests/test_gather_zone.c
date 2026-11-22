/* Gathering zones: tiles, orders, what they want, the jobs tiles offer and
 * what they give. */
#include "core/colonist.h"
#include "core/crown.h"
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
    if (!map_init(map, 16, 16, 1) || !gather_zones_init(zones, 16, 16, 1) ||
        !items_init(items, 16, 16, 1)) {
        return false;
    }
    map->rock[map_index(map, 5, 5, 0)] = ROCK_GRANITE;
    map->terrain[map_index(map, 9, 8, 0)] = TERRAIN_GRAVEL;
    map->water[map_index(map, 9, 8, 0)] = 0;
    map->terrain[map_index(map, 10, 8, 0)] = TERRAIN_SAND;
    map->water[map_index(map, 11, 8, 0)] = WATER_SHALLOWS_MM;
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
        CHECK(item_types_has(gather_resource_counted(def), def->item));
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
    /* Plants may wait for the full harvest. */
    CHECK(gather_resource_def(GATHER_WOOD)->ripe_option);
    CHECK(gather_resource_def(GATHER_BLACKBERRIES)->ripe_option);
    CHECK(gather_resource_def(GATHER_GRASS)->ripe_option);
    CHECK(gather_ripe_options() & GATHER_RESOURCE_BIT(GATHER_GRASS));
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
    CHECK_EQ_INT(gather_resource_group(GATHER_GRASS), GATHER_GROUP_FIBRES);
    CHECK_EQ_INT(gather_resource_group(GATHER_WOOD), GATHER_GROUP_WOOD);
    CHECK_EQ_INT(gather_resource_group(GATHER_BLACKBERRIES), GATHER_GROUP_FOOD);
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
        const PlantProduct *food = plant_product((PlantType)p, plant_food_slot((PlantType)p));
        if (food == NULL) {
            continue;
        }
        GatherResource resource = GATHER_RESOURCE_COUNT;
        for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
            if (gather_resource_def((GatherResource)r)->item == food->item) {
                resource = (GatherResource)r;
            }
        }
        REQUIRE(resource != GATHER_RESOURCE_COUNT);
        GatherJob job = gather_resource_def(resource)->job;
        GatherJob expected =
            food->dug ? (food->confusable ? GATHER_JOB_DIG_CAREFULLY : GATHER_JOB_DIG)
                      : (food->confusable ? GATHER_JOB_HARVEST_CAREFULLY : GATHER_JOB_HARVEST);
        CHECK_EQ_INT(job, expected);
    }
    /* Every product of every plant (fibres too) is the resource of a job that
     * takes it from the plant, in a group of its own: nothing grows for nothing. */
    for (int p = PLANT_NONE + 1; p < PLANT_COUNT; p++) {
        for (int slot = 0; slot < PLANT_PRODUCTS_MAX; slot++) {
            ItemType item = plant_stock_item((PlantType)p, slot);
            if (item == ITEM_NONE) {
                continue;
            }
            bool found = false;
            for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
                found = found || gather_resource_def((GatherResource)r)->item == item;
            }
            CHECK(found);
        }
    }
    CHECK(gather_job_def(GATHER_JOB_CUT_GRASS)->resources & GATHER_RESOURCE_BIT(GATHER_REEDS));
    CHECK_EQ_INT(gather_resource_group(GATHER_BRACKEN), GATHER_GROUP_FIBRES);
    CHECK(!gather_group_def(GATHER_GROUP_FIBRES)->food);
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
    CHECK(!gather_job_doable_at(GATHER_JOB_HARVEST, KNOWLEDGE_MASTERY_LEVEL - 0.1));
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
    map.water[map_index(&map, 12, 8, 0)] = WATER_DEEP_MM;
    CHECK(!gather_zone_can_cover(&map, 12, 8, 0));
    CHECK(!gather_zone_can_cover(&map, 5, 5, 0));
    CHECK(!gather_zone_can_cover(&map, -1, 0, 0));
    map.terrain[map_index(&map, 13, 8, 0)] = TERRAIN_OPEN; /* No floor. */
    map.water[map_index(&map, 13, 8, 0)] = 0;
    CHECK(!gather_zone_can_cover(&map, 13, 8, 0));
    map.terrain[map_index(&map, 13, 8, 0)] = TERRAIN_SOIL;
    map.water[map_index(&map, 13, 8, 0)] = 0;
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
        /* The food resources go on forever: the group's days of food limit them. */
        CHECK_EQ_INT(zone->orders[r].mode,
                     gather_resource_group((GatherResource)r) == GATHER_GROUP_FOOD ? GATHER_FOREVER
                                                                                   : GATHER_UNTIL);
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
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_FIBRES].mode, GATHER_FOREVER);
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
    const GatherOrder *grass = &zone->orders[GATHER_GRASS];
    uint32_t revision = zones.revision;
    gather_zone_set_enabled(&zones, index, GATHER_GRASS, true);
    CHECK(grass->enabled);
    CHECK(zones.revision != revision);
    gather_zone_set_target(&zones, index, GATHER_GRASS, 0);
    CHECK_EQ_INT(grass->target, GATHER_TARGET_MIN);
    gather_zone_set_target(&zones, index, GATHER_GRASS, 100000);
    CHECK_EQ_INT(grass->target, GATHER_TARGET_MAX);
    gather_zone_set_mode(&zones, index, GATHER_GRASS, GATHER_MODE_COUNT);
    CHECK_EQ_INT(grass->mode, GATHER_UNTIL);
    gather_zone_set_ripe_only(&zones, index, GATHER_GRASS, true);
    CHECK(grass->ripe_only);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    CHECK(!zone->orders[GATHER_WOOD].ripe_only);
    /* Days of food for food only; a new unit comes with its default target. */
    gather_zone_set_unit(&zones, index, GATHER_GRASS, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(grass->unit, GATHER_UNIT_ITEMS);
    gather_zone_set_unit(&zones, index, GATHER_ROSE_HIPS, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(zone->orders[GATHER_ROSE_HIPS].unit, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(zone->orders[GATHER_ROSE_HIPS].target, GATHER_FOOD_DAYS_DEFAULT);
    gather_zone_set_unit(&zones, index, GATHER_ROSE_HIPS, GATHER_UNIT_COUNT);
    CHECK_EQ_INT(zone->orders[GATHER_ROSE_HIPS].unit, GATHER_UNIT_FOOD_DAYS);
    gather_zone_set_unit(&zones, index, GATHER_ROSE_HIPS, GATHER_UNIT_ITEMS);
    CHECK_EQ_INT(zone->orders[GATHER_ROSE_HIPS].target, GATHER_TARGET_DEFAULT);
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
    gather_zone_set_enabled(&zones, 7, GATHER_GRASS, true);
    gather_zone_set_enabled(&zones, index, GATHER_RESOURCE_COUNT, true);
    gather_zone_set_target(&zones, -1, GATHER_GRASS, 5);
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
    order(&zones, &items, a, GATHER_GRASS, GATHER_FOREVER);
    CHECK_EQ_INT(gather_zones_wanting_priorities(&zones), 1u << GATHER_ZONE_PRIORITY_LOWEST);
    order(&zones, &items, b, GATHER_GRASS, GATHER_FOREVER);
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
    /* Until the colony has 10 armfuls of grass, fresh or dry. */
    order(&zones, &items, index, GATHER_GRASS, GATHER_UNTIL);
    gather_zone_set_target(&zones, index, GATHER_GRASS, 10);
    items_add(&items, 12, 12, 0, item_stack_pristine(ITEM_GRASS, 6));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_GRASS));
    CHECK_EQ_INT(zones.wanting_cells, 9);
    CHECK_EQ_INT((int)gather_resource_count(GATHER_GRASS, items.totals), 6);
    items_add(&items, 13, 12, 0, item_stack_pristine(ITEM_GRASS, 4));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    CHECK_EQ_INT(zones.wanting_cells, 0);
    /* Forever: whatever lies in the colony. */
    order(&zones, &items, index, GATHER_MOSS, GATHER_FOREVER);
    items_add(&items, 14, 12, 0, item_stack_pristine(ITEM_MOSS, 50));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_MOSS));
    /* A fixed count: what this order gathered, not what lies about. */
    order(&zones, &items, index, GATHER_BRACKEN, GATHER_DO_COUNT);
    items_add(&items, 15, 12, 0, item_stack_pristine(ITEM_BRACKEN, 50));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK(zone->wanted & GATHER_RESOURCE_BIT(GATHER_BRACKEN));
    /* Turned off: never wanted. */
    gather_zone_set_enabled(&zones, index, GATHER_MOSS, false);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_BRACKEN));
    free_world(&map, &zones, &items);
}

static void test_tile_jobs_in_order(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 2, 2, PLANT_OAK, 1.0f);
    map_add_litter(&map, 2, 2, 0, ITEM_HAZELNUTS); /* No gathering: for the haulers. */
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3, 0);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, 0, ALL_JOBS), GATHER_JOB_NONE);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    GatherJobMask no_chop = ALL_JOBS & ~GATHER_JOB_BIT(GATHER_JOB_CHOP);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, 0, ALL_JOBS), GATHER_JOB_CHOP);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 2, 2, 0, no_chop), GATHER_JOB_NONE);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 1, 1, 0, ALL_JOBS), GATHER_JOB_NONE);
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
    put_plant(&map, 1, 1, PLANT_OAK, 0.4f);
    const PlantDef *def = plant_def(PLANT_BRAMBLE);
    Plant *young = put_plant(&map, 2, 1, PLANT_BRAMBLE, def->products[0].min_growth - 0.01f);
    put_plant(&map, 3, 1, PLANT_BRAMBLE, def->products[0].min_growth);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3, 0);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    /* By default, young trees not fully grown are left alone. */
    CHECK(!gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_CHOP));
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    CHECK(gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_CHOP));
    /* A bramble too young bears no fruit; an older one does. */
    CHECK(!gather_job_available(&zones, &map, 2, 1, 0, GATHER_JOB_HARVEST));
    CHECK(plant_stock_available(young, 0) == 0);
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
    plant_take_stock(bramble, 0, 1000); /* Not in fruit yet. */
    Plant *cattail = put_plant(&map, 2, 1, PLANT_CATTAIL, 1.0f);
    Plant *carrot = put_plant(&map, 3, 1, PLANT_WILD_CARROT, 1.0f);
    Plant *mushrooms = put_plant(&map, 1, 3, PLANT_MUSHROOMS, 1.0f);
    map.water[map_index(&map, 2, 1, 0)] = WATER_SHALLOWS_MM; /* Cattails stand in water. */
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
    int berries = plant_stock_available(bramble, 0);
    REQUIRE(berries == plant_def(PLANT_BRAMBLE)->products[0].capacity);
    CHECK(gather_job_work_needed(&map, 1, 1, 0, GATHER_JOB_HARVEST) >
          gather_job_work_needed(&map, 1, 3, 0, GATHER_JOB_HARVEST_CAREFULLY));
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 1, 1, 0, GATHER_JOB_HARVEST, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_BLACKBERRIES], berries);
    CHECK_EQ_INT(bramble->type, PLANT_BRAMBLE);
    CHECK_NEAR(bramble->growth, 1.0f, 1e-6);
    /* Rhizomes: part of the rootstock, the cattail grows on. */
    int rhizomes = plant_stock_available(cattail, 0);
    CHECK(gather_zone_add_work(&zones, &map, &items, 2, 1, 0, GATHER_JOB_DIG, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_CATTAIL_RHIZOMES], rhizomes);
    CHECK_NEAR(cattail->growth, 1.0f, 1e-6);
    /* A wild carrot dug up is gone, but grows again from what is left. */
    int roots = plant_stock_available(carrot, 0);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 1, 0, GATHER_JOB_DIG_CAREFULLY, 1e6f, 60.0,
                               NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_WILD_CARROTS], roots);
    CHECK_EQ_INT(carrot->type, PLANT_WILD_CARROT);
    CHECK_NEAR(carrot->growth, 0.0f, 1e-6);
    /* Mushrooms picked: the patch is gone. */
    CHECK(gather_zone_add_work(&zones, &map, &items, 1, 3, 0, GATHER_JOB_HARVEST_CAREFULLY, 1e6f,
                               60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_MUSHROOMS],
                 plant_def(PLANT_MUSHROOMS)->products[0].capacity);
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
    Plant *young = put_plant(&map, 3, 1, PLANT_BRAMBLE, bramble_def->products[0].min_growth);
    Plant *bramble = put_plant(&map, 5, 1, PLANT_BRAMBLE, 1.0f);
    put_plant(&map, 1, 3, PLANT_GRASS, 0.8f);
    put_plant(&map, 3, 3, PLANT_GRASS, 1.0f);
    int index = gather_zone_create(&zones, &map, 0, 0, 6, 4, 0);
    order(&zones, &items, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_GRASS, GATHER_FOREVER);
    gather_zone_set_all_ripe_only(&zones, index, true);
    const GatherZone *zone = gather_zone_get(&zones, index);
    CHECK_EQ_INT(gather_zone_ripe_mask(zone), gather_ripe_options());
    /* Full stocks: the young bramble's few berries are all ripe. */
    REQUIRE(plant_stock_available(young, 0) > 0);
    CHECK(gather_job_available(&zones, &map, 3, 1, 0, GATHER_JOB_HARVEST));
    CHECK(gather_job_available(&zones, &map, 5, 1, 0, GATHER_JOB_HARVEST));
    /* Some taken: what is left waits until the stock is full again. */
    CHECK_EQ_INT(plant_take_stock(bramble, 0, 1), 1);
    CHECK(!gather_job_available(&zones, &map, 5, 1, 0, GATHER_JOB_HARVEST));
    long available[GATHER_RESOURCE_COUNT];
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_BLACKBERRIES], plant_stock_available(young, 0));
    /* Grass fully grown only. */
    CHECK(!gather_job_available(&zones, &map, 1, 3, 0, GATHER_JOB_CUT_GRASS));
    CHECK(gather_job_available(&zones, &map, 3, 3, 0, GATHER_JOB_CUT_GRASS));
    /* Without the option, whatever there is. */
    gather_zone_set_group_ripe_only(&zones, index, GATHER_GROUP_FOOD, false);
    CHECK(gather_job_available(&zones, &map, 5, 1, 0, GATHER_JOB_HARVEST));
    CHECK(!(gather_zone_ripe_mask(zone) & GATHER_RESOURCE_BIT(GATHER_BLACKBERRIES)));
    CHECK(gather_zone_ripe_mask(zone) & GATHER_RESOURCE_BIT(GATHER_GRASS));
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
    const Plant *grass = put_plant(&map, 0, 0, PLANT_GRASS, 1.0f);
    put_plant(&map, 1, 0, PLANT_GRASS, 1.0f);
    int index = gather_zone_create(&zones, &map, 0, 0, 1, 0, 0);
    const GatherZone *zone = gather_zone_get(&zones, index);
    GatherResourceMask fibres = gather_group_def(GATHER_GROUP_FIBRES)->members;
    GatherResourceMask two = GATHER_RESOURCE_BIT(GATHER_GRASS) | GATHER_RESOURCE_BIT(GATHER_MOSS);
    /* Turning a group on turns each of its resources on. */
    gather_zone_set_group_enabled(&zones, index, GATHER_GROUP_FIBRES, true);
    CHECK_EQ_INT(gather_zone_enabled_mask(zone), fibres);
    gather_zone_set_enabled(&zones, index, GATHER_BRACKEN, false);
    gather_zone_set_enabled(&zones, index, GATHER_HEATHER, false);
    gather_zone_set_enabled(&zones, index, GATHER_REEDS, false);
    gather_zone_set_mode(&zones, index, GATHER_GRASS, GATHER_FOREVER);
    gather_zone_set_mode(&zones, index, GATHER_MOSS, GATHER_FOREVER);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, two);
    /* The group until 3 armfuls in the colony: grass and moss both count. */
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_FIBRES, GATHER_UNTIL);
    gather_zone_set_group_target(&zones, index, GATHER_GROUP_FIBRES, 3);
    items_add(&items, 10, 10, 0, item_stack_pristine(ITEM_GRASS, 2));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, two);
    items_add(&items, 11, 10, 0, item_stack_pristine(ITEM_MOSS, 1));
    CHECK_EQ_INT((int)gather_group_count(GATHER_GROUP_FIBRES, items.totals), 3);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    /* The group gathers 2 armfuls, then stops, whatever its resources say. */
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_FIBRES, GATHER_DO_COUNT);
    gather_zone_set_group_target(&zones, index, GATHER_GROUP_FIBRES, 2);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, two);
    int armfuls = plant_stock_available(grass, 0);
    REQUIRE(armfuls >= 2);
    float cut = gather_job_work_needed(&map, 0, 0, 0, GATHER_JOB_CUT_GRASS);
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 0, 0, 0, GATHER_JOB_CUT_GRASS, cut, 60.0, NULL));
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_FIBRES].done, armfuls);
    CHECK_EQ_INT(zone->wanted, 0);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    /* Its own order limits a resource as well. */
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_FIBRES, GATHER_FOREVER);
    gather_zone_set_mode(&zones, index, GATHER_GRASS, GATHER_UNTIL);
    gather_zone_set_target(&zones, index, GATHER_GRASS, 1);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_MOSS));
    /* Turning the group off turns each of them off. */
    gather_zone_set_group_enabled(&zones, index, GATHER_GROUP_FIBRES, false);
    CHECK_EQ_INT(gather_zone_enabled_mask(zone), 0);
    gather_zone_set_group_unit(&zones, index, GATHER_GROUP_FIBRES, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_FIBRES].unit, GATHER_UNIT_ITEMS);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_WOOD));
    /* Invalid groups are ignored. */
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_NONE, GATHER_DO_COUNT);
    gather_zone_set_group_enabled(&zones, 9, GATHER_GROUP_WOOD, true);
    free_world(&map, &zones, &items);
}

/* "Apply to all": each resource of a group takes the group's mode, unit and target. */
static void test_apply_the_group_order_to_its_resources(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int index = gather_zone_create(&zones, &map, 0, 0, 1, 0, 0);
    const GatherZone *zone = gather_zone_get(&zones, index);
    gather_zone_set_group_enabled(&zones, index, GATHER_GROUP_FOOD, true);
    gather_zone_set_enabled(&zones, index, GATHER_MUSHROOMS, false);
    gather_zone_set_ripe_only(&zones, index, GATHER_BLACKBERRIES, true);
    gather_zone_set_mode(&zones, index, GATHER_ROSE_HIPS, GATHER_FOREVER);
    gather_zone_set_group_mode(&zones, index, GATHER_GROUP_FOOD, GATHER_UNTIL);
    gather_zone_set_group_unit(&zones, index, GATHER_GROUP_FOOD, GATHER_UNIT_ITEMS);
    gather_zone_set_group_target(&zones, index, GATHER_GROUP_FOOD, 42);
    gather_zone_apply_group_order(&zones, index, GATHER_GROUP_FOOD);
    GatherResourceMask food = gather_group_def(GATHER_GROUP_FOOD)->members;
    for (int r = 0; r < GATHER_RESOURCE_COUNT; r++) {
        if (food & GATHER_RESOURCE_BIT(r)) {
            CHECK_EQ_INT(zone->orders[r].mode, GATHER_UNTIL);
            CHECK_EQ_INT(zone->orders[r].unit, GATHER_UNIT_ITEMS);
            CHECK_EQ_INT(zone->orders[r].target, 42);
        }
    }
    /* What is on or off, and the full harvest, stay. */
    CHECK(!zone->orders[GATHER_MUSHROOMS].enabled);
    CHECK(zone->orders[GATHER_BLACKBERRIES].ripe_only);
    /* Days of food go to the food resources, which may count them. */
    gather_zone_set_group_unit(&zones, index, GATHER_GROUP_FOOD, GATHER_UNIT_FOOD_DAYS);
    gather_zone_set_group_target(&zones, index, GATHER_GROUP_FOOD, 7);
    gather_zone_apply_group_order(&zones, index, GATHER_GROUP_FOOD);
    CHECK_EQ_INT(zone->orders[GATHER_ROSE_HIPS].unit, GATHER_UNIT_FOOD_DAYS);
    CHECK_EQ_INT(zone->orders[GATHER_ROSE_HIPS].target, 7);
    /* Resources of the other groups are left alone. */
    CHECK(zone->orders[GATHER_GRASS].mode != GATHER_UNTIL ||
          zone->orders[GATHER_GRASS].target != 7);
    gather_zone_apply_group_order(&zones, index, GATHER_GROUP_NONE); /* Ignored. */
    gather_zone_apply_group_order(&zones, 9, GATHER_GROUP_FOOD);
    free_world(&map, &zones, &items);
}

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
    CHECK_EQ_INT((int)gather_food_days(item_types_of(ITEM_BLACKBERRIES), totals, daily), 3);
    CHECK_EQ_INT((int)gather_resource_food_days(GATHER_BLACKBERRIES, totals, daily), 3);
    CHECK_EQ_INT((int)gather_resource_food_days(GATHER_GRASS, totals, daily), 0);
    CHECK_EQ_INT((int)gather_group_food_days(GATHER_GROUP_FOOD, totals, daily), 3);
    CHECK_EQ_INT((int)gather_group_food_days(GATHER_GROUP_WOOD, totals, daily), 0);
    CHECK_EQ_INT((int)gather_food_days(item_types_of(ITEM_BLACKBERRIES), totals, 0.0f), 0);
    /* Cooked food counts for the group, not for a resource. */
    totals[ITEM_ROASTED_CARROTS] = (long)(2.0f * daily / roasted) + 1;
    CHECK_EQ_INT((int)gather_group_food_days(GATHER_GROUP_FOOD, totals, daily), 5);
    CHECK_EQ_INT((int)gather_resource_food_days(GATHER_WILD_CARROTS, totals, daily), 0);

    int index = gather_zone_create(&zones, &map, 0, 0, 1, 0, 0);
    const GatherZone *zone = gather_zone_get(&zones, index);
    gather_zone_set_group_enabled(&zones, index, GATHER_GROUP_FOOD, true);
    gather_zone_set_mode(&zones, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    gather_zone_set_mode(&zones, index, GATHER_ROSE_HIPS, GATHER_UNTIL);
    gather_zone_set_unit(&zones, index, GATHER_ROSE_HIPS, GATHER_UNIT_FOOD_DAYS);
    gather_zone_set_target(&zones, index, GATHER_ROSE_HIPS, 1);
    /* 5 days of food: the group (until 5 days by default) stops. */
    CHECK_EQ_INT(gather_zone_wanted(zone, totals, daily) & GATHER_RESOURCE_BIT(GATHER_BLACKBERRIES),
                 0);
    /* Below 5 days again: it starts over at once. */
    totals[ITEM_ROASTED_CARROTS] = 0;
    GatherResourceMask wanted = gather_zone_wanted(zone, totals, daily);
    CHECK(wanted & GATHER_RESOURCE_BIT(GATHER_BLACKBERRIES));
    /* No rose hips stored: less than a day of them. */
    CHECK(wanted & GATHER_RESOURCE_BIT(GATHER_ROSE_HIPS));
    totals[ITEM_ROSE_HIPS] = (long)(daily / item_def(ITEM_ROSE_HIPS)->nutrition) + 1;
    wanted = gather_zone_wanted(zone, totals, daily);
    CHECK(!(wanted & GATHER_RESOURCE_BIT(GATHER_ROSE_HIPS)));
    CHECK(wanted & GATHER_RESOURCE_BIT(GATHER_BLACKBERRIES));
    /* Twice the settlers: half the days. */
    CHECK(gather_zone_wanted(zone, totals, gather_daily_food(4)) &
          GATHER_RESOURCE_BIT(GATHER_ROSE_HIPS));
    free_world(&map, &zones, &items);
}

static void test_felling_a_tree(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 3, 3, PLANT_OAK, 1.0f);
    put_plant(&map, 8, 8, PLANT_OAK, 0.5f);
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
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], plant_def(PLANT_OAK)->cut_yield);
    CHECK(!gather_job_available(&zones, &map, 3, 3, 0, GATHER_JOB_CHOP));
    /* A half-grown tree gives half the wood. */
    CHECK(gather_zone_add_work(&zones, &map, &items, 8, 8, 0, GATHER_JOB_CHOP, 1e6f, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD],
                 plant_def(PLANT_OAK)->cut_yield + (plant_def(PLANT_OAK)->cut_yield + 1) / 2);
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
    put_plant(&map, 3, 3, PLANT_OAK, 1.0f);
    put_plant(&map, 12, 12, PLANT_OAK, 1.0f);
    int index = gather_zone_create(&zones, &map, 0, 0, 15, 15, 0);
    order(&zones, &items, index, GATHER_WOOD, GATHER_FOREVER);
    int wood_hp = item_def(ITEM_WOOD)->max_hit_points;
    const double beginner_level = KNOWLEDGE_MASTERY_LEVEL;
    /* A beginner brings back rough, damaged logs... */
    gather_zone_add_work(&zones, &map, &items, 3, 3, 0, GATHER_JOB_CHOP, 1e6f, beginner_level,
                         NULL);
    ItemStack beginner = items_get(&items, 3, 3, 0);
    CHECK_EQ_INT(beginner.type, ITEM_WOOD);
    CHECK_EQ_INT(beginner.quality, knowledge_product_score(beginner_level, NULL));
    CHECK(beginner.hit_points < wood_hp);
    /* ...a master superb, intact ones. */
    gather_zone_add_work(&zones, &map, &items, 12, 12, 0, GATHER_JOB_CHOP, 1e6f,
                         KNOWLEDGE_LEVEL_MAX, NULL);
    ItemStack master = items_get(&items, 12, 12, 0);
    CHECK_EQ_INT(master.quality, knowledge_product_score(KNOWLEDGE_LEVEL_MAX, NULL));
    CHECK_EQ_INT(master.hit_points, wood_hp);
    free_world(&map, &zones, &items);
}

static void test_grass(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    Plant *grass = put_plant(&map, 1, 3, PLANT_GRASS, 1.0f);
    Plant *short_grass = put_plant(&map, 3, 3, PLANT_GRASS, 0.4f);
    int index = gather_zone_create(&zones, &map, 0, 0, 4, 4, 0);
    order(&zones, &items, index, GATHER_GRASS, GATHER_FOREVER);
    /* Nuts lying about are no gathering: items for the haulers. */
    map_add_litter(&map, 3, 1, 0, ITEM_HAZELNUTS);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 3, 1, 0, ALL_JOBS), GATHER_JOB_NONE);

    /* Grass: tall enough only; it grows back from nothing. */
    CHECK(gather_job_available(&zones, &map, 1, 3, 0, GATHER_JOB_CUT_GRASS));
    CHECK(!gather_job_available(&zones, &map, 3, 3, 0, GATHER_JOB_CUT_GRASS));
    CHECK(gather_zone_add_work(&zones, &map, &items, 1, 3, 0, GATHER_JOB_CUT_GRASS, 1e6f, 60.0,
                               NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_GRASS], 3);
    CHECK_EQ_INT(grass->type, PLANT_GRASS);
    CHECK_NEAR(grass->growth, 0.0, 1e-6);
    CHECK(!gather_zone_add_work(&zones, &map, &items, 3, 3, 0, GATHER_JOB_CUT_GRASS, 1e6f, 60.0,
                                NULL));
    CHECK_NEAR(short_grass->growth, 0.4, 1e-6);
    free_world(&map, &zones, &items);
}

/* A bramble at (x, 0) bearing a single handful of blackberries. */
static void put_one_handful(Map *map, int x)
{
    Plant *bramble = put_plant(map, x, 0, PLANT_BRAMBLE, 1.0f);
    int available = plant_stock_available(bramble, 0);
    REQUIRE(available > 1);
    plant_take_stock(bramble, 0, available - 1);
    REQUIRE(plant_stock_available(bramble, 0) == 1);
}

static void test_fixed_count_stops(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    for (int x = 0; x < 4; x++) {
        put_one_handful(&map, x);
    }
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 0, 0);
    order(&zones, &items, index, GATHER_BLACKBERRIES, GATHER_DO_COUNT);
    gather_zone_set_target(&zones, index, GATHER_BLACKBERRIES, 3);
    const GatherZone *zone = gather_zone_get(&zones, index);
    float work = gather_job_work_needed(&map, 0, 0, 0, GATHER_JOB_HARVEST);
    for (int x = 0; x < 3; x++) {
        CHECK(gather_zone_add_work(&zones, &map, &items, x, 0, 0, GATHER_JOB_HARVEST, work, 60.0,
                                   NULL));
    }
    /* Done at once, whatever lies in the colony. */
    CHECK_EQ_INT(zone->orders[GATHER_BLACKBERRIES].done, 3);
    CHECK_EQ_INT(zone->wanted, 0);
    CHECK_EQ_INT(zones.wanting_cells, 0);
    CHECK(
        !gather_zone_add_work(&zones, &map, &items, 3, 0, 0, GATHER_JOB_HARVEST, work, 60.0, NULL));
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, 0);
    /* A higher target: more to do. Another mode starts over. */
    gather_zone_set_target(&zones, index, GATHER_BLACKBERRIES, 4);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_INT(zone->wanted, GATHER_RESOURCE_BIT(GATHER_BLACKBERRIES));
    gather_zone_set_mode(&zones, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    CHECK_EQ_INT(zone->orders[GATHER_BLACKBERRIES].done, 0);
    free_world(&map, &zones, &items);
}

static void test_progress_belongs_to_one_job(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    put_plant(&map, 2, 2, PLANT_GRASS, 1.0f);
    int index = gather_zone_create(&zones, &map, 0, 0, 3, 3, 0);
    order(&zones, &items, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    order(&zones, &items, index, GATHER_GRASS, GATHER_FOREVER);
    float cut = gather_job_work_needed(&map, 2, 2, 0, GATHER_JOB_CUT_GRASS);
    REQUIRE(cut > 100.0f);
    CHECK(!gather_zone_add_work(&zones, &map, &items, 2, 2, 0, GATHER_JOB_CUT_GRASS, 100.0f, 60.0,
                                NULL));
    CHECK(gather_zone_progress_fraction(&zones, &map, 2, 2, 0) > 0.0f);
    /* Work on another job there starts from scratch (a bramble came up instead). */
    put_plant(&map, 2, 2, PLANT_BRAMBLE, 1.0f);
    float harvest = gather_job_work_needed(&map, 2, 2, 0, GATHER_JOB_HARVEST);
    REQUIRE(harvest > 100.0f);
    CHECK(!gather_zone_add_work(&zones, &map, &items, 2, 2, 0, GATHER_JOB_HARVEST, 100.0f, 60.0,
                                NULL));
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, 2, 2, 0), 100.0f / harvest, 1e-4);
    CHECK_NEAR(zones.progress[map_index(&map, 2, 2, 0)], 100.0, 1e-4);
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, -1, 2, 0), 0.0, 1e-6);
    /* Removing the tile forgets the work. */
    gather_zones_remove_rect(&zones, 2, 2, 2, 2, 0);
    CHECK_NEAR(gather_zone_progress_fraction(&zones, &map, 2, 2, 0), 0.0, 1e-6);
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
    put_plant(&map, 8, 8, PLANT_OAK, 1.0f);
    put_plant(&map, 9, 8, PLANT_OAK, 0.5f);
    /* Blackberries, and nuts lying about (no gathering). */
    const Plant *bramble = put_plant(&map, 10, 8, PLANT_BRAMBLE, 1.0f);
    put_plant(&map, 11, 8, PLANT_BRAMBLE, 1.0f); /* Out of the zone. */
    map_add_litter(&map, 8, 8, 0, ITEM_HAZELNUTS);
    int berries = plant_stock_available(bramble, 0);
    REQUIRE(berries > 0);
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_BLACKBERRIES], berries);
    /* Wood: fully grown trees only by default, all of them without the option. */
    int full_wood = plant_def(PLANT_OAK)->cut_yield;
    int young_wood = (int)((float)full_wood * 0.5f + 0.5f);
    CHECK_EQ_INT((int)available[GATHER_WOOD], full_wood);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_WOOD], full_wood + young_wood);
    CHECK_EQ_INT((int)available[GATHER_GRASS], 0);
    /* What is gathered no longer counts. */
    order(&zones, &items, index, GATHER_BLACKBERRIES, GATHER_FOREVER);
    CHECK(
        gather_zone_add_work(&zones, &map, &items, 10, 8, 0, GATHER_JOB_HARVEST, 1e6f, 60.0, NULL));
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_BLACKBERRIES], 0);
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
    gather_zone_set_target(&zones, index, GATHER_MOSS, 3);
    uint64_t target = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    CHECK(target != created);
    gather_zone_set_ripe_only(&zones, index, GATHER_WOOD, false);
    uint64_t ripe = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    CHECK(ripe != target);
    gather_zone_set_priority(&zones, index, 1);
    CHECK(gather_zones_hash(&zones, HASH_FNV1A64_INIT) != ripe);
    free_world(&map, &zones, &items);
}

/* Uprooting zones: a kind of zone of their own in the same layer, merging with their own kind
 * only, uprooting the plants they are told to (no tree, ever), all of them at first. */
static void test_uprooting_zones(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int gather = gather_zones_add_rect(&zones, &map, 0, 0, 2, 2, 0, GATHER_ZONE_NONE, NULL);
    CHECK(!gather_zones_any_uprooting(&zones)); /* A gathering zone alone. */
    int uproot =
        gather_zones_add_uproot_view_rect(&zones, &map, 4, 0, 6, 2, 0, GATHER_ZONE_NONE, NULL);
    REQUIRE(gather != GATHER_ZONE_NONE && uproot != GATHER_ZONE_NONE && gather != uproot);
    CHECK(gather_zones_any_uprooting(&zones));
    CHECK(!gather_zone_is_uproot(&zones, gather));
    CHECK(gather_zone_is_uproot(&zones, uproot));
    const GatherZone *zone = gather_zone_get(&zones, uproot);
    CHECK(zone->uproot);
    CHECK(gather_zone_uproots(zone, PLANT_BRAMBLE));
    CHECK(gather_zone_uproots(zone, PLANT_GRASS));
    CHECK(!gather_zone_uproots(zone, PLANT_NONE));
    CHECK(!gather_zone_uproots(zone, PLANT_OAK)); /* Never a tree. */
    CHECK(!gather_zone_uproots(gather_zone_get(&zones, gather), PLANT_BRAMBLE));
    /* Nothing is gathered in it. */
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK_EQ_UINT(zone->wanted, 0);

    /* A rectangle over both kinds merges only zones of its kind; the other keeps its tiles. */
    CHECK_EQ_INT(
        gather_zones_add_uproot_view_rect(&zones, &map, 2, 1, 5, 1, 0, GATHER_ZONE_NONE, NULL),
        uproot);
    CHECK_EQ_INT(zones.used_count, 2);
    CHECK_EQ_INT(gather_zone_at(&zones, 2, 1, 0), gather);
    CHECK_EQ_INT(gather_zone_at(&zones, 3, 1, 0), uproot);
    CHECK_EQ_INT(gather_zone_get(&zones, gather)->cell_count, 9);
    CHECK_EQ_INT(gather_zone_get(&zones, uproot)->cell_count, 9 + 1);
    /* A gathering zone drawn over it, the uprooting one selected: a zone of its own kind. */
    int beside = gather_zones_add_rect(&zones, &map, 6, 1, 8, 1, 0, uproot, NULL);
    CHECK(beside != GATHER_ZONE_NONE && beside != uproot && beside != gather);
    CHECK_EQ_INT(gather_zone_at(&zones, 7, 1, 0), beside);
    CHECK_EQ_INT(gather_zone_at(&zones, 6, 1, 0), uproot);
    CHECK_EQ_INT(gather_zone_get(&zones, uproot)->cell_count, 10);
    CHECK_EQ_INT(gather_zone_get(&zones, beside)->cell_count, 2);

    /* The plants it uproots, one by one or all, and where a plant is wanted gone. */
    put_plant(&map, 4, 0, PLANT_BRAMBLE, 1.0f);
    put_plant(&map, 5, 0, PLANT_GRASS, 1.0f);
    put_plant(&map, 1, 0, PLANT_BRAMBLE, 1.0f); /* In the gathering zone. */
    put_plant(&map, 12, 12, PLANT_GRASS, 1.0f); /* In no zone. */
    CHECK(gather_zones_want_uprooted(&zones, &map, 4, 0, 0));
    CHECK(gather_zones_want_uprooted(&zones, &map, 5, 0, 0));
    CHECK(!gather_zones_want_uprooted(&zones, &map, 6, 0, 0)); /* No plant. */
    CHECK(!gather_zones_want_uprooted(&zones, &map, 1, 0, 0));
    CHECK(!gather_zones_want_uprooted(&zones, &map, 12, 12, 0));
    uint64_t before = gather_zones_hash(&zones, HASH_FNV1A64_INIT);
    gather_zone_set_uproot_plant(&zones, uproot, PLANT_GRASS, false);
    CHECK(!gather_zones_want_uprooted(&zones, &map, 5, 0, 0));
    CHECK(gather_zones_want_uprooted(&zones, &map, 4, 0, 0));
    CHECK(gather_zones_hash(&zones, HASH_FNV1A64_INIT) != before);
    gather_zone_set_all_uproot_plants(&zones, uproot, false);
    CHECK(!gather_zones_want_uprooted(&zones, &map, 4, 0, 0));
    CHECK(!gather_zones_any_uprooting(&zones)); /* It uproots no plant. */
    gather_zone_set_all_uproot_plants(&zones, uproot, true);
    CHECK(gather_zones_want_uprooted(&zones, &map, 5, 0, 0));
    CHECK(gather_zones_any_uprooting(&zones));
    gather_zone_set_uproot_plant(&zones, uproot, PLANT_OAK, true); /* A tree: refused. */
    CHECK(!gather_zone_uproots(gather_zone_get(&zones, uproot), PLANT_OAK));
    gather_zone_set_uproot_plant(&zones, gather, PLANT_BRAMBLE, true); /* A gathering zone. */
    CHECK(!gather_zone_uproots(gather_zone_get(&zones, gather), PLANT_BRAMBLE));
    /* Forbidden: nothing is uprooted. */
    gather_zone_set_forbidden(&zones, uproot, true);
    CHECK(!gather_zones_want_uprooted(&zones, &map, 4, 0, 0));
    CHECK(!gather_zones_any_uprooting(&zones));
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
    order(&zones, &items, a, GATHER_GRASS, GATHER_FOREVER);
    order(&zones, &items, b, GATHER_BLACKBERRIES, GATHER_FOREVER);
    /* Work under way in the younger zone. */
    put_plant(&map, 7, 1, PLANT_BRAMBLE, 1.0f);
    CHECK(!gather_zone_add_work(&zones, &map, &items, 7, 1, 0, GATHER_JOB_HARVEST, 100.0f, 60.0,
                                NULL));

    /* Over both: the oldest takes everything, with its orders. */
    CHECK_EQ_INT(gather_zones_add_rect(&zones, &map, 2, 1, 6, 1, 0, GATHER_ZONE_NONE, NULL), a);
    CHECK_EQ_INT(zones.used_count, 1);
    CHECK(gather_zone_get(&zones, b) == NULL);
    const GatherZone *merged = gather_zone_get(&zones, a);
    REQUIRE(merged != NULL);
    CHECK_EQ_INT(merged->cell_count, 9 + 9 + 3);
    CHECK(merged->orders[GATHER_GRASS].enabled);
    CHECK(!merged->orders[GATHER_BLACKBERRIES].enabled);
    CHECK_EQ_INT(gather_zone_at(&zones, 8, 2, 0), a);
    CHECK_NEAR(zones.progress[map_index(&map, 7, 1, 0)], 100.0, 1e-4); /* Kept. */

    /* The selected zone wins and keeps its orders. */
    int c = gather_zones_add_rect(&zones, &map, 0, 12, 1, 13, 0, GATHER_ZONE_NONE, NULL);
    REQUIRE(c != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zones_add_rect(&zones, &map, 1, 2, 1, 12, 0, c, NULL), c);
    CHECK(gather_zone_get(&zones, a) == NULL);
    CHECK(!gather_zone_get(&zones, c)->orders[GATHER_GRASS].enabled);
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
    map.plants[map_index(&map, 1, 1, 0)] = (Plant){.type = PLANT_OAK, .growth = 1.0f};
    CHECK_NEAR(gather_clear_work(&map, 1, 1, 0), 0.0, 1e-6); /* Not by hand. */
    CHECK(map_plant_blocks_building(&map, 1, 1, 0));

    /* Grass is cut: fresh grass. */
    put_plant(&map, 3, 3, PLANT_GRASS, 1.0f);
    CHECK(!map_plant_blocks_building(&map, 3, 3, 0));
    CHECK(gather_clear_work(&map, 3, 3, 0) > 0.0f);
    gather_clear_plant(&map, &items, 3, 3, 0, 60.0, NULL);
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3, 0)].type, PLANT_NONE);
    CHECK_EQ_INT((int)items.totals[ITEM_GRASS], 3);
    CHECK_NEAR(gather_clear_work(&map, 3, 3, 0), 0.0, 1e-6);

    /* A bramble is pulled up: its berries. */
    Plant *bramble = &map.plants[map_index(&map, 8, 3, 0)];
    *bramble = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    plant_settle(bramble, SEASON_DAY(SEASON_AUTUMN, 2));
    int berries = plant_stock_available(bramble, 0);
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

/* Grass is a plant like the others: a stock of armfuls that grows with it, taken
 * whole (back to growth 0), gathered by a zone when it is fully grown. */
static void test_grass_is_a_plant_with_a_stock(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    const PlantProduct *armfuls = plant_product(PLANT_GRASS, 0);
    REQUIRE(armfuls != NULL);
    CHECK_EQ_INT(armfuls->item, ITEM_GRASS);
    CHECK_EQ_INT(plant_food_slot(PLANT_GRASS), -1); /* Nobody eats it in place. */
    Plant *tall = put_plant(&map, 1, 1, PLANT_GRASS, 1.0f);
    Plant *short_grass = put_plant(&map, 3, 1, PLANT_GRASS, 0.4f);
    Plant *medium = put_plant(&map, 5, 1, PLANT_GRASS, 0.8f);
    CHECK(!plant_has_food(tall));
    CHECK_EQ_INT(plant_stock_available(tall, 0), armfuls->capacity);
    CHECK_EQ_INT(plant_stock_available(short_grass, 0), 0); /* Too short to cut. */
    CHECK(plant_stock_available(medium, 0) > 0);
    CHECK(plant_stock_available(medium, 0) < armfuls->capacity);
    CHECK(gather_plant_harvestable(&map, 1, 1, 0));
    CHECK(!gather_plant_harvestable(&map, 3, 1, 0));

    /* Taken whole: the stock, then it grows again from nothing. */
    int zone = gather_zone_create(&zones, &map, 0, 0, 6, 2, 0);
    order(&zones, &items, zone, GATHER_GRASS, GATHER_FOREVER);
    CHECK(gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_CUT_GRASS));
    CHECK(gather_zone_add_work(&zones, &map, &items, 1, 1, 0, GATHER_JOB_CUT_GRASS, 1e6f, 60.0,
                               NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_GRASS], armfuls->capacity);
    ItemStack cut = items_get_of(&items, 1, 1, 0, item_types_of(ITEM_GRASS));
    CHECK_NEAR(item_stack_moisture(cut), item_fresh_moisture(ITEM_GRASS), 1e-3); /* Fresh. */
    CHECK_EQ_INT(tall->type, PLANT_GRASS);
    CHECK_NEAR(tall->growth, 0.0f, 1e-6);
    CHECK_EQ_INT(plant_stock_available(tall, 0), 0);
    CHECK(!gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_CUT_GRASS));
    /* A few days on, it has grown and bears armfuls again. */
    for (int day = 0; day < 8; day++) { /* Light for half of each day. */
        PlantConditions warm = {
            .temperature = 20.0f, .fertility = 1.0f, .light = 1.0f, .light_peak = 1.0f};
        plant_advance(tall, &warm, HOURS_PER_DAY);
    }
    CHECK_NEAR(tall->growth, 1.0f, 1e-6);
    CHECK_EQ_INT(plant_stock_available(tall, 0), armfuls->capacity);
    CHECK(gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_CUT_GRASS));

    /* Waiting for the full harvest: fully grown only. */
    gather_zone_set_all_ripe_only(&zones, zone, true);
    CHECK(gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_CUT_GRASS));
    REQUIRE(medium->growth < 1.0f);
    CHECK(!gather_job_available(&zones, &map, 5, 1, 0, GATHER_JOB_CUT_GRASS));
    gather_zone_set_all_ripe_only(&zones, zone, false);
    CHECK(gather_job_available(&zones, &map, 5, 1, 0, GATHER_JOB_CUT_GRASS));
    free_world(&map, &zones, &items);
}

/* A plant taken whole waits until it is fully grown for the full harvest: a young
 * wild carrot, however full its stock, is left to grow. */
static void test_taken_whole_waits_until_grown(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int autumn = SEASON_DAY(SEASON_AUTUMN, 4);
    Plant *young = put_plant(&map, 1, 1, PLANT_WILD_CARROT,
                             plant_def(PLANT_WILD_CARROT)->products[0].min_growth);
    Plant *grown = put_plant(&map, 3, 1, PLANT_WILD_CARROT, 1.0f);
    plant_settle(young, autumn);
    plant_settle(grown, autumn);
    REQUIRE(plant_stock_available(young, 0) > 0);
    int zone = gather_zone_create(&zones, &map, 0, 0, 4, 2, 0);
    order(&zones, &items, zone, GATHER_WILD_CARROTS, GATHER_FOREVER);
    gather_zone_set_all_ripe_only(&zones, zone, true);
    CHECK(!gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_DIG_CAREFULLY));
    CHECK(gather_job_available(&zones, &map, 3, 1, 0, GATHER_JOB_DIG_CAREFULLY));
    gather_zone_set_all_ripe_only(&zones, zone, false);
    CHECK(gather_job_available(&zones, &map, 1, 1, 0, GATHER_JOB_DIG_CAREFULLY));
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
    put_plant(&map, 6, 6, PLANT_OAK, 1.0f);
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
    int berries = plant_stock_available(bramble, 0);
    REQUIRE(berries > 0);
    CHECK(gather_plant_harvestable(&map, 1, 1, 0));
    CHECK(gather_harvest_plant(&map, &items, 1, 1, 0, 60.0, NULL));
    CHECK_EQ_INT((int)items.totals[ITEM_BLACKBERRIES], berries);
    CHECK_EQ_INT(bramble->type, PLANT_BRAMBLE);
    CHECK(!gather_plant_harvestable(&map, 1, 1, 0));
    CHECK(!gather_harvest_plant(&map, &items, 1, 1, 0, 60.0, NULL));
    /* Grass cut grows again. */
    CHECK(gather_harvest_plant(&map, &items, 4, 1, 0, 60.0, NULL));
    CHECK(items.totals[ITEM_GRASS] > 0);
    CHECK_EQ_INT(grass->type, PLANT_GRASS);
    CHECK(!gather_plant_harvestable(&map, 4, 1, 0));
    /* Uprooting the cattail: its rhizomes first, then it is gone. */
    int rhizomes = plant_stock_available(cattail, 0);
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

/* Extending or shrinking one zone leaves the others alone. */
static void test_extend_and_shrink_one(void)
{
    Map map;
    GatherZones zones;
    Items items;
    REQUIRE(init_world(&map, &zones, &items));
    int a = gather_zone_create(&zones, &map, 1, 1, 2, 2, 0);
    int b = gather_zone_create(&zones, &map, 4, 1, 4, 2, 0);
    REQUIRE(a != GATHER_ZONE_NONE && b != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_extend_view(&zones, &map, a, 3, 1, 6, 2, 0, NULL), 6);
    CHECK_EQ_INT(gather_zone_get(&zones, a)->cell_count, 10);
    CHECK_EQ_INT(gather_zone_get(&zones, b)->cell_count, 2);
    CHECK_EQ_INT(gather_zone_at(&zones, 4, 1, 0), b);
    CHECK_EQ_INT(gather_zone_shrink_view(&zones, &map, a, 3, 1, 4, 2, 0), 2);
    CHECK_EQ_INT(gather_zone_get(&zones, b)->cell_count, 2);
    CHECK_EQ_INT(gather_zone_at(&zones, 3, 1, 0), GATHER_ZONE_NONE);
    CHECK(gather_zone_shrink_view(&zones, &map, a, 0, 0, 15, 15, 0) > 0);
    CHECK(gather_zone_get(&zones, a) == NULL);
    CHECK_EQ_INT(gather_zone_get(&zones, b)->cell_count, 2);
    CHECK_EQ_INT(gather_zone_shrink_view(&zones, &map, a, 0, 0, 15, 15, 0), 0);
    free_world(&map, &zones, &items);
}

/* Picking in the crowns (docs/TREES.md §7.2), on a map of four levels: soil
 * at the bottom, open space over it but for two blocks of foliage at (5, 5)
 * (medium, sparse over it) and two at (6, 5) (dense, sparse over it). */
static void test_picking_in_the_crowns(void)
{
    Map map;
    GatherZones zones;
    Items items;
    Crowns crowns;
    REQUIRE(map_init(&map, 16, 16, 4));
    REQUIRE(gather_zones_init(&zones, 16, 16, 4));
    REQUIRE(items_init(&items, 16, 16, 4));
    REQUIRE(crowns_init(&crowns, 16, 16, 4));
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        map.terrain[i] = i < map_level_tile_count(&map) ? TERRAIN_SOIL : TERRAIN_OPEN;
        map.rock[i] = ROCK_NONE;
    }
    map.terrain[map_index(&map, 5, 5, 1)] = TERRAIN_FOLIAGE_MEDIUM;
    map.terrain[map_index(&map, 5, 5, 2)] = TERRAIN_FOLIAGE_SPARSE;
    map.terrain[map_index(&map, 6, 5, 1)] = TERRAIN_FOLIAGE;
    map.terrain[map_index(&map, 6, 5, 2)] = TERRAIN_FOLIAGE_SPARSE;
    map_topology_changed(&map);
    map_litter_to_items(&map, &items);
    map.crowns = &crowns;
    CrownItem apple = {.type = ITEM_APPLES, .quality = ITEM_QUALITY_STANDARD, .hit_points = 100};
    CrownItem branch = {.type = ITEM_BRANCHES, .hit_points = 80};
    for (int i = 0; i < 3; i++) {
        REQUIRE(crowns_add(&crowns, &map, 5, 5, 1, apple));
    }
    REQUIRE(crowns_add(&crowns, &map, 5, 5, 1, branch));
    for (int i = 0; i < 2; i++) {
        REQUIRE(crowns_add(&crowns, &map, 6, 5, 1, apple));
        REQUIRE(crowns_add(&crowns, &map, 5, 5, 2, apple));
    }

    /* The zone on the ground below covers the crowns over it. */
    int index = gather_zone_create(&zones, &map, 0, 0, 9, 9, 0);
    REQUIRE(index != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_crown_zone(&zones, &map, 5, 5, 1), index);
    CHECK_EQ_INT(gather_crown_zone(&zones, &map, 5, 5, 2), index);
    CHECK_EQ_INT(gather_crown_zone(&zones, &map, 5, 5, 0), GATHER_ZONE_NONE); /* Not foliage. */
    CHECK_EQ_INT(gather_crown_zone(&zones, &map, 7, 5, 1), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 5, 5, 1, ALL_JOBS), GATHER_JOB_NONE); /* No order. */
    order(&zones, &items, index, GATHER_APPLES, GATHER_FOREVER);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 5, 5, 1, ALL_JOBS), GATHER_JOB_PICK);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 5, 5, 2, ALL_JOBS), GATHER_JOB_PICK);
    CHECK_EQ_INT(gather_tile_job(&zones, &map, 5, 5, 1, GATHER_JOB_BIT(GATHER_JOB_HARVEST)),
                 GATHER_JOB_NONE);
    CHECK(gather_job_available(&zones, &map, 6, 5, 1, GATHER_JOB_PICK));
    CHECK(!gather_job_available(&zones, &map, 7, 5, 1, GATHER_JOB_PICK));

    /* From the ground: the block right above, of any density. */
    CHECK(gather_pick_reaches(&zones, &map, 5, 5, 0, 5, 5, 1));
    CHECK(gather_pick_reaches(&zones, &map, 6, 5, 0, 6, 5, 1));
    CHECK(!gather_pick_reaches(&zones, &map, 5, 5, 0, 5, 5, 2));
    CHECK(!gather_pick_reaches(&zones, &map, 4, 5, 0, 5, 5, 1));
    /* Up on the dense foliage, only when the zone lets them climb. */
    CHECK(!gather_pick_reaches(&zones, &map, 6, 5, 1, 5, 5, 1));
    CHECK(!gather_zones_climb(&zones));
    long available[GATHER_RESOURCE_COUNT];
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_APPLES], 5); /* Not the two up at (5, 5, 2). */
    gather_zone_set_climb(&zones, index, true);
    CHECK(gather_zones_climb(&zones));
    CHECK(gather_pick_reaches(&zones, &map, 6, 5, 1, 5, 5, 1)); /* Beside it. */
    CHECK(gather_pick_reaches(&zones, &map, 6, 5, 1, 6, 5, 1)); /* Its own. */
    CHECK(gather_pick_reaches(&zones, &map, 6, 5, 1, 6, 5, 2)); /* Above it. */
    CHECK(!gather_pick_reaches(&zones, &map, 6, 5, 1, 5, 5, 2));
    gather_zone_available(&zones, &map, index, available);
    CHECK_EQ_INT((int)available[GATHER_APPLES], 7);

    /* Picked: what the zone wants falls to the ground below, as it was. */
    CHECK_NEAR(gather_job_work_needed(&map, 5, 5, 1, GATHER_JOB_PICK),
               3.0f * gather_job_work_needed(&map, 6, 5, 1, GATHER_JOB_PICK) / 2.0f, 1e-3);
    CHECK(gather_zone_add_work(&zones, &map, &items, 5, 5, 1, GATHER_JOB_PICK, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(crowns_count(&crowns, 5, 5, 1, item_types_of(ITEM_APPLES)), 0);
    CHECK_EQ_INT(crowns_count(&crowns, 5, 5, 1, item_types_of(ITEM_BRANCHES)), 1); /* Left. */
    ItemStack below = items_get(&items, 5, 5, 0);
    CHECK_EQ_INT(below.type, ITEM_APPLES);
    CHECK_EQ_INT(below.count, 3);
    const GatherZone *zone = gather_zone_get(&zones, index);
    CHECK_EQ_INT(zone->orders[GATHER_APPLES].done, 3);
    CHECK_EQ_INT(zone->groups[GATHER_GROUP_FOOD].done, 3);
    CHECK(!gather_job_available(&zones, &map, 5, 5, 1, GATHER_JOB_PICK));
    CHECK(crowns_check(&crowns));

    /* A zone over the foliage itself takes it over; turned off, nothing. */
    int upper = gather_zone_create(&zones, &map, 6, 5, 6, 5, 2);
    REQUIRE(upper != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_crown_zone(&zones, &map, 6, 5, 2), upper);
    CHECK_EQ_INT(gather_crown_zone(&zones, &map, 6, 5, 1), index);
    uint64_t hash = gather_zones_hash(&zones, 0);
    gather_zone_set_climb(&zones, index, false);
    CHECK(gather_zones_hash(&zones, 0) != hash);
    gather_zone_set_enabled(&zones, index, GATHER_APPLES, false);
    gather_zones_update(&zones, items.totals, 0.0f);
    CHECK(!gather_job_available(&zones, &map, 6, 5, 1, GATHER_JOB_PICK));
    CHECK(!gather_zone_add_work(&zones, &map, &items, 6, 5, 1, GATHER_JOB_PICK, 1e6f, 60.0, NULL));

    crowns_free(&crowns);
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
    RUN_TEST(test_apply_the_group_order_to_its_resources);
    RUN_TEST(test_days_of_food);
    RUN_TEST(test_food_in_season);
    RUN_TEST(test_felling_a_tree);
    RUN_TEST(test_products_depend_on_level);
    RUN_TEST(test_grass);
    RUN_TEST(test_fixed_count_stops);
    RUN_TEST(test_progress_belongs_to_one_job);
    RUN_TEST(test_map_stones);
    RUN_TEST(test_available_resources);
    RUN_TEST(test_plants_growing_wake_settlers);
    RUN_TEST(test_hash);
    RUN_TEST(test_uprooting_zones);
    RUN_TEST(test_overlapping_rectangle_merges_zones);
    RUN_TEST(test_blocked_tiles_are_left_out);
    RUN_TEST(test_clearing_plants);
    RUN_TEST(test_grass_is_a_plant_with_a_stock);
    RUN_TEST(test_taken_whole_waits_until_grown);
    RUN_TEST(test_zone_tiles_box);
    RUN_TEST(test_extend_and_shrink_one);
    RUN_TEST(test_picking_in_the_crowns);
    return test_summary();
}
