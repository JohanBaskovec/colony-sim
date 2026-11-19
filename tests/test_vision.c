/* Vision (core/vision.h): sight in the volume of the map. Hand-built scenes
 * for what blocks it, and a comparison with a brute-force line of sight on
 * generated worlds. */
#include "core/game.h"
#include "core/map.h"
#include "core/structure.h"
#include "core/vision.h"
#include "core/worldgen.h"

#include "test_framework.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct Scene {
    Map map;
    Vision vision;
    uint64_t *bits;
} Scene;

/* A map of soil on level 0 and open space over it, `depth` levels. */
static bool scene_init(Scene *scene, int width, int height, int depth)
{
    memset(scene, 0, sizeof(*scene));
    if (!map_init(&scene->map, width, height, depth)) {
        return false;
    }
    return true;
}

/* Starts the vision once the map is built. */
static bool scene_start(Scene *scene)
{
    map_topology_changed(&scene->map);
    if (!vision_init(&scene->vision, &scene->map)) {
        return false;
    }
    scene->bits = calloc(scene->vision.words, sizeof(*scene->bits));
    return scene->bits != NULL;
}

static void scene_free(Scene *scene)
{
    free(scene->bits);
    vision_free(&scene->vision);
    map_free(&scene->map);
}

static void scene_clear(Scene *scene)
{
    memset(scene->bits, 0, scene->vision.words * sizeof(*scene->bits));
}

static bool bit_at(const Scene *scene, int x, int y, int z)
{
    size_t index = map_index(&scene->map, x, y, z);
    return (scene->bits[index >> 6] >> (index & 63) & 1u) != 0;
}

static size_t bit_count(const Scene *scene)
{
    size_t count = 0;
    for (size_t i = 0; i < scene->vision.words; i++) {
        count += (size_t)__builtin_popcountll(scene->bits[i]);
    }
    return count;
}

/* Rock on a tile (a 2 m wall). */
static void put_rock(Scene *scene, int x, int y, int z)
{
    scene->map.rock[map_index(&scene->map, x, y, z)] = ROCK_GRANITE;
}

/* A floor (soil) on a tile. */
static void put_floor(Scene *scene, int x, int y, int z)
{
    scene->map.terrain[map_index(&scene->map, x, y, z)] = TERRAIN_SOIL;
}

static void test_open_plain(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 3));
    REQUIRE(scene_start(&scene));
    vision_cast(&scene.vision, &scene.map, 20, 20, 0, false, 0, scene.bits);
    /* Every tile of the ground, and nothing in the air over it. */
    CHECK_EQ_INT(bit_count(&scene), 40 * 40);
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 40; x++) {
            CHECK(bit_at(&scene, x, y, 0));
        }
    }
    /* From the corner of the map too. */
    scene_clear(&scene);
    vision_cast(&scene.vision, &scene.map, 0, 0, 0, false, 0, scene.bits);
    CHECK_EQ_INT(bit_count(&scene), 40 * 40);
    scene_free(&scene);
}

static void test_wall_casts_a_shadow(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 3));
    for (int y = 15; y <= 25; y++) {
        put_rock(&scene, 25, y, 0);
    }
    REQUIRE(scene_start(&scene));
    vision_cast(&scene.vision, &scene.map, 20, 20, 0, false, 0, scene.bits);
    CHECK(bit_at(&scene, 24, 20, 0));  /* In front of the wall. */
    CHECK(bit_at(&scene, 25, 20, 0));  /* The wall itself. */
    CHECK(!bit_at(&scene, 26, 20, 0)); /* Right behind it. */
    CHECK(!bit_at(&scene, 30, 20, 0));
    CHECK(!bit_at(&scene, 35, 22, 0));
    CHECK(bit_at(&scene, 30, 35, 0)); /* Past the end of the wall. */
    CHECK(bit_at(&scene, 26, 10, 0));
    CHECK(bit_at(&scene, 10, 20, 0)); /* The other way. */
    scene_free(&scene);
}

static void test_structures(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 3));
    /* A built wall, a door, a prop and a bed, each across the line east. */
    scene.map.structure[map_index(&scene.map, 25, 10, 0)] = STRUCTURE_WALL;
    scene.map.structure[map_index(&scene.map, 25, 20, 0)] = STRUCTURE_DOOR;
    scene.map.structure[map_index(&scene.map, 25, 30, 0)] = STRUCTURE_PROP;
    scene.map.structure[map_index(&scene.map, 25, 35, 0)] = STRUCTURE_GRASS_BED;
    REQUIRE(scene_start(&scene));
    vision_cast_eye(&scene.vision, &scene.map, 20, 10, 0, 20.5f, 10.5f, 0, scene.bits);
    CHECK(bit_at(&scene, 25, 10, 0));
    CHECK(!bit_at(&scene, 30, 10, 0)); /* Behind the wall. */
    scene_clear(&scene);
    vision_cast_eye(&scene.vision, &scene.map, 20, 20, 0, 20.5f, 20.5f, 0, scene.bits);
    CHECK(bit_at(&scene, 25, 20, 0));
    CHECK(!bit_at(&scene, 30, 20, 0)); /* A screen of branches hides too. */
    scene_clear(&scene);
    /* Open (somebody in the doorway, or kept open), it is seen through. */
    const uint8_t OPEN[] = {MAP_DOOR_PASSING, MAP_DOOR_KEPT_OPEN};
    for (int i = 0; i < 2; i++) {
        scene.map.doors[map_index(&scene.map, 25, 20, 0)] = OPEN[i];
        vision_cast_eye(&scene.vision, &scene.map, 20, 20, 0, 20.5f, 20.5f, 0, scene.bits);
        CHECK(bit_at(&scene, 30, 20, 0));
        scene_clear(&scene);
    }
    scene.map.doors[map_index(&scene.map, 25, 20, 0)] = 0;
    vision_cast_eye(&scene.vision, &scene.map, 20, 30, 0, 20.5f, 30.5f, 0, scene.bits);
    CHECK(bit_at(&scene, 30, 30, 0)); /* A post is seen through. */
    scene_clear(&scene);
    vision_cast_eye(&scene.vision, &scene.map, 20, 35, 0, 20.5f, 35.5f, 0, scene.bits);
    CHECK(bit_at(&scene, 30, 35, 0)); /* So is a bed. */
    scene_free(&scene);
}

static void test_own_tile_does_not_block(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 3));
    /* A settler standing in a doorway sees both ways. */
    scene.map.structure[map_index(&scene.map, 20, 20, 0)] = STRUCTURE_DOOR;
    REQUIRE(scene_start(&scene));
    vision_cast(&scene.vision, &scene.map, 20, 20, 0, false, 0, scene.bits);
    CHECK(bit_at(&scene, 30, 20, 0));
    CHECK(bit_at(&scene, 10, 20, 0));
    CHECK(bit_at(&scene, 20, 20, 0));
    scene_free(&scene);
}

static void test_floors_block_between_levels(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 5));
    /* A floor over the whole of level 2, a block on it at level 3. */
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 40; x++) {
            put_floor(&scene, x, y, 2);
        }
    }
    put_rock(&scene, 20, 20, 3);
    put_rock(&scene, 30, 20, 0); /* And a block on the ground. */
    REQUIRE(scene_start(&scene));
    /* From the ground: the ceiling is seen from below, what lies on it is not. */
    vision_cast(&scene.vision, &scene.map, 20, 20, 0, false, 0, scene.bits);
    CHECK(bit_at(&scene, 20, 20, 2));
    CHECK(bit_at(&scene, 30, 20, 0));
    CHECK(!bit_at(&scene, 20, 20, 3));
    /* From the floor over it: the ground and its block are not seen. */
    scene_clear(&scene);
    vision_cast(&scene.vision, &scene.map, 10, 20, 2, false, 0, scene.bits);
    CHECK(bit_at(&scene, 20, 20, 3));
    CHECK(bit_at(&scene, 20, 20, 2));
    CHECK(!bit_at(&scene, 30, 20, 0));
    CHECK(!bit_at(&scene, 10, 10, 0));
    scene_free(&scene);
}

static void test_open_space_is_seen_through(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 5));
    /* A plateau on level 1 with a hole in it (no floor, rock gone) over the
     * ground of level 0. */
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 40; x++) {
            put_floor(&scene, x, y, 1);
            put_rock(&scene, x, y, 0);
        }
    }
    for (int y = 18; y <= 22; y++) {
        for (int x = 18; x <= 22; x++) {
            scene.map.terrain[map_index(&scene.map, x, y, 1)] = TERRAIN_OPEN;
            scene.map.rock[map_index(&scene.map, x, y, 0)] = ROCK_NONE;
        }
    }
    REQUIRE(scene_start(&scene));
    /* From the rim of the pit, its bottom is seen, down through the hole. */
    vision_cast(&scene.vision, &scene.map, 17, 20, 1, false, 0, scene.bits);
    CHECK(bit_at(&scene, 20, 20, 0));
    CHECK(bit_at(&scene, 30, 20, 1));
    CHECK(!bit_at(&scene, 5, 20, 0)); /* Under the plateau: rock. */
    /* From further back, only the far wall of the pit: the near rim hides its floor. */
    scene_clear(&scene);
    vision_cast(&scene.vision, &scene.map, 10, 20, 1, false, 0, scene.bits);
    CHECK(bit_at(&scene, 23, 20, 0));
    CHECK(!bit_at(&scene, 20, 20, 0));
    scene_free(&scene);
}

static void test_a_tower_sees_further(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 60, 40, 6));
    /* Pillars of rock 2 m high across the plain, and a tower three levels high
     * with a floor on top. */
    for (int y = 0; y < 40; y++) {
        put_rock(&scene, 25, y, 0);
    }
    for (int level = 0; level < 3; level++) {
        put_rock(&scene, 20, 20, level);
    }
    put_floor(&scene, 20, 20, 3);
    REQUIRE(scene_start(&scene));
    /* On the ground the pillar wall hides what is behind it. */
    vision_cast(&scene.vision, &scene.map, 15, 20, 0, false, 0, scene.bits);
    CHECK(!bit_at(&scene, 40, 20, 0));
    /* From the top of the tower, it does not: the wall is below the eyes. */
    scene_clear(&scene);
    vision_cast(&scene.vision, &scene.map, 20, 20, 3, false, 0, scene.bits);
    CHECK(bit_at(&scene, 40, 20, 0));
    CHECK(bit_at(&scene, 50, 10, 0));
    CHECK(!bit_at(&scene, 26, 20, 0)); /* Right behind the wall, still hidden... */
    CHECK(bit_at(&scene, 30, 20, 0));  /* ...a little further, not. */
    scene_free(&scene);
}

static void test_cliff(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 50, 40, 4));
    /* A plateau of one level for x >= 25, its cliff a rock level. */
    for (int y = 0; y < 40; y++) {
        for (int x = 25; x < 50; x++) {
            put_rock(&scene, x, y, 0);
            put_floor(&scene, x, y, 1);
        }
    }
    REQUIRE(scene_start(&scene));
    /* At the foot, the face of the cliff but not the plateau (the eyes are below its edge). */
    vision_cast(&scene.vision, &scene.map, 20, 20, 0, false, 0, scene.bits);
    CHECK(bit_at(&scene, 25, 20, 0));
    CHECK(!bit_at(&scene, 30, 20, 1));
    CHECK(!bit_at(&scene, 40, 20, 1));
    /* From the plateau, the plains below, but not at the foot of the cliff. */
    scene_clear(&scene);
    vision_cast(&scene.vision, &scene.map, 30, 20, 1, false, 0, scene.bits);
    CHECK(bit_at(&scene, 10, 20, 0));
    CHECK(!bit_at(&scene, 24, 20, 0));
    CHECK(bit_at(&scene, 45, 20, 1));
    scene_free(&scene);
}

static void test_thin_foliage_is_seen_through(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 4));
    /* A floor of foliage over the whole of level 2, thin or dense; a block on level 3. */
    for (int y = 0; y < 40; y++) {
        for (int x = 0; x < 40; x++) {
            scene.map.terrain[map_index(&scene.map, x, y, 2)] =
                x < 20 ? TERRAIN_FOLIAGE_MEDIUM : TERRAIN_FOLIAGE;
        }
    }
    put_rock(&scene, 10, 20, 3);
    put_rock(&scene, 35, 20, 3);
    REQUIRE(scene_start(&scene));
    vision_cast(&scene.vision, &scene.map, 5, 20, 0, false, 0, scene.bits);
    CHECK(bit_at(&scene, 10, 20, 3)); /* Through thin foliage. */
    /* Under dense foliage the block over it is hidden, under thin it is not. */
    scene_clear(&scene);
    vision_cast(&scene.vision, &scene.map, 25, 20, 0, false, 0, scene.bits);
    CHECK(!bit_at(&scene, 35, 20, 3));
    scene_clear(&scene);
    vision_cast(&scene.vision, &scene.map, 15, 20, 0, false, 0, scene.bits);
    CHECK(bit_at(&scene, 10, 20, 3));
    scene_free(&scene);
}

static void test_peeking_round_a_corner(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 3));
    put_rock(&scene, 21, 20, 0); /* A pillar to the east of the settler. */
    REQUIRE(scene_start(&scene));
    vision_cast_eye(&scene.vision, &scene.map, 20, 20, 0, 20.5f, 20.5f, 0, scene.bits);
    CHECK(!bit_at(&scene, 27, 17, 0)); /* Hidden from the middle of the tile. */
    scene_clear(&scene);
    vision_cast(&scene.vision, &scene.map, 20, 20, 0, false, 0, scene.bits);
    CHECK(bit_at(&scene, 27, 17, 0)); /* Seen by leaning north. */
    CHECK(bit_at(&scene, 27, 23, 0));
    scene_free(&scene);
}

static void test_asleep(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 3));
    REQUIRE(scene_start(&scene));
    vision_cast(&scene.vision, &scene.map, 20, 20, 0, true, 0, scene.bits);
    CHECK_EQ_INT(bit_count(&scene), 9); /* Its tile and the eight around. */
    CHECK(bit_at(&scene, 21, 21, 0));
    CHECK(!bit_at(&scene, 22, 20, 0));
    scene_free(&scene);
}

static void test_radius(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 40, 40, 3));
    REQUIRE(scene_start(&scene));
    vision_cast(&scene.vision, &scene.map, 20, 20, 0, false, 5, scene.bits);
    CHECK_EQ_INT(bit_count(&scene), 11 * 11);
    CHECK(bit_at(&scene, 25, 25, 0));
    CHECK(!bit_at(&scene, 26, 20, 0));
    scene_free(&scene);
}

/* Viewers and the per-tile counts. */
static void test_viewers(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 60, 60, 3));
    for (int y = 0; y < 60; y++) {
        put_rock(&scene, 30, y, 0); /* A wall across the map. */
    }
    REQUIRE(scene_start(&scene));
    Vision *vision = &scene.vision;
    vision_viewer_set(vision, &scene.map, 0, 10, 30, 0, false, 0);
    vision_viewer_set(vision, &scene.map, 1, 12, 30, 0, false, 0);
    vision_viewer_set(vision, &scene.map, 2, 50, 30, 0, false, 0);
    CHECK(vision_seen(vision, 20, 30, 0));
    CHECK(vision_viewer_sees(vision, 0, 20, 30, 0));
    CHECK(!vision_viewer_sees(vision, 0, 40, 30, 0)); /* Behind the wall. */
    CHECK(vision_viewer_sees(vision, 2, 40, 30, 0));
    CHECK(!vision_viewer_sees(vision, 2, 20, 30, 0));
    CHECK_EQ_INT(vision->seen[map_index(&scene.map, 20, 30, 0)], 2);
    CHECK_EQ_INT(vision->seen[map_index(&scene.map, 40, 30, 0)], 1);
    /* One walks away: its counts follow, what it left behind is no longer seen. */
    vision_viewer_set(vision, &scene.map, 1, 12, 5, 0, false, 1);
    CHECK_EQ_INT(vision->seen[map_index(&scene.map, 20, 30, 0)], 2); /* Still in view. */
    vision_viewer_set(vision, &scene.map, 1, 40, 5, 0, false, 2);    /* Past the wall. */
    CHECK_EQ_INT(vision->seen[map_index(&scene.map, 20, 30, 0)], 1);
    CHECK_EQ_INT(vision->seen[map_index(&scene.map, 40, 30, 0)], 2);
    vision_viewer_remove(vision, 0);
    CHECK(!vision_seen(vision, 20, 30, 0));
    CHECK(vision_seen(vision, 40, 30, 0)); /* The two on the east side still see it. */
    vision_viewer_remove(vision, 1);
    vision_viewer_remove(vision, 2);
    for (size_t i = 0; i < map_tile_count(&scene.map); i++) {
        if (vision->seen[i] != 0) {
            CHECK(false);
            break;
        }
    }
    scene_free(&scene);
}

/* The counts are the sums of the fields, after any number of moves and map changes. */
static void test_counts_match_fields(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 50, 50, 3));
    REQUIRE(scene_start(&scene));
    Vision *vision = &scene.vision;
    uint64_t tick = 0;
    for (int step = 0; step < 120; step++) {
        tick += 7;
        if (step % 17 == 3) { /* A wall goes up or comes down. */
            int x = 5 + step % 40, y = 5 + step * 3 % 40;
            size_t index = map_index(&scene.map, x, y, 0);
            scene.map.rock[index] = scene.map.rock[index] == 0 ? ROCK_GRANITE : ROCK_NONE;
            map_topology_changed(&scene.map);
        }
        vision_update_map(vision, &scene.map);
        for (int slot = 0; slot < 4; slot++) {
            int x = (step * (3 + slot) + slot * 11) % 50, y = (step * (5 + slot) + slot * 7) % 50;
            if (scene.map.rock[map_index(&scene.map, x, y, 0)] != 0) {
                continue;
            }
            vision_viewer_set(vision, &scene.map, slot, x, y, 0, step % 13 == 0, tick);
        }
    }
    for (size_t i = 0; i < map_tile_count(&scene.map); i++) {
        int count = 0;
        for (int slot = 0; slot < VISION_MAX_VIEWERS; slot++) {
            const VisionViewer *viewer = &vision->viewers[slot];
            if (viewer->active && (viewer->field[i >> 6] >> (i & 63) & 1u) != 0) {
                count++;
            }
        }
        if (vision->seen[i] != count) {
            CHECK_EQ_INT(vision->seen[i], count);
            break;
        }
    }
    scene_free(&scene);
}

/* A viewer with a limit sees no further than it; lifting it brings the whole
 * field back at once. */
static void test_limited_viewer(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 60, 60, 3));
    REQUIRE(scene_start(&scene));
    Vision *vision = &scene.vision;
    vision_viewer_set_limited(vision, &scene.map, 0, 10, 30, 0, false, 5, 0);
    CHECK(vision_viewer_sees(vision, 0, 10, 30, 0));
    CHECK(vision_viewer_sees(vision, 0, 15, 30, 0));
    CHECK(!vision_viewer_sees(vision, 0, 20, 30, 0));
    CHECK(!vision_viewer_sees(vision, 0, 40, 30, 0));
    /* Moving about keeps the limit. */
    vision_viewer_set_limited(vision, &scene.map, 0, 14, 30, 0, false, 5, 1);
    CHECK(vision_viewer_sees(vision, 0, 19, 30, 0));
    CHECK(!vision_viewer_sees(vision, 0, 40, 30, 0));
    /* A wider limit, then none. */
    vision_viewer_set_limited(vision, &scene.map, 0, 14, 30, 0, false, 12, 2);
    CHECK(vision_viewer_sees(vision, 0, 26, 30, 0));
    CHECK(!vision_viewer_sees(vision, 0, 40, 30, 0));
    vision_viewer_set(vision, &scene.map, 0, 14, 30, 0, false, 3);
    CHECK(vision_viewer_sees(vision, 0, 40, 30, 0));
    /* And again. */
    vision_viewer_set_limited(vision, &scene.map, 0, 14, 30, 0, false, 3, 4);
    CHECK(!vision_viewer_sees(vision, 0, 40, 30, 0));
    CHECK(vision_viewer_sees(vision, 0, 17, 30, 0));
    CHECK(!vision_viewer_sees(vision, 0, 20, 30, 0));
    scene_free(&scene);
}

/* A change near a viewer is seen at once, one far away at the next whole recast. */
static void test_map_changes(void)
{
    Scene scene;
    REQUIRE(scene_init(&scene, 60, 60, 3));
    REQUIRE(scene_start(&scene));
    Vision *vision = &scene.vision;
    vision_viewer_set(vision, &scene.map, 0, 10, 30, 0, false, 0);
    CHECK(vision_viewer_sees(vision, 0, 40, 30, 0));
    /* A wall goes up 5 tiles away: the field behind it goes at once. */
    put_rock(&scene, 15, 30, 0);
    map_topology_changed(&scene.map);
    vision_update_map(vision, &scene.map);
    vision_viewer_set(vision, &scene.map, 0, 10, 30, 0, false, 1);
    CHECK(!vision_viewer_sees(vision, 0, 18, 30, 0));
    /* The far part is cast afresh after the interval. */
    vision_viewer_set(vision, &scene.map, 0, 10, 30, 0, false, 1 + VISION_FAR_INTERVAL);
    CHECK(!vision_viewer_sees(vision, 0, 40, 30, 0));
    scene_free(&scene);
}

/* In a game: settlers are viewers, tracking changes nothing of the simulation. */
static void test_game_tracks_settlers(void)
{
    uint64_t hashes[2];
    for (int on = 0; on < 2; on++) {
        Game game;
        NewGameParams params;
        game_default_params(&params);
        params.world.seed = 4;
        params.world.width = 64;
        params.world.height = 64;
        params.colonist_count = 3;
        REQUIRE(game_init(&game, &params));
        REQUIRE(game_set_vision(&game, on != 0));
        for (int i = 0; i < 900; i++) {
            game_tick(&game);
        }
        if (on) {
            REQUIRE(game.vision_on);
            for (uint32_t slot = 0; slot < game_colonist_slot_count(&game); slot++) {
                const Colonist *colonist =
                    game_colonist_const(&game, game_colonist_id_at(&game, slot));
                REQUIRE(colonist != NULL);
                CHECK(vision_viewer_sees(&game.vision, (int)slot, colonist->x, colonist->y,
                                         colonist->z));
                CHECK(vision_seen(&game.vision, colonist->x, colonist->y, colonist->z));
            }
        }
        hashes[on] = game_hash(&game);
        game_shutdown(&game);
    }
    CHECK(hashes[0] == hashes[1]);
}

/* ------------------------------------------------------------------------ */
/* Brute force                                                               */
/* ------------------------------------------------------------------------ */

static bool opaque_floor(uint8_t terrain)
{
    return terrain != TERRAIN_OPEN && terrain != TERRAIN_FOLIAGE_MEDIUM &&
           terrain != TERRAIN_FOLIAGE_SPARSE;
}

static bool solid_tile(const Map *map, size_t index)
{
    return map->rock[index] != 0 || map->structure[index] == STRUCTURE_WALL ||
           map->structure[index] == STRUCTURE_DOOR;
}

/* Whether the segment from the eye (e) to a point (p), in tiles with levels
 * counted in cells (a level is one cell high), enters the target tile. */
static bool segment_reaches(const Map *map, const double e[3], const double p[3],
                            const int start[3], const int target[3])
{
    int cell[3] = {start[0], start[1], start[2]};
    double d[3] = {p[0] - e[0], p[1] - e[1], p[2] - e[2]};
    int step[3];
    double next[3], delta[3];
    for (int a = 0; a < 3; a++) {
        step[a] = d[a] > 0 ? 1 : -1;
        delta[a] = d[a] != 0 ? fabs(1.0 / d[a]) : 1e30;
        double from = d[a] > 0 ? (double)(cell[a] + 1) - e[a] : e[a] - (double)cell[a];
        next[a] = d[a] != 0 ? from * delta[a] : 1e30;
    }
    for (int guard = 0; guard < 2000; guard++) {
        if (cell[0] == target[0] && cell[1] == target[1] && cell[2] == target[2]) {
            return true;
        }
        int axis = next[0] < next[1] ? (next[0] < next[2] ? 0 : 2) : (next[1] < next[2] ? 1 : 2);
        if (next[axis] > 1.0) {
            return false; /* The segment ends before the next cell. */
        }
        int old[3] = {cell[0], cell[1], cell[2]};
        cell[axis] += step[axis];
        next[axis] += delta[axis];
        if (!map_in_bounds(map, cell[0], cell[1], cell[2])) {
            return false;
        }
        size_t index = map_index(map, cell[0], cell[1], cell[2]);
        bool is_target = cell[0] == target[0] && cell[1] == target[1] && cell[2] == target[2];
        if (axis == 2 && step[2] < 0) {
            /* Down through the floor of the cell left. */
            size_t left = map_index(map, old[0], old[1], old[2]);
            if (map->rock[left] == 0 && opaque_floor(map->terrain[left])) {
                return false;
            }
        }
        if (solid_tile(map, index)) {
            return is_target;
        }
        if (axis == 2 && step[2] > 0 && opaque_floor(map->terrain[index])) {
            return is_target;
        }
    }
    return false;
}

/* Tiles a single eye sees, by sampling points of every tracked tile. */
static void brute_force(const Map *map, int x, int y, int z, uint64_t *bits)
{
    double eye[3] = {(double)x + 0.5, (double)y + 0.5, (double)z + (double)VISION_EYE_HEIGHT / 2.0};
    int start[3] = {x, y, z};
    static const double OFFSETS[3] = {0.1, 0.5, 0.9};
    for (int tz = 0; tz < map->depth; tz++) {
        for (int ty = 0; ty < map->height; ty++) {
            for (int tx = 0; tx < map->width; tx++) {
                if (!vision_tracked(map, tx, ty, tz)) {
                    continue;
                }
                int target[3] = {tx, ty, tz};
                bool seen = tx == x && ty == y && tz == z;
                /* A solid tile is seen by any point of its box, any other by a
                 * point of its floor. */
                size_t tile = map_index(map, tx, ty, tz);
                bool solid = solid_tile(map, tile);
                for (int k = 0; k < (solid ? 27 : 9) && !seen; k++) {
                    double p[3] = {(double)tx + OFFSETS[k % 3], (double)ty + OFFSETS[k / 3 % 3],
                                   (double)tz + (solid ? OFFSETS[k / 9] : 1.0e-6)};
                    seen = segment_reaches(map, eye, p, start, target);
                }
                if (seen) {
                    size_t index = map_index(map, tx, ty, tz);
                    bits[index >> 6] |= (uint64_t)1 << (index & 63);
                }
            }
        }
    }
}

static void compare_with_brute_force(const Map *map, Vision *vision, int x, int y, int z,
                                     int radius, size_t *wrong_hidden, size_t *wrong_seen,
                                     size_t *seen)
{
    uint64_t *fast = calloc(vision->words, sizeof(*fast));
    uint64_t *slow = calloc(vision->words, sizeof(*slow));
    if (fast == NULL || slow == NULL) {
        free(fast);
        free(slow);
        CHECK(false);
        return;
    }
    vision_cast_eye(vision, map, x, y, z, (float)x + 0.5f, (float)y + 0.5f, radius, fast);
    brute_force(map, x, y, z, slow);
    if (radius > 0) { /* The reference looks at the whole map: keep the square. */
        for (size_t i = 0; i < map_tile_count(map); i++) {
            int tx, ty, tz;
            map_tile_of(map, i, &tx, &ty, &tz);
            if (abs(tx - x) > radius || abs(ty - y) > radius) {
                fast[i >> 6] &= ~((uint64_t)1 << (i & 63));
                slow[i >> 6] &= ~((uint64_t)1 << (i & 63));
            }
        }
    }
    for (size_t w = 0; w < vision->words; w++) {
        *wrong_hidden += (size_t)__builtin_popcountll(slow[w] & ~fast[w]);
        *wrong_seen += (size_t)__builtin_popcountll(fast[w] & ~slow[w]);
        *seen += (size_t)__builtin_popcountll(slow[w]);
    }
    free(fast);
    free(slow);
}

/* On generated worlds (hills, cliffs, water, trees with trunks and foliage) the
 * rays and the brute-force line of sight agree on all but a few tiles at the
 * edges of shadows: closer to the eye (rays twice as dense) than in the whole
 * map. Returns the number of tiles the brute force sees. */
static size_t compare_worlds(int radius, size_t *wrong_hidden, size_t *wrong_seen)
{
    static const uint64_t SEEDS[] = {3, 11, 29};
    size_t seen = 0;
    for (size_t s = 0; s < sizeof(SEEDS) / sizeof(SEEDS[0]); s++) {
        WorldGenParams params;
        worldgen_default_params(&params);
        params.seed = SEEDS[s];
        params.width = 48;
        params.height = 48;
        Map map;
        if (!worldgen_generate(&map, &params)) {
            return 0;
        }
        Vision vision;
        if (!vision_init(&vision, &map)) {
            map_free(&map);
            return 0;
        }
        for (int k = 0; k < 6; k++) {
            int x = 6 + k * 7, y = 40 - k * 6;
            int z = map_ground_level(&map, x, y);
            if (!map_is_passable(&map, x, y, z)) {
                continue;
            }
            compare_with_brute_force(&map, &vision, x, y, z, radius, wrong_hidden, wrong_seen,
                                     &seen);
        }
        vision_free(&vision);
        map_free(&map);
    }
    return seen;
}

static void test_matches_brute_force(void)
{
    size_t hidden = 0, extra = 0;
    size_t seen = compare_worlds(VISION_NEAR_RADIUS, &hidden, &extra);
    REQUIRE(seen > 1000);
    if (hidden * 100 > seen * 3 || extra * 100 > seen * 7) {
        fprintf(stderr, "near: seen %zu, hidden by mistake %zu, seen by mistake %zu\n", seen,
                hidden, extra);
    }
    CHECK(hidden * 100 <= seen * 3);
    CHECK(extra * 100 <= seen * 7);

    hidden = extra = 0;
    seen = compare_worlds(0, &hidden, &extra);
    REQUIRE(seen > 1000);
    if (hidden * 100 > seen * 8 || extra * 100 > seen * 6) {
        fprintf(stderr, "whole: seen %zu, hidden by mistake %zu, seen by mistake %zu\n", seen,
                hidden, extra);
    }
    CHECK(hidden * 100 <= seen * 8);
    CHECK(extra * 100 <= seen * 6);
}

int main(void)
{
    RUN_TEST(test_open_plain);
    RUN_TEST(test_wall_casts_a_shadow);
    RUN_TEST(test_structures);
    RUN_TEST(test_own_tile_does_not_block);
    RUN_TEST(test_floors_block_between_levels);
    RUN_TEST(test_open_space_is_seen_through);
    RUN_TEST(test_a_tower_sees_further);
    RUN_TEST(test_cliff);
    RUN_TEST(test_thin_foliage_is_seen_through);
    RUN_TEST(test_peeking_round_a_corner);
    RUN_TEST(test_asleep);
    RUN_TEST(test_radius);
    RUN_TEST(test_viewers);
    RUN_TEST(test_counts_match_fields);
    RUN_TEST(test_limited_viewer);
    RUN_TEST(test_map_changes);
    RUN_TEST(test_game_tracks_settlers);
    RUN_TEST(test_matches_brute_force);
    return test_summary();
}
