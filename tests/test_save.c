#include "core/known.h"
#include "core/save.h"
#include "core/scenario.h"
#include "core/util.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

/* A game of `size` tiles a side at seed 42 with `colonists` settlers. */
static bool start_game(Game *game, int size, int colonists, float wildlife, Scenario scenario)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 42;
    params.world.width = size;
    params.world.height = size;
    params.colonist_count = colonists;
    params.wildlife = wildlife;
    if (!game_init(game, &params)) {
        return false;
    }
    if (scenario != SCENARIO_NONE && !scenario_apply(game, scenario)) {
        game_shutdown(game);
        return false;
    }
    return true;
}

static void run_ticks(Game *game, long ticks)
{
    for (long i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

/* Where two saves first differ: the section's tag and the offset in it
 * (printed, for finding what was left out). True when they are the same. */
static bool same_saves(const uint8_t *a, size_t a_size, const uint8_t *b, size_t b_size)
{
    size_t at = 8; /* After the header. */
    size_t end = a_size < b_size ? a_size : b_size;
    while (at + 8 <= end) {
        uint32_t length = (uint32_t)a[at + 4] | (uint32_t)a[at + 5] << 8 |
                          (uint32_t)a[at + 6] << 16 | (uint32_t)a[at + 7] << 24;
        size_t section_end = at + 8 + length;
        for (size_t i = at; i < section_end && i < end; i++) {
            if (a[i] != b[i]) {
                fprintf(stderr, "  saves differ in section %.4s at offset %zu (of %u)\n",
                        (const char *)&a[at], i - at - 8, length);
                return false;
            }
        }
        at = section_end;
        if (at + 8 >= a_size) {
            break; /* The footer. */
        }
    }
    if (a_size != b_size || memcmp(a, b, a_size) != 0) {
        fprintf(stderr, "  saves differ (sizes %zu and %zu)\n", a_size, b_size);
        return false;
    }
    return true;
}

/* Plays `before` ticks, saves; plays `after` more and saves again. Loads the
 * first save, plays `after` ticks and saves: both last saves must be the
 * same bytes, and the game hashes alike. */
static void check_future_unchanged(Game *game, long before, long after)
{
    run_ticks(game, before);
    size_t first_size = 0;
    uint8_t *first = game_save(game, &first_size);
    REQUIRE(first != NULL);

    Game loaded;
    REQUIRE(game_load(&loaded, first, first_size) == SAVE_OK);
    size_t again_size = 0;
    uint8_t *again = game_save(&loaded, &again_size);
    REQUIRE(again != NULL);
    CHECK(same_saves(first, first_size, again, again_size)); /* The round trip. */
    CHECK_EQ_UINT(game_hash(&loaded), game_hash(game));

    run_ticks(game, after);
    run_ticks(&loaded, after);
    size_t played_size = 0, replayed_size = 0;
    uint8_t *played = game_save(game, &played_size);
    uint8_t *replayed = game_save(&loaded, &replayed_size);
    REQUIRE(played != NULL && replayed != NULL);
    CHECK(same_saves(played, played_size, replayed, replayed_size));
    CHECK_EQ_UINT(game_hash(&loaded), game_hash(game));
    free(first);
    free(again);
    free(played);
    free(replayed);
    game_shutdown(&loaded);
}

static void test_a_new_game_comes_back(void)
{
    Game game;
    REQUIRE(start_game(&game, 48, 3, 0.0f, SCENARIO_NONE));
    check_future_unchanged(&game, 0, 200);
    game_shutdown(&game);
}

static void test_settlers_gathering_play_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 64, 6, 0.0f, SCENARIO_GATHERING));
    check_future_unchanged(&game, TICKS_PER_HOUR * 6, TICKS_PER_HOUR * 3);
    game_shutdown(&game);
}

static void test_building_plays_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 64, 8, 0.0f, SCENARIO_CONSTRUCTION));
    check_future_unchanged(&game, TICKS_PER_DAY, TICKS_PER_HOUR * 6);
    game_shutdown(&game);
}

/* An uprooting zone, its choice of plants and the orders it gave come back, and play on. */
static void test_uprooting_zones_play_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 64, 4, 0.0f, SCENARIO_NONE));
    int x = game.arrival.x, y = game.arrival.y, z = game.arrival.z;
    int zone = game_add_uproot_zone_rect(&game, x - 6, y - 6, x + 6, y + 6, z, GATHER_ZONE_NONE);
    REQUIRE(zone != GATHER_ZONE_NONE);
    gather_zone_set_uproot_plant(&game.gather_zones, zone, PLANT_GRASS, false);
    run_ticks(&game, GAME_UPROOT_CHECK_INTERVAL * 2);
    check_future_unchanged(&game, 0, TICKS_PER_HOUR);
    size_t size = 0;
    uint8_t *data = game_save(&game, &size);
    REQUIRE(data != NULL);
    Game loaded;
    REQUIRE(game_load(&loaded, data, size) == SAVE_OK);
    const GatherZone *back = gather_zone_get(&loaded.gather_zones, zone);
    REQUIRE(back != NULL);
    CHECK(back->uproot);
    CHECK(gather_zone_uproots(back, PLANT_BRAMBLE));
    CHECK(!gather_zone_uproots(back, PLANT_GRASS));
    CHECK_EQ_INT(back->cell_count, gather_zone_get(&game.gather_zones, zone)->cell_count);
    free(data);
    game_shutdown(&loaded);
    game_shutdown(&game);
}

static void test_allowed_areas_play_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 64, 4, 0.0f, SCENARIO_NONE));
    int x = game.arrival.x, y = game.arrival.y, top = game.map.depth - 1;
    int camp = game_add_area(&game, "Camp");
    REQUIRE(camp == 1);
    REQUIRE(game_set_area_rect(&game, camp, x - 5, y - 5, x + 5, y + 5, 0, top, true) > 0);
    REQUIRE(game_set_colonist_area(&game, game_colonist_id_at(&game, 1), camp));
    REQUIRE(game_set_area_rect(&game, 0, 0, 0, 63, 63, 0, top, false) > 0);
    REQUIRE(game_set_area_rect(&game, 0, x - 20, y - 20, x + 20, y + 20, 0, top, true) > 0);
    run_ticks(&game, TICKS_PER_HOUR);
    check_future_unchanged(&game, 0, TICKS_PER_HOUR);
    size_t size = 0;
    uint8_t *data = game_save(&game, &size);
    REQUIRE(data != NULL);
    Game loaded;
    REQUIRE(game_load(&loaded, data, size) == SAVE_OK);
    CHECK_EQ_INT(loaded.areas.count, 2);
    CHECK_STR_EQ(loaded.areas.list[camp].name, "Camp");
    CHECK_EQ_INT((int)loaded.areas.list[camp].tiles, (int)game.areas.list[camp].tiles);
    CHECK_EQ_INT(loaded.colonists[1].area, camp);
    free(data);
    game_shutdown(&loaded);
    game_shutdown(&game);
}

static void test_feuds_and_brawls_play_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 64, 8, 0.0f, SCENARIO_FEUDS));
    check_future_unchanged(&game, TICKS_PER_DAY, TICKS_PER_HOUR * 6);
    game_shutdown(&game);
}

static void test_cooking_plays_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 64, 6, 0.0f, SCENARIO_COOKING));
    check_future_unchanged(&game, TICKS_PER_HOUR * 12, TICKS_PER_HOUR * 6);
    game_shutdown(&game);
}

static void test_wild_animals_play_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 64, 4, 3.0f, SCENARIO_GATHERING));
    check_future_unchanged(&game, TICKS_PER_DAY, TICKS_PER_HOUR * 6);
    game_shutdown(&game);
}

/* The fog of war: what the colony saw, the picture of the world it keeps. */
static void test_the_fog_of_war_plays_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 64, 5, 1.0f, SCENARIO_GATHERING));
    REQUIRE(game_set_fog(&game, true));
    check_future_unchanged(&game, TICKS_PER_HOUR * 8, TICKS_PER_HOUR * 4);
    game_shutdown(&game);
}

/* Saved while water flows: the active blocks and their pipes. */
static void test_flowing_water_plays_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 48, 2, 0.0f, SCENARIO_NONE));
    run_ticks(&game, 100);
    int x = 24, y = 24;
    int z = map_ground_level(&game.map, x, y);
    map_set_water(&game.map, x, y, z, WATER_CELL_FULL);
    map_set_water(&game.map, x + 1, y, z, WATER_CELL_FULL);
    run_ticks(&game, 30);
    REQUIRE(game.water.active_count > 0);
    check_future_unchanged(&game, 0, 400);
    game_shutdown(&game);
}

/* Saved while water flows out over the edge of the map: the land beyond. */
static void test_water_at_the_edge_plays_on_alike(void)
{
    Game game;
    REQUIRE(start_game(&game, 48, 2, 0.0f, SCENARIO_NONE));
    run_ticks(&game, 100);
    for (int y = 20; y < 28; y++) {
        int z = map_ground_level(&game.map, 0, y);
        map_set_water(&game.map, 0, y, z, WATER_CELL_FULL);
        map_set_water(&game.map, 1, y, z, WATER_CELL_FULL);
    }
    run_ticks(&game, 60);
    REQUIRE(game.water.edge_in != 0);
    check_future_unchanged(&game, 0, 400);
    game_shutdown(&game);
}

/* Saved while a river flows: its source and outlet beyond the edges. */
static void test_a_river_plays_on_alike(void)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = 42;
    params.world.width = 48;
    params.world.height = 48;
    params.world.river.width = 6;
    params.colonist_count = 1;
    Game game;
    REQUIRE(game_init(&game, &params));
    run_ticks(&game, 200);
    REQUIRE(game.water.active_count > 0);
    REQUIRE(game.water.edge_in > 0);
    check_future_unchanged(&game, 0, 400);
    game_shutdown(&game);
}

/* A save tells when it was made, of how many settlers, from its first bytes. */
static void test_a_save_tells_of_itself(void)
{
    Game game;
    REQUIRE(start_game(&game, 32, 3, 0.0f, SCENARIO_NONE));
    run_ticks(&game, 50);
    size_t size = 0;
    uint8_t *data = game_save(&game, &size);
    REQUIRE(data != NULL);
    SaveInfo info;
    REQUIRE(save_read_info(data, SAVE_INFO_BYTES, &info));
    CHECK_EQ_UINT(info.tick, game.clock.tick);
    CHECK_EQ_UINT(info.settlers, 3);
    CHECK_EQ_UINT(info.seed, 42);
    CHECK_EQ_INT(info.width, 32);
    CHECK_EQ_INT(info.height, 32);
    CHECK_EQ_INT(info.depth, game.map.depth);
    CHECK(!info.colony_lost);
    CHECK(!save_read_info(data, 12, &info)); /* Cut inside its INFO. */
    data[4] = SAVE_VERSION + 1;
    CHECK(!save_read_info(data, size, &info));
    free(data);
    game_shutdown(&game);
}

/* A save cut short, changed, of another version or of nothing at all is
 * refused, and the game left empty. */
static void test_damaged_saves_are_refused(void)
{
    Game game;
    REQUIRE(start_game(&game, 32, 2, 0.0f, SCENARIO_NONE));
    size_t size = 0;
    uint8_t *data = game_save(&game, &size);
    REQUIRE(data != NULL);
    CHECK_EQ_INT(save_version_of(data, size), SAVE_VERSION);
    Game loaded;
    CHECK_EQ_INT(save_check(data, size), SAVE_OK);
    CHECK_EQ_INT(save_check(data, size / 2), SAVE_ERROR_DAMAGED);
    CHECK_EQ_INT(game_load(&loaded, data, size / 2), SAVE_ERROR_DAMAGED);
    CHECK(loaded.map.terrain == NULL);
    data[size / 3] ^= 0x40;
    CHECK_EQ_INT(save_check(data, size), SAVE_ERROR_DAMAGED);
    CHECK_EQ_INT(game_load(&loaded, data, size), SAVE_ERROR_DAMAGED);
    data[size / 3] ^= 0x40;
    data[4] = SAVE_VERSION + 1;
    CHECK_EQ_INT(save_check(data, size), SAVE_ERROR_VERSION);
    CHECK_EQ_INT(game_load(&loaded, data, size), SAVE_ERROR_VERSION);
    data[4] = SAVE_VERSION;
    data[0] = 'X';
    CHECK_EQ_INT(save_check(data, size), SAVE_ERROR_NOT_A_SAVE);
    CHECK_EQ_INT(game_load(&loaded, data, size), SAVE_ERROR_NOT_A_SAVE);
    CHECK_EQ_INT(save_version_of(data, size), -1);
    CHECK_EQ_INT(game_load(&loaded, NULL, 0), SAVE_ERROR_NOT_A_SAVE);
    data[0] = 'T';
    CHECK_EQ_INT(game_load(&loaded, data, size), SAVE_OK);
    game_shutdown(&loaded);
    free(data);
    game_shutdown(&game);
}

/* Bytes changed all over a save, its hash made right again (a save edited
 * by hand): loading refuses it or takes it, never reads out of bounds
 * (run under the sanitizers) nor leaks. */
static void test_edited_saves_never_break_loading(void)
{
    Game game;
    REQUIRE(start_game(&game, 32, 3, 1.0f, SCENARIO_GATHERING));
    run_ticks(&game, TICKS_PER_HOUR);
    size_t size = 0;
    uint8_t *data = game_save(&game, &size);
    REQUIRE(data != NULL);
    uint8_t *edited = malloc(size);
    REQUIRE(edited != NULL);
    int refused = 0;
    uint32_t state = 12345u;
    for (int trial = 0; trial < 300; trial++) {
        memcpy(edited, data, size);
        state = state * 1664525u + 1013904223u;
        size_t at = 8 + (size_t)(state % (uint32_t)(size - 16));
        state = state * 1664525u + 1013904223u;
        edited[at] ^= (uint8_t)(1u + state % 255u);
        uint64_t hash = hash_fnv1a64(HASH_FNV1A64_INIT, edited, size - 8);
        for (int i = 0; i < 8; i++) {
            edited[size - 8 + (size_t)i] = (uint8_t)(hash >> (8 * i));
        }
        Game loaded;
        SaveError error = game_load(&loaded, edited, size);
        CHECK(error == SAVE_OK || error == SAVE_ERROR_DAMAGED);
        refused += error != SAVE_OK;
        game_shutdown(&loaded);
    }
    CHECK(refused > 0);
    free(edited);
    free(data);
    game_shutdown(&game);
}

/*
 * The size of every struct the save walks, field by field (on 64-bit Linux,
 * where they were measured). A size that changed is a field added, removed
 * or resized: serialize it in its module's _serialize() (or tell why it is
 * not saved, docs/SAVE.md §3), raise SAVE_VERSION, then write the new size
 * here.
 */
static void test_serialized_structs_keep_their_size(void)
{
#if defined(__x86_64__) && defined(__linux__)
    static const struct {
        const char *name;
        size_t size;
        size_t expected;
    } SIZES[] = {
        {"Game", sizeof(Game), 2736832},
        {"AllowedAreas", sizeof(AllowedAreas), 672},
        {"AllowedArea", sizeof(AllowedArea), 40},
        {"Map", sizeof(Map), 1096},
        {"Plant", sizeof(Plant), 20},
        {"MapTopologyChange", sizeof(MapTopologyChange), 8},
        {"MapTreeFall", sizeof(MapTreeFall), 10},
        {"TilePos", sizeof(TilePos), 6},
        {"TileBox", sizeof(TileBox), 24},
        {"EntityId", sizeof(EntityId), 8},
        {"Pool", sizeof(Pool), 48},
        {"Rng", sizeof(Rng), 16},
        {"Designations", sizeof(Designations), 104},
        {"Dirt", sizeof(Dirt), 32},
        {"GatherZones", sizeof(GatherZones), 8024},
        {"GatherZone", sizeof(GatherZone), 248},
        {"GatherOrder", sizeof(GatherOrder), 8},
        {"GatherGroupOrder", sizeof(GatherGroupOrder), 6},
        {"Items", sizeof(Items), 11232},
        {"Item", sizeof(Item), 40},
        {"ItemStack", sizeof(ItemStack), 32},
        {"ItemId", sizeof(ItemId), 8},
        {"ItemMaker", sizeof(ItemMaker), 40},
        {"Crowns", sizeof(Crowns), 504},
        {"CrownItem", sizeof(CrownItem), 16},
        {"Corpses", sizeof(Corpses), 16},
        {"Corpse", sizeof(Corpse), 8048},
        {"Stockpiles", sizeof(Stockpiles), 4648},
        {"Stockpile", sizeof(Stockpile), 72},
        {"Workstations", sizeof(Workstations), 32296},
        {"Workstation", sizeof(Workstation), 504},
        {"Bill", sizeof(Bill), 40},
        {"Constructions", sizeof(Constructions), 51248},
        {"ConstructionSite", sizeof(ConstructionSite), 100},
        {"Beds", sizeof(Beds), 1560},
        {"Reservations", sizeof(Reservations), 4176},
        {"Reservation", sizeof(Reservation), 8},
        {"ItemMoves", sizeof(ItemMoves), 1288},
        {"ItemMove", sizeof(ItemMove), 20},
        {"Temperatures", sizeof(Temperatures), 672},
        {"TemperatureSource", sizeof(TemperatureSource), 8},
        {"Soil", sizeof(Soil), 72},
        {"Water", sizeof(Water), 208},
        {"WaterBlock", sizeof(WaterBlock), 3272},
        {"RiverFlow", sizeof(RiverFlow), 32},
        {"GroundWater", sizeof(GroundWater), 72},
        {"TileLight", sizeof(TileLight), 3376},
        {"TileLightSweep", sizeof(TileLightSweep), 40},
        {"Vision", sizeof(Vision), 2696},
        {"VisionViewer", sizeof(VisionViewer), 40},
        {"KnownWorld", sizeof(KnownWorld), 21064},
        {"KnownStation", sizeof(KnownStation), 4},
        {"KnownAnimal", sizeof(KnownAnimal), 32},
        {"Weather", sizeof(Weather), 48},
        {"RoofWatch", sizeof(RoofWatch), 8},
        {"Social", sizeof(Social), 411664},
        {"SocialBond", sizeof(SocialBond), 100},
        {"SocialMemory", sizeof(SocialMemory), 12},
        {"SocialTale", sizeof(SocialTale), 8},
        {"CombatQueue", sizeof(CombatQueue), 2308},
        {"CombatStrike", sizeof(CombatStrike), 36},
        {"Combatant", sizeof(Combatant), 12},
        {"BloodStains", sizeof(BloodStains), 81928},
        {"BloodStain", sizeof(BloodStain), 40},
        {"Animals", sizeof(Animals), 1559608},
        {"Animal", sizeof(Animal), 6088},
        {"Occupancy", sizeof(Occupancy), 5168},
        {"OccupancyBeing", sizeof(OccupancyBeing), 16},
        {"Notices", sizeof(Notices), 9608},
        {"Notice", sizeof(Notice), 48},
        {"Colonist", sizeof(Colonist), 8032},
        {"Path", sizeof(Path), 16},
        {"Experiences", sizeof(Experiences), 324},
        {"Experience", sizeof(Experience), 40},
        {"Mood", sizeof(Mood), 488},
        {"Attributes", sizeof(Attributes), 38},
        {"Health", sizeof(Health), 260},
        {"Injury", sizeof(Injury), 12},
        {"HurtNote", sizeof(HurtNote), 3},
        {"Journal", sizeof(Journal), 5640},
        {"JournalEntry", sizeof(JournalEntry), 88},
        {"ColonistAppearance", sizeof(ColonistAppearance), 5},
        {"GameDebug", sizeof(GameDebug), 5},
    };
    for (size_t i = 0; i < sizeof(SIZES) / sizeof(SIZES[0]); i++) {
        if (SIZES[i].size != SIZES[i].expected) {
            fprintf(stderr, "  %s is now %zu bytes (was %zu): see this test's comment\n",
                    SIZES[i].name, SIZES[i].size, SIZES[i].expected);
        }
        CHECK_EQ_UINT(SIZES[i].size, SIZES[i].expected);
    }
#endif
}

int main(void)
{
    RUN_TEST(test_a_new_game_comes_back);
    RUN_TEST(test_settlers_gathering_play_on_alike);
    RUN_TEST(test_building_plays_on_alike);
    RUN_TEST(test_uprooting_zones_play_on_alike);
    RUN_TEST(test_allowed_areas_play_on_alike);
    RUN_TEST(test_feuds_and_brawls_play_on_alike);
    RUN_TEST(test_cooking_plays_on_alike);
    RUN_TEST(test_wild_animals_play_on_alike);
    RUN_TEST(test_the_fog_of_war_plays_on_alike);
    RUN_TEST(test_flowing_water_plays_on_alike);
    RUN_TEST(test_water_at_the_edge_plays_on_alike);
    RUN_TEST(test_a_river_plays_on_alike);
    RUN_TEST(test_a_save_tells_of_itself);
    RUN_TEST(test_damaged_saves_are_refused);
    RUN_TEST(test_edited_saves_never_break_loading);
    RUN_TEST(test_serialized_structs_keep_their_size);
    return test_summary();
}
