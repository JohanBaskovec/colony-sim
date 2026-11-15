/* Temperature: outdoor air through the day and the year, heat spreading
 * between tiles, walls, roofs, caves, hearths, and sleeping in the cold. */
#include "core/game.h"
#include "core/hearth.h"
#include "core/temperature.h"

#include "test_framework.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Start of the middle day of a season. */
static uint64_t mid_season(Season season)
{
    return ((uint64_t)season * DAYS_PER_SEASON + DAYS_PER_SEASON / 2) * TICKS_PER_DAY;
}

/* A small game on a flat, empty soil map with no starting colonists, its
 * tiles at the outdoor temperature of `tick`. */
static bool init_flat_game(Game *game, uint64_t tick)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 5;
    params.world.width = 40;
    params.world.height = 40;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = 0; i < map_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
        game->temperatures.celsius[i] = temperature_outdoor(tick);
    }
    map_topology_changed(map);
    game->clock.tick = tick;
    return true;
}

static void run_hours(Game *game, float hours)
{
    int ticks = (int)(hours * (float)TICKS_PER_HOUR);
    for (int i = 0; i < ticks; i++) {
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
                game->colonists[slot].needs[NEED_FOOD] = 1.0f;
                game->colonists[slot].needs[NEED_WATER] = 1.0f;
            }
        }
        game_tick(game);
    }
}

/* A hut: walls around (x0, y0)-(x1, y1) (their tiles included), a door in
 * the middle of the south wall, roofed over everything when `roofed`. */
static void build_hut(Game *game, int x0, int y0, int x1, int y1, bool roofed)
{
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            bool edge = x == x0 || x == x1 || y == y0 || y == y1;
            if (edge) {
                bool door = y == y1 && x == (x0 + x1) / 2;
                map_set_structure(&game->map, x, y, door ? STRUCTURE_DOOR : STRUCTURE_WALL);
            }
            if (roofed) {
                map_set_roof(&game->map, x, y, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
            }
        }
    }
}

static int burning_hearth(Game *game, int x, int y)
{
    int index =
        workstations_place(&game->workstations, &game->map, WORKSTATION_OPEN_HEARTH, x, y);
    Workstation *station = workstation_get(&game->workstations, index);
    if (station != NULL) {
        station->fuel = HEARTH_FUEL_CAPACITY;
        station->keep_burning = false;
        hearth_light(&game->workstations, index);
    }
    return index;
}

/* Keeps a hearth full so that it burns for the whole test. */
static void refill(Game *game, int index)
{
    Workstation *station = workstation_get(&game->workstations, index);
    if (station != NULL) {
        station->fuel = HEARTH_FUEL_CAPACITY;
    }
}

static void test_outdoor(void)
{
    float means[SEASON_COUNT] = TEMPERATURE_SEASON_MEANS;
    for (int season = 0; season < SEASON_COUNT; season++) {
        uint64_t day = mid_season((Season)season);
        /* Coldest and warmest hours of the middle day, around the mean. */
        float coldest = temperature_outdoor(day + TEMPERATURE_COLDEST_HOUR * TICKS_PER_HOUR);
        float warmest = temperature_outdoor(day + TEMPERATURE_WARMEST_HOUR * TICKS_PER_HOUR);
        CHECK_NEAR(coldest, means[season] - TEMPERATURE_DAILY_SWING, 0.3);
        CHECK_NEAR(warmest, means[season] + TEMPERATURE_DAILY_SWING, 0.3);
        for (int hour = 0; hour < HOURS_PER_DAY; hour++) {
            float here = temperature_outdoor(day + (uint64_t)hour * TICKS_PER_HOUR);
            CHECK(here >= coldest - 0.01f && here <= warmest + 0.01f);
        }
    }
    /* Smooth through the day and the year: no jump from one hour to the next. */
    for (uint64_t tick = 0; tick < TICKS_PER_YEAR; tick += TICKS_PER_HOUR) {
        float step = temperature_outdoor(tick + TICKS_PER_HOUR) - temperature_outdoor(tick);
        CHECK(fabsf(step) < 2.0f);
    }
    /* The same day of the next year. */
    CHECK_NEAR(temperature_outdoor(12345), temperature_outdoor(TICKS_PER_YEAR + 12345), 1e-3);
    /* Spring and autumn in between summer and winter. */
    CHECK(temperature_outdoor(mid_season(SEASON_SUMMER)) >
          temperature_outdoor(mid_season(SEASON_SPRING)));
    CHECK(temperature_outdoor(mid_season(SEASON_WINTER)) <
          temperature_outdoor(mid_season(SEASON_AUTUMN)));
}

static void test_open_ground_follows_the_air(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, mid_season(SEASON_SPRING)));
    /* Warmed by hand: back to the air within the hour. */
    for (size_t i = 0; i < map_tile_count(&game.map); i++) {
        game.temperatures.celsius[i] = 30.0f;
    }
    run_hours(&game, 1.5f);
    float outdoor = temperature_outdoor(game.clock.tick);
    CHECK_NEAR(temperature_at(&game.temperatures, 20, 20), outdoor, 1.0);
    CHECK_NEAR(temperature_at(&game.temperatures, 0, 0), outdoor, 1.0);
    CHECK_NEAR(temperature_at(&game.temperatures, -1, 5), TEMPERATURE_GROUND, 1e-6);
    /* Following the day. */
    run_hours(&game, 10.0f);
    CHECK_NEAR(temperature_at(&game.temperatures, 20, 20), temperature_outdoor(game.clock.tick),
               1.0);
    game_shutdown(&game);
}

static void test_materials_and_covers(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, mid_season(SEASON_SPRING)));
    Map *map = &game.map;
    map_set_rock(map, 1, 1, ROCK_GRANITE);
    map_set_structure(map, 5, 5, STRUCTURE_WALL);
    map_set_structure(map, 6, 5, STRUCTURE_DOOR);
    map_set_structure(map, 7, 5, STRUCTURE_GRASS_BED);
    map_set_roof(map, 6, 6, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK_EQ_INT(temperature_material(map, 1, 1), TEMPERATURE_MATERIAL_ROCK);
    CHECK_EQ_INT(temperature_material(map, -1, 1), TEMPERATURE_MATERIAL_ROCK);
    CHECK_EQ_INT(temperature_material(map, 5, 5), TEMPERATURE_MATERIAL_WALL);
    CHECK_EQ_INT(temperature_material(map, 6, 5), TEMPERATURE_MATERIAL_DOOR);
    CHECK_EQ_INT(temperature_material(map, 8, 8), TEMPERATURE_MATERIAL_AIR);
    CHECK_EQ_INT(temperature_cover(map, 6, 6), TEMPERATURE_COVER_ROOFED);
    CHECK_EQ_INT(temperature_cover(map, 8, 8), TEMPERATURE_COVER_OPEN);
    /* Rock stays at the ground's temperature. */
    run_hours(&game, 0.1f);
    CHECK_NEAR(temperature_at(&game.temperatures, 1, 1), TEMPERATURE_GROUND, 1e-6);
    game_shutdown(&game);
}

/* The cached kind of a tile. */
static int cached_kind(const Game *game, int x, int y)
{
    return game->temperatures.kind[map_index(&game->map, x, y)];
}

static int kind_of(TemperatureMaterial material, TemperatureCover cover)
{
    return (int)material * TEMPERATURE_COVER_COUNT + (int)cover;
}

static void test_cache_follows_the_map(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, mid_season(SEASON_SPRING)));
    Map *map = &game.map;
    game_tick(&game);
    CHECK_EQ_INT(cached_kind(&game, 5, 5),
                 kind_of(TEMPERATURE_MATERIAL_AIR, TEMPERATURE_COVER_OPEN));
    /* One change at a time: only around the tile. */
    map_set_structure(map, 5, 5, STRUCTURE_WALL);
    game_tick(&game);
    CHECK_EQ_INT(cached_kind(&game, 5, 5),
                 kind_of(TEMPERATURE_MATERIAL_WALL, TEMPERATURE_COVER_OPEN));
    map_set_roof(map, 5, 5, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    game_tick(&game);
    CHECK_EQ_INT(cached_kind(&game, 5, 5),
                 kind_of(TEMPERATURE_MATERIAL_WALL, TEMPERATURE_COVER_ROOFED));
    map_set_structure(map, 5, 5, STRUCTURE_NONE);
    game_tick(&game);
    CHECK_EQ_INT(cached_kind(&game, 5, 5),
                 kind_of(TEMPERATURE_MATERIAL_AIR, TEMPERATURE_COVER_ROOFED));
    /* Rock put around a tile, one by one, makes a natural roof over it. */
    for (int y = 28; y <= 32; y++) {
        for (int x = 28; x <= 32; x++) {
            if (x != 30 || y != 30) {
                map_set_rock(map, x, y, ROCK_GRANITE);
                game_tick(&game);
            }
        }
    }
    CHECK_EQ_INT(cached_kind(&game, 30, 30),
                 kind_of(TEMPERATURE_MATERIAL_AIR, TEMPERATURE_COVER_NATURAL));
    CHECK_EQ_INT(cached_kind(&game, 28, 28),
                 kind_of(TEMPERATURE_MATERIAL_ROCK, TEMPERATURE_COVER_NATURAL));
    CHECK_NEAR(temperature_at(&game.temperatures, 28, 28), TEMPERATURE_GROUND, 1e-6);
    /* Dug out again. */
    map_set_rock(map, 28, 28, ROCK_NONE);
    game_tick(&game);
    CHECK_EQ_INT(cached_kind(&game, 28, 28) / TEMPERATURE_COVER_COUNT, TEMPERATURE_MATERIAL_AIR);
    /* Many changes at once: everything rebuilt. */
    for (int x = 0; x < 20; x++) {
        map_set_structure(map, x, 10, STRUCTURE_WALL);
    }
    game_tick(&game);
    for (int x = 0; x < 20; x++) {
        CHECK_EQ_INT(cached_kind(&game, x, 10) / TEMPERATURE_COVER_COUNT,
                     TEMPERATURE_MATERIAL_WALL);
    }
    game_shutdown(&game);
}

static void test_hearth_warms_around_it(void)
{
    Game game;
    REQUIRE(init_flat_game(&game, mid_season(SEASON_SPRING)));
    int hearth = burning_hearth(&game, 20, 20);
    for (int i = 0; i < 4; i++) {
        run_hours(&game, 0.5f);
        refill(&game, hearth);
    }
    float outdoor = temperature_outdoor(game.clock.tick);
    float at = temperature_at(&game.temperatures, 20, 20) - outdoor;
    float next = temperature_at(&game.temperatures, 21, 20) - outdoor;
    float near = temperature_at(&game.temperatures, 22, 20) - outdoor;
    float far = temperature_at(&game.temperatures, 26, 20) - outdoor;
    CHECK(at > 3.0f);
    CHECK(at > next && next > near && near > far);
    CHECK(far < 0.5f);
    game_shutdown(&game);
}

static void test_hut_keeps_the_warmth(void)
{
    /* Same hut and fire, roofed or not, and a roofed hut without fire. */
    float inside[3];
    for (int variant = 0; variant < 3; variant++) {
        Game game;
        REQUIRE(init_flat_game(&game, mid_season(SEASON_SPRING)));
        build_hut(&game, 18, 18, 22, 22, variant != 1);
        int hearth = variant != 2 ? burning_hearth(&game, 20, 20) : -1;
        for (int i = 0; i < 16; i++) {
            run_hours(&game, 0.5f);
            refill(&game, hearth);
        }
        float outdoor = temperature_outdoor(game.clock.tick);
        inside[variant] = temperature_at(&game.temperatures, 19, 21) - outdoor;
        /* The walls keep it in. */
        CHECK(fabsf(temperature_at(&game.temperatures, 17, 20) - outdoor) < 1.0f);
        game_shutdown(&game);
    }
    CHECK(inside[0] > inside[1] + 5.0f);
    CHECK(inside[0] > 10.0f);
    CHECK(fabsf(inside[2]) < 6.0f);
}

static void test_cave_stays_mild(void)
{
    Game game;
    uint64_t night = mid_season(SEASON_WINTER) + 3 * TICKS_PER_HOUR;
    REQUIRE(init_flat_game(&game, night));
    /* A rock mass with a hollow of 3 × 3 tiles in it, and a passage out. */
    for (int y = 10; y <= 24; y++) {
        for (int x = 10; x <= 24; x++) {
            bool hollow = x >= 16 && x <= 18 && y >= 16 && y <= 18;
            bool passage = x == 17 && y > 18;
            if (!hollow && !passage) {
                map_set_rock(&game.map, x, y, ROCK_GRANITE);
            }
        }
    }
    CHECK_EQ_INT(temperature_cover(&game.map, 17, 17), TEMPERATURE_COVER_NATURAL);
    run_hours(&game, 24.0f);
    float outdoor = temperature_outdoor(game.clock.tick);
    CHECK(outdoor < 2.0f);
    float cave = temperature_at(&game.temperatures, 17, 16);
    CHECK(cave > outdoor + 5.0f);
    CHECK(cave <= TEMPERATURE_GROUND + 0.01f);
    game_shutdown(&game);
}

static void test_sleep_temperature_factor(void)
{
    CHECK_NEAR(colonist_sleep_temperature_factor(20.0f), 1.0, 1e-6);
    CHECK_NEAR(colonist_sleep_temperature_factor(COLONIST_COLD_SLEEP_BELOW), 1.0, 1e-6);
    CHECK_NEAR(colonist_sleep_temperature_factor(COLONIST_COLD_SLEEP_BELOW - 4.0f),
               1.0f - 4.0f * COLONIST_SLEEP_LOSS_PER_DEGREE, 1e-6);
    CHECK_NEAR(colonist_sleep_temperature_factor(-40.0f), COLONIST_SLEEP_TEMPERATURE_FACTOR_MIN,
               1e-6);
    /* Too hot too. */
    CHECK_NEAR(colonist_sleep_temperature_factor(COLONIST_HOT_SLEEP_ABOVE), 1.0, 1e-6);
    CHECK_NEAR(colonist_sleep_temperature_factor(COLONIST_HOT_SLEEP_ABOVE + 6.0f),
               1.0f - 6.0f * COLONIST_SLEEP_LOSS_PER_DEGREE, 1e-6);
    CHECK_NEAR(colonist_sleep_temperature_factor(90.0f), COLONIST_SLEEP_TEMPERATURE_FACTOR_MIN,
               1e-6);
}

/* Ticks a settler sleeping at (x, y) takes to rest from 0.2. */
static int ticks_to_rest(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y));
    if (colonist == NULL) {
        return -1;
    }
    for (int type = 0; type < WORK_TYPE_COUNT; type++) {
        colonist_set_work_priority(colonist, (WorkType)type, WORK_PRIORITY_DISABLED);
    }
    colonist->needs[NEED_REST] = 0.2f;
    game_tick(game);
    int ticks = 0;
    while (colonist->task == TASK_SLEEP && ticks < 2 * TICKS_PER_DAY) {
        colonist->needs[NEED_FOOD] = colonist->needs[NEED_WATER] = 1.0f;
        game_tick(game);
        ticks++;
    }
    return ticks;
}

static void test_cold_sleep_is_longer(void)
{
    /* A winter night outdoors, and the same in a roofed hut with a fire. */
    uint64_t evening = mid_season(SEASON_WINTER) + 21 * TICKS_PER_HOUR;
    Game game;
    REQUIRE(init_flat_game(&game, evening));
    int cold = ticks_to_rest(&game, 10, 10);
    game_shutdown(&game);

    REQUIRE(init_flat_game(&game, evening));
    build_hut(&game, 18, 18, 22, 22, true);
    int hearth = burning_hearth(&game, 20, 20);
    for (int i = 0; i < 12; i++) {
        run_hours(&game, 0.5f);
        refill(&game, hearth);
    }
    game.clock.tick = evening;
    Workstation *station = workstation_get(&game.workstations, hearth);
    station->fuel = HEARTH_FUEL_CAPACITY;
    CHECK(temperature_at(&game.temperatures, 19, 19) > COLONIST_COLD_SLEEP_BELOW);
    int warm = ticks_to_rest(&game, 19, 19);
    game_shutdown(&game);

    /* On the ground both times: 8 hours from 0.2 when mild. */
    CHECK_NEAR((double)warm, 8.0 * TICKS_PER_HOUR, 0.05 * TICKS_PER_HOUR);
    CHECK(cold > warm + 2 * TICKS_PER_HOUR);
}

/* A rested settler doing nothing, then tired. */
static Colonist *spawn_sleeper(Game *game, int x, int y)
{
    Colonist *colonist = game_colonist(game, game_spawn_colonist(game, x, y));
    if (colonist != NULL) {
        for (int type = 0; type < WORK_TYPE_COUNT; type++) {
            colonist_set_work_priority(colonist, (WorkType)type, WORK_PRIORITY_DISABLED);
        }
        colonist->needs[NEED_REST] = 0.2f;
    }
    return colonist;
}

static float felt(const Game *game, int x, int y)
{
    return hearths_felt_temperature(&game->workstations, &game->map, &game->temperatures, x, y);
}

static void test_sleeping_beside_a_fire(void)
{
    uint64_t night = mid_season(SEASON_WINTER) + 22 * TICKS_PER_HOUR;
    Game game;
    REQUIRE(init_flat_game(&game, night));
    int hearth = burning_hearth(&game, 20, 20);
    Colonist *first = spawn_sleeper(&game, 10, 20);
    Colonist *second = spawn_sleeper(&game, 30, 22);
    REQUIRE(first != NULL && second != NULL);
    CHECK(felt(&game, 10, 20) < COLONIST_COLD_SLEEP_BELOW);
    run_hours(&game, 0.3f);
    refill(&game, hearth);
    /* Both asleep near the fire, each on a spot of its own, neither on the
     * hearth, both feeling right. */
    CHECK_EQ_INT(first->task, TASK_SLEEP);
    CHECK_EQ_INT(second->task, TASK_SLEEP);
    CHECK(!colonist_is_moving(first) && !colonist_is_moving(second));
    CHECK(first->x != second->x || first->y != second->y);
    CHECK(first->x != 20 || first->y != 20);
    CHECK(abs(first->x - 20) <= HEARTH_RADIANT_RADIUS && abs(first->y - 20) <= 5);
    float first_felt = felt(&game, first->x, first->y);
    CHECK(first_felt >= COLONIST_COLD_SLEEP_BELOW && first_felt <= COLONIST_HOT_SLEEP_ABOVE);
    float second_felt = felt(&game, second->x, second->y);
    CHECK(second_felt >= COLONIST_COLD_SLEEP_BELOW && second_felt <= COLONIST_HOT_SLEEP_ABOVE);
    /* Resting as fast as on mild ground: 8 hours from 0.2. */
    int ticks = 0;
    while (first->task == TASK_SLEEP && ticks < TICKS_PER_DAY) {
        run_hours(&game, 0.1f);
        refill(&game, hearth);
        ticks += TICKS_PER_HOUR / 10;
    }
    CHECK(ticks <= 8 * TICKS_PER_HOUR + TICKS_PER_HOUR / 2);
    game_shutdown(&game);

    /* Asleep where it was mild enough in the evening: the night gets colder,
     * and it moves to the fire. */
    uint64_t autumn = mid_season(SEASON_AUTUMN) + 20 * TICKS_PER_HOUR;
    REQUIRE(init_flat_game(&game, autumn));
    hearth = burning_hearth(&game, 20, 20);
    Colonist *late = spawn_sleeper(&game, 10, 20);
    REQUIRE(late != NULL);
    CHECK(felt(&game, 10, 20) >= COLONIST_COLD_SLEEP_BELOW);
    late->needs[NEED_REST] = 0.0f; /* A long night ahead. */
    run_hours(&game, 0.2f);
    CHECK_EQ_INT(late->task, TASK_SLEEP);
    CHECK_EQ_INT(late->x, 10);
    for (int i = 0; i < 16; i++) {
        run_hours(&game, 0.5f);
        refill(&game, hearth);
        late->needs[NEED_REST] = fminf(late->needs[NEED_REST], 0.5f);
    }
    CHECK(felt(&game, 10, 20) < COLONIST_COLD_SLEEP_BELOW);
    CHECK_EQ_INT(late->task, TASK_SLEEP);
    CHECK(abs(late->x - 20) <= HEARTH_RADIANT_RADIUS && abs(late->y - 20) <= 5);
    float late_felt = felt(&game, late->x, late->y);
    CHECK(late_felt >= COLONIST_COLD_SLEEP_BELOW && late_felt <= COLONIST_HOT_SLEEP_ABOVE);
    game_shutdown(&game);

    /* No fire: asleep where it stands. */
    REQUIRE(init_flat_game(&game, night));
    Colonist *alone = spawn_sleeper(&game, 10, 20);
    REQUIRE(alone != NULL);
    run_hours(&game, 0.3f);
    CHECK_EQ_INT(alone->task, TASK_SLEEP);
    CHECK_EQ_INT(alone->x, 10);
    CHECK_EQ_INT(alone->y, 20);
    game_shutdown(&game);

    /* Mild enough where it stands: no need to move. */
    uint64_t summer = mid_season(SEASON_SUMMER) + 22 * TICKS_PER_HOUR;
    REQUIRE(init_flat_game(&game, summer));
    burning_hearth(&game, 20, 20);
    Colonist *mild = spawn_sleeper(&game, 10, 20);
    REQUIRE(mild != NULL);
    run_hours(&game, 0.3f);
    CHECK_EQ_INT(mild->x, 10);
    CHECK_EQ_INT(mild->y, 20);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_outdoor);
    RUN_TEST(test_open_ground_follows_the_air);
    RUN_TEST(test_materials_and_covers);
    RUN_TEST(test_cache_follows_the_map);
    RUN_TEST(test_hearth_warms_around_it);
    RUN_TEST(test_hut_keeps_the_warmth);
    RUN_TEST(test_cave_stays_mild);
    RUN_TEST(test_sleep_temperature_factor);
    RUN_TEST(test_cold_sleep_is_longer);
    RUN_TEST(test_sleeping_beside_a_fire);
    return test_summary();
}
