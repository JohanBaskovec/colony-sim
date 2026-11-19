/* The fog of war (docs/VISION.md): the known world as a picture of the real
 * one, what is explored, the start, and revealing the map. */
#include "core/exploration.h"
#include "core/game.h"
#include "core/known.h"

#include "test_framework.h"

#include <string.h>

static bool make_game(Game *game, uint64_t seed, int colonists)
{
    NewGameParams params;
    game_default_params(&params);
    params.world.seed = seed;
    params.world.width = 64;
    params.world.height = 64;
    params.colonist_count = colonists;
    return game_init(game, &params);
}

static void test_known_world_starts_unknown(void)
{
    Game game;
    REQUIRE(make_game(&game, 5, 0));
    KnownWorld known;
    REQUIRE(known_init(&known, &game.map));
    CHECK_EQ_INT(known.map.width, game.map.width);
    CHECK_EQ_INT(known.map.depth, game.map.depth);
    for (size_t i = 0; i < map_tile_count(&known.map); i++) {
        if (known.map.terrain[i] != TERRAIN_OPEN || known.map.rock[i] != 0) {
            CHECK(false);
            break;
        }
    }
    CHECK_EQ_INT(known.map.structure_counts[STRUCTURE_NONE], (int)map_tile_count(&known.map));
    known_free(&known);
    game_shutdown(&game);
}

/* Copying every tile makes the known world the real one, counters included. */
static void test_copy_tiles(void)
{
    Game game;
    REQUIRE(make_game(&game, 6, 0));
    /* Something of everything. */
    int x = 20, y = 20, z = map_ground_level(&game.map, x, y);
    map_set_structure(&game.map, x, y, z, STRUCTURE_WALL);
    REQUIRE(map_set_forbidden(&game.map, x, y, z, MAP_FORBID_STRUCTURE, true));
    REQUIRE(game_debug_spawn_items(&game, x + 2, y, z, ITEM_BRANCHES, 3) > 0);
    dirt_add(&game.dirt, &game.map, x + 3, y, z, 40);
    KnownWorld known;
    REQUIRE(known_init(&known, &game.map));
    for (size_t i = 0; i < map_tile_count(&game.map); i++) {
        known_copy_tile(&known, &game.map, &game.items, &game.dirt, &game.workstations, i);
    }
    CHECK(map_hash(&known.map) == map_hash(&game.map));
    CHECK_EQ_INT(known.map.slope_count, game.map.slope_count);
    CHECK_EQ_INT(known.map.roof_count, game.map.roof_count);
    CHECK_EQ_INT(known.map.forbidden_count, game.map.forbidden_count);
    for (int s = 0; s < STRUCTURE_TYPE_COUNT; s++) {
        CHECK_EQ_INT(known.map.structure_counts[s], game.map.structure_counts[s]);
    }
    CHECK_EQ_INT(known.dirt.dirty_count, game.dirt.dirty_count);
    CHECK_EQ_INT(dirt_at(&known.dirt, x + 3, y, z), 40);
    for (int type = 0; type < ITEM_TYPE_COUNT; type++) {
        CHECK_EQ_INT(known.items.totals[type], game.items.totals[type]);
    }
    for (int level = 0; level < game.map.depth; level++) {
        CHECK_EQ_INT(known.items.level_stacks[level], game.items.level_stacks[level]);
    }
    /* A tile changed and copied again: the counters follow back. */
    map_remove_structure(&game.map, x, y, z);
    size_t index = map_index(&game.map, x, y, z);
    known_copy_tile(&known, &game.map, &game.items, &game.dirt, &game.workstations, index);
    CHECK_EQ_INT(known.map.structure_counts[STRUCTURE_WALL], 0);
    CHECK(map_hash(&known.map) == map_hash(&game.map));
    known_free(&known);
    game_shutdown(&game);
}

static void test_fog_explores_the_start(void)
{
    Game game;
    REQUIRE(make_game(&game, 7, 3));
    REQUIRE(game_set_fog(&game, true));
    REQUIRE(game.fog_on);
    CHECK(game.vision_on);
    /* The arrival point and what the settlers stand on are explored; the map is not, entirely. */
    CHECK(game.has_arrival);
    CHECK(game_tile_explored(&game, game.arrival.x, game.arrival.y, game.arrival.z));
    size_t tracked = 0;
    for (size_t i = 0; i < map_tile_count(&game.map); i++) {
        tracked += vision_tracked(&game.map, 0, 0, 0) ? 0 : 0;
        int x, y, z;
        map_tile_of(&game.map, i, &x, &y, &z);
        tracked += vision_tracked(&game.map, x, y, z);
    }
    CHECK(game.vision.explored_count > 100);
    CHECK(game.vision.explored_count < tracked);
    /* A way in from an edge: some explored tile lies on the border of the map. */
    bool border = false;
    for (int i = 0; i < 64 && !border; i++) {
        for (int z = 0; z < game.map.depth && !border; z++) {
            border =
                vision_explored(&game.vision, i, 0, z) || vision_explored(&game.vision, i, 63, z) ||
                vision_explored(&game.vision, 0, i, z) || vision_explored(&game.vision, 63, i, z);
        }
    }
    CHECK(border);
    /* The known world shows what is explored, as it is. */
    for (int dx = -1; dx <= 1; dx++) {
        int x = game.arrival.x + dx, y = game.arrival.y, z = game.arrival.z;
        size_t index = map_index(&game.map, x, y, z);
        CHECK_EQ_INT(game_view_map(&game)->terrain[index], game.map.terrain[index]);
    }
    /* A tile of the far corner of the map, unexplored, is unknown. */
    int unknown = 0;
    for (size_t i = 0; i < map_tile_count(&game.map); i++) {
        int x, y, z;
        map_tile_of(&game.map, i, &x, &y, &z);
        if (!vision_explored(&game.vision, x, y, z)) {
            unknown += game_view_map(&game)->terrain[i] == TERRAIN_OPEN &&
                       game_view_map(&game)->rock[i] == 0;
        }
    }
    CHECK(unknown > 0);
    game_shutdown(&game);
}

static void test_fog_is_deterministic(void)
{
    uint64_t hashes[2];
    size_t explored[2];
    for (int run = 0; run < 2; run++) {
        Game game;
        REQUIRE(make_game(&game, 8, 3));
        REQUIRE(game_set_fog(&game, true));
        for (int i = 0; i < 600; i++) {
            game_tick(&game);
        }
        hashes[run] = game_hash(&game);
        explored[run] = game.vision.explored_count;
        game_shutdown(&game);
    }
    CHECK(hashes[0] == hashes[1]);
    CHECK(explored[0] == explored[1]);
}

/* A flat plain with a rock wall across it (x = 30), a settler on each side in turn. */
static bool walled_plain(Game *game)
{
    if (!make_game(game, 9, 0)) {
        return false;
    }
    Map *map = &game->map;
    int ground = map->surface_level;
    for (size_t i = 0; i < map_tile_count(map); i++) {
        int x, y, z;
        map_tile_of(map, i, &x, &y, &z);
        map->terrain[i] = (uint8_t)(z <= ground ? TERRAIN_SOIL : TERRAIN_OPEN);
        map->rock[i] = (uint8_t)(z < ground ? ROCK_EARTH : ROCK_NONE);
        map->slope[i] = 0;
        memset(&map->plants[i], 0, sizeof(map->plants[i]));
        map->branches[i] = map->twigs[i] = map->cobbles[i] = map->flint[i] = map->nuts[i] = 0;
    }
    map->slope_count = 0;
    map->top_ground_level = ground + 1;
    for (int y = 0; y < map->height; y++) {
        map->rock[map_index(map, 30, y, ground)] = ROCK_GRANITE;
    }
    map_topology_changed(map);
    game->clock.tick = 12 * TICKS_PER_HOUR; /* Noon: nobody asleep. */
    return true;
}

/* What lies out of view is shown as it was last seen. */
static void test_remembers_what_was_last_seen(void)
{
    Game game;
    REQUIRE(walled_plain(&game));
    int z = game.map.surface_level;
    int tx = 24, ty = 30;
    EntityId id = game_spawn_colonist(&game, 20, 30, z);
    REQUIRE(game_colonist(&game, id) != NULL);
    REQUIRE(game_debug_spawn_items(&game, tx, ty, z, ITEM_BRANCHES, 2) > 0);
    REQUIRE(game_set_fog(&game, true));
    size_t index = map_index(&game.map, tx, ty, z);
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    CHECK(game_tile_seen(&game, tx, ty, z));
    CHECK_EQ_INT(game.known.items.stacks[index].type, ITEM_BRANCHES);
    /* The settler goes behind the wall: the tile is out of view, and the branches are taken. */
    REQUIRE(game_debug_teleport(&game, id, 45, 30, z));
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    CHECK(!game_tile_seen(&game, tx, ty, z));
    CHECK(game_tile_explored(&game, tx, ty, z));
    CHECK_EQ_INT(items_remove(&game.items, tx, ty, z, 2), 2);
    for (int i = 0; i < 30; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(game.items.stacks[index].type, ITEM_NONE);
    CHECK_EQ_INT(game.known.items.stacks[index].type, ITEM_BRANCHES); /* As last seen. */
    CHECK_EQ_INT(game_view_items(&game)->stacks[index].count, 2);
    /* Back in view: the known world catches up. */
    REQUIRE(game_debug_teleport(&game, id, 20, 30, z));
    for (int i = 0; i < 30; i++) {
        game_tick(&game);
    }
    CHECK(game_tile_seen(&game, tx, ty, z));
    CHECK_EQ_INT(game.known.items.stacks[index].type, ITEM_NONE);
    /* Beyond the wall nothing was ever seen, except what the way in showed. */
    game_shutdown(&game);
}

/* Puts an animal standing on a tile (for the tests of the ghosts, which call
 * known_update_animals() themselves while the game is not ticking). */
static void place_animal(Animal *animal, int x, int y)
{
    animal->x = x;
    animal->y = y;
    animal->path_index = animal->path.count; /* Not moving. */
}

/* Wild animals out of view: a ghost where the colony last saw one, as for
 * everything else, but none for one seen walking away. */
static void test_remembers_animals(void)
{
    Game game;
    REQUIRE(walled_plain(&game));
    int z = game.map.surface_level;
    EntityId settler = game_spawn_colonist(&game, 20, 30, z);
    REQUIRE(game_colonist(&game, settler) != NULL);
    REQUIRE(game_set_fog(&game, true));
    EntityId id = game_debug_spawn_animal(&game, ANIMAL_ROE_DEER, ANIMAL_FEMALE, 24, 30, z);
    REQUIRE(!entity_id_is_none(id));
    game_tick(&game);
    /* The game follows the animals it sees. */
    CHECK_EQ_INT(game.known.animals[id.index].state, KNOWN_ANIMAL_SEEN);
    CHECK(entity_id_equal(game.known.animals[id.index].id, id));

    /* The settler goes behind the wall: the game leaves a ghost where the deer was. */
    REQUIRE(game_debug_teleport(&game, settler, 45, 30, z));
    for (int i = 0; i < 5; i++) {
        game_tick(&game);
    }
    CHECK(!game_animal_seen(&game, id));
    CHECK(known_animal_ghost(&game.known, id.index) != NULL);
    REQUIRE(game_debug_teleport(&game, settler, 20, 30, z));
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }

    /* A memory of its own, updated only here (the game's moves the animal). */
    KnownWorld known;
    REQUIRE(known_init(&known, &game.map));
    Animal *animal = animals_get(&game.animals, id);
    REQUIRE(animal != NULL);
    place_animal(animal, 24, 30);
    known_update_animals(&known, &game.animals, &game.vision, 100);
    CHECK(known_animal_ghost(&known, id.index) == NULL); /* Seen: no ghost. */
    CHECK_EQ_INT(known.animals[id.index].state, KNOWN_ANIMAL_SEEN);

    /* The settler goes behind the wall: the deer stays there as a ghost. */
    REQUIRE(game_debug_teleport(&game, settler, 45, 30, z));
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    REQUIRE(!game_tile_seen(&game, 24, 30, z));
    place_animal(animal, 10, 30); /* Gone elsewhere, out of view. */
    known_update_animals(&known, &game.animals, &game.vision, 200);
    const KnownAnimal *ghost = known_animal_ghost(&known, id.index);
    REQUIRE(ghost != NULL);
    CHECK_NEAR(ghost->x, 24.5f, 0.001f);
    CHECK_NEAR(ghost->y, 30.5f, 0.001f);
    CHECK_EQ_INT(ghost->z, z);
    CHECK_EQ_INT(ghost->species, ANIMAL_ROE_DEER);
    CHECK_EQ_INT(ghost->tick, 100); /* When it was last seen. */
    CHECK(known_animal_ghost_at(&known, &game.map, 24.6f, 30.4f, z, 0.5f) == ghost);
    CHECK(known_animal_ghost_at(&known, &game.map, 27.0f, 30.4f, z, 0.5f) == NULL);
    CHECK(known_animal_ghost_at(&known, &game.map, 24.6f, 30.4f, z - 1, 0.5f) == NULL);

    /* Seen elsewhere: the ghost goes, the animal is followed. */
    place_animal(animal, 40, 30);
    known_update_animals(&known, &game.animals, &game.vision, 300);
    CHECK(known_animal_ghost(&known, id.index) == NULL);
    CHECK_EQ_INT(known.animals[id.index].state, KNOWN_ANIMAL_SEEN);

    /* Seen walking out of view (its last spot is in view): no ghost. */
    place_animal(animal, 10, 30);
    known_update_animals(&known, &game.animals, &game.vision, 301);
    CHECK_EQ_INT(known.animals[id.index].state, KNOWN_ANIMAL_NONE);

    /* Left behind at 40, then that tile seen again without it: forgotten. */
    place_animal(animal, 40, 30);
    known_update_animals(&known, &game.animals, &game.vision, 400);
    REQUIRE(game_debug_teleport(&game, settler, 20, 30, z));
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    REQUIRE(!game_tile_seen(&game, 40, 30, z));
    known_update_animals(&known, &game.animals, &game.vision, 500);
    REQUIRE(known_animal_ghost(&known, id.index) != NULL);
    REQUIRE(game_debug_teleport(&game, settler, 45, 30, z));
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    place_animal(animal, 10, 30);
    known_update_animals(&known, &game.animals, &game.vision, 600);
    CHECK(known_animal_ghost(&known, id.index) == NULL);
    known_free(&known);
    game_shutdown(&game);
}

/* Orders, zones and buildings are refused on tiles nobody has seen. */
static void test_tools_refuse_unexplored_tiles(void)
{
    Game game;
    REQUIRE(walled_plain(&game));
    int z = game.map.surface_level;
    EntityId id = game_spawn_colonist(&game, 10, 30, z);
    REQUIRE(game_colonist(&game, id) != NULL);
    /* The settlers arrived west of the wall: what lies east of it is unseen. */
    game.arrival = (TilePos){10, 30, (int16_t)z};
    game.has_arrival = true;
    REQUIRE(game_set_fog(&game, true));
    int ux = -1, uy = -1;
    for (int x = 50; x < 64 && ux < 0; x++) {
        for (int y = 0; y < 64 && ux < 0; y++) {
            if (!game_tile_explored(&game, x, y, z) && map_is_passable(&game.map, x, y, z)) {
                ux = x;
                uy = y;
            }
        }
    }
    REQUIRE(ux >= 0);
    int ex = 6, ey = 26; /* West of the wall, in view of the settler. */
    REQUIRE(game_tile_explored(&game, ex, ey, z));
    /* Zones: the unexplored tile is not taken, the explored one is. */
    CHECK(game_add_stockpile_rect(&game, ux, uy, ux, uy, z, -1) < 0);
    CHECK(game_add_stockpile_rect(&game, ex, ey, ex, ey, z, -1) >= 0);
    CHECK(game_add_gather_zone_rect(&game, ux, uy, ux, uy, z, -1) < 0);
    /* Buildings and plans. */
    CHECK(!game_can_place_structure(&game, STRUCTURE_WALL, ux, uy, z, DIRECTION_SOUTH));
    CHECK(game_can_place_structure(&game, STRUCTURE_WALL, ex + 1, ey, z, DIRECTION_SOUTH));
    CHECK(!game_can_place_workstation(&game, ux, uy, z));
    /* Forbidding. */
    CHECK(!game_set_forbidden(&game, ux, uy, z, FORBID_ITEMS, true));
    /* Without the fog, everything is allowed again. */
    REQUIRE(game_set_fog(&game, false));
    CHECK(game_can_place_structure(&game, STRUCTURE_WALL, ux, uy, z, DIRECTION_SOUTH));
    CHECK(game_add_gather_zone_rect(&game, ux, uy, ux, uy, z, -1) >= 0);
    game_shutdown(&game);
}

/* The colony's stock is what it knows of: an item out of view counts as last seen. */
static void test_stock_is_what_is_known(void)
{
    Game game;
    REQUIRE(walled_plain(&game));
    int z = game.map.surface_level;
    EntityId id = game_spawn_colonist(&game, 20, 30, z);
    REQUIRE(game_colonist(&game, id) != NULL);
    int tx = 24, ty = 30;
    REQUIRE(game_set_fog(&game, true));
    /* A stockpile where the settler stands, with branches in it. */
    int zone = game_add_stockpile_rect(&game, tx, ty, tx, ty, z, -1);
    REQUIRE(zone >= 0);
    REQUIRE(game_debug_spawn_items(&game, tx, ty, z, ITEM_BRANCHES, 4) > 0);
    for (int i = 0; i < 30; i++) {
        game_tick(&game);
    }
    CHECK(game_tile_seen(&game, tx, ty, z));
    long seen = game.items.totals[ITEM_BRANCHES];
    game_update_stock(&game);
    CHECK_EQ_INT(game.stock[ITEM_BRANCHES], seen);
    /* The settler goes behind the wall; the branches are taken (a hauler may have moved them
     * already: only what is on the tile counts). */
    REQUIRE(game_debug_teleport(&game, id, 45, 30, z));
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    CHECK(!game_tile_seen(&game, tx, ty, z));
    game_update_stock(&game);
    long remembered = game.stock[ITEM_BRANCHES];
    items_remove(&game.items, tx, ty, z, 4);
    for (int i = 0; i < 30; i++) {
        game_tick(&game);
    }
    game_update_stock(&game);
    CHECK_EQ_INT(game.stock[ITEM_BRANCHES], remembered); /* Still believed to be there. */
    CHECK_EQ_INT(game_view_items(&game)->totals[ITEM_BRANCHES], remembered);
    CHECK(game.items.totals[ITEM_BRANCHES] < remembered);
    /* Without the fog the stock is the real one. */
    REQUIRE(game_set_fog(&game, false));
    game_update_stock(&game);
    CHECK(game.stock[ITEM_BRANCHES] < remembered);
    game_shutdown(&game);
}

/* Settlers only plan through what the colony knows: food on a tile nobody has seen is not
 * looked for, however hungry they are; once it is known, they go for it. */
static void test_settlers_plan_on_the_known_world(void)
{
    Game game;
    REQUIRE(walled_plain(&game));
    Map *map = &game.map;
    int z = map->surface_level;
    /* A wall of 17 tiles hiding what lies behind it from the settler, who can walk round. */
    for (int y = 0; y < map->height; y++) {
        map->rock[map_index(map, 30, y, z)] = ROCK_NONE;
    }
    for (int y = 22; y <= 38; y++) {
        map->rock[map_index(map, 20, y, z)] = ROCK_GRANITE;
    }
    map_topology_changed(map);
    EntityId id = game_spawn_colonist(&game, 10, 30, z);
    Colonist *colonist = game_colonist(&game, id);
    REQUIRE(colonist != NULL);
    int fx = 23, fy = 30;
    REQUIRE(game_debug_spawn_items(&game, fx, fy, z, ITEM_BLACKBERRIES, 3) > 0);
    game.arrival = (TilePos){10, 30, (int16_t)z};
    game.has_arrival = true;
    REQUIRE(game_set_fog(&game, true));
    /* The way in may have shown what lies behind the wall: forget it (the known world there goes
     * back to nothing). */
    for (int y = fy - 3; y <= fy + 3; y++) {
        for (int x = fx - 3; x <= fx + 3; x++) {
            size_t index = map_index(map, x, y, z);
            game.vision.explored[index >> 6] &= ~((uint64_t)1 << (index & 63));
            game.known.map.terrain[index] = TERRAIN_OPEN;
            game.known.map.rock[index] = 0;
            memset(&game.known.map.plants[index], 0, sizeof(game.known.map.plants[index]));
            items_remove(&game.known.items, x, y, z, UINT16_MAX);
        }
    }
    REQUIRE(!game_tile_explored(&game, fx, fy, z)); /* Hidden behind the wall. */
    colonist->needs[NEED_FOOD] = 0.02f;
    bool went = false;
    for (int i = 0; i < 900; i++) {
        game_tick(&game);
        colonist = game_colonist(&game, id);
        went |= colonist->task == TASK_EAT && colonist->target.x == fx && colonist->target.y == fy;
    }
    CHECK(!went);
    CHECK_EQ_INT(game.items.stacks[map_index(map, fx, fy, z)].count, 3);
    /* The map is revealed: the food is known, and the settler goes for it. */
    game_reveal_map(&game);
    colonist = game_colonist(&game, id);
    colonist->needs[NEED_FOOD] = 0.02f;
    for (int i = 0; i < 1500 && !went; i++) {
        game_tick(&game);
        colonist = game_colonist(&game, id);
        went |= colonist->task == TASK_EAT && colonist->target.x == fx && colonist->target.y == fy;
    }
    CHECK(went);
    game_shutdown(&game);
}

/* Items that change where they are seen are copied into the known world at once: a settler
 * acts on what the one before it just did. */
static void test_known_items_follow_at_once(void)
{
    Game game;
    REQUIRE(walled_plain(&game));
    int z = game.map.surface_level;
    EntityId id = game_spawn_colonist(&game, 20, 30, z);
    REQUIRE(game_colonist(&game, id) != NULL);
    REQUIRE(game_set_fog(&game, true));
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    int tx = 23, ty = 30;
    REQUIRE(game_tile_seen(&game, tx, ty, z));
    size_t index = map_index(&game.map, tx, ty, z);
    REQUIRE(items_add(&game.items, tx, ty, z, item_stack_pristine(ITEM_BRANCHES, 3)) == 3);
    CHECK_EQ_INT(game.known.items.stacks[index].count, 3); /* No tick in between. */
    CHECK_EQ_INT(items_remove(&game.items, tx, ty, z, 2), 2);
    CHECK_EQ_INT(game.known.items.stacks[index].count, 1);
    CHECK_EQ_INT(game.known.items.totals[ITEM_BRANCHES], game.items.totals[ITEM_BRANCHES]);
    game_shutdown(&game);
}

/* A workstation destroyed out of view is still remembered where it was. */
static void test_remembers_workstations(void)
{
    Game game;
    REQUIRE(walled_plain(&game));
    int z = game.map.surface_level;
    EntityId id = game_spawn_colonist(&game, 20, 30, z);
    REQUIRE(game_colonist(&game, id) != NULL);
    int sx = 24, sy = 30;
    int index =
        workstations_place(&game.workstations, &game.map, WORKSTATION_OPEN_HEARTH, sx, sy, z);
    REQUIRE(index >= 0);
    REQUIRE(game_set_fog(&game, true));
    size_t tile = map_index(&game.map, sx, sy, z);
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    CHECK(game_tile_seen(&game, sx, sy, z));
    CHECK_EQ_INT(game.known.stations[tile].type_plus_one, WORKSTATION_OPEN_HEARTH + 1);
    /* Behind the wall, it is destroyed: the colony still believes in it. */
    REQUIRE(game_debug_teleport(&game, id, 45, 30, z));
    for (int i = 0; i < 20; i++) {
        game_tick(&game);
    }
    CHECK(!game_tile_seen(&game, sx, sy, z));
    workstations_remove(&game.workstations, index, (ItemStack[1]){0});
    for (int i = 0; i < 30; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(workstation_at(&game.workstations, sx, sy, z), WORKSTATION_NONE);
    CHECK_EQ_INT(game.known.stations[tile].type_plus_one, WORKSTATION_OPEN_HEARTH + 1);
    /* Back in view, it is seen to be gone. */
    REQUIRE(game_debug_teleport(&game, id, 20, 30, z));
    for (int i = 0; i < 30; i++) {
        game_tick(&game);
    }
    CHECK_EQ_INT(game.known.stations[tile].type_plus_one, 0);
    game_shutdown(&game);
}

static void test_reveal_map(void)
{
    Game game;
    REQUIRE(make_game(&game, 10, 3));
    REQUIRE(game_set_fog(&game, true));
    game_reveal_map(&game);
    game_tick(&game);
    for (size_t i = 0; i < map_tile_count(&game.map); i++) {
        int x, y, z;
        map_tile_of(&game.map, i, &x, &y, &z);
        if (vision_tracked(&game.map, x, y, z) && !vision_explored(&game.vision, x, y, z)) {
            CHECK(false);
            break;
        }
    }
    /* Revealed tiles are shown as they are (the ones in view are brought up to date within a
     * slice). */
    for (int i = 0; i < 12; i++) {
        game_tick(&game);
    }
    CHECK(map_hash(&game.known.map) == map_hash(&game.map));
    game_shutdown(&game);
}

static void test_fog_off_sees_everything(void)
{
    Game game;
    REQUIRE(make_game(&game, 11, 2));
    CHECK(!game.fog_on);
    CHECK(game_view_map(&game) == &game.map);
    CHECK(game_view_items(&game) == &game.items);
    CHECK(game_tile_explored(&game, 3, 3, 0));
    CHECK(game_tile_seen(&game, 3, 3, 0));
    CHECK(!game_tile_explored(&game, -1, 3, 0));
    REQUIRE(game_set_fog(&game, true));
    CHECK(game_view_map(&game) == &game.known.map);
    REQUIRE(game_set_fog(&game, false));
    CHECK(game_view_map(&game) == &game.map);
    game_shutdown(&game);
}

/* A move order only goes to what the colony has explored: the way into the fog is step by step
 * (the nearest explored tiles are taken when the spot clicked is not). */
static void test_move_order_stays_in_the_explored(void)
{
    Game game;
    REQUIRE(walled_plain(&game));
    Map *map = &game.map;
    int z = map->surface_level;
    for (int y = 0; y < map->height; y++) {
        map->rock[map_index(map, 30, y, z)] = ROCK_NONE; /* No wall. */
    }
    map_topology_changed(map);
    EntityId id = game_spawn_colonist(&game, 10, 30, z);
    REQUIRE(game_colonist(&game, id) != NULL);
    game.arrival = (TilePos){10, 30, (int16_t)z};
    game.has_arrival = true;
    REQUIRE(game_set_fog(&game, true));
    /* Forget a patch far away. */
    for (int y = 40; y <= 60; y++) {
        for (int x = 40; x <= 60; x++) {
            size_t index = map_index(map, x, y, z);
            game.vision.explored[index >> 6] &= ~((uint64_t)1 << (index & 63));
            game.known.map.terrain[index] = TERRAIN_OPEN;
        }
    }
    REQUIRE(!game_tile_explored(&game, 50, 50, z));
    CHECK_EQ_INT(game_order_move(&game, &id, 1, 50, 50, z), 0);
    CHECK(game_colonist(&game, id)->task != TASK_MOVE);
    game_reveal_map(&game);
    for (int i = 0; i < 400; i++) { /* The picture catches up. */
        game_tick(&game);
    }
    CHECK_EQ_INT(game_order_move(&game, &id, 1, 50, 50, z), 1);
    CHECK_EQ_INT(game_colonist(&game, id)->task, TASK_MOVE);
    game_shutdown(&game);
}

int main(void)
{
    RUN_TEST(test_move_order_stays_in_the_explored);
    RUN_TEST(test_known_world_starts_unknown);
    RUN_TEST(test_copy_tiles);
    RUN_TEST(test_fog_explores_the_start);
    RUN_TEST(test_fog_is_deterministic);
    RUN_TEST(test_remembers_what_was_last_seen);
    RUN_TEST(test_remembers_animals);
    RUN_TEST(test_tools_refuse_unexplored_tiles);
    RUN_TEST(test_stock_is_what_is_known);
    RUN_TEST(test_settlers_plan_on_the_known_world);
    RUN_TEST(test_known_items_follow_at_once);
    RUN_TEST(test_remembers_workstations);
    RUN_TEST(test_reveal_map);
    RUN_TEST(test_fog_off_sees_everything);
    return test_summary();
}
