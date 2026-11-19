/* Shadows on screen (core/shadow.h): the surfaces the rays start from, the
 * light put together, and the field of shadows worked out ahead of time. */
#include "core/game_clock.h"
#include "core/map.h"
#include "core/shadow.h"
#include "core/structure.h"
#include "core/sun.h"

#include "test_framework.h"

#include <math.h>

#define SIZE 64
#define DEPTH 6
#define WALL_Y 40
#define EQUINOX SEASON_DAY(SEASON_SPRING, 8)
#define BIG_BUDGET 100000000u

static uint64_t tick_at(int day, float hour)
{
    return (uint64_t)day * TICKS_PER_DAY + (uint64_t)(hour * (float)TICKS_PER_HOUR);
}

/* Flat ground and a wall of one level from x = 20 to 40 along y = WALL_Y. */
static bool scene_init(Map *map)
{
    if (!map_init(map, SIZE, SIZE, DEPTH)) {
        return false;
    }
    for (int x = 20; x <= 40; x++) {
        map_set_structure(map, x, WALL_Y, 0, STRUCTURE_WALL);
    }
    return true;
}

static void work_until_ready(ShadowField *field, const Map *map, uint64_t tick)
{
    for (int i = 0; i < 1000 && !shadow_field_ready(field, tick); i++) {
        shadow_field_work(field, map, tick, BIG_BUDGET);
    }
}

/* The ground, the top of a block, through open space and thin foliage to
 * what lies below, nothing at all. */
static void test_surface_heights(void)
{
    Map map;
    REQUIRE(map_init(&map, 16, 16, 4));
    float height = -1.0f;
    CHECK(shadow_surface_height(&map, 2, 2, 0, &height));
    CHECK_NEAR(height, SUN_RAY_LIFT, 1e-6);
    CHECK(shadow_surface_height(&map, 2, 2, 2, &height)); /* Open space down to the ground. */
    CHECK_NEAR(height, SUN_RAY_LIFT, 1e-6);
    map.rock[map_index(&map, 3, 3, 0)] = ROCK_GRANITE;
    CHECK(shadow_surface_height(&map, 3, 3, 1, &height)); /* The top of the rock. */
    CHECK_NEAR(height, 2.0f + SUN_RAY_LIFT, 1e-6);
    map_set_structure(&map, 4, 4, 0, STRUCTURE_WALL);
    CHECK(shadow_surface_height(&map, 4, 4, 0, &height));
    CHECK_NEAR(height, 2.0f + SUN_RAY_LIFT, 1e-6);
    map_set_structure(&map, 5, 4, 0, STRUCTURE_GRASS_BED); /* Furniture: the floor. */
    CHECK(shadow_surface_height(&map, 5, 4, 0, &height));
    CHECK_NEAR(height, SUN_RAY_LIFT, 1e-6);
    map.terrain[map_index(&map, 6, 6, 2)] = TERRAIN_FOLIAGE; /* Dense: seen, its top. */
    CHECK(shadow_surface_height(&map, 6, 6, 3, &height));
    CHECK_NEAR(height, 6.0f + SUN_RAY_LIFT, 1e-6);
    map.terrain[map_index(&map, 7, 6, 2)] = TERRAIN_FOLIAGE_SPARSE; /* Seen through. */
    CHECK(shadow_surface_height(&map, 7, 6, 2, &height));
    CHECK_NEAR(height, SUN_RAY_LIFT, 1e-6);
    map.terrain[map_index(&map, 8, 8, 0)] = TERRAIN_OPEN; /* Nothing down there. */
    CHECK(!shadow_surface_height(&map, 8, 8, 1, &height));
    map_free(&map);
}

/* §2.4: no direct sun under an overcast sky or under SUN_LOW_ELEVATION,
 * 1 - SKY_DIFFUSE_CLEAR with a high sun in a clear sky; the light of a point
 * in the sun is that of the open ground, in the shade the sky's share. */
static void test_light_put_together(void)
{
    CHECK_NEAR(shadow_direct_share(45.0f, 1.0f), 1.0f - SKY_DIFFUSE_CLEAR, 1e-6);
    CHECK_NEAR(shadow_direct_share(45.0f, 0.0f), 0.0, 1e-6);
    CHECK_NEAR(shadow_direct_share(0.0f, 1.0f), 0.0, 1e-6);
    CHECK_NEAR(shadow_direct_share(-10.0f, 1.0f), 0.0, 1e-6);
    float low = shadow_direct_share(0.5f * SUN_LOW_ELEVATION, 1.0f);
    CHECK(low > 0.0f && low < 1.0f - SKY_DIFFUSE_CLEAR);
    float previous = 0.0f;
    for (float elevation = 0.0f; elevation <= 20.0f; elevation += 0.5f) {
        float share = shadow_direct_share(elevation, 1.0f);
        CHECK(share >= previous);
        previous = share;
    }
    const float night = 0.1f, day = 0.9f, direct = 0.65f;
    CHECK_NEAR(shadow_light(night, day, direct, 1.0f, 1.0f), 1.0, 1e-6);
    CHECK_NEAR(shadow_light(night, day, direct, 1.0f, 0.0f), night + day * (1.0f - direct), 1e-6);
    CHECK_NEAR(shadow_light(night, day, 0.0f, 1.0f, 0.0f), 1.0, 1e-6);   /* Overcast. */
    CHECK_NEAR(shadow_light(night, day, direct, 0.0f, 0.0f), 0.0, 1e-6); /* Under rock. */
}

/* Once worked out, the field gives what the rays give at the moments they
 * were cast for, and nearly everywhere in between. */
static void test_field_matches_the_rays(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    ShadowField field;
    shadow_field_init(&field, SUN_LATITUDE_DEFAULT);
    shadow_field_view(&field, 0, 4, 0, 0, SIZE, SIZE);
    uint64_t start = tick_at(EQUINOX, 15.0f); /* A step starts there. */
    REQUIRE(start % SHADOW_STEP_TICKS == 0);
    work_until_ready(&field, &map, start);
    REQUIRE(shadow_field_ready(&field, start));

    const uint64_t offsets[] = {0, SHADOW_STEP_TICKS / 2, SHADOW_STEP_TICKS / 5,
                                SHADOW_STEP_TICKS * 4 / 5};
    for (int o = 0; o < 4; o++) {
        uint64_t tick = start + offsets[o];
        SunPosition sun = sun_at_tick(tick, SUN_LATITUDE_DEFAULT);
        int points = 0, wrong = 0;
        for (int y = 30; y < 50; y++) {
            for (int x = 10; x < 50; x++) {
                for (int s = 0; s < 4; s++) {
                    float px = (float)x + ((float)(s % 2) * 2.0f + 1.0f) / 8.0f;
                    float py = (float)y + ((float)(s / 2) * 2.0f + 1.0f) / 8.0f;
                    float height = 0.0f;
                    REQUIRE(shadow_surface_height(&map, x, y, 0, &height));
                    float exact = sun_lit(&map, sun, px, py, height); /* A wall's top. */
                    float shown = shadow_field_lit(&field, tick, 0.0f, px, py);
                    points++;
                    wrong += fabsf(exact - shown) > 0.05f;
                }
            }
        }
        /* At the moments cast for, only the points' positions differ; in
         * between, the edge as found by halving. */
        CHECK(wrong * 100 < points * 2);
    }
    /* Shaded behind the wall (the sun in the south-west at 15:00), lit away
     * from it. */
    CHECK(shadow_field_lit(&field, start, 0.0f, 35.5f, (float)WALL_Y - 0.5f) < 0.01f);
    CHECK(shadow_field_lit(&field, start, 0.0f, 5.5f, 5.5f) > 0.99f);
    shadow_field_free(&field);
    map_free(&map);
}

/* A point the edge of the wall's shadow crosses: the field changes once,
 * within a minute or so of the moment the rays change, never by a jump. */
static void test_shadows_glide(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    ShadowField field;
    shadow_field_init(&field, SUN_LATITUDE_DEFAULT);
    shadow_field_view(&field, 0, 8, 0, 0, SIZE, SIZE);
    uint64_t start = tick_at(EQUINOX, 15.0f);
    /* Points north of the wall (the middles of points of the field): the
     * shadow sweeps over some of them. */
    int crossed = 0;
    for (int k = 0; k < 8 * 20 && crossed < 3; k += 3) {
        const float px = 21.0f + ((float)k + 0.5f) / 8.0f;
        const float py = (float)WALL_Y - 1.0f - 2.5f / 8.0f;
        float first = sun_lit_floor(&map, sun_at_tick(start, SUN_LATITUDE_DEFAULT), px, py, 0);
        float last = sun_lit_floor(
            &map, sun_at_tick(start + SHADOW_STEP_TICKS - 1, SUN_LATITUDE_DEFAULT), px, py, 0);
        if (first == last) {
            continue;
        }
        crossed++;
        uint64_t exact_tick = 0, shown_tick = 0;
        float previous = -1.0f;
        float biggest_jump = 0.0f;
        for (uint64_t t = start; t < start + SHADOW_STEP_TICKS; t++) {
            if (t % 25 == 0 || exact_tick == 0) {
                work_until_ready(&field, &map, t);
            }
            float exact = sun_lit_floor(&map, sun_at_tick(t, SUN_LATITUDE_DEFAULT), px, py, 0);
            float shown = shadow_field_lit(&field, t, 0.0f, px, py);
            if (exact_tick == 0 && fabsf(exact - first) > 0.5f) {
                exact_tick = t;
            }
            if (shown_tick == 0 && fabsf(shown - first) > 0.5f) {
                shown_tick = t;
            }
            if (previous >= 0.0f) {
                biggest_jump = fmaxf(biggest_jump, fabsf(shown - previous));
            }
            previous = shown;
        }
        REQUIRE(exact_tick != 0 && shown_tick != 0);
        long gap = (long)shown_tick - (long)exact_tick;
        CHECK(labs(gap) <= SHADOW_STEP_TICKS / SHADOW_STEP_PARTS + 10);
        CHECK(biggest_jump < 0.2f); /* Blended over several ticks. */
    }
    CHECK(crossed > 0);
    shadow_field_free(&field);
    map_free(&map);
}

/* The work keeps to its budget, a row of points at a time; the next half
 * hour is ready before it comes. */
static void test_budget_and_the_half_hour_ahead(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    ShadowField field;
    shadow_field_init(&field, SUN_LATITUDE_DEFAULT);
    shadow_field_view(&field, 0, 2, 0, 0, SIZE, SIZE);
    uint64_t tick = tick_at(EQUINOX, 10.0f);
    const uint64_t budget = 2000;
    const uint64_t row = 3 * SHADOW_CHUNK * 2 + 2 * 5 * SHADOW_CHUNK * 2; /* At most. */
    int calls = 0;
    while (!shadow_field_ready(&field, tick) && calls < 10000) {
        uint64_t before = field.rays;
        REQUIRE(shadow_field_work(&field, &map, tick, budget));
        CHECK(field.rays - before < budget + row);
        calls++;
    }
    CHECK(calls > 3);
    CHECK(shadow_field_ready(&field, tick));
    /* Keep working: the next half hour is worked out ahead. */
    uint64_t next = (tick / SHADOW_STEP_TICKS + 1) * SHADOW_STEP_TICKS;
    for (int i = 0; i < 100; i++) {
        shadow_field_work(&field, &map, tick, BIG_BUDGET);
    }
    CHECK(shadow_field_ready(&field, next));
    uint64_t rays = field.rays;
    shadow_field_work(&field, &map, tick, BIG_BUDGET); /* Nothing left to do. */
    CHECK_EQ_UINT(field.rays, rays);
    shadow_field_free(&field);
    map_free(&map);
}

/* A wall built within the reach of a piece makes it out of date, and its
 * shadow appears once worked out; a change behind the shadows (away from
 * the sun) leaves the pieces alone. */
static void test_map_changes(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    ShadowField field;
    shadow_field_init(&field, SUN_LATITUDE_DEFAULT);
    uint64_t tick = tick_at(EQUINOX, 12.0f);         /* The sun in the south. */
    shadow_field_view(&field, 0, 2, 0, 0, SIZE, 32); /* The northern half. */
    work_until_ready(&field, &map, tick);
    for (int i = 0; i < 50; i++) {
        shadow_field_work(&field, &map, tick, BIG_BUDGET);
    }
    CHECK(shadow_field_lit(&field, tick, 0.0f, 10.5f, 20.5f) > 0.99f);

    /* Far to the north of the view and beyond the map's edge of it: no
     * piece looks that way. */
    uint64_t rays = field.rays;
    map_set_structure(&map, 10, 0, 0, STRUCTURE_GRASS_BED);
    shadow_field_work(&field, &map, tick, BIG_BUDGET);
    CHECK_EQ_UINT(field.rays, rays);

    map_set_structure(&map, 10, 22, 0, STRUCTURE_WALL); /* South of (10, 20). */
    CHECK(!shadow_field_ready(&field, tick));
    work_until_ready(&field, &map, tick);
    CHECK(shadow_field_lit(&field, tick, 0.0f, 10.5f, 20.5f) < 0.01f);
    shadow_field_free(&field);
    map_free(&map);
}

/* Another level shown, other points per tile, another map: worked out
 * again; until then the shadows of the same level are shown. Pieces long out
 * of view are forgotten. */
static void test_view_changes(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    ShadowField field;
    shadow_field_init(&field, SUN_LATITUDE_DEFAULT);
    uint64_t tick = tick_at(EQUINOX, 15.0f);
    shadow_field_view(&field, 0, 2, 0, 0, SIZE, SIZE);
    work_until_ready(&field, &map, tick);
    float shaded = shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f);
    CHECK(shaded < 0.01f);

    shadow_field_view(&field, 0, 4, 0, 0, SIZE, SIZE);
    CHECK(!shadow_field_ready(&field, tick));
    CHECK_NEAR(shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f), shaded, 1e-6);
    work_until_ready(&field, &map, tick);
    CHECK(shadow_field_ready(&field, tick));

    shadow_field_view(&field, 1, 4, 0, 0, SIZE, SIZE);
    CHECK(!shadow_field_ready(&field, tick));
    work_until_ready(&field, &map, tick);
    /* On level 1 the ground shows through open space: the same shadow. */
    CHECK(shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f) < 0.01f);

    Map other;
    REQUIRE(map_init(&other, SIZE, SIZE, DEPTH));
    shadow_field_work(&field, &other, tick, 0); /* Another map: everything goes. */
    CHECK(shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f) > 0.99f);
    work_until_ready(&field, &other, tick);
    CHECK(shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f) > 0.99f);
    map_free(&other);

    /* Out of view long enough, a piece lets its memory go. */
    shadow_field_view(&field, 1, 4, 0, 0, 16, 16);
    for (int i = 0; i <= SHADOW_FORGET_FRAMES; i++) {
        shadow_field_view(&field, 1, 4, 0, 0, 16, 16);
    }
    CHECK(field.chunks[field.columns * field.rows - 1].slots[0].capacity == 0);
    CHECK(field.chunks[0].slots[0].capacity > 0); /* Still in view. */
    shadow_field_free(&field);
    map_free(&map);
}

/* Another level shown keeps what it can: the shadow of a point depends only
 * on the surface drawn there, so the columns whose surface stays the same
 * keep their values (no ray cast for them) and are shown at once; only the
 * others are worked out again. */
static void test_level_change_keeps_the_same_surfaces(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    ShadowField field;
    shadow_field_init(&field, SUN_LATITUDE_DEFAULT);
    uint64_t tick = tick_at(EQUINOX, 15.0f);
    shadow_field_view(&field, 0, 2, 0, 0, SIZE, SIZE);
    work_until_ready(&field, &map, tick);
    float shaded = shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f);
    CHECK(shaded < 0.01f);

    /* Level 1 over flat ground and a wall of level 0: the same surfaces. */
    uint64_t rays = field.rays;
    shadow_field_view(&field, 1, 2, 0, 0, SIZE, SIZE);
    shadow_field_work(&field, &map, tick, 0);
    CHECK(shadow_field_ready(&field, tick));
    CHECK_NEAR(shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f), shaded, 1e-6);
    work_until_ready(&field, &map, tick);
    CHECK_EQ_UINT(field.rays, rays);

    /* A roof on level 1 over a few tiles: shown from level 1, only its
     * columns are worked out again, and the old shadows stay in view
     * meanwhile. */
    for (int x = 4; x < 7; x++) {
        map_set_roof(&map, x, 4, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    }
    shadow_field_view(&field, 0, 2, 0, 0, SIZE, SIZE);
    work_until_ready(&field, &map, tick);
    rays = field.rays;
    shadow_field_view(&field, 1, 2, 0, 0, SIZE, SIZE);
    shadow_field_work(&field, &map, tick, 0);
    CHECK(!shadow_field_ready(&field, tick));
    CHECK_NEAR(shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f), shaded, 1e-6);
    work_until_ready(&field, &map, tick);
    CHECK(field.rays - rays <= 3u * 4u * 3u * 2u); /* 3 columns, 4 points, a few rays each. */
    CHECK(field.rays > rays);
    SunPosition sun = sun_at_tick(tick, SUN_LATITUDE_DEFAULT); /* At the half hour's start. */
    CHECK_NEAR(shadow_field_lit(&field, tick, 0.0f, 5.25f, 4.25f),
               sun_lit_floor(&map, sun, 5.25f, 4.25f, 1), 1.0 / 255.0); /* The roof's top. */
    CHECK_NEAR(shadow_field_lit(&field, tick, 0.0f, 35.5f, (float)WALL_Y - 0.5f), shaded, 1e-6);
    /* Back on level 0, the ground under the roof as the rays say. */
    shadow_field_view(&field, 0, 2, 0, 0, SIZE, SIZE);
    work_until_ready(&field, &map, tick);
    CHECK_NEAR(shadow_field_lit(&field, tick, 0.0f, 5.25f, 4.25f),
               sun_lit_floor(&map, sun, 5.25f, 4.25f, 0), 1.0 / 255.0);
    shadow_field_free(&field);
    map_free(&map);
}

/* shadow_field_fill() gives shadow_field_lit() at the middle of the points. */
static void test_fill_matches_lit(void)
{
    Map map;
    REQUIRE(scene_init(&map));
    ShadowField field;
    shadow_field_init(&field, SUN_LATITUDE_DEFAULT);
    uint64_t tick = tick_at(EQUINOX, 15.0f) + 333;
    shadow_field_view(&field, 0, 4, 0, 0, SIZE, SIZE);
    work_until_ready(&field, &map, tick);
    const int x0 = 30, y0 = 33, columns = 7, rows = 5, samples = 3;
    float out[7 * 3 * 5 * 3];
    shadow_field_fill(&field, tick, 0.5f, x0, y0, columns, rows, samples, out);
    for (int j = 0; j < rows * samples; j++) {
        for (int i = 0; i < columns * samples; i++) {
            float x = (float)x0 + ((float)i + 0.5f) / (float)samples;
            float y = (float)y0 + ((float)j + 0.5f) / (float)samples;
            CHECK_NEAR(out[j * columns * samples + i], shadow_field_lit(&field, tick, 0.5f, x, y),
                       1e-6);
        }
    }
    /* Off the map: lit. */
    shadow_field_fill(&field, tick, 0.0f, -3, -3, 1, 1, 1, out);
    CHECK_NEAR(out[0], 1.0, 1e-6);
    shadow_field_free(&field);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_surface_heights);
    RUN_TEST(test_light_put_together);
    RUN_TEST(test_field_matches_the_rays);
    RUN_TEST(test_shadows_glide);
    RUN_TEST(test_budget_and_the_half_hour_ahead);
    RUN_TEST(test_map_changes);
    RUN_TEST(test_view_changes);
    RUN_TEST(test_level_change_keeps_the_same_surfaces);
    RUN_TEST(test_fill_matches_lit);
    return test_summary();
}
