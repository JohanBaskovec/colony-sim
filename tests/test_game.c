#include "core/game.h"
#include "core/scenario.h"
#include "core/shadow.h"
#include "core/soil.h"
#include "core/sun.h"
#include "core/worldgen.h"

#include "test_framework.h"

#include <stdint.h>

#include <stdlib.h>
#include <string.h>

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

/* Soil on level 0 and open air over it in (x0, y0) .. (x1, y1) (inclusive):
 * out in the open, away from the rock walls and trees of a flat world (the
 * plants of the ground stay, bare ground warms more in the sun). */
static void clear_area(Map *map, int x0, int y0, int x1, int y1)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            size_t index = map_index(map, x, y, 0);
            if (map->rock[index] != ROCK_NONE ||
                plant_is_tree((PlantType)map->plants[index].type)) {
                map->plants[index] = (Plant){0};
            }
            map_set_rock(map, x, y, 0, ROCK_NONE);
            map_set_terrain(map, x, y, 0, TERRAIN_SOIL);
            for (int z = 1; z < map->depth; z++) {
                map->plants[map_index(map, x, y, z)] = (Plant){0};
                map_set_rock(map, x, y, z, ROCK_NONE);
                map_set_terrain(map, x, y, z, TERRAIN_OPEN);
            }
        }
    }
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
    if (plant->type == count->type &&
        plant->growth >= plant_def(count->type)->products[0].min_growth) {
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
        /* A grove on the west half, meadow on the east; bare ground between. */
        bool grove = (int)(i % (size_t)map->width) < map->width / 2;
        map->plants[i] =
            grove && i % 7 == 0 ? (Plant){.type = PLANT_OAK, .growth = 1.0f} : (Plant){0};
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

/* A tile with grass on it holds a plant: no mushrooms come up there. */
static void test_mushrooms_need_bare_ground(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 3));
    Map *map = &game.map;
    make_grove(map);
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        if (map->plants[i].type == PLANT_NONE) {
            map->plants[i] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
        }
    }
    game.clock.tick = (uint64_t)SEASON_DAY(SEASON_AUTUMN, 4) * TICKS_PER_DAY;
    for (int i = 0; i < TICKS_PER_HOUR; i++) {
        game.weather.recent_rain = 20.0f;
        game_tick(&game);
    }
    CHECK_EQ_UINT(map_count_plants(map, PLANT_MUSHROOMS), 0);
    game_shutdown(&game);
}

static void test_mushrooms_come_and_go(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 3));
    Map *map = &game.map;
    make_grove(map);
    /* A mild summer night is too warm for them. */
    game.clock.tick = (uint64_t)SEASON_DAY(SEASON_SUMMER, 8) * TICKS_PER_DAY;
    game.weather.spell = 0.0f;
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
    /* Weeks on, in the winter cold: all gone. */
    for (size_t i = 0; i < map_tile_count(map); i++) {
        if (map->plants[i].type == PLANT_MUSHROOMS) {
            map->plants[i].climate = (int16_t)(-5.0f * PLANT_CLIMATE_SCALE);
        }
    }
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
    REQUIRE(init_flat_game(&game, 3)); /* Open sky: the outdoor temperature. */
    Map *map = &game.map;

    /* Controlled setup on known terrain, in the open. */
    size_t fertile = map_index(map, 25, 39, 0); /* Out in the open. */
    size_t barren = map_index(map, 26, 39, 0);
    map_set_terrain(map, 25, 39, 0, TERRAIN_SOIL);
    map_set_terrain(map, 26, 39, 0, TERRAIN_SAND);
    map->plants[fertile].type = PLANT_GRASS;
    map->plants[fertile].growth = 0.1f;
    map->plants[barren].type = PLANT_GRASS;
    map->plants[barren].growth = 0.1f;

    for (int i = 0; i < TICKS_PER_DAY; i++) {
        game_tick(&game);
    }

    /* A summer day: more than half the best rate a day makes (less in the
     * heat of the afternoon out in the open, warm for the grass, and on
     * ground a little dry). */
    float best = plant_def(PLANT_GRASS)->growth_per_day * terrain_def(TERRAIN_SOIL)->fertility;
    float gained = map->plants[fertile].growth - 0.1f;
    CHECK(gained > 0.55f * best && gained <= best + 1e-4f);
    /* Sand is below the minimum fertility of grass: no growth. */
    CHECK_NEAR(map->plants[barren].growth, 0.1, 1e-6);

    /* Growth saturates at 1. */
    for (int i = 0; i < TICKS_PER_DAY * 10; i++) {
        game_tick(&game);
    }
    CHECK_NEAR(map->plants[fertile].growth, 1.0, 1e-6);
    game_shutdown(&game);
}

/* What a plant meets on its tile: the sun by the hour, the sky, its own
 * share of them (the sky it sees, its share in the sun, docs/SHADOWS.md §5.2;
 * a tree standing on its trunk is not shaded by its own crown). */
/* The game starts with the generator's plants, all of them (it sets them
 * where they can live all year, core/worldgen.h): none would starve in the
 * light the game works out. */
static void test_generated_plants_are_kept(void)
{
    for (uint64_t seed = 1; seed <= 3; seed++) {
        NewGameParams params;
        game_default_params(&params);
        params.world.seed = seed;
        params.world.width = 64;
        params.world.height = 64;
        params.colonist_count = 0; /* No camp: the plants are the generator's. */
        Game *game = malloc(sizeof(*game));
        Map generated;
        REQUIRE(game != NULL);
        REQUIRE(game_init(game, &params));
        REQUIRE(worldgen_generate(&generated, &params.world));
        const Map *map = &game->map;
        int differ = 0, plants = 0;
        for (size_t i = 0; i < map_level_tile_count(map) * (size_t)game->light.levels; i++) {
            uint8_t was = generated.plants[i].type;
            if (was != PLANT_NONE && !plant_is_tree(was)) {
                plants++;
                differ += map->plants[i].type != was;
            }
        }
        CHECK(plants > 100);
        CHECK_EQ_INT(differ, 0);
        CHECK_EQ_UINT(worldgen_starving_plants(map, &game->light), 0);
        map_free(&generated);
        game_shutdown(game);
        free(game);
    }
}

static void test_plant_conditions(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 3));
    Map *map = &game.map;
    clear_area(map, 0, 0, 40, 40);
    for (int y = 2; y <= 8; y++) {
        for (int x = 2; x <= 8; x++) {
            map_set_terrain(map, x, y, 1, TERRAIN_FOLIAGE); /* A wide crown over (5, 5). */
        }
    }
    map_set_rock(map, 12, 12, 0, ROCK_WOOD); /* A tree's foot, its crown over it. */
    map_set_terrain(map, 12, 12, 1, TERRAIN_FOLIAGE);
    int midsummer = SEASON_DAY(SEASON_SUMMER, 8);
    game.clock.tick = (uint64_t)midsummer * TICKS_PER_DAY + 12 * TICKS_PER_HOUR;
    game.weather.type = WEATHER_CLEAR;
    game_refresh_light(&game);
    PlantConditions open = game_plant_conditions(&game, 20, 20, 0);
    CHECK_NEAR(open.light, 1.0, 1e-3);
    CHECK_NEAR(open.light_peak, 1.0, 1e-3);
    CHECK_NEAR(open.fertility,
               terrain_def((Terrain)map->terrain[map_index(map, 20, 20, 0)])->fertility, 1e-6);
    /* Under the crown: what it lets through, from above and from the sides. */
    PlantConditions shaded = game_plant_conditions(&game, 5, 5, 0);
    float share =
        game_direct_share(&game, sun_at_tick(game.clock.tick, SUN_LATITUDE_DEFAULT).elevation);
    CHECK(share > 0.5f);
    float expected = game_sunlight(game.clock.tick) * weather_light(&game.weather) *
                     ((1.0f - share) * tile_light_open(&game.light, 5, 5, 0) +
                      share * tile_light_direct(&game.light, 5, 5, 0));
    CHECK_NEAR(shaded.light, expected, 1e-5);
    CHECK(shaded.light > 0.15f && shaded.light < 0.5f);
    PlantConditions edge = game_plant_conditions(&game, 2, 5, 0);
    CHECK(edge.light > shaded.light); /* Brighter at the edge of the wood. */
    PlantConditions tree = game_plant_conditions(&game, 12, 12, 0);
    CHECK_NEAR(tree.light, 1.0, 1e-3);
    /* The light of a tile, for the light view. */
    CHECK_NEAR(game_light_at(&game, 20, 20, 0), open.light, 1e-6);
    CHECK_NEAR(game_light_at(&game, 5, 5, 0), shaded.light, 1e-6);
    CHECK_NEAR(game_light_peak_at(&game, 5, 5, 0), shaded.light_peak, 1e-6);
    CHECK_NEAR(game_light_at(&game, -1, 3, 0), 0.0, 1e-9); /* Out of the map. */
    CHECK_NEAR(game_light_peak_at(&game, 3, 3, 99), 0.0, 1e-9);
    /* Night: no light now, the day's best the same. */
    game.clock.tick = (uint64_t)midsummer * TICKS_PER_DAY + 1 * TICKS_PER_HOUR;
    PlantConditions night = game_plant_conditions(&game, 20, 20, 0);
    CHECK_NEAR(night.light, 0.0, 1e-6);
    CHECK_NEAR(night.light_peak, 1.0, 1e-3);
    /* Clouds and rain dim it, and the shade is the sky's alone. */
    game.clock.tick = (uint64_t)midsummer * TICKS_PER_DAY + 12 * TICKS_PER_HOUR;
    game.weather.type = WEATHER_STORM;
    PlantConditions storm = game_plant_conditions(&game, 20, 20, 0);
    CHECK_NEAR(storm.light, game_sunlight(game.clock.tick) * weather_light(&game.weather), 1e-3);
    CHECK(storm.light < open.light);
    PlantConditions storm_shade = game_plant_conditions(&game, 5, 5, 0);
    CHECK_NEAR(storm_shade.light, storm.light * tile_light_open(&game.light, 5, 5, 0), 1e-5);
    game_shutdown(&game);
}

/* Bare ground fills in: grass spreads to the free tiles around it (items
 * lying there do not stop it), but not onto what the colony uses
 * (docs/PLANTS.md §13). */
static void test_grass_spreads_into_bare_ground(void)
{
    Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 5;
    params.world.width = 64;
    params.world.height = 64;
    params.world.flat = true;
    params.colonist_count = 0;
    REQUIRE(game_init(&game, &params));
    Map *map = &game.map;
    make_grove(map);
    memset(map->plants, 0, map_level_tile_count(map) * sizeof(*map->plants));
    for (int y = 30; y <= 32; y++) {
        for (int x = 40; x <= 42; x++) {
            map->plants[map_index(map, x, y, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
        }
    }
    items_add(&game.items, 42, 33, 0, item_stack_pristine(ITEM_WOOD, 1)); /* No hindrance. */
    REQUIRE(stockpile_create(&game.stockpiles, map, 39, 30, 39, 32, 0) != STOCKPILE_NONE);
    for (int day = 0; day < 20; day++) { /* The first hour of each day: the spreading. */
        game.clock.tick = (uint64_t)(SEASON_DAY(SEASON_SUMMER, 1) + day) * TICKS_PER_DAY;
        for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
            game_tick(&game);
        }
    }
    CHECK(map_count_plants(map, PLANT_GRASS) >= 14);
    for (int y = 30; y <= 32; y++) {
        CHECK_EQ_INT(map->plants[map_index(map, 39, y, 0)].type, PLANT_NONE);
    }
    for (int y = 0; y < map->height; y++) { /* Nothing else came of nothing. */
        for (int x = 0; x < map->width; x++) {
            uint8_t type = map->plants[map_index(map, x, y, 0)].type;
            CHECK(type == PLANT_NONE || type == PLANT_GRASS || type == PLANT_MUSHROOMS);
        }
    }
    game_shutdown(&game);
}

/* A plant does not take root where its ground is too dry for it
 * (docs/CLIMATE.md §6): grass fills the moist side, not the parched one. */
static void test_plants_do_not_spread_onto_parched_ground(void)
{
    Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 5;
    params.world.width = 64;
    params.world.height = 64;
    params.world.flat = true;
    params.colonist_count = 0;
    REQUIRE(game_init(&game, &params));
    Map *map = &game.map;
    memset(map->plants, 0, map_level_tile_count(map) * sizeof(*map->plants));
    for (int y = 20; y <= 40; y++) {
        for (int x = 15; x <= 30; x++) {
            map_set_terrain(map, x, y, 0, TERRAIN_SOIL);
        }
    }
    for (int y = 25; y <= 35; y++) {
        map->plants[map_index(map, 22, y, 0)] = (Plant){.type = PLANT_GRASS, .growth = 1.0f};
    }
    const SoilDef *loam = soil_def(TERRAIN_SOIL);
    for (int day = 0; day < 20; day++) {
        for (int y = 20; y <= 40; y++) {
            for (int x = 15; x <= 30; x++) { /* West parched, east moist. */
                game.soil.water[map_index(map, x, y, 0)] =
                    x < 22 ? 0.9f * loam->wilting : loam->capacity;
            }
        }
        game.clock.tick = (uint64_t)(SEASON_DAY(SEASON_SUMMER, 1) + day) * TICKS_PER_DAY;
        for (int tick = 0; tick < TICKS_PER_HOUR; tick++) {
            game_tick(&game);
        }
    }
    int west = 0, east = 0;
    for (int y = 20; y <= 40; y++) {
        west += map->plants[map_index(map, 21, y, 0)].type == PLANT_GRASS;
        east += map->plants[map_index(map, 23, y, 0)].type == PLANT_GRASS;
    }
    CHECK_EQ_INT(west, 0);
    CHECK(east >= 4);
    game_shutdown(&game);
}

/* The crowns of deciduous trees follow the climate their trees live in: in
 * full leaf in summer, bare in winter, in between in spring and autumn, and
 * the light under them follows (docs/PLANTS.md §12). */
static void test_crowns_lose_their_leaves_in_the_cold(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 3));
    Map *map = &game.map;
    clear_area(map, 0, 0, 40, 40);
    for (int y = 2; y <= 8; y++) {
        for (int x = 2; x <= 8; x++) {
            map_set_terrain(map, x, y, 1, TERRAIN_FOLIAGE);
        }
    }
    CHECK_EQ_INT(map->bare, 0); /* A world starts in summer, in leaf. */
    game.weather.type = WEATHER_CLEAR;
    int midsummer = SEASON_DAY(SEASON_SUMMER, 8);
    game.clock.tick = (uint64_t)midsummer * TICKS_PER_DAY + 12 * TICKS_PER_HOUR;
    game_refresh_light(&game);
    float in_leaf = game_light_at(&game, 5, 5, 0);
    CHECK(in_leaf < 0.5f);

    /* A tree with a crown, alone, in the climate of each season. */
    memset(map->plants, 0, map_tile_count(map) * sizeof(*map->plants));
    map->plants[map_index(map, 9, 9, 0)] = (Plant){.type = PLANT_OAK, .growth = 1.0f};
    const float climates[] = {20.0f, 8.5f, 0.0f, 20.0f};
    const int expected[] = {0, MAP_BARE_STEPS / 2, MAP_BARE_STEPS, 0};
    for (size_t i = 0; i < sizeof(climates) / sizeof(climates[0]); i++) {
        Plant *tree = &map->plants[map_index(map, 9, 9, 0)];
        tree->state |= PLANT_CLIMATE_KNOWN;
        tree->climate = (int16_t)(climates[i] * PLANT_CLIMATE_SCALE);
        /* One whole pass over the plants; the tree's own climate moves a little. */
        for (int tick = 0; tick < GAME_PLANT_UPDATE_INTERVAL; tick++) {
            game_tick(&game);
        }
        CHECK(abs(map->bare - expected[i]) <= 1);
    }
    /* Bare, a crown lets much more light through. */
    map_set_bare(map, MAP_BARE_STEPS);
    game.clock.tick = (uint64_t)midsummer * TICKS_PER_DAY + 12 * TICKS_PER_HOUR;
    game.weather.type = WEATHER_CLEAR;
    game_refresh_light(&game);
    float bare = game_light_at(&game, 5, 5, 0);
    CHECK(bare > in_leaf + 0.3f && bare <= MAP_LIGHT_BARE_DENSE + 0.1f);
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
    game.clock.tick =
        (uint64_t)GAME_START_DAY * TICKS_PER_DAY + 11 * TICKS_PER_HOUR; /* Near noon. */
    game_refresh_light(&game);
    Map *map = &game.map;
    size_t count = map_tile_count(map);
    float *before = malloc(count * sizeof(*before));
    REQUIRE(before != NULL);
    size_t growing = 0;
    for (size_t i = 0; i < count; i++) {
        const Plant *plant = &map->plants[i];
        before[i] = plant->growth;
        if (plant->type != PLANT_NONE && plant->growth < 1.0f) {
            int x, y, z;
            map_tile_of(map, i, &x, &y, &z);
            PlantConditions conditions = game_plant_conditions(&game, x, y, z);
            conditions.light_peak = conditions.light; /* What it makes this hour. */
            growing += plant_growth_rate(plant, &conditions) > 0.0f;
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
    /* Each plant that could grow did (but for a few whose light crossed what
     * they need as the sun rose). */
    CHECK(grown <= growing + growing / 50 && grown + growing / 50 >= growing);
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
    /* About the same step (the temperature moved a little), not two. */
    float second = map->plants[index].growth - after_one;
    CHECK(second > 0.6f * step && second < 1.4f * step);
    free(before);
    game_shutdown(&game);
}

static void test_simulation_is_deterministic(void)
{
    static Game a, b;
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

    colonist->carried = item_stack_make(ITEM_WOOD, 2, TEST_QUALITY_FINE, 60);
    uint64_t carrying = game_hash(&game);
    colonist->carried.quality = TEST_QUALITY_ROUGH;
    CHECK(game_hash(&game) != carrying);
    colonist->carried.quality = TEST_QUALITY_FINE;
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
    int capacity = plant_stock_capacity(plant, 0);
    REQUIRE(capacity > 0);
    CHECK_EQ_INT(plant_stock_available(plant, 0), capacity);
    plant_take_stock(plant, 0, capacity);
    int regrow = plant_def(PLANT_BRAMBLE)->products[0].regrow_hours;
    for (int i = 0; i < (regrow + 1) * TICKS_PER_HOUR; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(plant_stock_available(plant, 0), 1);
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

/* Items in hand stay themselves: as many held as settlers carry, and as
 * many on the map as its tiles hold, through a day of gathering, hauling and
 * building. */
static void check_item_books(const Game *game)
{
    long carried = 0;
    for (uint32_t slot = 0; slot < COLONIST_MAX; slot++) {
        const Colonist *colonist =
            game_colonist_const(game, pool_id_at(&game->colonist_pool, slot));
        if (colonist != NULL && colonist->carried.count > 0) {
            carried += colonist->carried.count;
            int listed = 0;
            for (const Item *item = items_load_first(&game->items, colonist->carried); item != NULL;
                 item = items_next(&game->items, item)) {
                CHECK_EQ_INT(item->type, colonist->carried.type);
                CHECK_EQ_UINT(item->tile, ITEM_TILE_HELD);
                listed++;
            }
            CHECK_EQ_INT(listed, colonist->carried.count);
        }
    }
    CHECK_EQ_INT(game->items.held_count, carried);
    long lying = 0;
    size_t tiles = map_tile_count(&game->map);
    for (size_t i = 0; i < tiles; i++) {
        for (const Item *item = items_first(&game->items, i); item != NULL;
             item = items_next(&game->items, item)) {
            lying++;
        }
    }
    CHECK_EQ_INT(game->items.item_count, lying);
}

static void test_items_in_hand_are_kept(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, 21));
    const Colonist *first = game_colonist_const(&game, pool_id_at(&game.colonist_pool, 0));
    REQUIRE(first != NULL);
    int cx = first->x, cy = first->y, z = first->z;
    for (int y = cy - 2; y <= cy + 7; y++) {
        for (int x = cx - 7; x <= cx + 7; x++) {
            map_remove_plant(&game.map, x, y, z);
            map_set_rock(&game.map, x, y, z, ROCK_NONE);
            map_set_terrain(&game.map, x, y, z, TERRAIN_SOIL);
        }
    }
    REQUIRE(game_add_stockpile_rect(&game, cx - 1, cy - 1, cx + 1, cy + 1, z, STOCKPILE_NONE) !=
            STOCKPILE_NONE);
    /* Logs of two qualities, a little way off. */
    int dropped = 0;
    for (int i = 0; i < 6; i++) {
        int quality = i % 2 == 0 ? 20 : 90;
        ItemStack logs = item_stack_make(ITEM_WOOD, 5, quality, 40 + i);
        dropped += items_drop(&game.items, &game.map, cx - 6 + i * 2, cy + 6, z, logs);
    }
    REQUIRE(dropped == 30);
    long most_held = 0;
    for (int tick = 0; tick < TICKS_PER_HOUR * 12; tick++) {
        game_tick(&game);
        most_held = game.items.held_count > most_held ? game.items.held_count : most_held;
        if (tick % 500 == 0) {
            check_item_books(&game);
        }
    }
    check_item_books(&game);
    CHECK(most_held > 0); /* They did carry them. */
    /* Stored, each log is still the one it was: no average of the two. */
    int stored = 0;
    for (int y = cy - 1; y <= cy + 1; y++) {
        for (int x = cx - 1; x <= cx + 1; x++) {
            for (const Item *item = items_first(&game.items, map_index(&game.map, x, y, z));
                 item != NULL; item = items_next(&game.items, item)) {
                if (item->type == ITEM_WOOD) {
                    CHECK(item->quality == 20 || item->quality == 90);
                    CHECK(item->hit_points >= 40 && item->hit_points <= 45);
                    stored++;
                }
            }
        }
    }
    CHECK(stored > 10);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_init_and_shutdown);
    RUN_TEST(test_init_failure_leaves_game_safe_to_shut_down);
    RUN_TEST(test_settlers_arrive_near_food);
    RUN_TEST(test_mushrooms_need_bare_ground);
    RUN_TEST(test_mushrooms_come_and_go);
    RUN_TEST(test_settlers_arrive_near_flint);
    RUN_TEST(test_update_advances_ticks);
    RUN_TEST(test_plants_grow_over_time);
    RUN_TEST(test_plant_conditions);
    RUN_TEST(test_generated_plants_are_kept);
    RUN_TEST(test_crowns_lose_their_leaves_in_the_cold);
    RUN_TEST(test_grass_spreads_into_bare_ground);
    RUN_TEST(test_plants_do_not_spread_onto_parched_ground);
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
    RUN_TEST(test_items_in_hand_are_kept);
    return test_summary();
}
