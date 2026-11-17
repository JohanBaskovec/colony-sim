#include "core/game.h"

#include "test_framework.h"

#include <stdint.h>

#include <stdlib.h>

static bool init_small_game(Game *game, uint64_t seed)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = 64;
    params.world.height = 64;
    return game_init(game, &params);
}

/* The same on a single level of ground (see make_grove()). */
static bool init_flat_game(Game *game, uint64_t seed)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = 64;
    params.world.height = 64;
    params.world.flat = true;
    return game_init(game, &params);
}

static void test_init_and_shutdown(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 9));
    CHECK_EQ_UINT(game.seed, 9);
    CHECK_EQ_INT(game.map.width, 64);
    CHECK_EQ_INT(game.clock.speed, GAME_SPEED_NORMAL);
    CHECK_EQ_UINT(game.colonist_pool.count, GAME_DEFAULT_COLONIST_COUNT);
    game_shutdown(&game);
    CHECK(game.map.terrain == NULL);
}

typedef struct FoodPlantCount {
    PlantType type;
    int count;
} FoodPlantCount;

/* Counts the grown food plants of a kind and never accepts a tile, to visit
 * the whole area. */
static bool count_food_plant(const Map *map, int x, int y, int z, void *userdata)
{
    FoodPlantCount *count = userdata;
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    if (plant->type == count->type && plant->growth >= plant_def(count->type)->min_food_growth) {
        count->count++;
    }
    return false;
}

/* At the end of summer: brambles bearing fruit near the arrival point, and
 * hazels for the autumn. */
static void test_settlers_arrive_near_food(void)
{
    for (uint64_t seed = 1; seed <= 40; seed++) {
        Game game;
        REQUIRE(init_small_game(&game, seed));
        const Colonist *first = game_colonist_const(&game, game_colonist_id_at(&game, 0));
        REQUIRE(first != NULL);
        TilePos start = {(int16_t)first->x, (int16_t)first->y, (int16_t)first->z};
        FoodPlantCount brambles = {PLANT_BRAMBLE, 0};
        pathfinder_find_nearest(&game.pathfinder, &game.map, start, count_food_plant, &brambles,
                                GAME_SPAWN_FOOD_MAX_COST, NULL, NULL);
        CHECK(brambles.count >= GAME_SPAWN_BRAMBLES);
        /* Hazels are trees now: their trunks are not walked through, so they
         * are counted around the settlers' start. */
        int hazels = 0;
        for (int z = 0; z < game.map.depth; z++) {
            for (int y = start.y - 45; y <= start.y + 45; y++) {
                for (int x = start.x - 45; x <= start.x + 45; x++) {
                    if (map_in_bounds(&game.map, x, y, z)) {
                        const Plant *plant = &game.map.plants[map_index(&game.map, x, y, z)];
                        hazels += plant->type == PLANT_HAZEL && plant->growth >= TREE_CROWN_FROM;
                    }
                }
            }
        }
        CHECK(hazels >= GAME_SPAWN_HAZELS);
        game_shutdown(&game);
    }
}

/* Mushrooms come up under mature trees on the first autumn nights, and only
 * there; they go again within days. */
/* Soil everywhere: a grove on the west half, a meadow on the east. */
static void make_grove(Map *map)
{
    for (size_t i = map_level_tile_count(map); i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_OPEN; /* Open air over it: no trees rising there. */
        map->rock[i] = ROCK_NONE;
    }
    for (size_t i = 0; i < map_level_tile_count(map); i++) { /* The ground level. */
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        map->structure[i] = STRUCTURE_NONE;
        /* A grove on the west half, meadow on the east. */
        bool grove = (int)(i % (size_t)map->width) < map->width / 2;
        map->plants[i] = grove && i % 7 == 0 ? (Plant){.type = PLANT_TREE, .growth = 1.0f}
                                             : (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    }
    map_topology_changed(map);
}

/* Mushroom patches that came up during the first hour of an autumn day,
 * with `rain` hours of recent rain. */
static unsigned mushrooms_after(double rain)
{
    Game game;
    if (!init_flat_game(&game, 3)) {
        return 0;
    }
    make_grove(&game.map);
    game.clock.tick = (uint64_t)SEASON_DAY(SEASON_AUTUMN, 4) * TICKS_PER_DAY;
    for (int i = 0; i < TICKS_PER_HOUR; i++) {
        game.weather.recent_rain = (float)rain;
        game_tick(&game);
    }
    unsigned patches = (unsigned)map_count_plants(&game.map, PLANT_MUSHROOMS);
    game_shutdown(&game);
    return patches;
}

static void test_mushrooms_come_after_rain(void)
{
    unsigned dry = mushrooms_after(0.0);
    unsigned wet = mushrooms_after(20.0);
    CHECK(wet > 2 * dry);
    CHECK(wet > 0);
}

static void test_mushrooms_come_and_go(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 3));
    Map *map = &game.map;
    make_grove(map);
    /* Summer: none. */
    game.clock.tick = (uint64_t)SEASON_DAY(SEASON_SUMMER, 5) * TICKS_PER_DAY;
    for (int i = 0; i < TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK_EQ_UINT(map_count_plants(map, PLANT_MUSHROOMS), 0);
    /* The first nights of autumn. */
    int patches = 0;
    for (int day = 1; day <= 3; day++) {
        game.clock.tick = (uint64_t)SEASON_DAY(SEASON_AUTUMN, day) * TICKS_PER_DAY;
        for (int i = 0; i < TICKS_PER_HOUR; i++) {
            game_tick(&game);
        }
    }
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            const Plant *plant = &map->plants[map_index(map, x, y, 0)];
            if (plant->type == PLANT_MUSHROOMS) {
                patches++;
                CHECK(map_tree_near(map, x, y, 0, 2));
                CHECK(plant_has_food(plant));
            }
        }
    }
    CHECK(patches > 0);
    /* Two weeks on, in winter: all gone. */
    game.clock.tick = (uint64_t)SEASON_DAY(SEASON_WINTER, 2) * TICKS_PER_DAY;
    for (int i = 0; i < TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK_EQ_UINT(map_count_plants(map, PLANT_MUSHROOMS), 0);
    game_shutdown(&game);
}

/* Accepts a tile of a rich enough flint patch where stones can be gathered. */
static bool is_flint_spot(const Map *map, int x, int y, int z, void *userdata)
{
    (void)userdata;
    return map_stones(map, x, y, z, ITEM_FLINT) >= GAME_SPAWN_FLINT_STONES &&
           gather_stone_ground(map, x, y, z);
}

static void test_settlers_arrive_near_flint(void)
{
    for (uint64_t seed = 1; seed <= 40; seed++) {
        Game game;
        REQUIRE(init_small_game(&game, seed));
        const Colonist *first = game_colonist_const(&game, game_colonist_id_at(&game, 0));
        REQUIRE(first != NULL);
        TilePos start = {(int16_t)first->x, (int16_t)first->y, (int16_t)first->z};
        CHECK(pathfinder_find_nearest(&game.pathfinder, &game.map, start, is_flint_spot, NULL,
                                      GAME_SPAWN_FOOD_MAX_COST, NULL, NULL));
        game_shutdown(&game);
    }
}

static void test_init_failure_leaves_game_safe_to_shut_down(void)
{
    Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.width = 1;
    CHECK(!game_init(&game, &params));
    game_shutdown(&game);
}

static void test_update_advances_ticks(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 1));
    uint64_t start = game.clock.tick;
    int ran = game_update(&game, 0.25);
    CHECK_EQ_INT(ran, TICKS_PER_SECOND / 4);
    CHECK_EQ_UINT(game.clock.tick, start + (uint64_t)ran);

    game_clock_toggle_pause(&game.clock);
    CHECK_EQ_INT(game_update(&game, 1.0), 0);
    CHECK_EQ_UINT(game.clock.tick, start + (uint64_t)ran);
    game_shutdown(&game);
}

static void test_plants_grow_over_time(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 3));
    Map *map = &game.map;

    /* Controlled setup on known terrain. */
    size_t fertile = map_index(map, 1, 1, 0);
    size_t barren = map_index(map, 2, 1, 0);
    map_set_rock(map, 1, 1, 0, ROCK_NONE);
    map_set_rock(map, 2, 1, 0, ROCK_NONE);
    map_set_terrain(map, 1, 1, 0, TERRAIN_SOIL);
    map_set_terrain(map, 2, 1, 0, TERRAIN_SAND);
    map->plants[fertile].type = PLANT_GRASS;
    map->plants[fertile].growth = 0.1f;
    map->plants[barren].type = PLANT_GRASS;
    map->plants[barren].growth = 0.1f;

    for (int i = 0; i < TICKS_PER_DAY; i++) {
        game_tick(&game);
    }

    float expected =
        0.1f + plant_def(PLANT_GRASS)->growth_per_day * terrain_def(TERRAIN_SOIL)->fertility;
    CHECK_NEAR(map->plants[fertile].growth, expected, 1e-4);
    /* Sand is below the minimum fertility of grass: no growth. */
    CHECK_NEAR(map->plants[barren].growth, 0.1, 1e-6);

    /* Growth saturates at 1. */
    for (int i = 0; i < TICKS_PER_DAY * 10; i++) {
        game_tick(&game);
    }
    CHECK_NEAR(map->plants[fertile].growth, 1.0, 1e-6);
    game_shutdown(&game);
}

/* Growth is spread over the ticks of an interval: a tick only touches a
 * slice of the map, and after a whole interval every plant grew once. */
static void test_plant_growth_is_spread_over_ticks(void)
{
    Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 7;
    params.world.width = 64;
    params.world.height = 64;
    params.colonist_count = 0; /* Nobody eats the plants meanwhile. */
    REQUIRE(game_init(&game, &params));
    Map *map = &game.map;
    size_t count = map_tile_count(map);
    float *before = malloc(count * sizeof(*before));
    REQUIRE(before != NULL);
    size_t growing = 0;
    for (size_t i = 0; i < count; i++) {
        const Plant *plant = &map->plants[i];
        before[i] = plant->growth;
        const PlantDef *def = plant_def((PlantType)plant->type);
        if (plant->type != PLANT_NONE && plant->growth < 1.0f &&
            plant_days_contain(def->growth_days, game_day_of_year(game.clock.tick)) &&
            terrain_def((Terrain)map->terrain[i])->fertility >= def->min_fertility) {
            growing++;
        }
    }
    REQUIRE(growing > 100);

    size_t grown = 0, most_in_one_tick = 0;
    for (int tick = 0; tick < GAME_PLANT_UPDATE_INTERVAL; tick++) {
        game_tick(&game);
        size_t now = 0;
        for (size_t i = 0; i < count; i++) {
            if (map->plants[i].growth != before[i]) {
                now++;
            }
        }
        most_in_one_tick = now - grown > most_in_one_tick ? now - grown : most_in_one_tick;
        grown = now;
    }
    CHECK_EQ_UINT(grown, growing);
    CHECK(most_in_one_tick <= count / GAME_PLANT_UPDATE_INTERVAL + 1);

    /* Not twice: one more interval gives each plant one more step. */
    size_t index = 0;
    while (index < count &&
           (map->plants[index].type == PLANT_NONE || map->plants[index].growth == before[index] ||
            map->plants[index].growth >= 0.9f)) {
        index++;
    }
    REQUIRE(index < count);
    float step = map->plants[index].growth - before[index];
    float after_one = map->plants[index].growth;
    for (int tick = 0; tick < GAME_PLANT_UPDATE_INTERVAL; tick++) {
        game_tick(&game);
    }
    CHECK_NEAR(map->plants[index].growth - after_one, step, 1e-6);
    free(before);
    game_shutdown(&game);
}

static void test_simulation_is_deterministic(void)
{
    Game a, b;
    REQUIRE(init_small_game(&a, 42));
    REQUIRE(init_small_game(&b, 42));
    for (int i = 0; i < TICKS_PER_DAY; i++) {
        game_tick(&a);
        game_tick(&b);
    }
    CHECK_EQ_UINT(map_hash(&a.map), map_hash(&b.map));
    CHECK_EQ_UINT(game_hash(&a), game_hash(&b));
    CHECK_EQ_UINT(a.clock.tick, b.clock.tick);
    game_shutdown(&a);
    game_shutdown(&b);
}

static void test_hash_changes_as_simulation_runs(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 8));
    uint64_t before = game_hash(&game);
    for (int i = 0; i < 600; i++) {
        game_tick(&game);
    }
    CHECK(game_hash(&game) != before);
    game_shutdown(&game);
}

static void test_hash_covers_knowledge_and_carried_condition(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 8));
    Colonist *colonist = game_colonist(&game, game_colonist_id_at(&game, 0));
    REQUIRE(colonist != NULL);
    uint64_t before = game_hash(&game);
    colonist_set_knowledge_level(colonist, KNOWLEDGE_GATHERING, 51.0);
    uint64_t known = game_hash(&game);
    CHECK(known != before);
    colonist_learn_by_doing(colonist, KNOWLEDGE_GATHERING, 1, 0); /* A tiny gain. */
    uint64_t learned = game_hash(&game);
    CHECK(learned != known);
    colonist_set_strength(colonist, colonist->strength == 5 ? 6 : 5);
    CHECK(game_hash(&game) != learned);

    colonist->carried = item_stack_make(ITEM_WOOD, 2, ITEM_QUALITY_FINE, 60);
    uint64_t carrying = game_hash(&game);
    colonist->carried.quality = ITEM_QUALITY_ROUGH;
    CHECK(game_hash(&game) != carrying);
    colonist->carried.quality = ITEM_QUALITY_FINE;
    colonist->carried.hit_points = 30;
    CHECK(game_hash(&game) != carrying);
    colonist->carried = item_stack_make(ITEM_FRESH_GRASS, 2, 0, 10);
    carrying = game_hash(&game);
    colonist->carried.progress = 5;
    CHECK(game_hash(&game) != carrying);
    colonist->carried = (ItemStack){.type = ITEM_NONE};
    game_shutdown(&game);
}

static void test_fresh_grass_dries_on_the_ground(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 8));
    /* Somewhere passable, away from anyone's hands. */
    int x = -1, y = -1;
    for (int ty = 0; ty < game.map.height && x < 0; ty++) {
        for (int tx = 0; tx < game.map.width && x < 0; tx++) {
            if (map_is_passable(&game.map, tx, ty, 0)) {
                x = tx;
                y = ty;
            }
        }
    }
    REQUIRE(x >= 0);
    for (uint32_t slot = 0; slot < game_colonist_slot_count(&game); slot++) {
        game_remove_colonist(&game, game_colonist_id_at(&game, slot));
    }
    int hours = item_def(ITEM_FRESH_GRASS)->transform_hours;
    REQUIRE(items_add(&game.items, x, y, 0, item_stack_make(ITEM_FRESH_GRASS, 3, 0, 5)) == 3);

    for (int i = 0; i < (hours - 1) * TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    ItemStack stack = items_get(&game.items, x, y, 0);
    CHECK_EQ_INT(stack.type, ITEM_FRESH_GRASS);
    CHECK(stack.progress >= hours - 2);

    for (int i = 0; i < 2 * TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    stack = items_get(&game.items, x, y, 0);
    CHECK_EQ_INT(stack.type, ITEM_GRASS);
    CHECK_EQ_INT(stack.count, 3);
    /* The condition carries over (in percent, rounded to whole hit points). */
    CHECK_EQ_INT(stack.hit_points, item_hit_points_at_percent(ITEM_GRASS, 50));
    CHECK_EQ_INT((int)game.items.totals[ITEM_FRESH_GRASS], 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_GRASS], 3);
    game_shutdown(&game);
}

static void test_food_rots_on_the_ground(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 8));
    for (uint32_t slot = 0; slot < game_colonist_slot_count(&game); slot++) {
        game_remove_colonist(&game, game_colonist_id_at(&game, slot));
    }
    /* Two passable tiles side by side, in the open. */
    int x = -1, y = -1;
    for (int ty = 1; ty < game.map.height && x < 0; ty++) {
        for (int tx = 0; tx + 1 < game.map.width && x < 0; tx++) {
            if (map_is_passable(&game.map, tx, ty, 0) &&
                map_is_passable(&game.map, tx + 1, ty, 0) &&
                !map_natural_roof(&game.map, tx, ty, 0) &&
                !map_natural_roof(&game.map, tx + 1, ty, 0)) {
                x = tx;
                y = ty;
            }
        }
    }
    REQUIRE(x >= 0);
    CHECK_NEAR(game_item_rot_rate(&game, -1, y, 0), 0.0f, 1e-6f);
    float rate = game_item_rot_rate(&game, x, y, 0);
    CHECK_NEAR(rate, item_rot_rate(temperature_at(&game.temperatures, x, y, 0), false), 1e-6f);
    CHECK(rate > 0.0f); /* The end of summer. */

    REQUIRE(items_add(&game.items, x, y, 0, item_stack_pristine(ITEM_MUSHROOMS, 3)) == 3);
    REQUIRE(items_add(&game.items, x + 1, y, 0, item_stack_pristine(ITEM_HAZELNUTS, 3)) == 3);
    float hours = item_stack_rot_hours(items_get(&game.items, x, y, 0), rate);
    for (int i = 0; i < (int)(hours * 0.5f) * TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(items_get(&game.items, x, y, 0).type, ITEM_MUSHROOMS);
    CHECK(items_get(&game.items, x, y, 0).hit_points < item_def(ITEM_MUSHROOMS)->max_hit_points);
    for (int i = 0; i < 3 * 24 * TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(items_get(&game.items, x, y, 0).type, ITEM_NONE);
    CHECK_EQ_INT((int)game.items.totals[ITEM_MUSHROOMS], 0);
    /* Nuts keep. */
    ItemStack nuts = items_get(&game.items, x + 1, y, 0);
    CHECK_EQ_INT(nuts.type, ITEM_HAZELNUTS);
    CHECK(item_stack_hit_points_percent(nuts) >= 90);
    game_shutdown(&game);
}

static void test_grass_does_not_dry_in_a_storm(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 8));
    for (uint32_t slot = 0; slot < game_colonist_slot_count(&game); slot++) {
        game_remove_colonist(&game, game_colonist_id_at(&game, slot));
    }
    int x = -1, y = -1;
    for (int ty = 1; ty < game.map.height && x < 0; ty++) {
        for (int tx = 0; tx < game.map.width && x < 0; tx++) {
            if (map_is_passable(&game.map, tx, ty, 0) &&
                temperature_cover(&game.map, tx, ty, 0) == TEMPERATURE_COVER_OPEN) {
                x = tx;
                y = ty;
            }
        }
    }
    REQUIRE(x >= 0);
    REQUIRE(items_add(&game.items, x, y, 0, item_stack_pristine(ITEM_FRESH_GRASS, 2)) == 2);
    for (int i = 0; i < 6 * TICKS_PER_HOUR; i++) {
        game.weather.type = WEATHER_STORM;
        game.weather.spell = 5.0f; /* Rain, not snow. */
        game_tick(&game);
    }
    CHECK_EQ_INT(items_get(&game.items, x, y, 0).progress, 0);
    CHECK(weather_wetting(&game.weather, game.clock.tick) > 0.0f);
    game.weather.type = WEATHER_CLEAR;
    for (int i = 0; i < 3 * TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK(items_get(&game.items, x, y, 0).progress >= 2);
    game_shutdown(&game);
}

static void test_plant_stocks_grow_back(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 8));
    /* A bramble in fruit (a tree's branches and twigs lie on the ground, not on the plant). */
    Plant *plant = &game.map.plants[0];
    game.map.rock[0] = ROCK_NONE;
    *plant = (Plant){.type = PLANT_BRAMBLE, .growth = 1.0f};
    /* Generated plants start with full stocks. */
    plant_settle(plant, GAME_START_DAY);
    int capacity = plant_stock_capacity(plant, PLANT_STOCK_FOOD);
    REQUIRE(capacity > 0);
    CHECK_EQ_INT(plant_stock_available(plant, PLANT_STOCK_FOOD), capacity);
    plant_take_stock(plant, PLANT_STOCK_FOOD, capacity);
    int regrow = plant_def(PLANT_BRAMBLE)->stock_regrow_hours[PLANT_STOCK_FOOD];
    for (int i = 0; i < (regrow + 1) * TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(plant_stock_available(plant, PLANT_STOCK_FOOD), 1);
    game_shutdown(&game);
}

static void test_zones_of_different_kinds_never_overlap(void)
{
    Game game;
    REQUIRE(init_small_game(&game, 4));
    for (int y = 10; y <= 20; y++) {
        for (int x = 10; x <= 20; x++) {
            map_set_rock(&game.map, x, y, 0, ROCK_NONE);
            map_set_terrain(&game.map, x, y, 0, TERRAIN_SOIL);
        }
    }
    int pile = game_add_stockpile_rect(&game, 10, 10, 12, 12, 0, STOCKPILE_NONE);
    REQUIRE(pile != STOCKPILE_NONE);

    /* A gathering zone drawn over the stockpile gets the other tiles only. */
    int zone = game_add_gather_zone_rect(&game, 11, 11, 14, 14, 0, GATHER_ZONE_NONE);
    REQUIRE(zone != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_get(&game.gather_zones, zone)->cell_count, 16 - 4);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 12, 12, 0), GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 13, 12, 0), zone);

    /* And the other way round: the stockpile grows around the zone. */
    CHECK_EQ_INT(game_add_stockpile_rect(&game, 12, 10, 16, 10, 0, pile), pile);
    CHECK_EQ_INT(stockpile_get(&game.stockpiles, pile)->cell_count, 9 + 4);
    CHECK_EQ_INT(game_add_stockpile_rect(&game, 13, 13, 14, 14, 0, STOCKPILE_NONE), STOCKPILE_NONE);
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 13, 13, 0), STOCKPILE_NONE);

    /* Merging zones of the same kind still works across the other kind. */
    int other = game_add_gather_zone_rect(&game, 18, 18, 19, 19, 0, GATHER_ZONE_NONE);
    REQUIRE(other != GATHER_ZONE_NONE);
    CHECK_EQ_INT(game_add_gather_zone_rect(&game, 13, 10, 18, 18, 0, GATHER_ZONE_NONE), zone);
    CHECK_EQ_INT(game.gather_zones.used_count, 1);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 13, 10, 0), GATHER_ZONE_NONE); /* Stockpile. */
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 17, 17, 0), zone);
    CHECK_EQ_INT(gather_zone_at(&game.gather_zones, 19, 19, 0), zone);

    for (size_t i = 0; i < map_tile_count(&game.map); i++) {
        CHECK(game.stockpiles.zone[i] == 0 || game.gather_zones.zone[i] == 0);
    }
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_init_and_shutdown);
    RUN_TEST(test_init_failure_leaves_game_safe_to_shut_down);
    RUN_TEST(test_settlers_arrive_near_food);
    RUN_TEST(test_mushrooms_come_and_go);
    RUN_TEST(test_settlers_arrive_near_flint);
    RUN_TEST(test_update_advances_ticks);
    RUN_TEST(test_plants_grow_over_time);
    RUN_TEST(test_plant_growth_is_spread_over_ticks);
    RUN_TEST(test_simulation_is_deterministic);
    RUN_TEST(test_hash_changes_as_simulation_runs);
    RUN_TEST(test_hash_covers_knowledge_and_carried_condition);
    RUN_TEST(test_fresh_grass_dries_on_the_ground);
    RUN_TEST(test_food_rots_on_the_ground);
    RUN_TEST(test_mushrooms_come_after_rain);
    RUN_TEST(test_grass_does_not_dry_in_a_storm);
    RUN_TEST(test_plant_stocks_grow_back);
    RUN_TEST(test_zones_of_different_kinds_never_overlap);
    return test_summary();
}
