/* Trees in the volume: trunks of wood, floors of foliage, climbing them, how
 * they grow, and how felled trees fall (core/tree.h). */
#include "core/tree.h"

#include "core/designation.h"
#include "core/game.h"
#include "core/gather_zone.h"
#include "core/pathfinding.h"
#include "core/worldgen.h"

#include "test_framework.h"

#include <string.h>

#define SIZE 20
#define DEPTH 9

/* Growths of a tree at each stage of interest. */
#define SAPLING 0.3f     /* Stage 1: a plant like the others. */
#define YOUNG 0.45f      /* Trunk 1 level. */
#define HALF_GROWN 0.52f /* Trunk 2 levels, a few tiles of foliage. */
#define GROWN 1.0f       /* Trunk 3 levels, the whole crown. */
/* The commonest tree, the one most tests grow. */
#define OAK tree_def(PLANT_OAK)

/* Soil on level 0, open space above. */
static bool init_world(Map *map, Items *items)
{
    return map_init(map, SIZE, SIZE, DEPTH) && items_init(items, SIZE, SIZE, DEPTH);
}

static void put_tree(Map *map, int x, int y, int z, float growth)
{
    map->plants[map_index(map, x, y, z)] = (Plant){.type = PLANT_OAK, .growth = growth};
}

/* A tree written into the map. */
static void grow_tree(Map *map, int x, int y, int z, float growth, int size)
{
    map->plants[map_index(map, x, y, z)] =
        (Plant){.type = PLANT_OAK, .size = (uint8_t)size, .growth = growth};
    tree_build(map, x, y, z);
}

static bool is_trunk(const Map *map, int x, int y, int z)
{
    return map->rock[map_index(map, x, y, z)] == ROCK_WOOD;
}

static bool is_foliage(const Map *map, int x, int y, int z)
{
    return terrain_is_foliage(map->terrain[map_index(map, x, y, z)]) &&
           map->rock[map_index(map, x, y, z)] == ROCK_NONE;
}

static int count_on_line(const Items *items, int x, int y, int z, int dx, int dy, int from, int to,
                         ItemType type)
{
    int count = 0;
    for (int i = from; i <= to; i++) {
        count += items_get_of(items, x + dx * i, y + dy * i, z, ITEM_TYPE_BIT(type)).count;
    }
    return count;
}

static void test_shapes(void)
{
    Plant plant = {.type = PLANT_OAK, .growth = SAPLING};
    TreeShape shape;
    CHECK(!tree_shape(&plant, 0, 0, 0, &shape)); /* Saplings take no room. */
    plant.growth = 0.1f;
    CHECK(!tree_shape(&plant, 0, 0, 0, &shape));
    plant.growth = YOUNG;
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK_EQ_INT(shape.size, 1);
    CHECK_EQ_INT(shape.trunk, 1);
    CHECK_EQ_INT(shape.foliage_from, 1);
    CHECK_EQ_INT(tree_shape_levels(&shape), 2);
    plant.growth = HALF_GROWN;
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK_EQ_INT(shape.trunk, 2);
    plant.growth = GROWN;
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK_EQ_INT(shape.trunk, 3);
    CHECK_EQ_INT(shape.reach, OAK->reach);
    CHECK_EQ_INT(tree_size(&plant), 1);
    /* The trunk rises at its growths, and no sooner. */
    plant.growth = OAK->second_level_at - 0.01f;
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK_EQ_INT(shape.trunk, 1);
    plant.growth = OAK->third_level_at - 0.01f;
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK_EQ_INT(shape.trunk, 2);
    plant.growth = OAK->third_level_at;
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK_EQ_INT(shape.trunk, 3);

    plant.size = 2; /* Old: a bare trunk under its crown. */
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK_EQ_INT(shape.size, 2);
    CHECK_EQ_INT(shape.reach, 2);
    CHECK(shape.foliage_from > 1);
    CHECK_EQ_INT(tree_size(&plant), 2);
    plant.size = 3; /* Ancient: the tallest. */
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK_EQ_INT(shape.size, 3);
    CHECK_EQ_INT(tree_shape_levels(&shape), TREE_MAX_LEVELS);
    CHECK(shape.reach <= TREE_MAX_REACH);

    Plant garlic = {.type = PLANT_WILD_GARLIC, .growth = 1.0f};
    CHECK(!tree_shape(&garlic, 0, 0, 0, &shape)); /* Not a tree. */
    CHECK_EQ_INT(tree_size(&garlic), 0);

    /* Where trunk and foliage are, from the foot (fully grown). */
    plant = (Plant){.type = PLANT_OAK, .growth = GROWN};
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK(tree_trunk_holds(&shape, 0, 0, 0));
    CHECK(tree_trunk_holds(&shape, 0, 0, 2));
    CHECK(!tree_trunk_holds(&shape, 0, 0, 3));
    CHECK(!tree_trunk_holds(&shape, 1, 0, 0));
    CHECK(!tree_foliage_holds(&shape, 0, 0, 1)); /* The trunk is there. */
    CHECK(tree_foliage_holds(&shape, 0, 0, 3));  /* Over the top of the trunk. */
    CHECK(tree_foliage_holds(&shape, 1, 0, 1));
    CHECK(!tree_foliage_holds(&shape, OAK->reach + 1, 0, 1));
    CHECK(!tree_foliage_holds(&shape, 1, 0, 0)); /* Nothing on the ground. */
    CHECK(!tree_foliage_holds(&shape, 1, 0, 4));
    plant.size = 3; /* A wide crown is rounded. */
    REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
    CHECK(!tree_foliage_holds(&shape, -3, -3, shape.trunk));
    CHECK(!tree_foliage_holds(&shape, -3, -2, shape.trunk));
    CHECK(!tree_foliage_holds(&shape, 0, -1, shape.foliage_from - 1));
}

/* How far a coordinate is outside the trunk's span 0..size-1. */
static int outside(int d, int size)
{
    return d < 0 ? -d : (d >= size ? d - size + 1 : 0);
}

/* The crowns are ragged: the same position gives the same crown, two trees
 * differ, and a corner of a small one is there or not. Next to the trunk the
 * branches are dense; further out they thin, and only the dense ones are
 * walked on. */
static void test_crown_is_ragged_and_thins_out(void)
{
    Plant small = {.type = PLANT_OAK, .growth = GROWN};
    Plant ancient = {.type = PLANT_OAK, .size = 3, .growth = 1.0f};
    int corners = 0, trees = 40;
    int found[TREE_DENSITY_DENSE + 1] = {0};
    for (int i = 0; i < trees; i++) {
        TreeShape shape, again;
        REQUIRE(tree_shape(&small, 7 * i, 3 * i + 1, 0, &shape));
        REQUIRE(tree_shape(&small, 7 * i, 3 * i + 1, 0, &again));
        for (int dz = shape.foliage_from; dz <= shape.trunk; dz++) {
            /* The same tree always has the same crown. */
            CHECK_EQ_INT(tree_foliage_density(&shape, -1, -1, dz),
                         tree_foliage_density(&again, -1, -1, dz));
            /* Next to the trunk, always dense: the tree stays climbable. */
            CHECK_EQ_INT(tree_foliage_density(&shape, 1, 0, dz), TREE_DENSITY_DENSE);
            CHECK_EQ_INT(tree_foliage_density(&shape, 0, -1, dz), TREE_DENSITY_DENSE);
        }
        CHECK_EQ_INT(tree_foliage_density(&shape, 0, 0, shape.trunk), TREE_DENSITY_DENSE);
        corners += tree_foliage_holds(&shape, 2, 1, 2); /* An outer tile, there or not. */
        /* Next to the trunk, a grown tree has nothing but dense branches. */
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                TreeDensity density = tree_foliage_density(&shape, dx, dy, 2);
                CHECK(density == TREE_DENSITY_NONE || density == TREE_DENSITY_DENSE);
            }
        }
        /* A wide crown thins out from the trunk. */
        REQUIRE(tree_shape(&ancient, 5 * i, 11 * i, 0, &shape));
        for (int dy = -4; dy <= shape.size + 3; dy++) {
            for (int dx = -4; dx <= shape.size + 3; dx++) {
                TreeDensity density = tree_foliage_density(&shape, dx, dy, shape.trunk - 1);
                found[density]++;
                int ex = outside(dx, shape.size), ey = outside(dy, shape.size);
                int ring = ex > ey ? ex : ey;
                CHECK(ring <= 3 || density == TREE_DENSITY_NONE);
                CHECK(ring <= 1 || density != TREE_DENSITY_DENSE);
                CHECK(ring <= 2 || density != TREE_DENSITY_MEDIUM);
            }
        }
    }
    CHECK(corners > 0 && corners < trees); /* Sometimes there, sometimes not. */
    CHECK(found[TREE_DENSITY_DENSE] > 0);
    CHECK(found[TREE_DENSITY_MEDIUM] > 0);
    CHECK(found[TREE_DENSITY_SPARSE] > 0);
}

/* A tree that grows only adds foliage, and thickens what it had. */
static void test_growing_keeps_the_foliage(void)
{
    for (int i = 0; i < 30; i++) {
        int x = 3 * i, y = 5 * i + 2;
        Plant plant = {.type = PLANT_OAK, .growth = 0.5f};
        TreeShape before, after;
        REQUIRE(tree_shape(&plant, x, y, 0, &before));
        plant.growth = 1.0f;
        REQUIRE(tree_shape(&plant, x, y, 0, &after));
        for (int dz = before.foliage_from; dz <= before.trunk; dz++) {
            for (int dy = -2; dy <= 2; dy++) {
                for (int dx = -2; dx <= 2; dx++) {
                    TreeDensity was = tree_foliage_density(&before, dx, dy, dz);
                    if (was != TREE_DENSITY_NONE && !tree_trunk_holds(&after, dx, dy, dz)) {
                        CHECK(tree_foliage_density(&after, dx, dy, dz) >= was);
                    }
                }
            }
        }
    }
}

/* Branches too thin to stand on: nobody walks there and what falls goes
 * through, down to the first dense foliage or ground. */
static void test_thin_foliage_is_not_walked_on(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 8, 8, 0, GROWN, 3);
    TreeShape shape;
    REQUIRE(tree_shape(&map.plants[map_index(&map, 8, 8, 0)], 8, 8, 0, &shape));
    int thin = 0, dense = 0;
    for (int dz = shape.foliage_from; dz <= shape.trunk; dz++) {
        for (int dy = -shape.reach; dy < shape.size + shape.reach; dy++) {
            for (int dx = -shape.reach; dx < shape.size + shape.reach; dx++) {
                TreeDensity density = tree_foliage_density(&shape, dx, dy, dz);
                int x = 8 + dx, y = 8 + dy, z = dz;
                if (density == TREE_DENSITY_NONE) {
                    CHECK(!is_foliage(&map, x, y, z));
                    continue;
                }
                CHECK_EQ_INT(map.terrain[map_index(&map, x, y, z)], tree_density_terrain(density));
                CHECK_EQ_INT(map_is_passable(&map, x, y, z), density == TREE_DENSITY_DENSE);
                thin += density != TREE_DENSITY_DENSE;
                dense += density == TREE_DENSITY_DENSE;
            }
        }
    }
    CHECK(thin > 0 && dense > 0);
    /* What the tree sheds never stays on thin branches. */
    int laid = 0;
    for (uint32_t pick = 0; pick < 2000; pick++) {
        laid += tree_shed(&map, 8, 8, 0, ITEM_TWIGS, pick * 2654435761u);
    }
    CHECK(laid > 0);
    for (int z = 0; z < DEPTH; z++) {
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                if (is_foliage(&map, x, y, z) && !map_is_passable(&map, x, y, z)) {
                    CHECK_EQ_INT(map_litter(&map, x, y, z, ITEM_TWIGS), 0);
                }
            }
        }
    }
    items_free(&items);
    map_free(&map);
}

static bool init_flat_game(Game *game);

/* How many tiles of foliage of each density lie around (x, y), on every level. */
static void count_foliage(const Map *map, int x, int y, int counts[TREE_DENSITY_DENSE + 1])
{
    for (int d = 0; d <= TREE_DENSITY_DENSE; d++) {
        counts[d] = 0;
    }
    for (int z = 0; z < map->depth; z++) {
        for (int ty = y - 3; ty <= y + 3; ty++) {
            for (int tx = x - 3; tx <= x + 3; tx++) {
                if (map->rock[map_index(map, tx, ty, z)] == ROCK_NONE) {
                    counts[tree_terrain_density(
                        (Terrain)map->terrain[map_index(map, tx, ty, z)])]++;
                }
            }
        }
    }
}

/* The crown grows tile by tile, on every level: first the foliage over the
 * trunk, thin; tiles come in nearest first and thicken with age; none goes
 * and none thins. */
static void test_crown_grows_tile_by_tile(void)
{
    for (int i = 0; i < 20; i++) {
        int x = 2 * i + 5, y = 3 * i + 4;
        TreeShape shape;
        Plant plant = {.type = PLANT_OAK, .growth = TREE_CROWN_FROM + 0.01f};
        REQUIRE(tree_shape(&plant, x, y, 0, &shape));
        /* Just up: a single piece of trunk, foliage over it and next to nothing else. */
        CHECK_EQ_INT(shape.trunk, 1);
        CHECK_EQ_INT(tree_foliage_density(&shape, 0, 0, 1), TREE_DENSITY_SPARSE);
        int first = 0;
        for (int dy = -2; dy <= 2; dy++) {
            for (int dx = -2; dx <= 2; dx++) {
                first += tree_foliage_holds(&shape, dx, dy, 1);
            }
        }
        CHECK(first <= 3);
        TreeDensity last[5][5][4] = {{{TREE_DENSITY_NONE}}};
        int tiles_before = 0, dense_before = 0;
        for (int step = 1; step <= 60; step++) {
            plant.growth = TREE_CROWN_FROM + (1.0f - TREE_CROWN_FROM) * (float)step / 60.0f;
            REQUIRE(tree_shape(&plant, x, y, 0, &shape));
            CHECK_EQ_INT(shape.reach, OAK->reach);
            int tiles = 0, dense = 0;
            for (int dz = 1; dz <= shape.trunk; dz++) {
                for (int dy = -2; dy <= 2; dy++) {
                    for (int dx = -2; dx <= 2; dx++) {
                        TreeDensity density = tree_foliage_density(&shape, dx, dy, dz);
                        if (tree_trunk_holds(&shape, dx, dy, dz)) {
                            continue; /* The trunk rose into the foliage over it. */
                        }
                        CHECK(density >= last[dy + 2][dx + 2][dz]); /* Never less. */
                        last[dy + 2][dx + 2][dz] = density;
                        tiles += density != TREE_DENSITY_NONE;
                        dense += density == TREE_DENSITY_DENSE;
                    }
                }
            }
            CHECK(tiles >= tiles_before);
            CHECK(dense >= dense_before);
            tiles_before = tiles;
            dense_before = dense;
            /* Nothing is dense at once: a tile needs time to thicken. */
            if (plant.growth < TREE_CROWN_FROM + 2.0f * TREE_DENSIFY_GROWTH) {
                CHECK_EQ_INT(dense, 0);
            }
        }
        /* Fully grown: the nearest tiles dense, the outer ones thinner. */
        CHECK_EQ_INT(shape.trunk, 3);
        CHECK_EQ_INT(tree_foliage_density(&shape, 1, 0, 1), TREE_DENSITY_DENSE);
        CHECK_EQ_INT(tree_foliage_density(&shape, 0, -1, 3), TREE_DENSITY_DENSE);
        CHECK_EQ_INT(tree_foliage_density(&shape, 0, 0, 3), TREE_DENSITY_DENSE);
        for (int dz = 1; dz <= 3; dz++) {
            CHECK(tree_foliage_density(&shape, 2, 0, dz) <= TREE_DENSITY_MEDIUM);
        }
        CHECK(tiles_before > 3 * 8); /* A crown on each of the three levels. */
    }
}

/* In the game, the crown of a tree growing up spreads and thickens in the
 * map, hour after hour. */
static void test_crown_grows_in_the_game(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    Plant *plant = &map->plants[map_index(map, 16, 16, 0)];
    put_tree(map, 16, 16, 0, YOUNG);
    game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL);
    REQUIRE(is_trunk(map, 16, 16, 0));
    int young[TREE_DENSITY_DENSE + 1], older[TREE_DENSITY_DENSE + 1], full[TREE_DENSITY_DENSE + 1];
    count_foliage(map, 16, 16, young);
    CHECK_EQ_INT(young[TREE_DENSITY_DENSE], 0);
    plant->growth = 0.7f;
    game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL);
    CHECK(is_trunk(map, 16, 16, 2));
    count_foliage(map, 16, 16, older);
    CHECK(older[TREE_DENSITY_SPARSE] + older[TREE_DENSITY_MEDIUM] + older[TREE_DENSITY_DENSE] >
          young[TREE_DENSITY_SPARSE] + young[TREE_DENSITY_MEDIUM]);
    plant->growth = 0.9995f; /* Fully grown within the hour. */
    game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL);
    CHECK(plant->growth >= 1.0f);
    count_foliage(map, 16, 16, full);
    CHECK(full[TREE_DENSITY_DENSE] > older[TREE_DENSITY_DENSE]); /* It thickens... */
    CHECK(full[TREE_DENSITY_SPARSE] + full[TREE_DENSITY_MEDIUM] + full[TREE_DENSITY_DENSE] >=
          older[TREE_DENSITY_SPARSE] + older[TREE_DENSITY_MEDIUM] + older[TREE_DENSITY_DENSE]);
    game_shutdown(&game);
}

/* A tree that stands in the map goes on growing through every stage in the
 * game: the new level of its trunk rises where its own foliage was. */
static void test_trees_grow_through_the_stages(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    put_tree(map, 10, 10, 0, YOUNG);
    game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL); /* Takes room. */
    REQUIRE(is_trunk(map, 10, 10, 0));
    CHECK(!is_trunk(map, 10, 10, 1));
    Plant *plant = &map->plants[map_index(map, 10, 10, 0)];
    plant->growth = OAK->second_level_at - 0.0005f; /* Within the hour. */
    game_debug_skip(&game, 3 * GAME_PLANT_UPDATE_INTERVAL);
    CHECK(plant->growth > OAK->second_level_at);
    CHECK(is_trunk(map, 10, 10, 1));
    CHECK(!is_trunk(map, 10, 10, 2));
    plant->growth = OAK->third_level_at - 0.0005f;
    game_debug_skip(&game, 3 * GAME_PLANT_UPDATE_INTERVAL);
    CHECK(is_trunk(map, 10, 10, 2));
    game_shutdown(&game);
}

static void test_debug_plants_and_grows_a_tree(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    CHECK(game_debug_plant_tree(&game, 10, 10, 0));
    CHECK_EQ_INT(map->plants[map_index(map, 10, 10, 0)].type, PLANT_OAK);
    CHECK(map->plants[map_index(map, 10, 10, 0)].growth == 0.0f);
    CHECK(!game_debug_plant_tree(&game, 10, 10, 0)); /* Already one. */
    CHECK(!game_debug_plant_tree(&game, 11, 10, 0)); /* Trunks never touch. */
    CHECK(game_debug_plant_tree(&game, 13, 10, 0));
    map_set_terrain(map, 5, 5, 0, TERRAIN_SHALLOW_WATER);
    CHECK(!game_debug_plant_tree(&game, 5, 5, 0)); /* Too wet and poor. */
    CHECK(!game_debug_plant_tree(&game, -1, 5, 0));
    CHECK(!game_debug_grow_tree(&game, 3, 3, 0, false)); /* Not a tree. */

    /* A step is a tenth: the trunk rises at 0.4. */
    for (int click = 0; click < 3; click++) {
        CHECK(game_debug_grow_tree(&game, 10, 10, 0, false));
    }
    CHECK(!is_trunk(map, 10, 10, 0));
    CHECK(game_debug_grow_tree(&game, 10, 10, 0, false));
    CHECK(is_trunk(map, 10, 10, 0));
    CHECK(is_foliage(map, 10, 10, 1)); /* The first foliage, over the trunk. */
    /* From any tile of the tree, up to full growth: three levels of trunk and
     * a crown with thin foliage. */
    for (int click = 0; click < 6; click++) {
        CHECK(game_debug_grow_tree(&game, 10, 10, 1, false)); /* From its trunk. */
    }
    CHECK(map->plants[map_index(map, 10, 10, 0)].growth == 1.0f);
    CHECK(is_trunk(map, 10, 10, 2));
    int counts[TREE_DENSITY_DENSE + 1];
    count_foliage(map, 10, 10, counts);
    CHECK(counts[TREE_DENSITY_MEDIUM] + counts[TREE_DENSITY_SPARSE] > 0);
    CHECK(!game_debug_grow_tree(&game, 10, 10, 0, false)); /* Fully grown. */
    /* All at once. */
    CHECK(game_debug_grow_tree(&game, 13, 10, 0, true));
    CHECK(is_trunk(map, 13, 10, 2));
    CHECK(map->plants[map_index(map, 13, 10, 0)].growth == 1.0f);

    /* What lies where the trunk rises is pushed aside, as in play. */
    CHECK(game_debug_plant_tree(&game, 4, 4, 0));
    for (int click = 0; click < 3; click++) {
        CHECK(game_debug_grow_tree(&game, 4, 4, 0, false));
    }
    items_add(&game.items, 4, 4, 0, item_stack_pristine(ITEM_TWIGS, 1));
    CHECK(game_debug_grow_tree(&game, 4, 4, 0, false));
    CHECK(is_trunk(map, 4, 4, 0));
    CHECK_EQ_INT(items_count_at(&game.items, 4, 4, 0, ITEM_NONE), 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], 1);
    game_shutdown(&game);
}

/* Thin foliage coming and going leaves the connectivity alone (crowns spread
 * all the time); dense foliage, that is walked on, and thin foliage over a
 * slope, that would close its way up, do not. */
static void test_thin_foliage_leaves_the_topology_alone(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    uint32_t revision = map.topology_revision;
    map_set_terrain(&map, 5, 5, 2, TERRAIN_FOLIAGE_SPARSE);
    map_set_terrain(&map, 5, 5, 2, TERRAIN_FOLIAGE_MEDIUM);
    map_set_terrain(&map, 5, 5, 2, TERRAIN_OPEN);
    CHECK_EQ_UINT(map.topology_revision, revision);
    map_set_terrain(&map, 5, 5, 2, TERRAIN_FOLIAGE); /* Walked on. */
    CHECK(map.topology_revision != revision);
    map_set_terrain(&map, 5, 5, 2, TERRAIN_OPEN);
    map_set_slope(&map, 6, 5, 1, true);
    revision = map.topology_revision;
    map_set_terrain(&map, 6, 5, 2, TERRAIN_FOLIAGE_SPARSE); /* Over a slope. */
    CHECK(map.topology_revision != revision);
    items_free(&items);
    map_free(&map);
}

/* A young tree growing (its trunk rising into the thin foliage over it, new
 * thin foliage around) changes nothing for the connectivity: nobody can
 * stand on any of it. The first dense foliage does. */
static void test_young_tree_growth_leaves_the_topology_alone(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, YOUNG, 1);
    CHECK(is_foliage(&map, 10, 10, 1));
    uint32_t revision = map.topology_revision;
    grow_tree(&map, 10, 10, 0, HALF_GROWN, 1);
    CHECK(is_trunk(&map, 10, 10, 1));
    CHECK_EQ_UINT(map.topology_revision, revision);
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    CHECK(map.topology_revision != revision);
    items_free(&items);
    map_free(&map);
}

/* The eye goes through thin foliage to what lies below it, and stops at
 * dense foliage. */
static void test_thin_foliage_is_seen_through(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    map_set_terrain(&map, 5, 5, 3, TERRAIN_FOLIAGE_SPARSE);
    map_set_terrain(&map, 5, 5, 2, TERRAIN_FOLIAGE_MEDIUM);
    CHECK_EQ_INT(map_view_level(&map, 5, 5, 4), 0); /* The ground, through both. */
    CHECK_EQ_INT(map_view_level(&map, 5, 5, 3), 0);
    map_set_terrain(&map, 5, 5, 2, TERRAIN_FOLIAGE);
    CHECK_EQ_INT(map_view_level(&map, 5, 5, 4), 2); /* Dense: the floor seen. */
    CHECK_EQ_INT(map_view_level(&map, 5, 5, 2), 2);
    items_free(&items);
    map_free(&map);
}

/* A hazel is a small tree: a shrub as a seedling, then a trunk of at most two
 * levels with a crown like the other trees'. */
static void test_hazel_is_a_small_tree(void)
{
    CHECK(plant_is_tree(PLANT_OAK) && plant_is_tree(PLANT_HAZEL));
    CHECK(!plant_is_tree(PLANT_BRAMBLE) && !plant_is_tree(PLANT_GRASS));
    Plant hazel = {.type = PLANT_HAZEL, .growth = SAPLING};
    TreeShape shape;
    CHECK(!tree_shape(&hazel, 3, 4, 0, &shape)); /* A seedling takes no room. */
    hazel.growth = YOUNG;
    REQUIRE(tree_shape(&hazel, 3, 4, 0, &shape));
    CHECK_EQ_INT(shape.size, 1);
    CHECK_EQ_INT(shape.trunk, 1);
    hazel.growth = GROWN;
    REQUIRE(tree_shape(&hazel, 3, 4, 0, &shape));
    CHECK_EQ_INT(shape.trunk, 2); /* Never taller than that. */
    CHECK_EQ_INT(tree_size(&hazel), 1);
    hazel.size = 3; /* Only some species grow wide, not the hazel. */
    CHECK_EQ_INT(tree_size(&hazel), 1);
    /* Less wood than a tree, felled like one. */
    Plant oak = {.type = PLANT_OAK, .growth = 1.0f};
    CHECK(tree_wood_yield(&hazel) > 0 && tree_wood_yield(&hazel) < tree_wood_yield(&oak));
    CHECK(tree_fell_work(&hazel) > 0.0f);

    /* In the map: a trunk, foliage, and nothing built over it. */
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    map.plants[map_index(&map, 10, 10, 0)] = (Plant){.type = PLANT_HAZEL, .growth = GROWN};
    REQUIRE(tree_shape(&map.plants[map_index(&map, 10, 10, 0)], 10, 10, 0, &shape));
    REQUIRE(tree_room_to_grow(&map, 10, 10, 0, &shape));
    tree_build(&map, 10, 10, 0);
    CHECK(is_trunk(&map, 10, 10, 0));
    CHECK(is_trunk(&map, 10, 10, 1));
    CHECK(!is_trunk(&map, 10, 10, 2));
    CHECK(is_foliage(&map, 10, 10, 2)); /* Over the trunk. */
    TreeRef tree;
    REQUIRE(tree_at(&map, 10, 10, 1, &tree));
    CHECK_EQ_INT(tree_plant(&map, &tree)->type, PLANT_HAZEL);
    CHECK(map_plant_blocks_building(&map, 10, 10, 0));
    items_free(&items);
    map_free(&map);
}

/* Tiles of foliage of each density in a crown. */
static void crown_densities(const TreeShape *shape, int counts[TREE_DENSITY_DENSE + 1])
{
    for (int d = 0; d <= TREE_DENSITY_DENSE; d++) {
        counts[d] = 0;
    }
    for (int dz = 0; dz < TREE_MAX_LEVELS; dz++) {
        for (int dy = -TREE_MAX_REACH; dy <= TREE_MAX_REACH + TREE_MAX_SIZE; dy++) {
            for (int dx = -TREE_MAX_REACH; dx <= TREE_MAX_REACH + TREE_MAX_SIZE; dx++) {
                counts[tree_foliage_density(shape, dx, dy, dz)]++;
            }
        }
    }
}

/* Each species has its own crown (docs/TREES.md §3): the beech's is denser
 * than the oak's, the ash's and the birch's thin (nothing to walk on, so no
 * climbing), the birch and the wild pear narrow, the wild apple low; only
 * some grow old or ancient. */
static void test_species_crowns(void)
{
    for (int type = 0; type < PLANT_COUNT; type++) {
        CHECK((tree_def((PlantType)type) != NULL) == plant_is_tree((uint8_t)type));
    }
    int dense[PLANT_COUNT] = {0}, all[PLANT_COUNT] = {0};
    for (int type = PLANT_TREE_FIRST; type <= PLANT_TREE_LAST; type++) {
        Plant plant = {.type = (uint8_t)type, .growth = GROWN};
        TreeShape shape;
        REQUIRE(tree_shape(&plant, 7, 9, 0, &shape));
        CHECK_EQ_INT(shape.reach, tree_def((PlantType)type)->reach);
        int counts[TREE_DENSITY_DENSE + 1];
        crown_densities(&shape, counts);
        dense[type] = counts[TREE_DENSITY_DENSE];
        all[type] = counts[TREE_DENSITY_SPARSE] + counts[TREE_DENSITY_MEDIUM] + dense[type];
        CHECK(all[type] > 0);
    }
    CHECK(dense[PLANT_BEECH] > dense[PLANT_OAK]);
    CHECK(dense[PLANT_CHESTNUT] > dense[PLANT_OAK]);
    CHECK(dense[PLANT_OAK] > 0);
    CHECK_EQ_INT(dense[PLANT_ASH], 0);
    CHECK_EQ_INT(dense[PLANT_BIRCH], 0);
    CHECK(all[PLANT_BIRCH] < all[PLANT_OAK]);
    CHECK(all[PLANT_WILD_PEAR] < all[PLANT_OAK]);
    /* Heights: the wild apple and the hazel stay at two levels of trunk. */
    static const struct {
        PlantType type;
        int trunk;
    } HEIGHTS[] = {{PLANT_OAK, 3},   {PLANT_BEECH, 3},      {PLANT_CHESTNUT, 3},  {PLANT_ASH, 3},
                   {PLANT_BIRCH, 3}, {PLANT_WILD_APPLE, 2}, {PLANT_WILD_PEAR, 3}, {PLANT_HAZEL, 2}};
    for (size_t i = 0; i < sizeof(HEIGHTS) / sizeof(HEIGHTS[0]); i++) {
        Plant plant = {.type = (uint8_t)HEIGHTS[i].type, .growth = GROWN};
        TreeShape shape;
        REQUIRE(tree_shape(&plant, 0, 0, 0, &shape));
        CHECK_EQ_INT(shape.trunk, HEIGHTS[i].trunk);
    }
    /* Old and ancient: the oak, beech and chestnut can be both, the ash old
     * only, the others neither. */
    static const struct {
        PlantType type;
        int widest;
    } WIDTHS[] = {{PLANT_OAK, 3},   {PLANT_BEECH, 3},      {PLANT_CHESTNUT, 3}, {PLANT_ASH, 2},
                  {PLANT_BIRCH, 1}, {PLANT_WILD_APPLE, 1}, {PLANT_WILD_PEAR, 1}};
    for (size_t i = 0; i < sizeof(WIDTHS) / sizeof(WIDTHS[0]); i++) {
        Plant plant = {.type = (uint8_t)WIDTHS[i].type, .size = 3, .growth = 1.0f};
        CHECK_EQ_INT(tree_size(&plant), WIDTHS[i].widest);
    }
    /* Yields follow the species. */
    Plant oak = {.type = PLANT_OAK, .growth = 1.0f}, birch = {.type = PLANT_BIRCH, .growth = 1.0f};
    CHECK(tree_wood_yield(&birch) < tree_wood_yield(&oak));
    CHECK_EQ_INT(tree_crown_yield(&birch, ITEM_BRANCHES), tree_def(PLANT_BIRCH)->crown_branches);
    CHECK(tree_shed_chance(&birch, ITEM_TWIGS) > tree_shed_chance(&oak, ITEM_TWIGS));

    /* In the map: an oak's crown can be climbed, a birch's cannot. */
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 5, 5, 0, GROWN, 1);
    map.plants[map_index(&map, 14, 14, 0)] = (Plant){.type = PLANT_BIRCH, .growth = GROWN};
    tree_build(&map, 14, 14, 0);
    bool oak_climbable = false, birch_climbable = false;
    for (int d = 0; d < 4; d++) {
        static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
        oak_climbable |= map_can_climb_tree(&map, 5 + DX[d], 5 + DY[d], 0);
        birch_climbable |= map_can_climb_tree(&map, 14 + DX[d], 14 + DY[d], 0);
    }
    CHECK(oak_climbable);
    CHECK(!birch_climbable);
    items_free(&items);
    map_free(&map);
}

/* Every tile of a tree's trunk and foliage is marked with its species; a
 * tile two crowns share goes to the other tree when one is taken out. */
static void test_foliage_knows_its_species(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    map.plants[map_index(&map, 10, 10, 0)] = (Plant){.type = PLANT_BEECH, .growth = GROWN};
    tree_build(&map, 10, 10, 0);
    map.plants[map_index(&map, 13, 10, 0)] = (Plant){.type = PLANT_ASH, .growth = GROWN};
    tree_build(&map, 13, 10, 0);
    for (int z = 0; z < 3; z++) {
        CHECK_EQ_INT(map.tree_species[map_index(&map, 10, 10, z)], PLANT_BEECH);
        CHECK_EQ_INT(map.tree_species[map_index(&map, 13, 10, z)], PLANT_ASH);
    }
    REQUIRE(is_foliage(&map, 9, 10, 1));
    CHECK_EQ_INT(map.tree_species[map_index(&map, 9, 10, 1)], PLANT_BEECH);
    REQUIRE(is_foliage(&map, 14, 10, 1));
    CHECK_EQ_INT(map.tree_species[map_index(&map, 14, 10, 1)], PLANT_ASH);
    /* The crowns meet between the trunks: the denser beech's foliage. */
    REQUIRE(is_foliage(&map, 12, 10, 1));
    CHECK_EQ_INT(map.tree_species[map_index(&map, 12, 10, 1)], PLANT_BEECH);
    tree_clear(&map, &items, 10, 10, 0);
    CHECK(is_foliage(&map, 12, 10, 1)); /* The ash's too: it stays, the ash's now. */
    CHECK_EQ_INT(map.tree_species[map_index(&map, 12, 10, 1)], PLANT_ASH);
    CHECK(!is_foliage(&map, 9, 10, 1));
    CHECK_EQ_INT(map.tree_species[map_index(&map, 9, 10, 1)], PLANT_NONE);
    CHECK_EQ_INT(map.tree_species[map_index(&map, 10, 10, 1)], PLANT_NONE);
    items_free(&items);
    map_free(&map);
}

/* A hazel is felled like any tree (it once stood through the felling). */
static void test_hazel_is_felled(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    map.plants[map_index(&map, 10, 4, 0)] = (Plant){.type = PLANT_HAZEL, .growth = GROWN};
    tree_build(&map, 10, 4, 0);
    REQUIRE(is_trunk(&map, 10, 4, 1));
    TreeFall fall = tree_fell(&map, &items, 10, 4, 0, item_stack_pristine(ITEM_WOOD, 6));
    CHECK(fall.length > 0);
    CHECK_EQ_INT(map.plants[map_index(&map, 10, 4, 0)].type, PLANT_NONE);
    CHECK(!is_trunk(&map, 10, 4, 0) && !is_trunk(&map, 10, 4, 1));
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 6);
    CHECK_EQ_INT((int)items.totals[ITEM_BRANCHES], tree_def(PLANT_HAZEL)->crown_branches);
    items_free(&items);
    map_free(&map);
}

/* The nuts fall in autumn only, the more the more grown the tree is, and only
 * from trees that bear any. */
static void test_nuts_fall_in_autumn(void)
{
    ItemType item = ITEM_NONE;
    Plant hazel = {.type = PLANT_HAZEL, .growth = GROWN};
    int summer = SEASON_DAY(SEASON_SUMMER, 5), autumn = SEASON_DAY(SEASON_AUTUMN, 5);
    CHECK(tree_fall_chance(&hazel, summer, &item) == 0.0f);
    CHECK_EQ_INT(item, ITEM_NONE);
    float chance = tree_fall_chance(&hazel, autumn, &item);
    CHECK_EQ_INT(item, ITEM_HAZELNUTS);
    CHECK(chance > 0.0f && chance < 1.0f);
    /* The whole yield of the season, on average. */
    float days = (float)DAYS_PER_SEASON;
    CHECK_NEAR(chance * days * HOURS_PER_DAY, (float)plant_def(PLANT_HAZEL)->fall_yield, 0.5);
    hazel.growth = 0.5f;
    CHECK(tree_fall_chance(&hazel, autumn, &item) < chance);
    item = ITEM_NONE;
    Plant oak = {.type = PLANT_OAK, .growth = 1.0f};
    CHECK(tree_fall_chance(&oak, autumn, &item) == 0.0f);
    CHECK_EQ_INT(item, ITEM_NONE);
    CHECK(tree_fall_chance(&(Plant){.type = PLANT_BRAMBLE, .growth = 1.0f}, autumn, &item) == 0.0f);
}

static int litter_total(const Map *map, ItemType type);

/* Total nuts lying on the map. */
static int nuts_total(const Map *map)
{
    return litter_total(map, ITEM_HAZELNUTS);
}

/* In the game, a grown hazel lets its nuts fall in autumn, on the ground and
 * on its dense foliage, never in other seasons. */
static void test_hazel_drops_nuts_in_the_game(void)
{
    for (int autumn = 0; autumn < 2; autumn++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        Map *map = &game.map;
        game.clock.tick =
            (uint64_t)SEASON_DAY(autumn ? SEASON_AUTUMN : SEASON_SPRING, 2) * TICKS_PER_DAY;
        map->plants[map_index(map, 16, 16, 0)] = (Plant){.type = PLANT_HAZEL, .growth = 1.0f};
        game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL); /* Takes room. */
        REQUIRE(is_trunk(map, 16, 16, 0));
        game_debug_skip(&game, 150 * TICKS_PER_HOUR);
        int nuts = nuts_total(map);
        CHECK_EQ_INT(nuts, (int)game.items.totals[ITEM_HAZELNUTS]); /* Items, all of them. */
        CHECK_EQ_INT(map->nuts[map_index(map, 17, 16, 0)], 0);
        if (!autumn) {
            CHECK_EQ_INT(nuts, 0);
        } else {
            CHECK(nuts > 5); /* About 30 over the season: 12 or so in 150 hours. */
            for (int z = 0; z < DEPTH; z++) {
                for (int y = 0; y < SIZE; y++) {
                    for (int x = 0; x < SIZE; x++) {
                        int here = map_litter(map, x, y, z, ITEM_HAZELNUTS);
                        CHECK(here <= MAP_NUTS_MAX);
                        if (here > 0) {
                            CHECK(abs(x - 16) <= OAK->reach && abs(y - 16) <= OAK->reach);
                            CHECK(!is_trunk(map, x, y, z));
                            CHECK(z == 0 || map_is_passable(map, x, y, z)); /* Dense foliage. */
                        }
                    }
                }
            }
        }
        game_shutdown(&game);
    }
}

/* Chestnuts, wild apples and wild pears fall like hazelnuts, each in its
 * season: items under the tree, never more than MAP_NUTS_MAX on a tile. A
 * new world (no game) has none of them. */
static void test_fruit_falls_in_the_game(void)
{
    static const struct {
        PlantType tree;
        ItemType fruit;
        int out_of_season; /* A day it bears none. */
    } FRUIT[] = {{PLANT_CHESTNUT, ITEM_CHESTNUTS, SEASON_DAY(SEASON_SUMMER, 14)},
                 {PLANT_WILD_APPLE, ITEM_APPLES, SEASON_DAY(SEASON_SPRING, 5)},
                 {PLANT_WILD_PEAR, ITEM_PEARS, SEASON_DAY(SEASON_SUMMER, 14)}};
    int autumn = SEASON_DAY(SEASON_AUTUMN, 5);
    for (size_t i = 0; i < sizeof(FRUIT) / sizeof(FRUIT[0]); i++) {
        Plant plant = {.type = (uint8_t)FRUIT[i].tree, .growth = 1.0f};
        ItemType item = ITEM_NONE;
        CHECK(tree_fall_chance(&plant, FRUIT[i].out_of_season, &item) == 0.0f);
        CHECK(tree_fall_chance(&plant, autumn, &item) > 0.0f);
        CHECK_EQ_INT(item, FRUIT[i].fruit);

        Game game;
        REQUIRE(init_flat_game(&game));
        Map *map = &game.map;
        game.clock.tick = (uint64_t)autumn * TICKS_PER_DAY;
        map->plants[map_index(map, 16, 16, 0)] = plant;
        game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL); /* Takes room. */
        REQUIRE(is_trunk(map, 16, 16, 0));
        game_debug_skip(&game, 150 * TICKS_PER_HOUR);
        int fallen = litter_total(map, FRUIT[i].fruit);
        CHECK(fallen > 5); /* 30 to 40 over 10 to 14 days. */
        CHECK_EQ_INT(fallen, (int)game.items.totals[FRUIT[i].fruit]);
        for (int z = 0; z < DEPTH; z++) {
            for (int y = 0; y < SIZE; y++) {
                for (int x = 0; x < SIZE; x++) {
                    CHECK(map_litter(map, x, y, z, FRUIT[i].fruit) <= MAP_NUTS_MAX);
                }
            }
        }
        game_shutdown(&game);
    }
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    CHECK(!map_add_litter(&map, 3, 3, 0, ITEM_APPLES));
    CHECK_EQ_INT(map_litter(&map, 3, 3, 0, ITEM_APPLES), 0);
    items_free(&items);
    map_free(&map);
}

/* Fallen nuts are items: each rots on its own, as any food lying out. */
static void test_nuts_rot_one_by_one(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    for (int n = 0; n < MAP_NUTS_MAX; n++) {
        REQUIRE(map_add_litter(map, 5, 5, 0, ITEM_HAZELNUTS));
    }
    size_t tile = map_index(map, 5, 5, 0);
    int hit_points[MAP_NUTS_MAX];
    int count = 0;
    for (const Item *item = items_first(&game.items, tile); item != NULL;
         item = items_next(&game.items, item)) {
        REQUIRE(count < MAP_NUTS_MAX);
        hit_points[count++] = item->hit_points;
    }
    REQUIRE(count == MAP_NUTS_MAX);
    CHECK(game_item_rot_rate(&game, 5, 5, 0) > 0.0f);
    game_debug_skip(&game, 3 * 24 * TICKS_PER_HOUR); /* Three days. */
    int rotting = 0, kept = 0;
    for (const Item *item = items_first(&game.items, tile); item != NULL;
         item = items_next(&game.items, item)) {
        rotting += item->hit_points < hit_points[kept] || item->progress > 0;
        kept++;
    }
    CHECK_EQ_INT(kept, MAP_NUTS_MAX); /* They keep for months. */
    CHECK_EQ_INT(rotting, MAP_NUTS_MAX);
    game_shutdown(&game);
}

static void test_build_and_find(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    /* The trunk: wood on three levels, nothing under it over the foot. */
    for (int z = 0; z < 3; z++) {
        CHECK(is_trunk(&map, 10, 10, z));
    }
    CHECK(!is_trunk(&map, 10, 10, 3));
    CHECK_EQ_INT(map.terrain[map_index(&map, 10, 10, 1)], TERRAIN_OPEN);
    CHECK_EQ_INT(map.terrain[map_index(&map, 10, 10, 0)], TERRAIN_SOIL);
    CHECK(!map_is_passable(&map, 10, 10, 0));
    CHECK(!map_is_passable(&map, 10, 10, 2));
    /* Foliage around it on levels 1 to 3, over it on the top. */
    CHECK(is_foliage(&map, 11, 10, 1));
    CHECK(is_foliage(&map, 10, 9, 2));
    CHECK(is_foliage(&map, 10, 10, 3));
    CHECK(!is_foliage(&map, 10 + OAK->reach + 1, 10, 1));
    CHECK(!is_foliage(&map, 11, 10, 0));
    CHECK(!is_foliage(&map, 11, 10, 4));
    CHECK(map_is_passable(&map, 11, 10, 1)); /* Walked on. */
    CHECK(!map_is_buildable(&map, 11, 10, 1));

    /* Found from any of its tiles. */
    TreeRef tree;
    REQUIRE(tree_at(&map, 10, 10, 2, &tree));
    CHECK_EQ_INT(tree.x, 10);
    CHECK_EQ_INT(tree.y, 10);
    CHECK_EQ_INT(tree.z, 0);
    CHECK_EQ_INT(tree.shape.trunk, 3);
    REQUIRE(tree_at(&map, 9, 9, 1, &tree));
    CHECK_EQ_INT(tree.x, 10);
    REQUIRE(tree_at(&map, 10, 10, 3, &tree));
    CHECK_EQ_INT(tree.z, 0);
    CHECK(tree_plant(&map, &tree) == &map.plants[map_index(&map, 10, 10, 0)]);
    CHECK(!tree_at(&map, 10 + OAK->reach + 1, 10, 1, &tree));
    CHECK(!tree_at(&map, 11, 10, 0, &tree));
    CHECK(!tree_at(&map, -1, 10, 1, &tree));

    /* Foliage is no ceiling and no roof; the ground stays the ground; the
     * trunk is not mined. */
    CHECK(!map_has_ceiling(&map, 11, 10, 0));
    CHECK(map_ceiling_supported(&map, 11, 10, 0));
    CHECK_EQ_INT(map_cover_level(&map, 11, 10, 0), -1);
    CHECK(!map_is_roofed(&map, 11, 10, 1));
    CHECK_EQ_INT(map_ground_level(&map, 11, 10), 0);
    CHECK_EQ_INT(map_ground_level(&map, 10, 10), 0);
    CHECK(!designation_can_apply(&map, 10, 10, 1, DESIGNATION_MINE));
    CHECK(map_plant_blocks_building(&map, 10, 10, 0));

    /* An old tree: a 2 x 2 trunk, found from any tile of it. */
    grow_tree(&map, 3, 3, 0, GROWN, 2);
    CHECK(is_trunk(&map, 4, 4, 4));
    CHECK(is_trunk(&map, 3, 4, 0));
    CHECK(!is_trunk(&map, 5, 3, 0));
    REQUIRE(tree_at(&map, 4, 4, 2, &tree));
    CHECK_EQ_INT(tree.x, 3);
    CHECK_EQ_INT(tree.y, 3);
    CHECK_EQ_INT(tree.shape.size, 2);
    CHECK(!is_foliage(&map, 5, 3, 1)); /* A bare trunk under its crown. */
    REQUIRE(tree_at(&map, 6, 4, tree.shape.foliage_from, &tree));
    CHECK_EQ_INT(tree.x, 3);
    /* It sheds more branches and twigs (by its area), and gives more when felled. */
    Plant *old = &map.plants[map_index(&map, 3, 3, 0)];
    Plant *grown = &map.plants[map_index(&map, 10, 10, 0)];
    CHECK(tree_shed_chance(old, ITEM_TWIGS) == 4.0f * tree_shed_chance(grown, ITEM_TWIGS));
    Plant full = *grown;
    full.growth = 1.0f;
    CHECK_EQ_INT(tree_crown_yield(old, ITEM_BRANCHES), 4 * tree_crown_yield(&full, ITEM_BRANCHES));

    /* Foliage leaves the way up a slope open. */
    map_set_slope(&map, 15, 16, 0, true);
    map_set_terrain(&map, 15, 17, 1, TERRAIN_SOIL);
    grow_tree(&map, 16, 16, 0, GROWN, 1);
    CHECK(is_foliage(&map, 17, 16, 1));
    CHECK(map_is_open(&map, 15, 16, 1));
    CHECK(map_can_climb(&map, 15, 16, 0, 15, 17));
    items_free(&items);
    map_free(&map);
}

static void test_climbing(void)
{
    Map map;
    Items items;
    Pathfinder finder;
    REQUIRE(init_world(&map, &items));
    REQUIRE(pathfinder_init(&finder, SIZE, SIZE, DEPTH));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    /* Up from beside the trunk, straight to the foliage over it. */
    CHECK(map_beside_trunk(&map, 11, 10, 0));
    CHECK(!map_beside_trunk(&map, 11, 11, 0)); /* Diagonal: not beside. */
    CHECK(map_can_climb_tree(&map, 11, 10, 0));
    CHECK(!map_can_climb_tree(&map, 11, 11, 0));
    CHECK(!map_can_climb_tree(&map, 12, 10, 0));
    CHECK(map_can_climb_tree(&map, 11, 10, 1)); /* From one floor to the next. */
    CHECK(map_can_climb_tree(&map, 11, 10, 2));
    CHECK(!map_can_climb_tree(&map, 11, 10, 3)); /* The top. */
    TilePos links[MAP_MAX_LEVEL_LINKS];
    int count = map_level_links(&map, 11, 10, 1, links);
    bool up = false, down = false;
    for (int i = 0; i < count; i++) {
        up |= links[i].x == 11 && links[i].y == 10 && links[i].z == 2;
        down |= links[i].x == 11 && links[i].y == 10 && links[i].z == 0;
    }
    CHECK(up);
    CHECK(down);
    /* A path to the top of the tree, climbing at MAP_TREE_CLIMB_COST. */
    Path path = {0};
    TilePos start = {16, 10, 0}, top = {10, 10, 3};
    REQUIRE(pathfinder_find(&finder, &map, start, top, PATH_NO_COST_LIMIT, &path));
    CHECK_EQ_INT(path.steps[path.count - 1].z, 3);
    CHECK(finder.last_cost >= 3.0f * MAP_TREE_CLIMB_COST);
    for (int i = 1; i < path.count; i++) {
        if (path.steps[i].z != path.steps[i - 1].z) { /* Straight up, beside the trunk. */
            CHECK_EQ_INT(path.steps[i].x, path.steps[i - 1].x);
            CHECK_EQ_INT(path.steps[i].y, path.steps[i - 1].y);
        }
    }
    /* Animals climb no tree: neither up to the foliage, nor down from it. */
    finder.walker = PATH_WALKER_ANIMAL;
    CHECK(!pathfinder_find(&finder, &map, start, top, PATH_NO_COST_LIMIT, &path));
    CHECK(!pathfinder_find(&finder, &map, top, start, PATH_NO_COST_LIMIT, &path));
    CHECK(path_walker_can_step(PATH_WALKER_SETTLER, &map, (TilePos){11, 10, 0},
                               (TilePos){11, 10, 1}));
    CHECK(!path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){11, 10, 0},
                                (TilePos){11, 10, 1}));
    CHECK(!path_walker_can_step(PATH_WALKER_ANIMAL, &map, (TilePos){11, 10, 1},
                                (TilePos){11, 10, 0}));
    path_free(&path);
    pathfinder_free(&finder);
    items_free(&items);
    map_free(&map);
}

static void test_room_to_grow(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    TreeShape grown, half;
    Plant plant = {.type = PLANT_OAK, .growth = GROWN};
    REQUIRE(tree_shape(&plant, 0, 0, 0, &grown));
    plant.growth = HALF_GROWN;
    REQUIRE(tree_shape(&plant, 0, 0, 0, &half));
    CHECK(tree_room_to_grow(&map, 5, 5, 0, &grown));
    CHECK(!tree_room_to_grow(&map, 5, 5, DEPTH - 2, &grown)); /* Out of the map. */
    /* Under an overhang: no room for the trunk. */
    map_set_terrain(&map, 5, 5, 2, TERRAIN_SOIL);
    CHECK(tree_room_to_grow(&map, 5, 5, 0, &half));
    CHECK(!tree_room_to_grow(&map, 5, 5, 0, &grown));
    map_set_terrain(&map, 5, 5, 2, TERRAIN_OPEN);
    /* A tree grows into its own foliage. */
    grow_tree(&map, 10, 10, 0, HALF_GROWN, 1);
    CHECK(is_foliage(&map, 10, 10, 2));
    CHECK(tree_room_to_grow(&map, 10, 10, 0, &grown));
    CHECK(tree_grows_into(10, 10, 0, &grown, 10, 10, 2));
    CHECK(!tree_grows_into(10, 10, 0, &half, 10, 10, 2));
    map.plants[map_index(&map, 10, 10, 0)].growth = GROWN;
    tree_build(&map, 10, 10, 0);
    CHECK(is_trunk(&map, 10, 10, 2));
    CHECK(is_foliage(&map, 10, 10, 3));
    CHECK(is_foliage(&map, 11, 10, 1)); /* Its lower foliage stays. */
    /* Not into another tree's foliage. */
    put_tree(&map, 11, 11, 0, HALF_GROWN);
    CHECK(!tree_room_to_grow(&map, 11, 11, 0, &half));
    /* Nor through a structure. */
    map_set_structure(&map, 3, 3, 0, STRUCTURE_WALL);
    CHECK(!tree_room_to_grow(&map, 3, 3, 0, &half));
    items_free(&items);
    map_free(&map);
}

/* A small game on a flat soil map without trees nor colonists. */
static bool init_flat_game(Game *game)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.flat = true;
    params.world.seed = 7;
    params.world.width = 32;
    params.world.height = 32;
    params.colonist_count = 0;
    if (!game_init(game, &params)) {
        return false;
    }
    Map *map = &game->map;
    for (size_t i = 0; i < map_tile_count(map); i++) {
        bool ground = i < map_level_tile_count(map);
        map->terrain[i] = ground ? TERRAIN_SOIL : TERRAIN_OPEN;
        map->rock[i] = ROCK_NONE;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
        map_clear_litter_at(map, i, ITEM_TYPE_BIT(ITEM_BRANCHES) | ITEM_TYPE_BIT(ITEM_TWIGS));
    }
    map_topology_changed(map);
    return true;
}

/* In the game, a sapling that grows takes room at once: what lies there is
 * pushed next door. */
static void test_growing_in_the_game(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    float almost = 0.399f; /* An hour from stage 2. */
    put_tree(map, 5, 5, 0, almost);
    put_tree(map, 20, 20, 0, almost);
    items_add(&game.items, 20, 20, 0, item_stack_pristine(ITEM_TWIGS, 1));
    stockpile_create(&game.stockpiles, map, 4, 4, 6, 6, 0);
    game_debug_skip(&game, 2 * GAME_PLANT_UPDATE_INTERVAL);
    TreeShape shape;
    CHECK(tree_shape(&map->plants[map_index(map, 5, 5, 0)], 5, 5, 0, &shape));
    CHECK(is_trunk(map, 5, 5, 0));
    CHECK(is_foliage(map, 5, 5, 1));                           /* Over the trunk. */
    CHECK_EQ_INT(stockpile_at(&game.stockpiles, 5, 5, 0), -1); /* No stockpile on a trunk. */
    CHECK(stockpile_at(&game.stockpiles, 4, 4, 0) >= 0);
    /* The other pushed the twigs aside. */
    CHECK(is_trunk(map, 20, 20, 0));
    CHECK_EQ_INT(items_count_at(&game.items, 20, 20, 0, ITEM_NONE), 0);
    CHECK_EQ_INT((int)game.items.totals[ITEM_TWIGS], 1);
    CHECK_EQ_INT(items_check(&game.items).problems, 0);
    game_shutdown(&game);
}

static void test_clear_and_shared_foliage(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    grow_tree(&map, 12, 10, 0, GROWN, 1); /* Their crowns meet on x = 11. */
    items_add(&items, 9, 10, 2, item_stack_pristine(ITEM_TWIGS, 3));
    items_add(&items, 10, 10, 3, item_stack_pristine(ITEM_BRANCHES, 2));
    tree_clear(&map, &items, 10, 10, 0);
    for (int z = 0; z < 3; z++) {
        CHECK(!is_trunk(&map, 10, 10, z));
    }
    CHECK(map_is_passable(&map, 10, 10, 0));
    CHECK(map_is_open(&map, 9, 10, 2));
    CHECK(map_is_open(&map, 10, 10, 3));
    CHECK(is_foliage(&map, 11, 10, 1)); /* The other tree's too: it stays. */
    CHECK(is_foliage(&map, 11, 10, 3));
    CHECK(is_trunk(&map, 12, 10, 1));
    /* What lay up there fell to the ground. */
    CHECK_EQ_INT(items_get(&items, 9, 10, 2).count, 0);
    CHECK_EQ_INT((int)items.totals[ITEM_TWIGS], 3);
    CHECK_EQ_INT((int)items.totals[ITEM_BRANCHES], 2);
    CHECK_EQ_INT(items_get(&items, 9, 10, 0).type, ITEM_TWIGS);
    CHECK_EQ_INT(items_get(&items, 10, 10, 0).type, ITEM_BRANCHES);
    items_free(&items);
    map_free(&map);
}

static void test_aim(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    CHECK_EQ_INT(tree_fall_length(4), 4 * TREE_FALL_TILES_PER_LEVEL);
    CHECK_EQ_INT(tree_fall_length(0), 0);
    /* Nothing around: to the south, full length (4 levels). */
    TreeFall fall = tree_fall_aim(&map, 10, 10, 0);
    CHECK_EQ_INT(fall.direction, DIRECTION_SOUTH);
    CHECK_EQ_INT(fall.length, 8);
    CHECK_EQ_INT(fall.harmed, 0);
    /* A wall far down the line to the south: east instead. */
    map_set_structure(&map, 10, 17, 0, STRUCTURE_WALL);
    CHECK_EQ_INT(tree_fall_toward(&map, 10, 10, 0, DIRECTION_SOUTH).harmed, 1);
    CHECK_EQ_INT(tree_fall_aim(&map, 10, 10, 0).direction, DIRECTION_EAST);
    /* Rock close to the east: the trunk would lie short there; north is
     * clear and long. */
    map_set_rock(&map, 13, 10, 0, ROCK_GRANITE);
    CHECK_EQ_INT(tree_fall_toward(&map, 10, 10, 0, DIRECTION_EAST).length, 2);
    CHECK_EQ_INT(tree_fall_aim(&map, 10, 10, 0).direction, DIRECTION_NORTH);
    /* The edge of the map stops it too. */
    grow_tree(&map, 1, 1, 0, GROWN, 1);
    CHECK_EQ_INT(tree_fall_toward(&map, 1, 1, 0, DIRECTION_WEST).length, 1);
    /* A sapling does not fall. */
    put_tree(&map, 16, 3, 0, SAPLING);
    CHECK_EQ_INT(tree_fall_aim(&map, 16, 3, 0).length, 0);
    /* A wide trunk falls as a wide band: a wall beside its middle line is
     * hit. */
    Map wide;
    Items wide_items;
    REQUIRE(init_world(&wide, &wide_items));
    grow_tree(&wide, 4, 2, 0, GROWN, 2);
    map_set_structure(&wide, 5, 7, 0, STRUCTURE_WALL); /* Under its east half. */
    CHECK_EQ_INT(tree_fall_toward(&wide, 4, 2, 0, DIRECTION_SOUTH).harmed, 1);
    CHECK_EQ_INT(tree_fall_toward(&wide, 4, 2, 0, DIRECTION_EAST).harmed, 0);
    items_free(&wide_items);
    map_free(&wide);
    items_free(&items);
    map_free(&map);
}

static void test_fell(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 2, 0, GROWN, 1);
    Plant *plant = &map.plants[map_index(&map, 10, 2, 0)];
    int branches = tree_crown_yield(plant, ITEM_BRANCHES);
    int twigs = tree_crown_yield(plant, ITEM_TWIGS);
    CHECK_EQ_INT(branches, OAK->crown_branches);
    CHECK_EQ_INT(twigs, OAK->crown_twigs);
    ItemStack wood =
        item_stack_make(ITEM_WOOD, 24, ITEM_QUALITY_STANDARD, item_def(ITEM_WOOD)->max_hit_points);
    TreeFall fall = tree_fell(&map, &items, 10, 2, 0, wood);
    CHECK_EQ_INT(fall.direction, DIRECTION_SOUTH);
    CHECK_EQ_INT(fall.length, 8);
    CHECK_EQ_INT(map.plants[map_index(&map, 10, 2, 0)].type, PLANT_NONE);
    CHECK(!is_trunk(&map, 10, 2, 1));
    CHECK(map_is_open(&map, 11, 2, 1));
    /* The trunk on its foot and the first tiles, the crown at the far end. */
    CHECK_EQ_INT(count_on_line(&items, 10, 2, 0, 0, 1, 0, 6, ITEM_WOOD), 24);
    CHECK_EQ_INT(items_get_of(&items, 10, 2, 0, ITEM_TYPE_BIT(ITEM_WOOD)).count, 4);
    CHECK_EQ_INT(count_on_line(&items, 10, 2, 0, 0, 1, 7, 8, ITEM_BRANCHES), branches);
    /* The twigs with the branches, or beside them when the tiles are full. */
    int near_crown = 0;
    for (int y = 8; y <= 12; y++) {
        for (int x = 9; x <= 11; x++) {
            near_crown += items_get_of(&items, x, y, 0, ITEM_TYPE_BIT(ITEM_WET_TWIGS)).count;
        }
    }
    CHECK_EQ_INT(near_crown, twigs);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 24);
    /* Nothing more where there is no tree. */
    fall = tree_fell(&map, &items, 10, 2, 0, wood);
    CHECK_EQ_INT(fall.length, 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 24);
    /* A sapling is cut where it stands. */
    put_tree(&map, 3, 3, 0, SAPLING);
    fall = tree_fell(&map, &items, 3, 3, 0, item_stack_pristine(ITEM_WOOD, 2));
    CHECK_EQ_INT(fall.length, 0);
    CHECK_EQ_INT(map.plants[map_index(&map, 3, 3, 0)].type, PLANT_NONE);
    CHECK_EQ_INT(items_get_of(&items, 3, 3, 0, ITEM_TYPE_BIT(ITEM_WOOD)).count, 2);
    items_free(&items);
    map_free(&map);
}

/* The order to fell trees: on the tree's foot (any tile of its trunk points
 * there), carried out as tree_fell() says, its wood by the tree's size. */
static void test_chop_order(void)
{
    Map map;
    Items items;
    Designations designations;
    REQUIRE(init_world(&map, &items));
    REQUIRE(designations_init(&designations, SIZE, SIZE, DEPTH));
    grow_tree(&map, 4, 4, 0, GROWN, 1);
    grow_tree(&map, 12, 12, 0, GROWN, 2);
    put_tree(&map, 16, 4, 0, SAPLING);

    /* Nobody can fell yet (woodcutting and axes come later). */
    const DesignationDef *def = designation_def(DESIGNATION_CHOP);
    REQUIRE(def != NULL);
    CHECK_EQ_INT(def->work_type, WORK_WOODCUTTING);
    CHECK(!designation_doable_at(DESIGNATION_CHOP, 100.0));

    /* On a tree's foot or a sapling; the order's tile from any trunk tile. */
    CHECK(designation_can_apply(&map, 4, 4, 0, DESIGNATION_CHOP));
    CHECK(designation_can_apply(&map, 16, 4, 0, DESIGNATION_CHOP));
    CHECK(!designation_can_apply(&map, 5, 4, 0, DESIGNATION_CHOP));
    CHECK(!designation_can_apply(&map, 4, 4, 1, DESIGNATION_CHOP)); /* Trunk, not the foot. */
    CHECK(!designation_can_apply(&map, 4, 4, 0, DESIGNATION_MINE)); /* Wood is not mined. */
    int x = 13, y = 13, z = 2;
    designation_target(&map, &x, &y, &z);
    CHECK(x == 12 && y == 12 && z == 0);
    x = 5, y = 4, z = 2; /* Foliage: stays. */
    REQUIRE(is_foliage(&map, 5, 4, 2));
    designation_target(&map, &x, &y, &z);
    CHECK(x == 5 && y == 4 && z == 2);

    /* Marking the four trunk tiles of the wide tree marks its foot once. */
    CHECK_EQ_INT(designations_set_rect(&designations, &map, 12, 12, 13, 13, 1, DESIGNATION_CHOP),
                 1);
    CHECK_EQ_INT(designation_get(&designations, 12, 12, 0), DESIGNATION_CHOP);
    CHECK_EQ_INT(designation_get(&designations, 13, 13, 1), DESIGNATION_NONE);
    CHECK(designation_set(&designations, &map, 4, 4, 2, DESIGNATION_CHOP)); /* Top of trunk. */
    CHECK_EQ_INT(designation_get(&designations, 4, 4, 0), DESIGNATION_CHOP);
    CHECK(designation_set(&designations, &map, 16, 4, 0, DESIGNATION_CHOP));
    CHECK(!designation_set(&designations, &map, 8, 8, 0, DESIGNATION_CHOP)); /* No tree. */
    CHECK_EQ_INT(designations.count, 3);
    CHECK_EQ_INT(designations_count_for_work(&designations, WORK_TYPE_BIT(WORK_WOODCUTTING)), 3);

    /* Work and wood by the tree's size. */
    const Plant *small = &map.plants[map_index(&map, 4, 4, 0)];
    const Plant *wide = &map.plants[map_index(&map, 12, 12, 0)];
    float needed = designation_work_needed(&designations, &map, 4, 4, 0);
    CHECK_NEAR(needed, tree_fell_work(small), 1e-3);
    CHECK_NEAR(needed, TREE_FELL_BASE_WORK + TREE_FELL_GROWTH_WORK, 1e-3);
    CHECK_NEAR(designation_work_needed(&designations, &map, 12, 12, 0),
               TREE_FELL_BASE_WORK + 4.0f * TREE_FELL_GROWTH_WORK, 1e-3);
    int count;
    CHECK_EQ_INT(designation_yield(&designations, &map, 4, 4, 0, &count), ITEM_WOOD);
    CHECK_EQ_INT(count, tree_wood_yield(small));
    CHECK_EQ_INT(count, plant_def(PLANT_OAK)->cut_yield);
    CHECK_EQ_INT(designation_yield(&designations, &map, 12, 12, 0, &count), ITEM_WOOD);
    CHECK_EQ_INT(count, tree_wood_yield(wide));
    CHECK_EQ_INT(count, 4 * plant_def(PLANT_OAK)->cut_yield);

    /* Felled once the work is done: the tree falls with its wood. */
    CHECK(!designation_add_work(&designations, &map, &items, 4, 4, 0, needed * 0.5f, 75.0, NULL));
    CHECK(is_trunk(&map, 4, 4, 0));
    CHECK(designation_add_work(&designations, &map, &items, 4, 4, 0, needed * 0.5f, 75.0, NULL));
    CHECK_EQ_INT(map.plants[map_index(&map, 4, 4, 0)].type, PLANT_NONE);
    CHECK(!is_trunk(&map, 4, 4, 0) && !is_trunk(&map, 4, 4, 2));
    CHECK(!is_foliage(&map, 5, 4, 2));
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], plant_def(PLANT_OAK)->cut_yield);
    CHECK_EQ_INT(designation_get(&designations, 4, 4, 0), DESIGNATION_NONE);
    CHECK_EQ_INT(designations.harmful_tree_falls, 0u);

    /* Cancelled from a trunk tile too. */
    CHECK_EQ_INT(designations_set_rect(&designations, &map, 13, 13, 13, 13, 2, DESIGNATION_NONE),
                 1);
    CHECK_EQ_INT(designation_get(&designations, 12, 12, 0), DESIGNATION_NONE);

    /* A tree with no clear side falls onto a building: counted. */
    grow_tree(&map, 4, 4, 0, GROWN, 1);
    map_set_structure(&map, 4, 5, 0, STRUCTURE_WALL);
    map_set_structure(&map, 5, 4, 0, STRUCTURE_WALL);
    map_set_structure(&map, 4, 3, 0, STRUCTURE_WALL);
    map_set_structure(&map, 3, 4, 0, STRUCTURE_WALL);
    REQUIRE(designation_set(&designations, &map, 4, 4, 0, DESIGNATION_CHOP));
    CHECK(designation_add_work(&designations, &map, &items, 4, 4, 0, needed, 75.0, NULL));
    CHECK_EQ_INT(designations.harmful_tree_falls, 1u);
    designations_free(&designations);
    items_free(&items);
    map_free(&map);
}

/* A tree on the edge of a hill, aimed out over the drop: what lies over open
 * space falls to the ground below. Walls hit lose half their hit points. */
static void test_fell_over_drop_and_onto_walls(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    for (int y = 0; y < SIZE; y++) {
        for (int x = 0; x < 10; x++) { /* The hill: x < 10, ground on level 1. */
            map.rock[map_index(&map, x, y, 0)] = ROCK_EARTH;
            map.terrain[map_index(&map, x, y, 1)] = TERRAIN_SOIL;
        }
    }
    map_topology_changed(&map);
    grow_tree(&map, 8, 5, 1, GROWN, 1);
    map_set_structure(&map, 8, 7, 1, STRUCTURE_WALL); /* South, north and west: walls. */
    map_set_structure(&map, 8, 2, 1, STRUCTURE_WALL);
    map_set_structure(&map, 5, 5, 1, STRUCTURE_WALL);
    ItemStack wood =
        item_stack_make(ITEM_WOOD, 14, ITEM_QUALITY_STANDARD, item_def(ITEM_WOOD)->max_hit_points);
    TreeFall fall = tree_fell(&map, &items, 8, 5, 1, wood);
    CHECK_EQ_INT(fall.direction, DIRECTION_EAST);
    CHECK_EQ_INT(fall.harmed, 0);
    /* Its foot and the next tile on the hill, the rest down on level 0. */
    CHECK(items_get(&items, 8, 5, 1).count > 0);
    CHECK(items_get(&items, 9, 5, 1).count > 0);
    CHECK(items_get(&items, 10, 5, 0).count > 0);
    CHECK_EQ_INT(items_get(&items, 10, 5, 1).count, 0);
    CHECK_EQ_INT((int)items.totals[ITEM_WOOD], 14);

    /* Walls on every side now: it comes down on one, which loses half its
     * hit points (never all of them). */
    grow_tree(&map, 4, 10, 1, GROWN, 1);
    map_set_structure(&map, 4, 13, 1, STRUCTURE_WALL);
    map_set_structure(&map, 4, 7, 1, STRUCTURE_WALL);
    map_set_structure(&map, 1, 10, 1, STRUCTURE_WALL);
    map_set_structure(&map, 7, 10, 1, STRUCTURE_WALL);
    int full = map_structure_hit_points(&map, 4, 13, 1);
    fall = tree_fell(&map, &items, 4, 10, 1, (ItemStack){0});
    CHECK_EQ_INT(fall.direction, DIRECTION_SOUTH);
    CHECK_EQ_INT(fall.harmed, 1);
    CHECK_EQ_INT(map_structure_hit_points(&map, 4, 13, 1),
                 full - full * TREE_FALL_DAMAGE_PERCENT / 100);
    CHECK_EQ_INT(map_structure_hit_points(&map, 4, 7, 1), full);
    for (int i = 0; i < 2; i++) {
        grow_tree(&map, 4, 10, 1, GROWN, 1);
        tree_fell(&map, &items, 4, 10, 1, (ItemStack){0});
    }
    CHECK_EQ_INT(map_structure(&map, 4, 13, 1), STRUCTURE_WALL); /* Battered, still there. */
    CHECK(map_structure_hit_points(&map, 4, 13, 1) >= 1);
    items_free(&items);
    map_free(&map);
}

/* Branches and twigs lying on a tile (Map.branches, Map.twigs) of a map. */
static int litter_total(const Map *map, ItemType type)
{
    int total = 0;
    for (int z = 0; z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                total += map_litter(map, x, y, z, type);
            }
        }
    }
    return total;
}

/* Where a tree's litter lies: on the ground under the crown, never in the
 * foliage nor on a trunk, at most MAP_*_MAX of each on a tile. */
static void test_shedding(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    int on_foliage = 0, on_ground = 0;
    for (uint32_t pick = 0; pick < 4000; pick++) {
        tree_shed(&map, 10, 10, 0, ITEM_TWIGS, pick * 2654435761u);
    }
    for (int z = 0; z < DEPTH; z++) {
        for (int y = 0; y < SIZE; y++) {
            for (int x = 0; x < SIZE; x++) {
                int twigs = map_litter(&map, x, y, z, ITEM_TWIGS);
                CHECK(twigs <= MAP_TWIGS_MAX);
                if (twigs > 0) {
                    CHECK(!is_trunk(&map, x, y, z));
                    CHECK(abs(x - 10) <= OAK->reach &&
                          abs(y - 10) <= OAK->reach); /* Under the crown. */
                    on_foliage += is_foliage(&map, x, y, z);
                    on_ground += z == 0;
                }
            }
        }
    }
    CHECK_EQ_INT(on_foliage, 0);
    CHECK(on_ground > 0);
    /* Tiles fill up: the ground tiles around the trunk (24 under a crown of
     * two rings), and nothing else. */
    CHECK(litter_total(&map, ITEM_TWIGS) > 8 * MAP_TWIGS_MAX);
    CHECK(litter_total(&map, ITEM_TWIGS) <= 24 * MAP_TWIGS_MAX);
    CHECK_EQ_INT(litter_total(&map, ITEM_BRANCHES), 0);
    /* A full tile takes no more. */
    int before = litter_total(&map, ITEM_TWIGS);
    for (uint32_t pick = 0; pick < 200; pick++) {
        tree_shed(&map, 10, 10, 0, ITEM_TWIGS, pick);
    }
    CHECK(litter_total(&map, ITEM_TWIGS) >= before);
    /* Not a tree, nor a sapling: nothing falls. */
    CHECK(!tree_shed(&map, 1, 1, 0, ITEM_TWIGS, 0));
    put_tree(&map, 15, 15, 0, SAPLING);
    CHECK(!tree_shed(&map, 15, 15, 0, ITEM_TWIGS, 0));
    items_free(&items);
    map_free(&map);
}

/* Young trees shed twigs only, half as often; grown trees shed both. */
static void test_shedding_by_age(void)
{
    Plant plant = {.type = PLANT_OAK, .growth = SAPLING};
    CHECK(tree_shed_chance(&plant, ITEM_TWIGS) == 0.0f);
    CHECK(tree_shed_chance(&plant, ITEM_BRANCHES) == 0.0f);
    plant.growth = YOUNG;
    float young_twigs = tree_shed_chance(&plant, ITEM_TWIGS);
    CHECK(young_twigs > 0.0f);
    CHECK(tree_shed_chance(&plant, ITEM_BRANCHES) == 0.0f);
    plant.growth = HALF_GROWN;
    CHECK(tree_shed_chance(&plant, ITEM_BRANCHES) == 0.0f);
    plant.growth = GROWN;
    CHECK(tree_shed_chance(&plant, ITEM_TWIGS) == 2.0f * young_twigs);
    CHECK(tree_shed_chance(&plant, ITEM_BRANCHES) > 0.0f);
    plant.type = PLANT_WILD_GARLIC;
    CHECK(tree_shed_chance(&plant, ITEM_TWIGS) == 0.0f);
    CHECK_EQ_INT(tree_crown_yield(&plant, ITEM_TWIGS), 0);
}

/* A new world has twigs and branches under its grown trees and twigs under
 * the young ones, and the same ones from the same seed. */
static void test_litter_in_a_new_world(void)
{
    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = 42;
    params.width = params.height = 64;
    Map map;
    REQUIRE(worldgen_generate(&map, &params));
    int grown = 0, young = 0, twigs_under_grown = 0, branches_under_young = 0;
    for (int z = 0; z <= map.top_ground_level; z++) {
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                const Plant *plant = &map.plants[map_index(&map, x, y, z)];
                TreeShape shape;
                if (!tree_shape(plant, x, y, z, &shape) || !is_trunk(&map, x, y, z)) {
                    continue;
                }
                bool is_grown = shape.size > 1 || shape.trunk == 3;
                grown += is_grown;
                young += !is_grown;
                for (int dy = -shape.reach; dy < shape.size + shape.reach; dy++) {
                    for (int dx = -shape.reach; dx < shape.size + shape.reach; dx++) {
                        int tx = x + dx, ty = y + dy;
                        if (!map_in_bounds(&map, tx, ty, z)) {
                            continue;
                        }
                        bool twigs = map_litter(&map, tx, ty, z, ITEM_TWIGS) > 0;
                        bool branches = map_litter(&map, tx, ty, z, ITEM_BRANCHES) > 0;
                        twigs_under_grown += is_grown && twigs;
                        branches_under_young += !is_grown && branches;
                    }
                }
            }
        }
    }
    REQUIRE(grown > 0);
    CHECK(young > 0);
    CHECK(twigs_under_grown > 0);
    CHECK(litter_total(&map, ITEM_BRANCHES) > 0);
    /* Branches near a young tree only come from a grown neighbour's crown. */
    CHECK(branches_under_young < litter_total(&map, ITEM_BRANCHES));
    uint64_t hash = map_hash(&map);
    map_free(&map);
    REQUIRE(worldgen_generate(&map, &params));
    CHECK(map_hash(&map) == hash);
    map_free(&map);
}

/* The litter of a felled tree's foliage falls to the ground (what does not
 * fit lies as items). */
static void test_felling_drops_the_litter_of_the_foliage(void)
{
    Map map;
    Items items;
    REQUIRE(init_world(&map, &items));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    CHECK(map_add_litter(&map, 11, 10, 2, ITEM_TWIGS)); /* On the foliage. */
    CHECK(map_add_litter(&map, 11, 10, 2, ITEM_BRANCHES));
    for (int i = 0; i < MAP_TWIGS_MAX; i++) {
        map_add_litter(&map, 11, 10, 0, ITEM_TWIGS); /* The ground below is full. */
    }
    tree_clear(&map, &items, 10, 10, 0);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 2, ITEM_TWIGS), 0);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 2, ITEM_BRANCHES), 0);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 0, ITEM_BRANCHES), 1);
    CHECK_EQ_INT(map_litter(&map, 11, 10, 0, ITEM_TWIGS), MAP_TWIGS_MAX);
    CHECK_EQ_INT((int)items.totals[ITEM_TWIGS], 1);
    items_free(&items);
    map_free(&map);
}

/* A zone on the ground covers the ground, not the foliage above it; a zone
 * may be drawn over the foliage. Felling works from the foot. */
static void test_gathering_on_trees(void)
{
    Map map;
    Items items;
    GatherZones zones;
    REQUIRE(init_world(&map, &items));
    REQUIRE(gather_zones_init(&zones, SIZE, SIZE, DEPTH));
    grow_tree(&map, 10, 10, 0, GROWN, 1);
    grow_tree(&map, 3, 3, 0, GROWN, 1);
    for (int d = 0; d < DIRECTION_COUNT; d++) { /* Walls all round the second tree. */
        map_set_structure(&map, 3 + direction_dx((Direction)d) * 2,
                          3 + direction_dy((Direction)d) * 2, 0, STRUCTURE_WALL);
    }
    map_add_litter(&map, 11, 10, 1, ITEM_HAZELNUTS); /* On the foliage. */
    CHECK(gather_zone_can_cover(&map, 10, 10, 0));   /* The foot of the trunk. */
    CHECK(!gather_zone_can_cover(&map, 10, 10, 1));
    CHECK(gather_zone_can_cover(&map, 11, 10, 1)); /* Foliage. */
    /* A zone on the ground: none over the foliage above it. */
    int zone = gather_zone_create(&zones, &map, 0, 0, SIZE - 1, SIZE - 1, 0);
    CHECK_EQ_INT(gather_zone_at(&zones, 11, 10, 1), GATHER_ZONE_NONE);
    /* A zone over the foliage itself. */
    int upper = gather_zone_create(&zones, &map, 0, 0, SIZE - 1, SIZE - 1, 1);
    REQUIRE(upper != GATHER_ZONE_NONE);
    CHECK_EQ_INT(gather_zone_at(&zones, 11, 10, 1), upper);

    gather_zone_set_enabled(&zones, zone, GATHER_WOOD, true);
    gather_zone_set_mode(&zones, zone, GATHER_WOOD, GATHER_FOREVER);
    gather_zone_set_ripe_only(&zones, zone, GATHER_WOOD, false);
    gather_zones_update(&zones, items.totals, 1.0f);
    CHECK(gather_job_available(&zones, &map, 10, 10, 0, GATHER_JOB_CHOP));
    CHECK(!gather_job_available(&zones, &map, 11, 10, 1, GATHER_JOB_CHOP));
    CHECK(gather_zone_add_work(&zones, &map, &items, 10, 10, 0, GATHER_JOB_CHOP, 1e6f, 60.0, NULL));
    CHECK_EQ_INT(map.plants[map_index(&map, 10, 10, 0)].type, PLANT_NONE);
    CHECK(!is_trunk(&map, 10, 10, 0));
    CHECK_EQ_INT(items_get(&items, 10, 12, 0).type, ITEM_WOOD); /* Along the trunk. */
    /* The crown came down with its twigs, green and so wet, and what lay on it. */
    CHECK_EQ_INT(map_litter(&map, 11, 10, 0, ITEM_HAZELNUTS), 1);
    CHECK_EQ_INT((int)items.totals[ITEM_WET_TWIGS], OAK->crown_twigs);
    CHECK_EQ_UINT(zones.harmful_tree_falls, 0);
    CHECK(gather_zone_add_work(&zones, &map, &items, 3, 3, 0, GATHER_JOB_CHOP, 1e6f, 60.0, NULL));
    CHECK_EQ_UINT(zones.harmful_tree_falls, 1);
    gather_zones_free(&zones);
    items_free(&items);
    map_free(&map);
}

/* Nuts lying on dense foliage are items like any: a hauler climbs the tree
 * to bring them to a stockpile that takes them, and leaves them up there
 * otherwise. */
static void test_hauler_climbs_for_nuts_on_the_foliage(void)
{
    for (int wanted = 0; wanted < 2; wanted++) {
        Game game;
        REQUIRE(init_flat_game(&game));
        Map *map = &game.map;
        put_tree(map, 16, 16, 0, GROWN);
        game_debug_skip(&game, GAME_PLANT_UPDATE_INTERVAL); /* Takes room. */
        REQUIRE(is_trunk(map, 16, 16, 0));
        REQUIRE(map_is_passable(map, 17, 16, 1)); /* Dense foliage. */
        REQUIRE(map_add_litter(map, 17, 16, 1, ITEM_HAZELNUTS));
        REQUIRE(map_add_litter(map, 17, 16, 1, ITEM_HAZELNUTS));
        map_clear_litter(map); /* No stones in the stockpile. */
        int pile = stockpile_create(&game.stockpiles, map, 8, 16, 8, 16, 0);
        REQUIRE(pile != STOCKPILE_NONE);
        stockpile_set_allowed(&game.stockpiles, pile,
                              wanted ? ITEM_TYPE_BIT(ITEM_HAZELNUTS) : ITEM_TYPE_BIT(ITEM_WOOD));
        EntityId id = game_spawn_colonist(&game, 10, 16, 0);
        Colonist *colonist = game_colonist(&game, id);
        REQUIRE(colonist != NULL);
        for (int need = 0; need < NEED_COUNT; need++) {
            colonist->needs[need] = 1.0f;
        }
        bool climbed = false;
        for (int tick = 0; tick < 2 * 3600; tick++) {
            game_tick(&game);
            climbed = climbed || colonist->z > 0;
        }
        CHECK_EQ_INT(climbed, wanted);
        CHECK_EQ_INT(map_litter(map, 17, 16, 1, ITEM_HAZELNUTS), wanted ? 0 : 2);
        CHECK_EQ_INT(map_litter(map, 8, 16, 0, ITEM_HAZELNUTS), wanted ? 2 : 0);
        game_shutdown(&game);
    }
}

/* In the running game, trees shed twigs and branches over the hours. */
static void test_trees_shed_in_the_game(void)
{
    Game game;
    REQUIRE(init_flat_game(&game));
    Map *map = &game.map;
    put_tree(map, 16, 16, 0, GROWN);
    game_debug_skip(&game, GAME_PLANT_UPDATE_INTERVAL); /* Takes room. */
    REQUIRE(is_trunk(map, 16, 16, 0));
    CHECK_EQ_INT(litter_total(map, ITEM_TWIGS), 0);
    game_debug_skip(&game, 10 * 24 * GAME_PLANT_UPDATE_INTERVAL);
    CHECK(litter_total(map, ITEM_TWIGS) > 0);
    CHECK(litter_total(map, ITEM_BRANCHES) > 0);
    game_shutdown(&game);
    /* A young tree: twigs only. */
    REQUIRE(init_flat_game(&game));
    map = &game.map;
    put_tree(map, 16, 16, 0, YOUNG);
    game_debug_skip(&game, GAME_PLANT_UPDATE_INTERVAL);
    REQUIRE(is_trunk(map, 16, 16, 0));
    game_debug_skip(&game, 6 * 24 * GAME_PLANT_UPDATE_INTERVAL); /* Still young. */
    CHECK(litter_total(map, ITEM_TWIGS) > 0);
    CHECK_EQ_INT(litter_total(map, ITEM_BRANCHES), 0);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_shapes);
    RUN_TEST(test_crown_is_ragged_and_thins_out);
    RUN_TEST(test_growing_keeps_the_foliage);
    RUN_TEST(test_thin_foliage_is_not_walked_on);
    RUN_TEST(test_crown_grows_tile_by_tile);
    RUN_TEST(test_crown_grows_in_the_game);
    RUN_TEST(test_thin_foliage_leaves_the_topology_alone);
    RUN_TEST(test_trees_grow_through_the_stages);
    RUN_TEST(test_debug_plants_and_grows_a_tree);
    RUN_TEST(test_young_tree_growth_leaves_the_topology_alone);
    RUN_TEST(test_thin_foliage_is_seen_through);
    RUN_TEST(test_hazel_is_a_small_tree);
    RUN_TEST(test_species_crowns);
    RUN_TEST(test_foliage_knows_its_species);
    RUN_TEST(test_hazel_is_felled);
    RUN_TEST(test_nuts_fall_in_autumn);
    RUN_TEST(test_hazel_drops_nuts_in_the_game);
    RUN_TEST(test_fruit_falls_in_the_game);
    RUN_TEST(test_nuts_rot_one_by_one);
    RUN_TEST(test_build_and_find);
    RUN_TEST(test_climbing);
    RUN_TEST(test_room_to_grow);
    RUN_TEST(test_growing_in_the_game);
    RUN_TEST(test_clear_and_shared_foliage);
    RUN_TEST(test_aim);
    RUN_TEST(test_fell);
    RUN_TEST(test_fell_over_drop_and_onto_walls);
    RUN_TEST(test_chop_order);
    RUN_TEST(test_shedding);
    RUN_TEST(test_shedding_by_age);
    RUN_TEST(test_litter_in_a_new_world);
    RUN_TEST(test_felling_drops_the_litter_of_the_foliage);
    RUN_TEST(test_gathering_on_trees);
    RUN_TEST(test_hauler_climbs_for_nuts_on_the_foliage);
    RUN_TEST(test_trees_shed_in_the_game);
    return test_summary();
}
