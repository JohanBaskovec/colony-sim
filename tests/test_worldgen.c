#include "core/worldgen.h"

#include "core/game_clock.h"
#include "core/shadow.h"
#include "core/sun.h"
#include "core/temperature.h"
#include "core/tree.h"

#include "test_framework.h"

#include <stdlib.h>

static WorldGenParams params_for_seed(uint64_t seed)
{
    WorldGenParams params;
    worldgen_default_params(&params);
    params.seed = seed;
    params.width = 128;
    params.height = 128;
    return params;
}

static void test_same_seed_gives_identical_map(void)
{
    WorldGenParams params = params_for_seed(12345);
    Map a, b;
    REQUIRE(worldgen_generate(&a, &params));
    REQUIRE(worldgen_generate(&b, &params));
    CHECK_EQ_UINT(map_hash(&a), map_hash(&b));
    map_free(&a);
    map_free(&b);
}

static void test_different_seeds_give_different_maps(void)
{
    WorldGenParams p1 = params_for_seed(1);
    WorldGenParams p2 = params_for_seed(2);
    Map a, b;
    REQUIRE(worldgen_generate(&a, &p1));
    REQUIRE(worldgen_generate(&b, &p2));
    CHECK(map_hash(&a) != map_hash(&b));
    map_free(&a);
    map_free(&b);
}

static void test_invalid_size_fails(void)
{
    WorldGenParams params = params_for_seed(1);
    params.width = 4;
    Map map;
    CHECK(!worldgen_generate(&map, &params));
}

static void test_tiles_are_valid_and_consistent(void)
{
    WorldGenParams params = params_for_seed(777);
    Map map;
    REQUIRE(worldgen_generate(&map, &params));

    for (size_t i = 0; i < map_tile_count(&map); i++) {
        CHECK(map.terrain[i] < TERRAIN_COUNT);
        CHECK(map.rock[i] < ROCK_COUNT);
        CHECK(map.plants[i].type < PLANT_COUNT);

        const Plant *plant = &map.plants[i];
        if (plant->type != PLANT_NONE) {
            float fertility = terrain_def((Terrain)map.terrain[i])->fertility;
            /* A tree's foot is under its trunk (core/tree.h). */
            CHECK(map.rock[i] == ROCK_NONE ||
                  (plant_is_tree(plant->type) && map.rock[i] == ROCK_WOOD));
            CHECK(fertility >= plant_def((PlantType)plant->type)->min_fertility);
            /* Withered plants (wild garlic at the end of summer) wait at 0. */
            CHECK(plant->growth > 0.0f || plant_is_dormant(plant));
            CHECK(plant->growth <= 1.0f);
        }
        /* A floor under the rock, should it be dug out. */
        if (map.rock[i] != ROCK_NONE && map.rock[i] != ROCK_WOOD) {
            CHECK_EQ_INT(map.terrain[i], rock_def((RockType)map.rock[i])->floor);
        }
    }
    map_free(&map);
}

/* The inside of the ground in layers (docs/WATER.md §5.1): right under a
 * floor, the ground's own soft layer (sand under sand, gravel under gravel,
 * earth under soil) or rock under bare rock; lenses of clay under that top
 * layer only; rock deeper down, but for beds of sand and gravel at one level
 * under about a third of the land; never soft ground under rock. */
static void test_ground_layers(void)
{
    long columns = 0, beds = 0, clay = 0, lens_columns = 0;
    for (uint64_t seed = 1; seed <= 3; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        int bed_level = map.surface_level - WORLDGEN_AQUIFER_DEPTH;
        REQUIRE(bed_level >= 0);
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                int ground = map_ground_level(&map, x, y);
                Terrain floor = (Terrain)map.terrain[map_index(&map, x, y, ground)];
                bool soil = floor == TERRAIN_SOIL || floor == TERRAIN_RICH_SOIL;
                bool rock_above = false;
                for (int z = ground - 1; z >= 0; z--) {
                    RockType rock = (RockType)map.rock[map_index(&map, x, y, z)];
                    int depth = ground - z;
                    REQUIRE(rock != ROCK_NONE && rock != ROCK_WOOD);
                    bool bed =
                        z == bed_level && depth > 1 && (rock == ROCK_SAND || rock == ROCK_GRAVEL);
                    if (depth == 1) {
                        if (floor == TERRAIN_SAND) {
                            CHECK_EQ_INT(rock, ROCK_SAND);
                        } else if (floor == TERRAIN_GRAVEL) {
                            CHECK_EQ_INT(rock, ROCK_GRAVEL);
                        } else if (floor == TERRAIN_ROUGH_STONE) {
                            CHECK(!rock_is_soft(rock));
                        } else if (soil) {
                            CHECK_EQ_INT(rock, ROCK_EARTH);
                        }
                    }
                    if (rock == ROCK_CLAY) {
                        CHECK(depth == 2 && !bed);
                        clay++;
                    }
                    if (depth > WORLDGEN_EARTH_LEVELS && !bed) {
                        CHECK(!rock_is_soft(rock)); /* Rock deeper down. */
                    }
                    /* Soft ground lies on rock, not under it, but for the beds. */
                    CHECK(!rock_above || !rock_is_soft(rock) || bed);
                    rock_above = rock_above || !rock_is_soft(rock);
                }
                if (ground > bed_level + 1) {
                    RockType at = (RockType)map.rock[map_index(&map, x, y, bed_level)];
                    columns++;
                    beds += at == ROCK_SAND || at == ROCK_GRAVEL;
                }
                /* Where a lens may lie: under soft ground but sand. */
                lens_columns += floor != TERRAIN_ROUGH_STONE && floor != TERRAIN_GRAVEL &&
                                floor != TERRAIN_SAND && ground >= 2;
            }
        }
        map_free(&map);
    }
    REQUIRE(columns > 0 && lens_columns > 0);
    CHECK(beds * 5 > columns && beds * 2 < columns);            /* About a third of the land. */
    CHECK(clay * 20 > lens_columns && clay * 4 < lens_columns); /* About a fifth. */
}

/* Stones: none on sand or water, 0 to MAP_STONES_MAX on stony ground, with
 * every count from 0 to the most turning up, and flint on some tiles only. */
static void test_stones(void)
{
    long tiles = 0, flint_tiles = 0, cobble_total = 0;
    long seen[MAP_STONES_MAX + 1] = {0};
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                int z = map_ground_level(&map, x, y);
                int cobbles = map_stones(&map, x, y, z, ITEM_COBBLE);
                int flint = map_stones(&map, x, y, z, ITEM_FLINT);
                Terrain type = (Terrain)map.terrain[map_index(&map, x, y, z)];
                const TerrainDef *terrain = terrain_def(type);
                if (!terrain->stony) {
                    CHECK_EQ_INT(cobbles + flint, 0);
                    continue;
                }
                CHECK(cobbles + flint <= MAP_STONES_MAX);
                seen[cobbles + flint]++;
                tiles++;
                flint_tiles += flint > 0;
                cobble_total += cobbles;
            }
        }
        map_free(&map);
    }
    for (int count = 0; count <= 2; count++) {
        CHECK(seen[count] > 0);
    }
    /* Few cobbles: about one stony tile in five has one (every one is an item). */
    CHECK(cobble_total * 10 > tiles);
    CHECK(cobble_total * 3 < tiles);
    CHECK(flint_tiles > 0);
    CHECK(flint_tiles < tiles / 4);
    CHECK(!terrain_def(TERRAIN_SAND)->stony);
    CHECK(terrain_def(TERRAIN_GRAVEL)->stony);
}

/* Sanity check of the overall composition over several seeds. */
static void test_maps_are_mostly_usable_land(void)
{
    for (uint64_t seed = 1; seed <= 8; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));

        size_t passable = 0;
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                passable += map_is_passable(&map, x, y, map_ground_level(&map, x, y));
            }
        }
        double ratio = (double)passable / (double)map_level_tile_count(&map);
        if (ratio < 0.5) {
            fprintf(stderr, "seed %llu: only %.0f%% passable\n", (unsigned long long)seed,
                    ratio * 100.0);
        }
        CHECK(ratio >= 0.5);
        CHECK(map_count_plants(&map, PLANT_OAK) > 0);
        map_free(&map);
    }
}

/* Every column: rock up to its ground, a floor on the ground, open space
 * above. Plains on the surface level, lakes and rivers in hollows below it
 * (shallows and beaches one level down, deep water two), solid ground under
 * them, hills above; a few levels in all. */
static void test_relief(void)
{
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        const int surface = map.surface_level;
        CHECK_EQ_INT(surface, WORLDGEN_UNDERGROUND_LEVELS);
        /* Room over the plains for the hills, the trees on them and roofs. */
        CHECK_EQ_INT(map.depth, surface + 1 + WORLDGEN_LEVELS_ABOVE);
        CHECK_EQ_INT(map.top_ground_level, surface + WORLDGEN_HILL_HEIGHT);
        size_t levels[MAP_MAX_LEVELS] = {0};
        size_t water = 0;
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                int ground = map_ground_level(&map, x, y);
                levels[ground]++;
                CHECK(ground >= surface - 2); /* Solid deeper down. */
                /* Open air over it, for the tallest tree (and a roof). */
                CHECK(ground <= map.top_ground_level);
                for (int z = 0; z < map.depth; z++) {
                    size_t i = map_index(&map, x, y, z);
                    if (z < ground) {
                        CHECK(map.rock[i] != ROCK_NONE);
                        if (ground - z > WORLDGEN_EARTH_LEVELS) { /* Rock deep down. */
                            CHECK(map.rock[i] != ROCK_EARTH);
                        }
                    } else if (z == ground) {
                        CHECK(map_has_floor(&map, x, y, z));
                        CHECK(map.rock[i] == ROCK_NONE || map.rock[i] == ROCK_WOOD); /* A trunk. */
                    } else {
                        TreeRef tree; /* Open air, but for the trees. */
                        CHECK(map_is_open(&map, x, y, z) || tree_at(&map, x, y, z, &tree));
                        CHECK(map.plants[i].type == PLANT_NONE);
                    }
                }
                Terrain terrain = (Terrain)map.terrain[map_index(&map, x, y, ground)];
                WaterClass depth = map_water_class(&map, x, y, ground);
                if (depth == WATER_DEEP) {
                    CHECK_EQ_INT(ground, surface - 2);
                    water++;
                } else if (depth != WATER_DRY) {
                    CHECK_EQ_INT(ground, surface - 1);
                    water++;
                }
                /* Rough stone on top of rock outcrops, rock inside them. */
                if (ground > surface && terrain == TERRAIN_ROUGH_STONE) {
                    RockType inside = (RockType)map.rock[map_index(&map, x, y, ground - 1)];
                    CHECK(inside == ROCK_GRANITE || inside == ROCK_SANDSTONE);
                }
            }
        }
        CHECK(water > 0);
        /* Lowlands: plains and the water in their hollows. */
        CHECK(levels[surface] + levels[surface - 1] + levels[surface - 2] >
              map_level_tile_count(&map) / 4);
        CHECK(levels[surface] > map_level_tile_count(&map) / 8);
        CHECK(levels[surface + 1] > 0 && levels[surface + 2] > 0);
        map_free(&map);
    }
}

/* The tallest ground of a map stands as many levels over the plains as asked,
 * whatever the seed, and the map has room over it for the tallest tree. */
static void test_hill_height(void)
{
    for (uint64_t seed = 1; seed <= 6; seed++) {
        for (int height = 2; height <= WORLDGEN_MAX_HILL_HEIGHT; height += 6) {
            WorldGenParams params = params_for_seed(seed);
            params.hill_height = height;
            Map map;
            REQUIRE(worldgen_generate(&map, &params));
            CHECK_EQ_INT(map.top_ground_level, map.surface_level + height);
            CHECK(map.depth >= map.top_ground_level + TREE_MAX_LEVELS - 2);
            map_free(&map);
        }
    }
    /* No more underground than the map's levels leave. */
    WorldGenParams deep = params_for_seed(1);
    deep.underground_levels = 40;
    Map map;
    REQUIRE(worldgen_generate(&map, &deep));
    CHECK_EQ_INT(map.surface_level, WORLDGEN_MAX_UNDERGROUND);
    CHECK(map.depth <= MAP_MAX_LEVELS);
    map_free(&map);
}

/* Lakes are water at rest over a bed of silt (docs/WATER.md W1): one surface
 * for all of them, WORLDGEN_LAKE_MM over the floor of the shallows; deep water
 * fills the level of its bed; beaches stay dry on the plains; nothing but
 * shore plants stands in the water, and no water lies anywhere else. */
static void test_lakes_at_rest(void)
{
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        const int surface = map.surface_level;
        const long level = (long)(surface - 1) * WATER_CELL_FULL + WORLDGEN_LAKE_MM;
        int shallows = 0, deep = 0;
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                int ground = map_ground_level(&map, x, y);
                size_t floor = map_index(&map, x, y, ground);
                for (int z = 0; z < map.depth; z++) {
                    int water = map_water(&map, x, y, z);
                    if (water == 0) {
                        continue;
                    }
                    CHECK(z == ground || z == ground + 1); /* Over the bed only. */
                    if (z == map.depth - 1 || map_water(&map, x, y, z + 1) == 0) {
                        CHECK_EQ_INT((long)z * WATER_CELL_FULL + water, level); /* Its top. */
                    }
                }
                if (map.water[floor] == 0) {
                    CHECK(ground >= surface); /* Beaches and banks on the plains. */
                    continue;
                }
                CHECK_EQ_INT(map.terrain[floor], TERRAIN_SILT);
                PlantType plant = (PlantType)map.plants[floor].type;
                CHECK(plant == PLANT_NONE || plant == PLANT_CATTAIL || plant == PLANT_REED);
                CHECK(map_is_water(&map, x, y, ground));
                if (ground == surface - 2) {
                    CHECK_EQ_INT(map_water_class(&map, x, y, ground), WATER_DEEP);
                    CHECK(!map_is_passable(&map, x, y, ground));
                    CHECK(map_is_water(&map, x, y, ground + 1)); /* Seen from the banks. */
                    deep++;
                } else {
                    CHECK_EQ_INT(ground, surface - 1);
                    CHECK_EQ_INT(map_water_class(&map, x, y, ground), WATER_SHALLOWS);
                    CHECK(map_is_passable(&map, x, y, ground));
                    shallows++;
                }
            }
        }
        CHECK(shallows > 0);
        CHECK(deep > 0);
        map_free(&map);
    }
}

/* Slopes lead up onto soft hills and out of the shallows; rock outcrops and
 * deep water stand behind cliffs. */
/* A river crosses the map in its channel, at the lakes' level and at rest,
 * deep in its middle when wide, in a valley whose floor stays on the plains;
 * the flat world of the tests has none. */
static void test_a_river_crosses_the_map(void)
{
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params = params_for_seed(seed);
        params.river.width = seed % 2 == 0 ? RIVER_MAX_WIDTH : 6;
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        RiverParams course;
        river_resolve(&params.river, seed, &course);
        size_t columns = (size_t)map.width * (size_t)map.height;
        float *distance = malloc(columns * sizeof(*distance));
        REQUIRE(distance != NULL);
        REQUIRE(river_distances(&course, seed, map.width, map.height, distance));
        const int surface = map.surface_level;
        const long level = (long)(surface - 1) * WATER_CELL_FULL + WORLDGEN_LAKE_MM;
        float half = (float)course.width * 0.5f;
        size_t channel = 0, deep = 0, wrong = 0, valley_high = 0;
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                float d = distance[(size_t)y * (size_t)map.width + (size_t)x];
                int ground = map_ground_level(&map, x, y);
                size_t floor = map_index(&map, x, y, ground);
                if (d < half) {
                    channel++;
                    deep += ground == surface - 2;
                    int top = ground;
                    while (top + 1 < map.depth && map_water(&map, x, y, top + 1) > 0) {
                        top++;
                    }
                    long surface_mm = (long)top * WATER_CELL_FULL + map_water(&map, x, y, top);
                    wrong += map.terrain[floor] != TERRAIN_SILT || ground >= surface ||
                             surface_mm != level;
                } else if (d < half + (float)RIVER_VALLEY_TILES) {
                    valley_high += ground > surface; /* The valley's floor. */
                }
            }
        }
        CHECK(channel > (size_t)(course.width * map.width) / 3);
        CHECK_EQ_UINT(wrong, 0);
        CHECK_EQ_UINT(valley_high, 0);
        if (course.width == RIVER_MAX_WIDTH) {
            CHECK(deep > channel / 2);
        }
        free(distance);
        map_free(&map);
    }
    /* None in a flat world. */
    WorldGenParams flat = params_for_seed(3);
    flat.flat = true;
    Map without, with;
    REQUIRE(worldgen_generate(&without, &flat));
    flat.river.width = 6;
    REQUIRE(worldgen_generate(&with, &flat));
    CHECK_EQ_UINT(map_hash(&with), map_hash(&without));
    map_free(&without);
    map_free(&with);
}

static void test_slopes(void)
{
    static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
    WorldGenParams params = params_for_seed(3);
    Map map;
    REQUIRE(worldgen_generate(&map, &params));
    CHECK(map.slope_count > 0);
    int counted = 0, wading = 0;
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        if (map.slope[i] == 0) {
            continue;
        }
        counted++;
        int x, y, z;
        map_tile_of(&map, i, &x, &y, &z);
        CHECK_EQ_INT(z, map_ground_level(&map, x, y));
        bool leads_up = false;
        for (int d = 0; d < 4; d++) {
            leads_up |= map_can_climb(&map, x, y, z, x + DX[d], y + DY[d]);
        }
        CHECK(leads_up);
        CHECK(map_water_class(&map, x, y, z) != WATER_DEEP);
        wading += map_is_water(&map, x, y, z);
    }
    CHECK_EQ_INT(counted, map.slope_count);
    CHECK(wading > 0); /* Shallows slope up their banks. */
    for (int y = 0; y < map.height; y++) {
        for (int x = 0; x < map.width; x++) {
            int z = map_ground_level(&map, x, y);
            if (z <= map.surface_level ||
                map.terrain[map_index(&map, x, y, z)] != TERRAIN_ROUGH_STONE) {
                continue;
            }
            for (int d = 0; d < 4; d++) { /* Nothing climbs onto an outcrop. */
                CHECK(!map_can_climb(&map, x + DX[d], y + DY[d], z - 1, x, y));
            }
        }
    }
    map_free(&map);
}

/* Flat worlds: one level of ground, rock outcrops as rock walls on it, open
 * air over it (room for trees and roofs). */
static void test_flat(void)
{
    WorldGenParams params = params_for_seed(3);
    params.flat = true;
    Map map;
    REQUIRE(worldgen_generate(&map, &params));
    CHECK_EQ_INT(map.depth, TREE_MAX_LEVELS);
    CHECK_EQ_INT(map.surface_level, 0);
    CHECK_EQ_INT(map.top_ground_level, 0);
    TreeRef tree;
    for (int z = 1; z < map.depth; z++) { /* Open air, but for the trees. */
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                CHECK(map_is_open(&map, x, y, z) || tree_at(&map, x, y, z, &tree));
            }
        }
    }
    CHECK_EQ_INT(map.slope_count, 0);
    size_t rock = 0;
    for (size_t i = 0; i < map_tile_count(&map); i++) {
        rock += map.rock[i] != ROCK_NONE && map.rock[i] != ROCK_WOOD;
        CHECK(map.rock[i] == ROCK_NONE || map.rock[i] == ROCK_WOOD ||
              map.terrain[i] == TERRAIN_ROUGH_STONE);
    }
    CHECK(rock > 0);
    map_free(&map);
}

/* Trees stand apart and are written into the map: every tree past its
 * sapling stages has its trunk, every tile of trunk or foliage belongs to a
 * tree; old and ancient trees turn up, rarely. */
static void test_trees(void)
{
    long trees = 0, wide = 0;
    long species[PLANT_COUNT] = {0}, on_sand[PLANT_COUNT] = {0}, on_gravel[PLANT_COUNT] = {0};
    for (uint64_t seed = 1; seed <= 4; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        for (int z = 0; z < map.depth; z++) {
            for (int y = 0; y < map.height; y++) {
                for (int x = 0; x < map.width; x++) {
                    size_t i = map_index(&map, x, y, z);
                    TreeRef tree;
                    if (map.rock[i] == ROCK_WOOD ||
                        (map.rock[i] == ROCK_NONE && terrain_is_foliage(map.terrain[i]))) {
                        CHECK(tree_at(&map, x, y, z, &tree));
                    }
                    const Plant *plant = &map.plants[i];
                    if (!plant_is_tree(plant->type) || plant->type == PLANT_HAZEL) {
                        continue;
                    }
                    trees++;
                    wide += plant->size > 1;
                    species[plant->type]++;
                    on_sand[plant->type] += map.terrain[i] == TERRAIN_SAND;
                    on_gravel[plant->type] += map.terrain[i] == TERRAIN_GRAVEL;
                    /* Only some species grow old. */
                    CHECK(plant->size <= 1 || tree_def((PlantType)plant->type)->can_be_old);
                    CHECK(plant->size <= 2 || tree_def((PlantType)plant->type)->can_be_ancient);
                    TreeShape shape;
                    CHECK_EQ_INT(map.rock[i],
                                 tree_shape(plant, x, y, z, &shape) ? ROCK_WOOD : ROCK_NONE);
                    for (int dy = -1; dy <= 1; dy++) {
                        for (int dx = -1; dx <= 1; dx++) {
                            bool other =
                                (dx != 0 || dy != 0) && map_in_bounds(&map, x + dx, y + dy, z) &&
                                plant_is_tree(map.plants[map_index(&map, x + dx, y + dy, z)].type);
                            CHECK(!other);
                        }
                    }
                }
            }
        }
        map_free(&map);
    }
    CHECK(trees > 0);
    CHECK(wide > 0);
    CHECK(wide < trees / 20);
    /* Every species grows somewhere (docs/TREES.md §4), the oak most, the
     * wild fruit trees least; only the birch grows on sand, neither the
     * beech nor the ash on gravel. */
    for (int type = PLANT_TREE_FIRST; type < PLANT_TREE_LAST; type++) {
        CHECK(species[type] > 0);
        CHECK(species[type] <= species[PLANT_OAK]);
        CHECK(species[type] >= species[PLANT_WILD_PEAR] || type == PLANT_WILD_APPLE);
        CHECK(on_sand[type] == 0 || type == PLANT_BIRCH);
    }
    CHECK(on_sand[PLANT_BIRCH] > 0);
    CHECK_EQ_INT(on_gravel[PLANT_BEECH] + on_gravel[PLANT_ASH], 0);
}

/* The understory follows the light under the crowns (docs/PLANTS.md §10): nothing is
 * generated where it is too dark for it to live, moss and wood sorrel stand
 * in deeper shade than grass, every new plant shows up, and reeds stand in
 * the water or along it. */
static void test_the_understory_follows_the_shade(void)
{
    long counts[PLANT_COUNT] = {0};
    double cover[PLANT_COUNT] = {0};
    long reeds_away_from_water = 0;
    float share = shadow_direct_share(
        sun_position(SUN_LATITUDE_DEFAULT, GAME_START_DAY, 12.0f).elevation, 1.0f);
    for (uint64_t seed = 1; seed <= 3; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        TileLight light;
        REQUIRE(worldgen_generate_lit(&map, &params, &light));
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                int z = map_ground_level(&map, x, y);
                const Plant *plant = &map.plants[map_index(&map, x, y, z)];
                if (plant->type == PLANT_NONE || plant_is_tree(plant->type) ||
                    map.rock[map_index(&map, x, y, z)] != ROCK_NONE) {
                    continue;
                }
                /* The light of the tile at noon on the first day (what lasts
                 * all year: test_generated_plants_live_all_year()). */
                counts[plant->type]++;
                cover[plant->type] += (1.0f - share) * tile_light_open(&light, x, y, z) +
                                      share * tile_light_noon(&light, x, y, z);
                if (plant->type == PLANT_REED) {
                    bool by_water = false;
                    for (int d = 0; d < 9; d++) {
                        by_water = by_water || map_is_water(&map, x + d % 3 - 1, y + d / 3 - 1, z);
                    }
                    reeds_away_from_water += !by_water;
                }
            }
        }
        tile_light_free(&light);
        map_free(&map);
    }
    for (int type = PLANT_MOSS; type < PLANT_COUNT; type++) {
        CHECK(counts[type] > 0);
    }
    CHECK(counts[PLANT_GRASS] > 0 && counts[PLANT_CATTAIL] > 0);
    CHECK_EQ_INT(reeds_away_from_water, 0);
    double grass = cover[PLANT_GRASS] / (double)counts[PLANT_GRASS];
    CHECK(cover[PLANT_MOSS] / (double)counts[PLANT_MOSS] < grass);
    CHECK(cover[PLANT_WOOD_SORREL] / (double)counts[PLANT_WOOD_SORREL] < grass);
    CHECK(cover[PLANT_WILD_GARLIC] / (double)counts[PLANT_WILD_GARLIC] < grass);
    CHECK(cover[PLANT_HEATHER] / (double)counts[PLANT_HEATHER] > grass); /* In the full sun. */
    CHECK(cover[PLANT_DOG_ROSE] / (double)counts[PLANT_DOG_ROSE] > grass);
}

/* Plants of poor ground (heather, bilberries) live on sand, gravel and plain
 * soil, never on rich soil. */
static void test_poor_ground_plants_stay_on_poor_ground(void)
{
    long on_poor = 0, on_rich = 0;
    for (uint64_t seed = 1; seed <= 3; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                size_t i = map_index(&map, x, y, map_ground_level(&map, x, y));
                PlantType type = (PlantType)map.plants[i].type;
                if (type != PLANT_HEATHER && type != PLANT_BILBERRY) {
                    continue;
                }
                Terrain terrain = (Terrain)map.terrain[i];
                on_poor += terrain == TERRAIN_SAND || terrain == TERRAIN_GRAVEL;
                on_rich += terrain == TERRAIN_RICH_SOIL;
            }
        }
        map_free(&map);
    }
    CHECK(on_poor > 0);
    CHECK_EQ_INT(on_rich, 0);
}

/* Heather and bracken hate wet feet (docs/CLIMATE.md §6): none on the
 * banks, where the water table soaks the ground, while they still grow
 * inland; cattails and reeds stand by the water. */
static void test_no_heath_on_the_banks(void)
{
    long on_banks = 0, inland = 0, shore = 0;
    for (uint64_t seed = 1; seed <= 3; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map;
        REQUIRE(worldgen_generate(&map, &params));
        for (int y = 0; y < map.height; y++) {
            for (int x = 0; x < map.width; x++) {
                int z = map_ground_level(&map, x, y);
                PlantType type = (PlantType)map.plants[map_index(&map, x, y, z)].type;
                bool bank = false;
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dx = -1; dx <= 1; dx++) {
                        bank = bank || map_is_water(&map, x + dx, y + dy, z);
                    }
                }
                if (type == PLANT_HEATHER || type == PLANT_BRACKEN) {
                    on_banks += bank;
                    inland += !bank;
                }
                shore += bank && (type == PLANT_CATTAIL || type == PLANT_REED);
            }
        }
        map_free(&map);
    }
    CHECK_EQ_INT(on_banks, 0);
    CHECK(inland > 100);
    CHECK(shore > 100);
}

static float noon_sun(int day)
{
    return sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f).elevation;
}

static float mean_of_day(int day)
{
    return temperature_season_mean((uint64_t)day * TICKS_PER_DAY + TICKS_PER_DAY / 2);
}

/* A plant is judged by its light on the brightest of its last awake days
 * under the leaves, as many as it bears in the dark: in the autumn for grass;
 * at midsummer for moss, which bears two months; in the spring for wild
 * garlic, which rests from the heat on. Trees, and what needs no light, are
 * not judged. */
static void test_plant_dark_days(void)
{
    for (int type = PLANT_NONE + 1; type < PLANT_COUNT; type++) {
        int day = worldgen_plant_dark_day((PlantType)type);
        const PlantDef *def = plant_def((PlantType)type);
        if (plant_is_tree((uint8_t)type) || def->dark_days <= 0.0f) {
            CHECK_EQ_INT(day, -1);
            continue;
        }
        REQUIRE(day >= 0 && day < DAYS_PER_YEAR);
        CHECK(mean_of_day(day) >= def->t_min); /* Awake. */
    }
    /* Grass bears the dark for ten days: judged in the autumn, ten days before
     * the cold puts it to rest, its sun lower than now. */
    int grass = worldgen_plant_dark_day(PLANT_GRASS);
    CHECK(grass >= SEASON_DAY(SEASON_AUTUMN, 0) && grass < SEASON_DAY(SEASON_WINTER, 0));
    CHECK(mean_of_day(grass + 10) < plant_def(PLANT_GRASS)->t_min + 5.0f);
    CHECK(noon_sun(grass) < noon_sun(GAME_START_DAY) - 10.0f);
    /* Moss bears it for two months: judged on its brightest day in leaf. */
    CHECK(noon_sun(worldgen_plant_dark_day(PLANT_MOSS)) > noon_sun(grass) + 15.0f);
    /* Wild garlic is gone before the summer's heat. */
    int garlic = worldgen_plant_dark_day(PLANT_WILD_GARLIC);
    CHECK(garlic < GAME_START_DAY);
    CHECK_EQ_INT(worldgen_plant_dark_day(PLANT_NONE), -1);
    CHECK_EQ_INT(worldgen_plant_dark_day(PLANT_COUNT), -1);
}

/* Ground three levels up everywhere, a pit one tile wide down to level 0 at
 * (24, 24): grass at its bottom sees a sliver of sky and never the sun. */
static void build_pit(Map *map)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            bool pit = x == 24 && y == 24;
            for (int z = 0; z < map->depth; z++) {
                if (z < 3 && !(pit && z > 0)) {
                    map_set_terrain(map, x, y, z,
                                    z == 0 && pit ? TERRAIN_SOIL : TERRAIN_ROUGH_STONE);
                    if (!pit) {
                        map_set_rock(map, x, y, z, ROCK_GRANITE);
                    }
                } else {
                    map_set_terrain(map, x, y, z, z == 3 && !pit ? TERRAIN_SOIL : TERRAIN_OPEN);
                }
            }
        }
    }
    map->surface_level = 3;
    map->top_ground_level = 3;
}

/* Grass at the bottom of a deep pit would starve before the year is out;
 * the same grass in the open, or by the rim, lives, and so would moss at the
 * bottom (it needs next to no light). */
static void test_a_deep_pit_starves_its_plants(void)
{
    Map map;
    REQUIRE(map_init(&map, 48, 48, 8));
    build_pit(&map);
    map.plants[map_index(&map, 24, 24, 0)].type = PLANT_GRASS; /* The bottom. */
    map.plants[map_index(&map, 10, 10, 3)].type = PLANT_GRASS; /* The open. */
    map.plants[map_index(&map, 24, 25, 3)].type = PLANT_GRASS; /* The rim, south. */
    map.plants[map_index(&map, 30, 30, 3)].type = PLANT_HEATHER;
    TileLight light;
    REQUIRE(tile_light_init(&light, &map, GAME_START_TICK));
    CHECK(tile_light_open(&light, 24, 24, 0) < 0.1f);
    CHECK_EQ_UINT(worldgen_starving_plants(&map, &light), 1);
    /* Moss lives on the sliver of sky. */
    map.plants[map_index(&map, 24, 24, 0)].type = PLANT_MOSS;
    CHECK_EQ_UINT(worldgen_starving_plants(&map, &light), 0);
    tile_light_free(&light);
    map_free(&map);
}

/* Nothing the generator sets would starve in the dark before the year is
 * out: every plant gets light enough on its dark day; the light it hands
 * over is the map's, and the map the same as without it. */
static void test_generated_plants_live_all_year(void)
{
    for (uint64_t seed = 1; seed <= 3; seed++) {
        WorldGenParams params = params_for_seed(seed);
        Map map, plain;
        TileLight light;
        REQUIRE(worldgen_generate_lit(&map, &params, &light));
        REQUIRE(worldgen_generate(&plain, &params));
        CHECK(map_hash(&map) == map_hash(&plain));
        CHECK_EQ_INT(light.levels, map.top_ground_level + 1);
        int plants = 0;
        for (size_t i = 0; i < (size_t)light.count; i++) {
            uint8_t type = map.plants[i].type;
            plants += type != PLANT_NONE && !plant_is_tree(type);
        }
        CHECK(plants > 500);
        CHECK_EQ_UINT(worldgen_starving_plants(&map, &light), 0);
        tile_light_free(&light);
        map_free(&map);
        map_free(&plain);
    }
}

int main(void)
{
    RUN_TEST(test_same_seed_gives_identical_map);
    RUN_TEST(test_different_seeds_give_different_maps);
    RUN_TEST(test_invalid_size_fails);
    RUN_TEST(test_tiles_are_valid_and_consistent);
    RUN_TEST(test_maps_are_mostly_usable_land);
    RUN_TEST(test_ground_layers);
    RUN_TEST(test_stones);
    RUN_TEST(test_relief);
    RUN_TEST(test_hill_height);
    RUN_TEST(test_lakes_at_rest);
    RUN_TEST(test_a_river_crosses_the_map);
    RUN_TEST(test_slopes);
    RUN_TEST(test_flat);
    RUN_TEST(test_trees);
    RUN_TEST(test_the_understory_follows_the_shade);
    RUN_TEST(test_plant_dark_days);
    RUN_TEST(test_a_deep_pit_starves_its_plants);
    RUN_TEST(test_generated_plants_live_all_year);
    RUN_TEST(test_poor_ground_plants_stay_on_poor_ground);
    RUN_TEST(test_no_heath_on_the_banks);
    return test_summary();
}
