/* Blood on the ground: drops behind those who bleed, pools under those who
 * stay still, stains drying and fading (core/blood.h). */
#include "core/blood.h"
#include "core/game.h"

#include "test_framework.h"
#include "test_settlers.h"

#include <math.h>
#include <string.h>

enum { TORSO = 1 };

static Health hurt_human(float cut)
{
    Health health;
    health_init(&health, BODY_HUMAN, 1.0f, 1.0f, 1.0f);
    if (cut > 0.0f) {
        health_hurt(&health, TORSO, INJURY_CUT, cut);
    }
    return health;
}

static bool init_map(Map *map)
{
    if (!map_init(map, 16, 16, 1)) {
        return false;
    }
    map->terrain[map_index(map, 0, 0, 0)] = TERRAIN_SHALLOW_WATER;
    return true;
}

static void test_intensity(void)
{
    Health healthy = hurt_human(0.0f);
    CHECK_NEAR(blood_intensity(&healthy), 0.0f, 1e-6f);
    Health light = hurt_human(3.0f), heavy = hurt_human(30.0f);
    CHECK(blood_intensity(&light) > 0.0f);
    CHECK(blood_intensity(&light) < blood_intensity(&heavy));
    CHECK(blood_intensity(&heavy) <= 1.0f);
    /* A bruise does not bleed. */
    Health bruised = hurt_human(0.0f);
    health_hurt(&bruised, TORSO, INJURY_BRUISE, 10.0f);
    CHECK_NEAR(blood_intensity(&bruised), 0.0f, 1e-6f);
}

/* Walking `ticks` ticks with `health`, left to right along the row y = 5. */
static int walk(BloodStains *stains, const Map *map, const Health *health, int ticks)
{
    Rng rng;
    rng_seed(&rng, 11, 2);
    int fell = 0;
    for (int t = 0; t < ticks; t++) {
        BloodSource source = {2.5f + (float)t * 0.02f, 5.5f, 0, 1.0f, 0.0f, true, 3};
        fell += blood_bleed(stains, &rng, map, health, &source, (uint64_t)t);
    }
    return fell;
}

/* Drops fall on steps while it walks, more the worse it bleeds, near its
 * path and behind it as it goes. */
static void test_drops_while_walking(void)
{
    Map map;
    REQUIRE(init_map(&map));
    static BloodStains light_stains, heavy_stains;
    memset(&light_stains, 0, sizeof(light_stains));
    memset(&heavy_stains, 0, sizeof(heavy_stains));
    Health light = hurt_human(3.0f), heavy = hurt_human(30.0f), healthy = hurt_human(0.0f);
    int ticks = 50 * BLOOD_STEP_TICKS;
    int light_count = walk(&light_stains, &map, &light, ticks);
    int heavy_count = walk(&heavy_stains, &map, &heavy, ticks);
    CHECK(light_count > 0);
    CHECK(heavy_count > light_count * 2);
    CHECK(heavy_count <= 2 * 50); /* At most two a step. */
    CHECK_EQ_INT(heavy_stains.count, heavy_count);
    float light_size = 0.0f, heavy_size = 0.0f;
    for (int i = 0; i < light_stains.count; i++) {
        light_size += blood_recent(&light_stains, i)->size / (float)light_stains.count;
    }
    for (int i = 0; i < heavy_stains.count; i++) {
        const BloodStain *drop = blood_recent(&heavy_stains, i);
        heavy_size += drop->size / (float)heavy_stains.count;
        CHECK_EQ_INT(drop->kind, BLOOD_DROP);
        CHECK(fabsf(drop->y - 5.5f) < 0.2f);
        CHECK_NEAR(drop->dir_x, 1.0f, 1e-6f);
    }
    CHECK(heavy_size > light_size);
    /* The newest lies furthest along. */
    CHECK(blood_recent(&heavy_stains, 0)->x > blood_recent(&heavy_stains, heavy_count - 1)->x);
    /* Nothing from a body that does not bleed. */
    static BloodStains none;
    memset(&none, 0, sizeof(none));
    CHECK_EQ_INT(walk(&none, &map, &healthy, ticks), 0);
    map_free(&map);
}

/* Not on water. */
static void test_no_blood_on_water(void)
{
    Map map;
    REQUIRE(init_map(&map));
    static BloodStains stains;
    memset(&stains, 0, sizeof(stains));
    Rng rng;
    rng_seed(&rng, 3, 1);
    Health heavy = hurt_human(30.0f);
    for (int t = 0; t < 40 * BLOOD_STEP_TICKS; t++) {
        BloodSource on_water = {0.5f, 0.5f, 0, 0.0f, 0.0f, t % 2 == 0, 0};
        blood_bleed(&stains, &rng, &map, &heavy, &on_water, (uint64_t)t);
    }
    for (int i = 0; i < stains.count; i++) {
        const BloodStain *stain = blood_recent(&stains, i);
        CHECK(!(floorf(stain->x) == 0.0f && floorf(stain->y) == 0.0f));
    }
    map_free(&map);
}

/* Standing still, one pool grows under it, up to a limit. */
static void test_pool_while_still(void)
{
    Map map;
    REQUIRE(init_map(&map));
    static BloodStains stains;
    memset(&stains, 0, sizeof(stains));
    Rng rng;
    rng_seed(&rng, 5, 1);
    Health heavy = hurt_human(30.0f);
    BloodSource still = {6.5f, 6.5f, 0, 0.0f, 0.0f, false, 0};
    float size = 0.0f;
    for (int t = 0; t < 400 * BLOOD_POOL_FEED_TICKS; t++) {
        blood_bleed(&stains, &rng, &map, &heavy, &still, (uint64_t)t);
        const BloodStain *last = blood_recent(&stains, 0);
        if (last != NULL) {
            CHECK(last->size >= size - 1e-6f); /* It only grows. */
            size = last->size;
        }
    }
    const BloodStain *pool = blood_recent(&stains, 0);
    REQUIRE(pool != NULL);
    CHECK_EQ_INT(pool->kind, BLOOD_POOL);
    CHECK(pool->size > BLOOD_POOL_START);
    CHECK(pool->size <= BLOOD_POOL_MAX + 1e-6f);
    /* Only one pool still shows: the places it moved from are empty. */
    int pools = 0;
    BloodLook look;
    for (int i = 0; i < stains.count; i++) {
        pools += blood_look(blood_recent(&stains, i), 400 * BLOOD_POOL_FEED_TICKS, &look);
    }
    CHECK_EQ_INT(pools, 1);
    map_free(&map);
}

/* A stain is fresh and shining, then dry, then gone. */
static void test_look(void)
{
    BloodStain stain = {.x = 1.5f, .y = 1.5f, .kind = BLOOD_DROP, .size = 0.03f, .tick = 1000};
    BloodLook look;
    CHECK(!blood_look(&stain, 999, &look));
    REQUIRE(blood_look(&stain, 1000, &look));
    CHECK_NEAR(look.dryness, 0.0f, 1e-6f);
    CHECK(look.shine > 0.9f);
    CHECK(look.opacity > 0.5f);
    REQUIRE(blood_look(&stain, 1000 + BLOOD_STAIN_LIFE_TICKS * 3 / 4, &look));
    CHECK_NEAR(look.dryness, 1.0f, 1e-6f);
    CHECK_NEAR(look.shine, 0.0f, 1e-6f);
    CHECK(look.opacity < 0.9f);
    CHECK(!blood_look(&stain, 1000 + BLOOD_STAIN_LIFE_TICKS, &look));
}

/* The newest BLOOD_STAIN_MAX are kept. */
static void test_ring(void)
{
    Map map;
    REQUIRE(init_map(&map));
    static BloodStains stains;
    memset(&stains, 0, sizeof(stains));
    Rng rng;
    rng_seed(&rng, 9, 1);
    Health heavy = hurt_human(30.0f);
    REQUIRE(!health_is_dead(&heavy));
    int fell = 0;
    for (int t = 0; fell <= BLOOD_STAIN_MAX + 10 && t < 1000000; t++) {
        BloodSource source = {8.5f, 8.5f, 0, 0.0f, 1.0f, true, 0};
        fell += blood_bleed(&stains, &rng, &map, &heavy, &source, (uint64_t)t);
    }
    CHECK_EQ_INT(stains.count, BLOOD_STAIN_MAX);
    CHECK(blood_recent(&stains, BLOOD_STAIN_MAX) == NULL);
    CHECK(blood_recent(&stains, 0)->tick >= blood_recent(&stains, BLOOD_STAIN_MAX - 1)->tick);
    map_free(&map);
}

/* In a game: a settler bleeding as it walks leaves a trail; the game's hash
 * does not see it. */
static void test_trail_in_a_game(void)
{
    static Game game;
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 5;
    params.world.width = 32;
    params.world.height = 32;
    params.colonist_count = 0;
    REQUIRE(game_init(&game, &params));
    Map *map = &game.map;
    for (size_t i = 0; i < map_level_tile_count(map); i++) {
        map->terrain[i] = TERRAIN_SOIL;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
    }
    map_topology_changed(map);
    EntityId id = game_spawn_colonist(&game, 4, 10, 0);
    Colonist *settler = game_colonist(&game, id);
    REQUIRE(settler != NULL);
    make_ordinary(settler);
    health_hurt(&settler->health, TORSO, INJURY_CUT, 14.0f);
    REQUIRE(game_order_move(&game, &id, 1, 20, 10, 0) == 1);
    for (int t = 0; t < 8 * TICKS_PER_SECOND; t++) {
        game_tick(&game);
    }
    CHECK(game.blood.count > 3);
    int behind = 0;
    for (int i = 0; i < game.blood.count; i++) {
        const BloodStain *stain = blood_recent(&game.blood, i);
        behind += stain->kind == BLOOD_DROP && stain->x > 4.0f && stain->x < (float)settler->x + 1;
    }
    CHECK(behind > 0);
    /* The stains are not part of the hash. */
    uint64_t with_blood = game_hash(&game);
    static BloodStains saved;
    saved = game.blood;
    memset(&game.blood, 0, sizeof(game.blood));
    CHECK(game_hash(&game) == with_blood);
    game.blood = saved;
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_intensity);
    RUN_TEST(test_drops_while_walking);
    RUN_TEST(test_no_blood_on_water);
    RUN_TEST(test_pool_while_still);
    RUN_TEST(test_look);
    RUN_TEST(test_ring);
    RUN_TEST(test_trail_in_a_game);
    return test_summary();
}
