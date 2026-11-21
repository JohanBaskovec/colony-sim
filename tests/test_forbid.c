/* Forbidding things to the settlers: items, plants, structures, roofs and
 * stations they must leave alone, doors and ladders they may not go through
 * (map_forbidden(), items_forbidden(), Workstation.forbidden, the
 * game_set_forbidden() API). */
#include "core/game.h"
#include "core/gather_zone.h"
#include "core/crown.h"
#include "core/hauling.h"
#include "core/pathfinding.h"
#include "core/tree.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <string.h>

/* A small game on a flat, empty soil map with no starting colonists, at noon. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true; /* One level of ground, open air over it. */
    params.world.seed = 5;
    params.world.width = 48;
    params.world.height = 48;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN; /* Open air over it: no trees rising there. */
        map->water[i] = 0;
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) { /* The ground level. */
        map->terrain[i] = TERRAIN_SOIL;
        map->water[i] = 0;
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_COBBLE));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_FLINT));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_BRANCHES) | ITEM_TYPE_BIT(ITEM_TWIGS));
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map); /* The layers were written directly. */
    game->clock.tick = 12 * TICKS_PER_HOUR;
    return true;
}

/* Rested colonist at level 75 in every knowledge (speed x1). */
static Colonist *spawn_worker(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y, 0));
    if (colonist != NULL) {
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            colonist_set_knowledge_level(colonist, (KnowledgeType)k, 75.0);
        }
        colonist_set_strength(colonist, COLONIST_STRENGTH_STANDARD);
        make_ordinary(colonist);
    }
    return colonist;
}

/* Keeps needs satisfied (but `keep_food`: food is left as it is) and the hour at noon. */
static void run_ticks_fed(Game *game, int ticks, bool keep_food)
{
    for (int i = 0; i < ticks; i++) {
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
                for (int need = 0; need < NEED_COUNT; need++) {
                    if (need != NEED_FOOD || !keep_food) {
                        game->colonists[slot].needs[need] = 1.0f;
                    }
                }
            }
        }
        game->clock.tick = 12 * TICKS_PER_HOUR;
        game_tick(game);
    }
}

static void run_ticks(Game *game, int ticks)
{
    run_ticks_fed(game, ticks, false);
}

/* A wall from y = 0 to the bottom of the map along x = `wall_x`, with a
 * door (or a ladder's place left open) on (wall_x, door_y). */
static void wall_with_door(Game *game, int wall_x, int door_y)
{
    for (int y = 0; y < game->map.height; y++) {
        map_set_structure(&game->map, wall_x, y, 0, y == door_y ? STRUCTURE_DOOR : STRUCTURE_WALL);
    }
}

static void test_map_forbid_bits(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    uint64_t hash = map_hash(map);

    /* Only what is there can be forbidden. */
    CHECK(!map_can_forbid(map, 5, 5, 0, MAP_FORBID_PLANT));
    CHECK(!map_set_forbidden(map, 5, 5, 0, MAP_FORBID_PLANT, true));
    CHECK(!map_set_forbidden(map, 5, 5, 0, MAP_FORBID_STRUCTURE, true));
    CHECK(!map_set_forbidden(map, 5, 5, 0, MAP_FORBID_ROOF, true));
    CHECK(!map_set_forbidden(map, -1, 5, 0, MAP_FORBID_PLANT, true));
    CHECK_EQ_INT(map->forbidden_count, 0);
    CHECK_EQ_UINT(map_hash(map), hash); /* Nothing forbidden: the hash as it was. */

    map->plants[map_index(map, 5, 5, 0)] = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    CHECK(map_can_forbid(map, 5, 5, 0, MAP_FORBID_PLANT));
    CHECK(map_set_forbidden(map, 5, 5, 0, MAP_FORBID_PLANT, true));
    CHECK(!map_set_forbidden(map, 5, 5, 0, MAP_FORBID_PLANT, true)); /* Already. */
    CHECK(map_forbidden(map, 5, 5, 0, MAP_FORBID_PLANT));
    CHECK(!map_forbidden(map, 5, 5, 0, MAP_FORBID_STRUCTURE));
    CHECK_EQ_INT(map->forbidden_count, 1);
    CHECK(map_hash(map) != hash);
    /* The forbidding goes with the plant: a new one is allowed. */
    map_remove_plant(map, 5, 5, 0);
    CHECK_EQ_INT(map->forbidden_count, 0);
    map->plants[map_index(map, 5, 5, 0)] = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    CHECK(!map_forbidden(map, 5, 5, 0, MAP_FORBID_PLANT));

    /* A bed forbidden from either of its tiles: the bit is on its head. */
    map_set_structure(map, 10, 10, 0, STRUCTURE_GRASS_BED);
    int foot_x = 10, foot_y = 10;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if ((dx != 0 || dy != 0) && (dx == 0 || dy == 0) &&
                map_structure(map, 10 + dx, 10 + dy, 0) == STRUCTURE_GRASS_BED) {
                foot_x = 10 + dx;
                foot_y = 10 + dy;
            }
        }
    }
    REQUIRE(foot_x != 10 || foot_y != 10);
    CHECK(map_set_forbidden(map, foot_x, foot_y, 0, MAP_FORBID_STRUCTURE, true));
    CHECK(map_forbidden(map, 10, 10, 0, MAP_FORBID_STRUCTURE));
    CHECK(map_forbidden(map, foot_x, foot_y, 0, MAP_FORBID_STRUCTURE));
    CHECK(map_set_forbidden(map, 10, 10, 0, MAP_FORBID_STRUCTURE, false));
    CHECK(!map_forbidden(map, foot_x, foot_y, 0, MAP_FORBID_STRUCTURE));
    /* Taken apart: the forbidding goes with it. */
    CHECK(map_set_forbidden(map, 10, 10, 0, MAP_FORBID_STRUCTURE, true));
    map_set_structure(map, 10, 10, 0, STRUCTURE_NONE);
    CHECK_EQ_INT(map->forbidden_count, 0);
    map_set_structure(map, 10, 10, 0, STRUCTURE_WALL);
    CHECK(!map_forbidden(map, 10, 10, 0, MAP_FORBID_STRUCTURE));

    /* A roof. */
    map_set_structure(map, 20, 20, 0, STRUCTURE_WALL);
    map_set_roof(map, 20, 20, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    REQUIRE(map_roof(map, 20, 20, 1) == STRUCTURE_ROOF);
    CHECK(map_set_forbidden(map, 20, 20, 1, MAP_FORBID_ROOF, true));
    CHECK(map_forbidden(map, 20, 20, 1, MAP_FORBID_ROOF));
    CHECK(!map_forbidden(map, 20, 20, 0, MAP_FORBID_STRUCTURE));
    map_set_roof(map, 20, 20, 1, STRUCTURE_NONE, ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(map->forbidden_count, 0);
    game_shutdown(&game);
}

/* A forbidden door is locked: nobody goes through. */
static void test_locked_doors(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    wall_with_door(&game, 20, 10);
    regions_update(&game.regions, map);
    TilePos west = {10, 10, 0}, east = {30, 10, 0};
    CHECK(regions_connected(&game.regions, map, west, east));
    uint32_t topology = map->topology_revision;

    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_STRUCTURE, true));
    CHECK(map->topology_revision != topology); /* Where one can walk changed. */
    CHECK(!map_is_passable(map, 20, 10, 0));
    regions_update(&game.regions, map);
    CHECK(!regions_connected(&game.regions, map, west, east));

    /* A settler west of it stays there; walking east it would need the door. */
    Colonist *colonist = spawn_worker(&game, 18, 10);
    REQUIRE(colonist != NULL);
    items_add(&game.items, 30, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 5));
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 10, 10, 11, 11, 0) != STOCKPILE_NONE);
    run_ticks(&game, 600);
    CHECK(colonist->x < 20);
    CHECK_EQ_INT((int)items_get(&game.items, 30, 10, 0).count, 5);

    /* Unlocked: the way opens again and the berries are brought in. */
    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_STRUCTURE, false));
    CHECK(map_is_passable(map, 20, 10, 0));
    regions_update(&game.regions, map);
    CHECK(regions_connected(&game.regions, map, west, east));
    run_ticks(&game, 3000);
    CHECK_EQ_INT((int)items_get(&game.items, 30, 10, 0).count, 0);

    game_shutdown(&game);
}

/* A locked ladder leads nowhere: the way up the hill is shut. */
static void test_locked_ladder(void)
{
    Map map;
    Pathfinder finder;
    Path path = {0};
    REQUIRE(map_init(&map, 16, 16, 2));
    REQUIRE(pathfinder_init(&finder, 16, 16, 2));
    for (int y = 0; y < map.height; y++) { /* A hill over x >= 8, one level up. */
        for (int x = 8; x < map.width; x++) {
            map_set_rock(&map, x, y, 0, ROCK_EARTH);
            map_set_terrain(&map, x, y, 1, TERRAIN_SOIL);
        }
    }
    TilePos start = {2, 5, 0}, goal = {12, 5, 1};
    REQUIRE(map_ladder_fits(&map, 7, 9, 0));
    map_set_structure(&map, 7, 9, 0, STRUCTURE_LADDER);
    CHECK(pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path));
    uint32_t topology = map.topology_revision;
    CHECK(map_set_forbidden(&map, 7, 9, 0, MAP_FORBID_STRUCTURE, true));
    CHECK(map.topology_revision != topology);
    CHECK_NEAR(map_climb_cost(&map, 7, 9, 0), 0.0, 1e-6);
    CHECK(!map_can_climb(&map, 7, 9, 0, 8, 9));
    CHECK(!pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path));
    CHECK(map_set_forbidden(&map, 7, 9, 0, MAP_FORBID_STRUCTURE, false));
    CHECK_NEAR(map_climb_cost(&map, 7, 9, 0), MAP_LADDER_CLIMB_COST, 1e-6);
    CHECK(pathfinder_find(&finder, &map, start, goal, PATH_NO_COST_LIMIT, &path));
    path_free(&path);
    pathfinder_free(&finder);
    map_free(&map);
}

static void test_forbidden_items(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Items *items = &game.items;
    uint64_t hash = game_hash(&game);
    CHECK(!game_can_forbid(&game, 10, 10, 0, FORBID_ITEMS));
    CHECK(!game_set_forbidden(&game, 10, 10, 0, FORBID_ITEMS, true)); /* Nothing there. */

    items_add(items, 10, 10, 0, item_stack_pristine(ITEM_BRANCHES, 4));
    hash = game_hash(&game);
    CHECK(game_can_forbid(&game, 10, 10, 0, FORBID_ITEMS));
    CHECK(game_set_forbidden(&game, 10, 10, 0, FORBID_ITEMS, true));
    CHECK(game_forbidden(&game, 10, 10, 0, FORBID_ITEMS));
    CHECK(game_hash(&game) != hash);
    /* Settlers see nothing there; the items are still on the map. */
    CHECK_EQ_INT(items_get_allowed(items, 10, 10, 0).type, ITEM_NONE);
    CHECK_EQ_INT(items_get(items, 10, 10, 0).count, 4);
    CHECK_EQ_INT((int)items->totals[ITEM_BRANCHES], 4);

    /* Items dropped there go around it, even of the same kind. */
    CHECK_EQ_INT(items_drop(items, &game.map, 10, 10, 0, item_stack_pristine(ITEM_BRANCHES, 3)), 3);
    CHECK_EQ_INT(items_get(items, 10, 10, 0).count, 4);

    /* Allowed again: settlers see it, and haulers look again. */
    uint32_t additions = items->additions;
    CHECK(game_set_forbidden(&game, 10, 10, 0, FORBID_ITEMS, false));
    CHECK(items->additions != additions);
    CHECK_EQ_INT(items_get_allowed(items, 10, 10, 0).count, 4);

    /* The forbidding goes with the stack. */
    CHECK(game_set_forbidden(&game, 10, 10, 0, FORBID_ITEMS, true));
    items_remove(items, 10, 10, 0, 4);
    CHECK(!items_forbidden(items, 10, 10, 0));
    CHECK_EQ_INT(items->forbidden_count, 0);
    items_add(items, 10, 10, 0, item_stack_pristine(ITEM_BRANCHES, 1));
    CHECK(!items_forbidden(items, 10, 10, 0));
    game_shutdown(&game);
}

/* Forbidden stacks are neither hauled away nor added to. */
static void test_haulers_leave_forbidden_items(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    items_add(&game.items, 20, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 5));
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 20, 10, 20, 10, 0) != STOCKPILE_NONE);
    CHECK(game_set_forbidden(&game, 10, 10, 0, FORBID_ITEMS, true));
    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_ITEMS, true));
    REQUIRE(spawn_worker(&game, 14, 14) != NULL);
    run_ticks(&game, 1200);
    CHECK_EQ_INT(items_get(&game.items, 10, 10, 0).count, 10);
    CHECK_EQ_INT(items_get(&game.items, 20, 10, 0).count, 5);

    /* The pile allowed again: the berries go on it. */
    CHECK(game_set_forbidden(&game, 10, 10, 0, FORBID_ITEMS, false));
    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_ITEMS, false));
    run_ticks(&game, 2000);
    CHECK_EQ_INT(items_get(&game.items, 10, 10, 0).count, 0);
    CHECK_EQ_INT(items_get(&game.items, 20, 10, 0).count, 15);
    game_shutdown(&game);
}

/* A hauler on its way to a stack forbidden meanwhile leaves it. */
static void test_forbidding_stops_a_hauler_on_its_way(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 30, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    REQUIRE(stockpile_create(&game.stockpiles, &game.map, 5, 10, 5, 10, 0) != STOCKPILE_NONE);
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    run_ticks(&game, 1);
    REQUIRE(colonist->task == TASK_HAUL);
    CHECK(game_set_forbidden(&game, 30, 10, 0, FORBID_ITEMS, true));
    run_ticks(&game, 1200);
    CHECK_EQ_INT(items_get(&game.items, 30, 10, 0).count, 10);
    CHECK_EQ_INT(colonist->carried.count, 0);
    CHECK_EQ_INT(game.reservations.count, 0);
    game_shutdown(&game);
}

/* A hungry settler does not eat forbidden food. */
static void test_forbidden_food_is_not_eaten(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 12, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    CHECK(game_set_forbidden(&game, 12, 10, 0, FORBID_ITEMS, true));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_FOOD] = 0.2f;
    run_ticks_fed(&game, 600, true);
    CHECK(colonist->task != TASK_EAT);
    CHECK_EQ_INT(items_get(&game.items, 12, 10, 0).count, 10);

    CHECK(game_set_forbidden(&game, 12, 10, 0, FORBID_ITEMS, false));
    colonist->search_cooldown[SEARCH_FOOD] = 0;
    run_ticks_fed(&game, 600, true);
    CHECK(items_get(&game.items, 12, 10, 0).count < 10);
    game_shutdown(&game);
}

/* A forbidden bed is not slept in. */
static void test_forbidden_bed_is_left_alone(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 13, 10, 0, STRUCTURE_GRASS_BED);
    CHECK(game_set_forbidden(&game, 13, 10, 0, FORBID_STRUCTURE, true));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_REST] = 0.2f;
    game_tick(&game);
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    CHECK_EQ_INT(beds_owner(&game.beds, 13, 10, 0), 0);
    for (int i = 0; i < TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    CHECK(colonist->x == 10 && colonist->y == 10); /* Sleeps where it is. */
    game_shutdown(&game);
}

/* A bed forbidden while a settler walks to it: the settler sleeps elsewhere;
 * one asleep in a bed forbidden meanwhile sleeps on. */
static void test_bed_forbidden_on_the_way(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    map_set_structure(&game.map, 20, 10, 0, STRUCTURE_GRASS_BED);
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    colonist->needs[NEED_REST] = 0.2f;
    game_tick(&game);
    REQUIRE(colonist->task == TASK_SLEEP);
    REQUIRE(colonist->target.x == 20 && colonist->target.y == 10);
    for (int i = 0; i < 30; i++) {
        game_tick(&game);
    }
    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_STRUCTURE, true));
    for (int i = 0; i < TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    CHECK(colonist->x < 20); /* Asleep where it was. */
    CHECK_EQ_INT(game.reservations.count, 0);

    /* Allowed again, it goes to bed the next night; forbidden then, it sleeps on. */
    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_STRUCTURE, false));
    colonist->needs[NEED_REST] = 1.0f;
    run_ticks(&game, 10);
    colonist->needs[NEED_REST] = 0.2f;
    for (int i = 0; i < TICKS_PER_HOUR && !(colonist->x == 20 && colonist->y == 10); i++) {
        game_tick(&game);
    }
    REQUIRE(colonist->x == 20 && colonist->y == 10);
    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_STRUCTURE, true));
    for (int i = 0; i < 600; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(colonist->task, TASK_SLEEP);
    CHECK(colonist->x == 20 && colonist->y == 10);
    game_shutdown(&game);
}

/* Orders on forbidden things wait: a wall is not taken apart, a tree not
 * felled, even with the cheat. */
static void test_orders_on_forbidden_things_wait(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    map_set_structure(map, 12, 10, 0, STRUCTURE_WALL);
    CHECK(game_set_forbidden(&game, 12, 10, 0, FORBID_STRUCTURE, true));
    REQUIRE(game_designate_rect(&game, 12, 10, 12, 10, 0, DESIGNATION_DECONSTRUCT) == 1);
    CHECK(designation_forbidden(&game.designations, map, 12, 10, 0));
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_EQ_INT(map_structure(map, 12, 10, 0), STRUCTURE_WALL);
    CHECK(colonist->task != TASK_WORK);

    /* Allowed: the order is carried out. */
    uint32_t revision = game.designations.revision;
    CHECK(game_set_forbidden(&game, 12, 10, 0, FORBID_STRUCTURE, false));
    CHECK(game.designations.revision != revision);
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_EQ_INT(map_structure(map, 12, 10, 0), STRUCTURE_NONE);

    /* A tree forbidden from a tile of its trunk: the cheat leaves it standing. */
    map->plants[map_index(map, 30, 30, 0)] = (Plant){.type = PLANT_OAK, .growth = 1.0f};
    tree_build(map, 30, 30, 0);
    REQUIRE(map->rock[map_index(map, 30, 30, 1)] == ROCK_WOOD);
    CHECK(game_can_forbid(&game, 30, 30, 1, FORBID_PLANT));
    CHECK(game_set_forbidden(&game, 30, 30, 1, FORBID_PLANT, true));
    CHECK(map_forbidden(map, 30, 30, 0, MAP_FORBID_PLANT)); /* On its foot. */
    CHECK(game_forbidden(&game, 30, 30, 1, FORBID_PLANT));
    REQUIRE(game_designate_rect(&game, 30, 30, 30, 30, 0, DESIGNATION_CHOP) == 1);
    game_set_instant_fell_and_mine(&game, true);
    run_ticks(&game, 2);
    CHECK_EQ_INT(map->plants[map_index(map, 30, 30, 0)].type, PLANT_OAK);
    CHECK_EQ_INT(designation_get(&game.designations, 30, 30, 0), DESIGNATION_CHOP);
    CHECK(game_set_forbidden(&game, 30, 30, 0, FORBID_PLANT, false));
    run_ticks(&game, 1);
    CHECK_EQ_INT(map->plants[map_index(map, 30, 30, 0)].type, PLANT_NONE);
    CHECK_EQ_INT(map->forbidden_count, 0);
    game_shutdown(&game);
}

/* A forbidden plant offers no gathering job. */
static void test_forbidden_plants_are_not_gathered(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    Plant *bramble = &map->plants[map_index(map, 5, 5, 0)];
    bramble->type = PLANT_BRAMBLE;
    bramble->growth = 1.0f;
    plant_settle(bramble, GAME_START_DAY);
    int zone = game_add_gather_zone_rect(&game, 4, 4, 6, 6, 0, GATHER_ZONE_NONE);
    REQUIRE(zone != GATHER_ZONE_NONE);
    gather_zone_set_enabled(&game.gather_zones, zone, GATHER_BLACKBERRIES, true);
    gather_zone_set_mode(&game.gather_zones, zone, GATHER_BLACKBERRIES, GATHER_FOREVER);
    gather_zones_update(&game.gather_zones, game.items.totals, 0.0f);
    GatherJobMask all = (GatherJobMask)((1u << GATHER_JOB_COUNT) - 1u);
    GatherJob job = gather_tile_job(&game.gather_zones, map, 5, 5, 0, all);
    REQUIRE(job != GATHER_JOB_NONE);

    CHECK(game_set_forbidden(&game, 5, 5, 0, FORBID_PLANT, true));
    CHECK_EQ_INT(gather_tile_job(&game.gather_zones, map, 5, 5, 0, all), GATHER_JOB_NONE);
    CHECK(!gather_job_available(&game.gather_zones, map, 5, 5, 0, job));
    uint32_t revision = game.gather_zones.revision;
    CHECK(game_set_forbidden(&game, 5, 5, 0, FORBID_PLANT, false));
    CHECK(game.gather_zones.revision != revision); /* Settlers look again. */
    CHECK_EQ_INT(gather_tile_job(&game.gather_zones, map, 5, 5, 0, all), job);
    game_shutdown(&game);
}

/* Nobody crafts at a forbidden station. */
static void test_forbidden_station_is_not_used(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Colonist *colonist = spawn_worker(&game, 10, 10);
    REQUIRE(colonist != NULL);
    colonist_add_tool(colonist, item_stack_make(ITEM_COBBLE, 1, ITEM_QUALITY_STANDARD, 0));
    int index =
        workstations_place(&game.workstations, &game.map, WORKSTATION_CRAFTING_SPOT, 14, 10, 0);
    REQUIRE(index != WORKSTATION_NONE);
    items_add(&game.items, 15, 10, 0, item_stack_make(ITEM_FLINT, 2, ITEM_QUALITY_STANDARD, 0));
    workstation_add_bill(&game.workstations, index, RECIPE_FLAKES);
    CHECK(!game_can_forbid(&game, 13, 10, 0, FORBID_STATION));
    CHECK(game_can_forbid(&game, 14, 10, 0, FORBID_STATION));
    CHECK(game_set_forbidden(&game, 14, 10, 0, FORBID_STATION, true));
    CHECK(game_forbidden(&game, 14, 10, 0, FORBID_STATION));
    run_ticks(&game, 1200);
    CHECK_EQ_INT((int)game.items.totals[ITEM_FLAKE], 0);
    CHECK(colonist->task != TASK_CRAFT);

    uint32_t revision = game.workstations.revision;
    CHECK(game_set_forbidden(&game, 14, 10, 0, FORBID_STATION, false));
    CHECK(game.workstations.revision != revision);
    run_ticks(&game, 1200);
    CHECK(game.items.totals[ITEM_FLAKE] > 0);
    game_shutdown(&game);
}

/* The forbid tool: everything on the tiles of a rectangle. */
static void test_forbid_rect(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    items_add(&game.items, 3, 3, 0, item_stack_pristine(ITEM_BRANCHES, 2));
    map->plants[map_index(map, 4, 3, 0)] = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    map_set_structure(map, 5, 3, 0, STRUCTURE_DOOR);
    REQUIRE(workstations_place(&game.workstations, map, WORKSTATION_CRAFTING_SPOT, 6, 3, 0) !=
            WORKSTATION_NONE);
    map->plants[map_index(map, 3, 4, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    items_add(&game.items, 9, 9, 0, item_stack_pristine(ITEM_BRANCHES, 2)); /* Outside. */
    CHECK_EQ_INT(game_set_forbidden_rect(&game, 6, 4, 2, 2, 0, true), 5);
    CHECK_EQ_INT(game_set_forbidden_rect(&game, 2, 2, 6, 4, 0, true), 0); /* Already. */
    CHECK(game_forbidden(&game, 3, 3, 0, FORBID_ITEMS));
    CHECK(game_forbidden(&game, 4, 3, 0, FORBID_PLANT));
    CHECK(game_forbidden(&game, 5, 3, 0, FORBID_STRUCTURE));
    CHECK(game_forbidden(&game, 6, 3, 0, FORBID_STATION));
    CHECK(!game_forbidden(&game, 9, 9, 0, FORBID_ITEMS));
    CHECK(game_forbidden(&game, 3, 4, 0, FORBID_PLANT)); /* Grass is a plant like the others. */
    CHECK_EQ_INT(game_set_forbidden_rect(&game, 0, 0, 47, 47, 0, false), 5);
    CHECK(!game_forbidden(&game, 3, 4, 0, FORBID_PLANT));
    CHECK_EQ_INT(map->forbidden_count, 0);
    CHECK_EQ_INT(game.items.forbidden_count, 0);
    game_shutdown(&game);
}

/* A forbidden stockpile gets nothing and gives nothing back; what lies in it
 * still counts as stored. */
static void test_forbidden_stockpile(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    items_add(&game.items, 10, 10, 0, item_stack_pristine(ITEM_BLACKBERRIES, 10));
    items_add(&game.items, 30, 10, 0, item_stack_pristine(ITEM_BRANCHES, 4));
    int near = game_add_stockpile_rect(&game, 20, 10, 20, 10, 0, STOCKPILE_NONE);
    int far = game_add_stockpile_rect(&game, 30, 10, 30, 10, 0, STOCKPILE_NONE);
    REQUIRE(near != STOCKPILE_NONE && far != STOCKPILE_NONE);
    stockpile_set_allowed(&game.stockpiles, far, ITEM_TYPE_BIT(ITEM_BRANCHES));
    stockpile_set_allowed(&game.stockpiles, near, ITEM_TYPE_BIT(ITEM_BLACKBERRIES));
    CHECK(game_can_forbid(&game, 20, 10, 0, FORBID_STOCKPILE));
    CHECK(!game_can_forbid(&game, 21, 10, 0, FORBID_STOCKPILE));
    CHECK(!game_forbidden(&game, 20, 10, 0, FORBID_STOCKPILE));
    uint32_t revision = game.stockpiles.revision;
    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_STOCKPILE, true));
    CHECK(!game_set_forbidden(&game, 20, 10, 0, FORBID_STOCKPILE, true)); /* Already. */
    CHECK(game_forbidden(&game, 20, 10, 0, FORBID_STOCKPILE));
    CHECK(game.stockpiles.revision != revision); /* Haulers look again. */
    CHECK_EQ_INT(haul_destination_room(&game.stockpiles, &game.items, 20, 10, 0,
                                       item_stack_pristine(ITEM_BLACKBERRIES, 1)),
                 0);
    /* The branches lie stored in the forbidden far stockpile and a better one
     * for them appears: they stay put until it is allowed. */
    CHECK(game_set_forbidden(&game, 30, 10, 0, FORBID_STOCKPILE, true));
    int better = game_add_stockpile_rect(&game, 25, 10, 25, 10, 0, STOCKPILE_NONE);
    REQUIRE(better != STOCKPILE_NONE);
    stockpile_set_allowed(&game.stockpiles, better, ITEM_TYPE_BIT(ITEM_BRANCHES));
    stockpile_set_priority(&game.stockpiles, better, STOCKPILE_PRIORITY_HIGHEST);
    long stored[ITEM_TYPE_COUNT] = {0};
    stockpiles_count_stored(&game.stockpiles, &game.items, stored);
    CHECK_EQ_INT((int)stored[ITEM_BRANCHES], 4); /* Still counts. */
    REQUIRE(spawn_worker(&game, 14, 14) != NULL);
    run_ticks(&game, 1500);
    CHECK_EQ_INT(items_get(&game.items, 10, 10, 0).count, 10);
    CHECK_EQ_INT(items_get(&game.items, 30, 10, 0).count, 4);
    CHECK_EQ_INT(items_get(&game.items, 25, 10, 0).count, 0);

    /* Both allowed again: the berries go in, the branches to the better one. */
    CHECK(game_set_forbidden(&game, 20, 10, 0, FORBID_STOCKPILE, false));
    CHECK(game_set_forbidden(&game, 30, 10, 0, FORBID_STOCKPILE, false));
    run_ticks(&game, 3000);
    CHECK_EQ_INT(items_get(&game.items, 20, 10, 0).count, 10);
    CHECK_EQ_INT(items_get(&game.items, 25, 10, 0).count, 4);
    game_shutdown(&game);
}

/* Nobody gathers in a forbidden zone; allowed again, the zone's orders stand. */
static void test_forbidden_gather_zone(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    Plant *bramble = &map->plants[map_index(map, 5, 5, 0)];
    bramble->type = PLANT_BRAMBLE;
    bramble->growth = 1.0f;
    plant_settle(bramble, GAME_START_DAY);
    int zone = game_add_gather_zone_rect(&game, 4, 4, 6, 6, 0, GATHER_ZONE_NONE);
    REQUIRE(zone != GATHER_ZONE_NONE);
    gather_zone_set_enabled(&game.gather_zones, zone, GATHER_BLACKBERRIES, true);
    gather_zone_set_mode(&game.gather_zones, zone, GATHER_BLACKBERRIES, GATHER_FOREVER);
    gather_zones_update(&game.gather_zones, game.items.totals, 0.0f);
    GatherJobMask all = (GatherJobMask)((1u << GATHER_JOB_COUNT) - 1u);
    GatherJob job = gather_tile_job(&game.gather_zones, map, 5, 5, 0, all);
    REQUIRE(job != GATHER_JOB_NONE);
    CHECK(game.gather_zones.wanting_cells > 0);

    CHECK(game_can_forbid(&game, 4, 4, 0, FORBID_GATHER_ZONE));
    CHECK(!game_can_forbid(&game, 8, 8, 0, FORBID_GATHER_ZONE));
    uint32_t revision = game.gather_zones.revision;
    CHECK(game_set_forbidden(&game, 6, 6, 0, FORBID_GATHER_ZONE, true)); /* From any tile. */
    CHECK(!game_set_forbidden(&game, 4, 4, 0, FORBID_GATHER_ZONE, true));
    CHECK(game_forbidden(&game, 5, 5, 0, FORBID_GATHER_ZONE));
    CHECK(game.gather_zones.revision != revision);
    CHECK_EQ_INT(gather_tile_job(&game.gather_zones, map, 5, 5, 0, all), GATHER_JOB_NONE);
    CHECK_EQ_INT(game.gather_zones.wanting_cells, 0);
    gather_zones_update(&game.gather_zones, game.items.totals, 0.0f); /* Stays so. */
    CHECK_EQ_INT(gather_tile_job(&game.gather_zones, map, 5, 5, 0, all), GATHER_JOB_NONE);
    REQUIRE(spawn_worker(&game, 10, 10) != NULL);
    run_ticks(&game, TICKS_PER_HOUR);
    CHECK_EQ_INT(game.items.totals[ITEM_BLACKBERRIES], 0);

    CHECK(game_set_forbidden(&game, 5, 5, 0, FORBID_GATHER_ZONE, false));
    gather_zones_update(&game.gather_zones, game.items.totals, 0.0f);
    CHECK_EQ_INT(gather_tile_job(&game.gather_zones, map, 5, 5, 0, all), job);
    CHECK(gather_zone_get(&game.gather_zones, zone)->orders[GATHER_BLACKBERRIES].enabled);
    run_ticks(&game, 2 * TICKS_PER_HOUR);
    CHECK(game.items.totals[ITEM_BLACKBERRIES] > 0);
    game_shutdown(&game);
}

/* A forbidden plan gets no material and no work; allowed, it goes on. */
static void test_forbidden_site(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int wall = game_place_structure(&game, STRUCTURE_WALL, 12, 10, 0, DIRECTION_EAST);
    REQUIRE(wall != CONSTRUCTION_NONE);
    const ConstructionSite *site = construction_get(&game.constructions, wall);
    REQUIRE(site != NULL);
    CHECK(game_can_forbid(&game, 12, 10, 0, FORBID_SITE));
    CHECK(!game_can_forbid(&game, 13, 10, 0, FORBID_SITE) ||
          construction_at(&game.constructions, 13, 10, 0) == wall); /* Any tile of the site. */
    CHECK(!construction_forbidden(site, &game.map));
    uint32_t revision = game.constructions.revision;
    CHECK(game_set_forbidden(&game, 12, 10, 0, FORBID_SITE, true));
    CHECK(!game_set_forbidden(&game, 12, 10, 0, FORBID_SITE, true));
    CHECK(game_forbidden(&game, 12, 10, 0, FORBID_SITE));
    CHECK(game.constructions.revision != revision);
    CHECK(construction_forbidden(site, &game.map));
    CHECK(!construction_can_work(site, &game.map));

    /* Even the cheat that builds at once leaves it. */
    game_set_instant_build(&game, true);
    run_ticks(&game, 5);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10, 0), STRUCTURE_NONE);
    CHECK_EQ_INT(construction_at(&game.constructions, 12, 10, 0), wall);
    CHECK(game_set_forbidden(&game, 12, 10, 0, FORBID_SITE, false));
    run_ticks(&game, 5);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10, 0), STRUCTURE_WALL);
    CHECK(!game_can_forbid(&game, 12, 10, 0, FORBID_SITE)); /* The plan is gone. */
    game_shutdown(&game);
}

/* Settlers bring no material to a forbidden plan and build nothing. */
static void test_settlers_leave_a_forbidden_site(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int wall = game_place_structure(&game, STRUCTURE_WALL, 12, 10, 0, DIRECTION_EAST);
    REQUIRE(wall != CONSTRUCTION_NONE);
    for (int i = 0; i < 6; i++) {
        items_add(&game.items, 8 + i, 6, 0, item_stack_pristine(ITEM_GRASS, 40));
        items_add(&game.items, 8 + i, 7, 0, item_stack_pristine(ITEM_BRANCHES, 20));
    }
    CHECK(game_set_forbidden(&game, 12, 10, 0, FORBID_SITE, true));
    REQUIRE(spawn_worker(&game, 10, 10) != NULL);
    run_ticks(&game, 2 * TICKS_PER_HOUR);
    const ConstructionSite *site = construction_get(&game.constructions, wall);
    REQUIRE(site != NULL);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10, 0), STRUCTURE_NONE);
    for (int slot = 0; slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
        CHECK_EQ_INT(site->delivered[slot].count, 0);
    }
    CHECK_EQ_INT(game.reservations.count, 0);

    /* Allowed: the materials come and the wall is built. */
    CHECK(game_set_forbidden(&game, 12, 10, 0, FORBID_SITE, false));
    run_ticks(&game, 6 * TICKS_PER_HOUR);
    CHECK_EQ_INT(map_structure(&game.map, 12, 10, 0), STRUCTURE_WALL);
    game_shutdown(&game);
}

/* The forbid rectangle takes plans in, but not whole zones. */
static void test_forbid_rect_plans_and_zones(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    int wall = game_place_structure(&game, STRUCTURE_WALL, 12, 10, 0, DIRECTION_EAST);
    REQUIRE(wall != CONSTRUCTION_NONE);
    int stockpile = game_add_stockpile_rect(&game, 20, 10, 22, 10, 0, STOCKPILE_NONE);
    int zone = game_add_gather_zone_rect(&game, 30, 10, 32, 10, 0, GATHER_ZONE_NONE);
    REQUIRE(stockpile != STOCKPILE_NONE && zone != GATHER_ZONE_NONE);
    CHECK_EQ_INT(game_set_forbidden_rect(&game, 0, 0, 47, 47, 0, true), 1);
    CHECK(game_forbidden(&game, 12, 10, 0, FORBID_SITE));
    CHECK(!game_forbidden(&game, 21, 10, 0, FORBID_STOCKPILE));
    CHECK(!game_forbidden(&game, 31, 10, 0, FORBID_GATHER_ZONE));
    CHECK_EQ_INT(game_set_forbidden_rect(&game, 0, 0, 47, 47, 0, false), 1);
    CHECK(!game_forbidden(&game, 12, 10, 0, FORBID_SITE));
    game_shutdown(&game);
}

/* A game as played starts with the items beyond FREE_RADIUS tiles of the arrival point
 * forbidden, so that nobody walks across the map for materials; the others lie allowed. */
static void test_items_start_forbidden_far_from_the_arrival(void)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 11;
    params.world.width = 96;
    params.world.height = 96;
    params.colonist_count = 3;
    params.free_radius = 10;
    Game game;
    REQUIRE(game_init(&game, &params));
    REQUIRE(game.has_arrival);
    int near_allowed = 0, far_forbidden = 0, wrong = 0;
    for (int z = 0; z < game.map.depth; z++) {
        for (int y = 0; y < game.map.height; y++) {
            for (int x = 0; x < game.map.width; x++) {
                if (items_get(&game.items, x, y, z).count == 0) {
                    continue;
                }
                int dx = x - game.arrival.x, dy = y - game.arrival.y;
                bool near = dx * dx + dy * dy <= 10 * 10;
                bool forbidden = items_forbidden(&game.items, x, y, z);
                near_allowed += near && !forbidden;
                far_forbidden += !near && forbidden;
                wrong += near == forbidden;
            }
        }
    }
    CHECK_EQ_INT(wrong, 0);
    CHECK(near_allowed > 0); /* The flint near the arrival, at least. */
    CHECK(far_forbidden > 0);
    game_shutdown(&game);

    /* The default (tests, benchmarks): nothing forbidden. */
    params.free_radius = 0;
    REQUIRE(game_init(&game, &params));
    for (int z = 0; z < game.map.depth; z++) {
        for (int y = 0; y < game.map.height; y++) {
            for (int x = 0; x < game.map.width; x++) {
                CHECK(!items_forbidden(&game.items, x, y, z));
            }
        }
    }
    game_shutdown(&game);
}

/* What falls from the trees during a game lands forbidden beyond the free radius of the arrival
 * point, and allowed within it: nobody walks across the map for a branch. */
static void test_what_falls_from_trees_lands_forbidden_far_away(void)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 40;
    params.world.height = 40;
    params.colonist_count = 1;
    params.free_radius = 10;
    Game game;
    REQUIRE(game_init(&game, &params));
    REQUIRE(game.has_arrival);
    CHECK_EQ_INT(game.free_radius, 10);
    const int ax = game.arrival.x, ay = game.arrival.y;
    const int near_x = ax + 2, far_x = ax + 18 < 36 ? ax + 18 : ax - 18;
    CHECK(far_x >= 0 && far_x + 4 < 40);
    /* Dead wood hanging in foliage over a near and a far tile, marked by a quality of its own. */
    const int marked = 91;
    int hang[2] = {near_x, far_x};
    for (int i = 0; i < 2; i++) {
        for (int dx = 0; dx < 4; dx++) { /* A tile holds only so much. */
            map_set_rock(&game.map, hang[i] + dx, ay, 1, ROCK_NONE); /* No trunk in the way. */
            map_set_terrain(&game.map, hang[i] + dx, ay, 1, TERRAIN_FOLIAGE);
        }
        for (int n = 0; n < 40; n++) {
            CrownItem branch = {.type = ITEM_BRANCHES,
                                .quality = (uint8_t)marked,
                                .hit_points = (uint16_t)item_def(ITEM_BRANCHES)->max_hit_points};
            REQUIRE(crowns_add(&game.crowns, &game.map, hang[i] + n / 10, ay, 1, branch));
        }
    }
    /* The settler does nothing meanwhile: it would carry the branches. */
    EntityId settler = game_colonist_id_at(&game, 0);
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        game_set_work_priority(&game, settler, (WorkType)type, WORK_PRIORITY_DISABLED);
    }
    /* A few hours: some of them fall (a chance an hour each). */
    for (int i = 0; i < 6 * TICKS_PER_HOUR; i++) {
        for (int need = 0; need < NEED_COUNT; need++) {
            game.colonists[settler.index].needs[need] = 1.0f;
        }
        game_tick(&game);
    }
    int near_found = 0, far_found = 0, wrong = 0;
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 40; x++) {
            for (const Item *item = items_first(&game.items, map_index(&game.map, x, y, 0));
                 item != NULL; item = items_next(&game.items, item)) {
                if (item->type != ITEM_BRANCHES || item->quality != marked) {
                    continue;
                }
                bool near = (x - ax) * (x - ax) + (y - ay) * (y - ay) <= 100;
                near_found += near;
                far_found += !near;
                wrong += near == (item->forbidden != 0);
            }
        }
    }
    CHECK(near_found > 0);
    CHECK(far_found > 0);
    CHECK_EQ_INT(wrong, 0);
    /* Not for the other drops: what a settler puts down far away stays allowed. */
    ItemStack axe = item_stack_pristine(ITEM_HAND_AXE, 1); /* None lies about in a new world. */
    REQUIRE(items_drop(&game.items, &game.map, far_x, ay + 8, 0, axe) == 1);
    bool allowed = false, seen = false;
    for (int y = ay + 4; y <= ay + 12; y++) {
        for (int x = far_x - 4 > 0 ? far_x - 4 : 0; x <= far_x + 4 && x < 40; x++) {
            for (const Item *item = items_first(&game.items, map_index(&game.map, x, y, 0));
                 item != NULL; item = items_next(&game.items, item)) {
                if (item->type == ITEM_HAND_AXE) {
                    seen = true;
                    allowed |= item->forbidden == 0;
                }
            }
        }
    }
    CHECK(seen);
    CHECK(allowed);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_map_forbid_bits);
    RUN_TEST(test_locked_doors);
    RUN_TEST(test_locked_ladder);
    RUN_TEST(test_forbidden_items);
    RUN_TEST(test_haulers_leave_forbidden_items);
    RUN_TEST(test_forbidding_stops_a_hauler_on_its_way);
    RUN_TEST(test_forbidden_food_is_not_eaten);
    RUN_TEST(test_forbidden_bed_is_left_alone);
    RUN_TEST(test_bed_forbidden_on_the_way);
    RUN_TEST(test_orders_on_forbidden_things_wait);
    RUN_TEST(test_forbidden_plants_are_not_gathered);
    RUN_TEST(test_forbidden_station_is_not_used);
    RUN_TEST(test_forbid_rect);
    RUN_TEST(test_forbidden_stockpile);
    RUN_TEST(test_forbidden_gather_zone);
    RUN_TEST(test_forbidden_site);
    RUN_TEST(test_settlers_leave_a_forbidden_site);
    RUN_TEST(test_forbid_rect_plans_and_zones);
    RUN_TEST(test_items_start_forbidden_far_from_the_arrival);
    RUN_TEST(test_what_falls_from_trees_lands_forbidden_far_away);
    return test_summary();
}
