#include "core/game.h"

#include "core/conversation.h"
#include "core/log.h"
#include "core/tree.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Spawn area requirements: enough reachable land around the starting point. */
#define SPAWN_AREA_COST 30.0f
#define SPAWN_AREA_MIN_TILES 900
#define SPAWN_MAX_ATTEMPTS 400
#define SPAWN_PLACEMENT_MAX_COST 40.0f
/* Mixed into the game seed for the stream colonist looks are rolled from. */
#define GAME_APPEARANCE_SEED 0x5eed1ce5a77ea12aULL
/* Preferred: drinkable water within this travel cost of the spawn point. */
#define SPAWN_WATER_MAX_COST 60.0f
/* Food plants for the first settlers are counted and planted within this
 * travel cost of the arrival point: closer than GAME_SPAWN_FOOD_MAX_COST,
 * since the settlers stand a few tiles away from it. Planted ones are at
 * least SPAWN_PLANT_MIN_DISTANCE tiles (Manhattan) from it, each candidate
 * spot being taken with SPAWN_PLANT_SPOT_CHANCE. */
#define SPAWN_PLANT_MAX_COST (GAME_SPAWN_FOOD_MAX_COST - 10.0f)
#define SPAWN_PLANT_MIN_DISTANCE 3
#define SPAWN_PLANT_SPOT_CHANCE 0.08f
/* Mushrooms (docs/KNOWLEDGE.md §7.3): each day of their season, a patch
 * appears with MUSHROOM_CHANCE on open soil within MUSHROOM_TREE_RANGE tiles
 * of a mature tree, ×(1 + recent rain / MUSHROOM_RAIN_HOURS), at most
 * ×MUSHROOM_RAIN_MAX after much rain (Weather.recent_rain), and an existing
 * one goes with MUSHROOM_VANISH_CHANCE (five days on average). */
#define MUSHROOM_CHANCE 0.0015f
#define MUSHROOM_RAIN_HOURS 4.0f
#define MUSHROOM_RAIN_MAX 4.0f
#define MUSHROOM_VANISH_CHANCE 0.2f
#define MUSHROOM_TREE_RANGE 2
/* Flint patch laid for the first settlers when there is none near them:
 * SPAWN_FLINT_PATCH_STONES flint nodules in the middle, SPAWN_FLINT_PATCH_FALLOFF
 * fewer per step, over SPAWN_FLINT_PATCH_RADIUS steps (Manhattan). */
#define SPAWN_FLINT_PATCH_RADIUS 2
#define SPAWN_FLINT_PATCH_STONES 8
#define SPAWN_FLINT_PATCH_FALLOFF 2

void game_default_params(NewGameParams *params)
{
    worldgen_default_params(&params->world);
    params->colonist_count = GAME_DEFAULT_COLONIST_COUNT;
}

/* ---- Colonist storage ---------------------------------------------------- */

uint32_t game_colonist_slot_count(const Game *game)
{
    return game->colonist_pool.high_water;
}

EntityId game_colonist_id_at(const Game *game, uint32_t slot)
{
    return pool_id_at(&game->colonist_pool, slot);
}

Colonist *game_colonist(Game *game, EntityId id)
{
    return pool_is_alive(&game->colonist_pool, id) ? &game->colonists[id.index] : NULL;
}

const Colonist *game_colonist_const(const Game *game, EntityId id)
{
    return pool_is_alive(&game->colonist_pool, id) ? &game->colonists[id.index] : NULL;
}

bool game_tile_has_colonist(const Game *game, int x, int y, int z)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (entity_id_is_none(game_colonist_id_at(game, slot))) {
            continue;
        }
        const Colonist *colonist = &game->colonists[slot];
        if (colonist->x == x && colonist->y == y && colonist->z == z) {
            return true;
        }
    }
    return false;
}

static bool name_in_use(const Game *game, const char *name)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            strcmp(game->colonists[slot].name, name) == 0) {
            return true;
        }
    }
    return false;
}

/* Random name, avoiding duplicates while unused names remain. */
static const char *pick_name(Game *game)
{
    size_t count = colonist_name_count();
    size_t start = rng_range_u32(&game->rng, (uint32_t)count);
    for (size_t i = 0; i < count; i++) {
        const char *name = colonist_name_at((start + i) % count);
        if (!name_in_use(game, name)) {
            return name;
        }
    }
    return colonist_name_at(start);
}

EntityId game_spawn_colonist(Game *game, int x, int y, int z)
{
    if (!map_is_passable(&game->map, x, y, z)) {
        return ENTITY_NONE;
    }
    const char *name = pick_name(game);
    EntityId id = pool_alloc(&game->colonist_pool);
    if (entity_id_is_none(id)) {
        return ENTITY_NONE;
    }
    Colonist *colonist = &game->colonists[id.index];
    colonist_init(colonist, name, rng_range_i32(&game->rng, 18, 60), x, y, z);
    colonist->needs[NEED_FOOD] = 0.55f + 0.35f * rng_next_float(&game->rng);
    colonist->needs[NEED_WATER] = 0.6f + 0.4f * rng_next_float(&game->rng);
    colonist->needs[NEED_REST] = 0.6f + 0.4f * rng_next_float(&game->rng);
    colonist->timer = rng_range_i32(&game->rng, 0, COLONIST_IDLE_MAX_TICKS);
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        if (knowledge_def((KnowledgeType)knowledge)->starting) {
            colonist_set_knowledge_level(colonist, (KnowledgeType)knowledge,
                                         knowledge_random_starting_level(&game->rng));
        }
    }
    colonist_set_strength(colonist, colonist_random_strength(&game->rng, colonist->age));
    colonist_reset_practice(colonist, game->clock.tick);
    /* Looks come from their own stream: adding a sprite variant must not
     * change what the simulation does. */
    Rng looks;
    rng_seed(&looks, game->seed ^ GAME_APPEARANCE_SEED,
             ((uint64_t)id.generation << 32) | id.index);
    colonist->appearance = appearance_roll(&looks, colonist->age);
    return id;
}

/* Reservation owners are colonist slots + 1 and must fit in a byte. */
_Static_assert(COLONIST_MAX < 255, "colonist slots must fit in reservation owners");

static ColonistContext colonist_context(Game *game, uint32_t slot)
{
    ColonistContext context = {
        .map = &game->map,
        .designations = &game->designations,
        .dirt = &game->dirt,
        .gather_zones = &game->gather_zones,
        .items = &game->items,
        .stockpiles = &game->stockpiles,
        .workstations = &game->workstations,
        .constructions = &game->constructions,
        .beds = &game->beds,
        .reservations = &game->reservations,
        .pathfinder = &game->pathfinder,
        .regions = &game->regions,
        .stock = game->stock,
        .rng = &game->rng,
        .tick = game->clock.tick,
        .night = colonist_is_night(game->clock.tick),
        .worker_id = (uint8_t)(slot + 1),
        .colonists = game->colonists,
        .colonist_pool = &game->colonist_pool,
        .teach = game->teach,
        .temperatures = &game->temperatures,
        .wetting = weather_wetting(&game->weather, game->clock.tick),
        .free_crafts = game->debug.free_crafts,
        .no_tools = game->debug.no_tools,
    };
    return context;
}

bool game_remove_colonist(Game *game, EntityId id)
{
    Colonist *colonist = game_colonist(game, id);
    if (colonist == NULL) {
        return false;
    }
    ColonistContext context = colonist_context(game, id.index);
    colonist_abandon_task(colonist, &context); /* Frees its reservations. */
    beds_release_owner(&game->beds, (uint8_t)(id.index + 1));
    colonist_drop_tools(colonist, &game->items, &game->map);
    colonist_free(colonist);
    memset(colonist, 0, sizeof(*colonist));
    return pool_release(&game->colonist_pool, id);
}

/* ---- Starting colonists -------------------------------------------------- */

static bool is_drinking_spot(const Map *map, int x, int y, int z, void *userdata)
{
    (void)userdata;
    return map_is_drinking_spot(map, x, y, z);
}

static bool map_has_water(const Map *map)
{
    for (int z = 0; z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                if (map_is_water(map, x, y, z)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* Passable tile near the map center with enough reachable land around it,
 * preferably on ground where berry bushes grow and close to drinkable water. */
static bool find_spawn_center(Game *game, TilePos *out)
{
    Map *map = &game->map;
    int center_x = map->width / 2;
    int center_y = map->height / 2;
    bool want_water = map_has_water(map);
    bool have_fallback = false;
    TilePos fallback = {0, 0, 0};
    for (int attempt = 0; attempt < SPAWN_MAX_ATTEMPTS; attempt++) {
        int radius = 2 + attempt / 2;
        int x = clampi(center_x + rng_range_i32(&game->rng, -radius, radius), 0, map->width - 1);
        int y = clampi(center_y + rng_range_i32(&game->rng, -radius, radius), 0, map->height - 1);
        int z = map_ground_level(map, x, y);
        if (!map_is_passable(map, x, y, z)) {
            continue;
        }
        TilePos candidate = {(int16_t)x, (int16_t)y, (int16_t)z};
        if (pathfinder_reachable_count(&game->pathfinder, map, candidate, SPAWN_AREA_COST) <
            SPAWN_AREA_MIN_TILES) {
            continue;
        }
        bool fertile = terrain_def((Terrain)map->terrain[map_index(map, x, y, z)])->fertility >=
                       plant_def(PLANT_BRAMBLE)->min_fertility;
        if (fertile && (!want_water || pathfinder_find_nearest(&game->pathfinder, map, candidate,
                                                               is_drinking_spot, NULL,
                                                               SPAWN_WATER_MAX_COST, NULL,
                                                               NULL))) {
            *out = candidate;
            return true;
        }
        if (!have_fallback) {
            have_fallback = true;
            fallback = candidate;
        }
    }
    if (have_fallback) {
        LOG_WARN("No fertile spawn area close to water found");
        *out = fallback;
        return true;
    }
    /* Tiny or very rugged map: settle for any passable tile. */
    for (int z = 0; z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                if (map_is_passable(map, x, y, z)) {
                    *out = (TilePos){(int16_t)x, (int16_t)y, (int16_t)z};
                    return true;
                }
            }
        }
    }
    return false;
}

/* A food plant of a kind, grown enough to bear food in its season. */
typedef struct FoodPlantCount {
    PlantType type;
    int count;
} FoodPlantCount;

/* Counts the food plants it visits and never accepts a tile, so that the
 * search covers the whole area. */
static bool count_food_plant(const Map *map, int x, int y, int z, void *userdata)
{
    FoodPlantCount *count = userdata;
    const Plant *plant = &map->plants[map_index(map, x, y, z)];
    if (plant->type == count->type &&
        plant->growth >= plant_def(count->type)->min_food_growth) {
        count->count++;
    }
    return false;
}

typedef struct PlantSpotSearch {
    TilePos center;
    Rng *rng;
    float chance;  /* Of taking a suitable spot. */
    PlantType type; /* What is planted. */
} PlantSpotSearch;

/* Where a food plant can be planted: fertile enough open ground with at most
 * grass on it, not next to the arrival point. Each suitable spot is only
 * taken with some chance (drawn in the search order, which is deterministic)
 * so that the plants end up scattered. */
static bool is_plant_spot(const Map *map, int x, int y, int z, void *userdata)
{
    PlantSpotSearch *search = userdata;
    size_t index = map_index(map, x, y, z);
    const Plant *plant = &map->plants[index];
    if (map->rock[index] != ROCK_NONE || map_is_water(map, x, y, z) ||
        (plant->type != PLANT_NONE && plant->type != PLANT_GRASS) ||
        terrain_def((Terrain)map->terrain[index])->fertility <
            plant_def(search->type)->min_fertility ||
        abs(x - search->center.x) + abs(y - search->center.y) < SPAWN_PLANT_MIN_DISTANCE) {
        return false;
    }
    return search->chance >= 1.0f || rng_next_float(search->rng) < search->chance;
}

/* The first settlers bring no food: plants fully grown food plants of a kind
 * near their arrival point until there are `wanted` within reach. */
static void ensure_food_plants_near(Game *game, TilePos center, PlantType type, int wanted)
{
    FoodPlantCount count = {type, 0};
    pathfinder_find_nearest(&game->pathfinder, &game->map, center, count_food_plant, &count,
                            SPAWN_PLANT_MAX_COST, NULL, NULL);
    PlantSpotSearch search = {center, &game->rng, SPAWN_PLANT_SPOT_CHANCE, type};
    int day = game_day_of_year(game->clock.tick);
    for (int planted = count.count; planted < wanted; planted++) {
        TilePos spot;
        bool found = pathfinder_find_nearest(&game->pathfinder, &game->map, center,
                                             is_plant_spot, &search, SPAWN_PLANT_MAX_COST, NULL,
                                             &spot);
        if (!found) {
            /* Few suitable spots: take the nearest one. */
            search.chance = 1.0f;
            found = pathfinder_find_nearest(&game->pathfinder, &game->map, center, is_plant_spot,
                                            &search, SPAWN_PLANT_MAX_COST, NULL, &spot);
        }
        if (!found) {
            LOG_WARN("No room for food plants near the starting colonists");
            return;
        }
        Plant *plant = &game->map.plants[map_index(&game->map, spot.x, spot.y, spot.z)];
        memset(plant, 0, sizeof(*plant));
        plant->type = (uint8_t)type;
        plant->growth = 1.0f;
        plant_settle(plant, day);
    }
}

/* A tile of a rich enough flint patch where stones can be gathered. */
static bool is_flint_spot(const Map *map, int x, int y, int z, void *userdata)
{
    (void)userdata;
    return map_stones(map, x, y, z, ITEM_FLINT) >= GAME_SPAWN_FLINT_STONES &&
           gather_stone_ground(map, x, y, z);
}

typedef struct PatchSpotSearch {
    TilePos center;
    Rng *rng;
    float chance; /* Of taking a suitable spot. */
} PatchSpotSearch;

/* Where a flint patch can be laid: ground where stones can be gathered, not
 * next to the arrival point; scattered like food plants (see is_plant_spot()). */
static bool is_patch_spot(const Map *map, int x, int y, int z, void *userdata)
{
    PatchSpotSearch *search = userdata;
    if (!gather_stone_ground(map, x, y, z) ||
        abs(x - search->center.x) + abs(y - search->center.y) < SPAWN_PLANT_MIN_DISTANCE) {
        return false;
    }
    return search->chance >= 1.0f || rng_next_float(search->rng) < search->chance;
}

/* The first settlers need flint to knap: lays a small flint patch near their
 * arrival point when there is none within reach. */
static void ensure_flint_near(Game *game, TilePos center)
{
    Map *map = &game->map;
    if (pathfinder_find_nearest(&game->pathfinder, map, center, is_flint_spot, NULL,
                                SPAWN_PLANT_MAX_COST, NULL, NULL)) {
        return;
    }
    PatchSpotSearch search = {center, &game->rng, SPAWN_PLANT_SPOT_CHANCE};
    TilePos spot;
    if (!pathfinder_find_nearest(&game->pathfinder, map, center, is_patch_spot, &search,
                                 SPAWN_PLANT_MAX_COST, NULL, &spot)) {
        search.chance = 1.0f;
        if (!pathfinder_find_nearest(&game->pathfinder, map, center, is_patch_spot, &search,
                                     SPAWN_PLANT_MAX_COST, NULL, &spot)) {
            LOG_WARN("No room for flint near the starting colonists");
            return;
        }
    }
    /* Richest in the middle, fading over SPAWN_FLINT_PATCH_RADIUS tiles. */
    for (int dy = -SPAWN_FLINT_PATCH_RADIUS; dy <= SPAWN_FLINT_PATCH_RADIUS; dy++) {
        for (int dx = -SPAWN_FLINT_PATCH_RADIUS; dx <= SPAWN_FLINT_PATCH_RADIUS; dx++) {
            int x = spot.x + dx, y = spot.y + dy, z = spot.z;
            int distance = abs(dx) + abs(dy);
            if (distance > SPAWN_FLINT_PATCH_RADIUS ||
                !gather_stone_ground(map, x, y, z)) {
                continue;
            }
            int flint = SPAWN_FLINT_PATCH_STONES - SPAWN_FLINT_PATCH_FALLOFF * distance;
            if (flint > map_stones(map, x, y, z, ITEM_FLINT)) {
                /* Some cobbles make room for the flint. */
                map_set_stones(map, x, y, z, map_stones(map, x, y, z, ITEM_COBBLE), flint);
            }
        }
    }
}

static bool is_free_tile(const Map *map, int x, int y, int z, void *userdata)
{
    return map_is_passable(map, x, y, z) && !game_tile_has_colonist(userdata, x, y, z);
}

static void spawn_starting_colonists(Game *game, int count)
{
    TilePos center;
    if (count <= 0 || !find_spawn_center(game, &center)) {
        return;
    }
    ensure_food_plants_near(game, center, PLANT_BRAMBLE, GAME_SPAWN_BRAMBLES);
    ensure_food_plants_near(game, center, PLANT_HAZEL, GAME_SPAWN_HAZELS);
    ensure_flint_near(game, center);
    for (int i = 0; i < count; i++) {
        TilePos tile;
        if (!pathfinder_find_nearest(&game->pathfinder, &game->map, center, is_free_tile, game,
                                     SPAWN_PLACEMENT_MAX_COST, NULL, &tile)) {
            LOG_WARN("Not enough room to place every starting colonist");
            return;
        }
        game_spawn_colonist(game, tile.x, tile.y, tile.z);
    }
}

/* ---- Lifecycle ----------------------------------------------------------- */

bool game_init(Game *game, const NewGameParams *params)
{
    memset(game, 0, sizeof(*game));
    if (!worldgen_generate(&game->map, &params->world) ||
        !designations_init(&game->designations, game->map.width, game->map.height,
                           game->map.depth) ||
        !dirt_init(&game->dirt, game->map.width, game->map.height, game->map.depth) ||
        !gather_zones_init(&game->gather_zones, game->map.width, game->map.height,
                           game->map.depth) ||
        !items_init(&game->items, game->map.width, game->map.height, game->map.depth) ||
        !stockpiles_init(&game->stockpiles, game->map.width, game->map.height, game->map.depth) ||
        !workstations_init(&game->workstations, game->map.width, game->map.height,
                           game->map.depth) ||
        !constructions_init(&game->constructions, game->map.width, game->map.height,
                            game->map.depth) ||
        !beds_init(&game->beds, game->map.width, game->map.height, game->map.depth) ||
        !pathfinder_init(&game->pathfinder, game->map.width, game->map.height, game->map.depth) ||
        !regions_init(&game->regions, game->map.width, game->map.height, game->map.depth) ||
        !rooms_init(&game->rooms, game->map.width, game->map.height, game->map.depth) ||
        !pool_init(&game->colonist_pool, COLONIST_MAX)) {
        game_shutdown(game);
        return false;
    }
    game->seed = params->world.seed;
    reservations_init(&game->reservations);
    game_clock_init(&game->clock);
    rng_seed(&game->rng, params->world.seed, 0x73696dULL /* "sim" */);
    roof_watch_init(&game->roof_watch, &game->map);
    weather_init(&game->weather, game->seed, game->clock.tick);
    if (!temperatures_init(&game->temperatures, &game->map,
                           weather_outdoor(&game->weather, game->clock.tick))) {
        game_shutdown(game);
        return false;
    }
    spawn_starting_colonists(game, params->colonist_count);
    regions_update(&game->regions, &game->map); /* Built here, not during the first tick. */
    game_update_stock(game);
    game_count_knowledge(game); /* Who knows what at the start: no notice for it. */
    return true;
}

/* Whether some bill counts the stock. */
static bool stock_counted(const Game *game)
{
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = workstation_get_const(&game->workstations, index);
        for (int bill = 0; station != NULL && bill < station->bill_count; bill++) {
            if (station->bills[bill].mode == BILL_UNTIL_STOCK) {
                return true;
            }
        }
    }
    return false;
}

void game_update_stock(Game *game)
{
    memset(game->stock, 0, sizeof(game->stock));
    stockpiles_count_stored(&game->stockpiles, &game->items, game->stock);
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = workstation_get_const(&game->workstations, index);
        if (station == NULL) {
            continue;
        }
        ItemStack stack = items_get(&game->items, station->x, station->y, station->z);
        if (stack.count > 0 && stockpile_storage_priority(&game->stockpiles, station->x, station->y,
                                                          station->z, stack) == 0) {
            game->stock[stack.type] += stack.count; /* Not counted as stored already. */
        }
    }
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            colonist->task == TASK_HAUL && colonist->carried.count > 0) {
            game->stock[colonist->carried.type] += colonist->carried.count;
        }
    }
}

void game_shutdown(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        colonist_free(&game->colonists[slot]);
    }
    pool_free(&game->colonist_pool);
    pathfinder_free(&game->pathfinder);
    regions_free(&game->regions);
    rooms_free(&game->rooms);
    temperatures_free(&game->temperatures);
    designations_free(&game->designations);
    dirt_free(&game->dirt);
    gather_zones_free(&game->gather_zones);
    items_free(&game->items);
    stockpiles_free(&game->stockpiles);
    workstations_free(&game->workstations);
    constructions_free(&game->constructions);
    beds_free(&game->beds);
    map_free(&game->map);
    memset(game, 0, sizeof(*game));
}

/* ---- Simulation ---------------------------------------------------------- */

/* A number in [0, 1) drawn from the seed, a tile, a day and `salt`: chance
 * events of the world that do not draw from (and so do not shift) the game's
 * random stream. */
static float world_roll(uint64_t seed, size_t tile, int day, uint32_t salt)
{
    uint64_t key[3] = {seed, (uint64_t)tile, ((uint64_t)(uint32_t)day << 32) | salt};
    uint64_t hash = hash_fnv1a64(HASH_FNV1A64_INIT, key, sizeof(key));
    hash ^= hash >> 33; /* Mixes the last bytes into the high bits. */
    hash *= 0xff51afd7ed558ccdULL;
    hash ^= hash >> 33;
    return (float)(hash >> 40) / (float)(1u << 24);
}

/* Chance that a mushroom patch appears on a tile today: more after rain. */
static float mushroom_chance(const Weather *weather)
{
    return MUSHROOM_CHANCE *
           fminf(1.0f + weather->recent_rain / MUSHROOM_RAIN_HOURS, MUSHROOM_RAIN_MAX);
}

/* Once a day per tile: mushroom patches come and go under the trees. */
static void mushrooms_day(Game *game, size_t index, int day)
{
    Map *map = &game->map;
    Plant *plant = &map->plants[index];
    if (plant->type == PLANT_MUSHROOMS) {
        if (world_roll(game->seed, index, day, 1) < MUSHROOM_VANISH_CHANCE) {
            int x, y, z;
            map_tile_of(map, index, &x, &y, &z);
            map_remove_plant(map, x, y, z);
        }
        return;
    }
    if ((plant->type != PLANT_NONE && plant->type != PLANT_GRASS) ||
        !plant_days_contain(plant_def(PLANT_MUSHROOMS)->food_days, day) ||
        world_roll(game->seed, index, day, 0) >= mushroom_chance(&game->weather)) {
        return;
    }
    int x, y, z;
    map_tile_of(map, index, &x, &y, &z);
    Terrain terrain = (Terrain)map->terrain[index];
    if ((terrain != TERRAIN_SOIL && terrain != TERRAIN_RICH_SOIL) ||
        map->rock[index] != ROCK_NONE || map->structure[index] != STRUCTURE_NONE ||
        construction_at(&game->constructions, x, y, z) != CONSTRUCTION_NONE ||
        workstation_at(&game->workstations, x, y, z) >= 0 ||
        !map_tree_near(map, x, y, z, MUSHROOM_TREE_RANGE)) {
        return;
    }
    *plant = (Plant){.type = PLANT_MUSHROOMS, .growth = 1.0f};
    plant_settle(plant, day);
}

/* Whether an item or a settler is on a tile the trunk of a tree of `shape`
 * on (x, y, z) would grow into. */
static bool tree_in_the_way(const Game *game, int x, int y, int z, const TreeShape *shape)
{
    const Map *map = &game->map;
    for (int dz = 0; dz < shape->trunk; dz++) {
        for (int dy = 0; dy < shape->size; dy++) {
            for (int dx = 0; dx < shape->size; dx++) {
                int tx = x + dx, ty = y + dy, tz = z + dz;
                if (map_in_bounds(map, tx, ty, tz) &&
                    map->rock[map_index(map, tx, ty, tz)] != ROCK_WOOD &&
                    (items_get(&game->items, tx, ty, tz).count > 0 ||
                     game_tile_has_colonist(game, tx, ty, tz))) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* A tree whose shape changed as it grew (or that takes room and is not in
 * the map yet) is written into the map; one whose trunk has no room, or
 * would grow into something, waits at its stage (`old_growth`). */
static void grow_tree(Game *game, size_t index, float old_growth)
{
    Map *map = &game->map;
    Plant *plant = &map->plants[index];
    TreeShape shape, before;
    if (!tree_shape(plant, &shape)) {
        return;
    }
    Plant old = *plant;
    old.growth = old_growth;
    bool had_shape = tree_shape(&old, &before);
    bool built = map->rock[index] == ROCK_WOOD;
    if (built && had_shape && before.trunk == shape.trunk) {
        return;
    }
    int x, y, z;
    map_tile_of(map, index, &x, &y, &z);
    if (!tree_room_to_grow(map, x, y, z, &shape) || tree_in_the_way(game, x, y, z, &shape)) {
        if (built || !had_shape) {
            plant->growth = old_growth;
        }
        return;
    }
    tree_build(map, x, y, z);
    /* No stockpile on a trunk. */
    stockpiles_remove_rect(&game->stockpiles, x, y, x + shape.size - 1, y + shape.size - 1, z);
}

/*
 * Every plant grows once per GAME_PLANT_UPDATE_INTERVAL ticks, but not all in
 * the same tick: each tick handles the next slice of the ground levels, so that a large
 * map does not stall the simulation once per in-game hour. In the first hour
 * of the day, each tile also has its daily events (mushrooms).
 */
/* Tiles that may hold plants: the levels up to the highest ground
 * (Map.top_ground_level), not the open space and foliage over it. */
static uint64_t ground_tile_count(const Map *map)
{
    return (uint64_t)map_level_tile_count(map) * (uint64_t)(map->top_ground_level + 1);
}

static void update_plants(Game *game)
{
    Map *map = &game->map;
    uint64_t count = ground_tile_count(map);
    uint64_t phase = game->clock.tick % GAME_PLANT_UPDATE_INTERVAL;
    size_t begin = (size_t)(phase * count / GAME_PLANT_UPDATE_INTERVAL);
    size_t end = (size_t)((phase + 1) * count / GAME_PLANT_UPDATE_INTERVAL);
    int day = game_day_of_year(game->clock.tick);
    bool first_hour = game_hour_from_tick(game->clock.tick) == 0;

    for (size_t i = begin; i < end; i++) {
        if (first_hour) {
            mushrooms_day(game, i, day);
        }
        Plant *plant = &map->plants[i];
        if (plant->type != PLANT_NONE) {
            float old_growth = plant->growth;
            plant_advance(plant, terrain_def((Terrain)map->terrain[i])->fertility,
                          GAME_PLANT_UPDATE_INTERVAL / TICKS_PER_HOUR, day);
            if (plant->type == PLANT_TREE) {
                grow_tree(game, i, old_growth);
            } else if (plant->type == PLANT_NONE && map->forbidden_count > 0) {
                int x, y, z; /* Vanished: what was forbidden of it goes too. */
                map_tile_of(map, i, &x, &y, &z);
                map_remove_plant(map, x, y, z);
            }
        }
    }
}

float game_item_rot_rate(const Game *game, int x, int y, int z)
{
    bool rains = weather_wetting(&game->weather, game->clock.tick) > 0.0f;
    return temperature_rot_rate(&game->temperatures, &game->map, x, y, z, rains);
}

float game_daily_food(const Game *game)
{
    return gather_daily_food((int)game->colonist_pool.count);
}

float game_outdoor_temperature(const Game *game)
{
    return weather_outdoor(&game->weather, game->clock.tick);
}

/* ItemConditionsFn for items_advance(): out in the rain, food rots faster
 * and nothing dries. */
static ItemConditions tile_conditions(const void *context, size_t tile)
{
    const Game *game = context;
    int x, y, z;
    map_tile_of(&game->map, tile, &x, &y, &z);
    bool rains = weather_wetting(&game->weather, game->clock.tick) > 0.0f;
    bool open = temperature_cover(&game->map, x, y, z) == TEMPERATURE_COVER_OPEN;
    return (ItemConditions){temperature_rot_rate(&game->temperatures, &game->map, x, y, z, rains),
                            !(rains && open)};
}

/* Items lying on the ground change over time (fresh grass dries, food
 * rots), slice by slice like plants, over every level (foliage holds items
 * too; levels without any are skipped). */
static void update_items(Game *game)
{
    uint64_t count = map_tile_count(&game->map);
    uint64_t phase = game->clock.tick % GAME_ITEM_UPDATE_INTERVAL;
    size_t begin = (size_t)(phase * count / GAME_ITEM_UPDATE_INTERVAL);
    size_t end = (size_t)((phase + 1) * count / GAME_ITEM_UPDATE_INTERVAL);
    items_advance(&game->items, begin, end, GAME_ITEM_UPDATE_INTERVAL / TICKS_PER_HOUR,
                  tile_conditions, game);
}

static void update_colonists(Game *game)
{
    /* Fixed slot order keeps the simulation deterministic. */
    ColonistContext context = colonist_context(game, 0); /* The same for everyone... */
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
            context.worker_id = (uint8_t)(slot + 1); /* ...but for who is acting. */
            /* Someone before may have changed the map (a felled tree, a new wall): stale
             * regions would make every search pre-check answer "maybe". */
            regions_update(&game->regions, &game->map);
            Colonist *colonist = &game->colonists[slot];
            colonist_tick(colonist, &context);
            if (colonist->mood.break_started) {
                static const NoticeType NOTICES[MOOD_BREAK_COUNT] = {
                    [MOOD_BREAK_SULK] = NOTICE_BREAK_SULK,
                    [MOOD_BREAK_WANDER_OFF] = NOTICE_BREAK_WANDER_OFF,
                    [MOOD_BREAK_BINGE] = NOTICE_BREAK_BINGE,
                };
                colonist->mood.break_started = false;
                notices_push(&game->notices, NOTICES[colonist->mood.break_type], KNOWLEDGE_NONE,
                             colonist->name, game->clock.tick);
            }
        }
    }
}

/* Unpractised knowledges fade a little, once a day. */
static void fade_knowledge(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
            colonist_fade_knowledge(&game->colonists[slot], game->clock.tick);
        }
    }
}

/* Burning hearths warm their tile, every tick. */
static void heat_from_hearths(Game *game)
{
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = &game->workstations.list[index];
        if (station->used && station->burning) {
            temperatures_add_heat(&game->temperatures, station->x, station->y, station->z,
                                  HEARTH_HEAT_PER_HOUR / (float)TICKS_PER_HOUR);
        }
    }
}

/* GameDebug.instant_build: every site that can be finished is, plants in
 * the way cleared first. What it built leaves zones, as a builder's work
 * does. */
static void finish_constructions_now(Game *game)
{
    for (int index = 0; index < CONSTRUCTION_MAX; index++) {
        const ConstructionSite *site = construction_get(&game->constructions, index);
        if (site == NULL) {
            continue;
        }
        if (construction_forbidden(site, &game->map)) {
            continue; /* Left alone, as by builders. */
        }
        ConstructionSite done = *site; /* Gone from the list once built. */
        int length = construction_length(&done);
        for (int part = 0; part < length; part++) {
            int x, y, z;
            construction_tile(&done, part, &x, &y, &z);
            if (gather_clear_work(&game->map, x, y, z) > 0.0f) {
                gather_clear_plant(&game->map, &game->items, x, y, z, GAME_DEBUG_BUILD_LEVEL, NULL);
            }
        }
        if (!construction_finish_now(&game->constructions, &game->map, &game->items,
                                     &game->workstations, index, GAME_DEBUG_BUILD_LEVEL, NULL)) {
            continue;
        }
        bool takes_tiles = construction_workstation(&done) != WORKSTATION_TYPE_COUNT ||
                           structure_blocks((StructureType)done.type);
        for (int part = 0; takes_tiles && part < length; part++) {
            int x, y, z;
            construction_tile(&done, part, &x, &y, &z);
            stockpiles_remove_rect(&game->stockpiles, x, y, x, y, z);
            gather_zones_remove_rect(&game->gather_zones, x, y, x, y, z);
        }
    }
}

void game_set_instant_build(Game *game, bool on)
{
    game->debug.instant_build = on;
}

void game_set_free_crafts(Game *game, bool on)
{
    game->debug.free_crafts = on;
}

void game_set_no_tools(Game *game, bool on)
{
    game->debug.no_tools = on;
}

void game_set_instant_deconstruct(Game *game, bool on)
{
    game->debug.instant_deconstruct = on;
}

void game_set_instant_fell_and_mine(Game *game, bool on)
{
    game->debug.instant_fell_and_mine = on;
}

void game_debug_next_weather(Game *game)
{
    game->weather.type = (uint8_t)((game->weather.type + 1u) % WEATHER_TYPE_COUNT);
}

void game_debug_cold_snap(Game *game)
{
    game->weather.spell = -WEATHER_SPELL_MAX;
}

void game_debug_sadden(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
            Mood *mood = &game->colonists[slot].mood;
            mood_remember(mood, THOUGHT_DEBUG_GLOOM);
            mood->break_cooldown = 0;
        }
    }
}

void game_debug_learn_all(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (entity_id_is_none(game_colonist_id_at(game, slot))) {
            continue;
        }
        Colonist *colonist = &game->colonists[slot];
        for (int k = 0; k < KNOWLEDGE_COUNT; k++) {
            if (colonist->knowledge[k] < GAME_DEBUG_KNOWLEDGE_LEVEL) {
                colonist_set_knowledge_level(colonist, (KnowledgeType)k,
                                             GAME_DEBUG_KNOWLEDGE_LEVEL);
            }
            colonist->knowledge_practice[k] = game->clock.tick;
        }
    }
}

void game_debug_skip(Game *game, uint64_t ticks)
{
    for (uint64_t i = 0; i < ticks; i++) {
        game_tick(game);
    }
}

void game_debug_next_season(Game *game)
{
    uint64_t tick = game->clock.tick;
    uint64_t season_ticks = (uint64_t)DAYS_PER_SEASON * TICKS_PER_DAY;
    uint64_t time_of_day = tick % TICKS_PER_DAY;
    game->clock.tick = (tick / season_ticks + 1) * season_ticks + time_of_day;
}

int game_debug_spawn_items(Game *game, int x, int y, int z, ItemType type, int count)
{
    if (type == ITEM_NONE || (unsigned)type >= ITEM_TYPE_COUNT || count <= 0 ||
        !map_is_passable(&game->map, x, y, z)) {
        return 0;
    }
    int placed = 0;
    while (placed < count) { /* One stack at a time: drops spread past a full tile. */
        int limit = item_stack_limit(type);
        int batch = count - placed < limit ? count - placed : limit;
        ItemStack stack = item_stack_make(type, batch, ITEM_QUALITY_STANDARD,
                                          item_def(type)->max_hit_points);
        int dropped = items_drop(&game->items, &game->map, x, y, z, stack);
        placed += dropped;
        if (dropped < batch) {
            break; /* No room left around. */
        }
    }
    return placed;
}

bool game_debug_teleport(Game *game, EntityId id, int x, int y, int z)
{
    Colonist *colonist = game_colonist(game, id);
    if (colonist == NULL || !map_is_passable(&game->map, x, y, z)) {
        return false;
    }
    ColonistContext context = colonist_context(game, id.index);
    colonist_abandon_task(colonist, &context); /* Frees its reservations. */
    colonist->x = x;
    colonist->y = y;
    colonist->z = z;
    colonist->path_index = 0;
    colonist->move_progress = 0.0f;
    for (int kind = 0; kind < SEARCH_COUNT; kind++) { /* Somewhere new: look again. */
        colonist->search_cooldown[kind] = 0;
        colonist->search_failures[kind] = 0;
        colonist->search_quiet[kind] = 0;
    }
    return true;
}

/* GameDebug.instant_deconstruct: every order to take a structure apart (or
 * a roof down) is carried out, as a settler's work would (a refresh site on a
 * structure taken apart goes too, its materials dropped). */
static void deconstruct_now(Game *game)
{
    Designations *designations = &game->designations;
    Map *map = &game->map;
    if (designations->count_by_work[WORK_CONSTRUCTION] == 0) {
        return;
    }
    for (int z = 0; z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                DesignationType type = designation_get(designations, x, y, z);
                if (type != DESIGNATION_DECONSTRUCT ||
                    designation_forbidden(designations, map, x, y, z)) {
                    continue;
                }
                if (!designation_can_apply(map, x, y, z, type)) {
                    designation_set(designations, map, x, y, z, DESIGNATION_NONE);
                    continue;
                }
                int site = construction_at(&game->constructions, x, y, z);
                designation_add_work(designations, map, &game->items, x, y, z,
                                     designation_work_needed(designations, map, x, y, z),
                                     GAME_DEBUG_BUILD_LEVEL, NULL);
                const ConstructionSite *refresh = construction_get(&game->constructions, site);
                if (refresh != NULL && refresh->refresh &&
                    map_structure(map, refresh->x, refresh->y, refresh->z) !=
                        (StructureType)refresh->type) {
                    game_remove_construction(game, site);
                }
            }
        }
    }
}

/* GameDebug.instant_fell_and_mine: every order to fell a tree or to mine
 * rock is carried out, as a settler's work would (rock holding a ceiling up
 * waits); orders left with nothing to do go. */
static void fell_and_mine_now(Game *game)
{
    Designations *designations = &game->designations;
    Map *map = &game->map;
    if (designations->count_by_work[WORK_WOODCUTTING] == 0 &&
        designations->count_by_work[WORK_MINING] == 0) {
        return;
    }
    for (int z = 0; z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                DesignationType type = designation_get(designations, x, y, z);
                if (type != DESIGNATION_CHOP && type != DESIGNATION_MINE) {
                    continue;
                }
                if (!designation_can_apply(map, x, y, z, type)) {
                    designation_set(designations, map, x, y, z, DESIGNATION_NONE);
                    continue;
                }
                if (designation_waits_for_support(designations, map, x, y, z) ||
                    designation_forbidden(designations, map, x, y, z)) {
                    continue;
                }
                designation_add_work(designations, map, &game->items, x, y, z,
                                     designation_work_needed(designations, map, x, y, z),
                                     GAME_DEBUG_BUILD_LEVEL, NULL);
            }
        }
    }
}

/* Drops a stack on the first passable tile beside (x, y, z), if any. */
static void drop_beside(Game *game, int x, int y, int z, ItemStack stack)
{
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    for (int i = 0; stack.count > 0 && i < 4; i++) {
        if (map_is_passable(&game->map, x + DX[i], y + DY[i], z)) {
            items_drop(&game->items, &game->map, x + DX[i], y + DY[i], z, stack);
            return;
        }
    }
}

/* A gallery filled up again: the sites, stations and zones on it go (and
 * the site of a roof over it; what they held is put beside it). Settlers
 * caught there step out by themselves (see colonist_tick()). */
static void clear_caved_in(Game *game, TilePos tile)
{
    int x = tile.x, y = tile.y, z = tile.z;
    for (int overhead = 0; overhead <= 1; overhead++) {
        int site = overhead ? construction_worked_from(&game->constructions, x, y, z, true)
                            : construction_at(&game->constructions, x, y, z);
        ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
        int count = site != CONSTRUCTION_NONE
                        ? constructions_remove(&game->constructions, site, returned)
                        : 0;
        for (int i = 0; i < count; i++) {
            drop_beside(game, x, y, z, returned[i]);
        }
    }
    int station = workstation_at(&game->workstations, x, y, z);
    if (station != WORKSTATION_NONE) {
        ItemStack returned[1];
        int count = workstations_remove(&game->workstations, station, returned);
        for (int i = 0; i < count; i++) {
            drop_beside(game, x, y, z, returned[i]);
        }
    }
    stockpiles_remove_rect(&game->stockpiles, x, y, x, y, z);
    gather_zones_remove_rect(&game->gather_zones, x, y, x, y, z);
}

void game_tick(Game *game)
{
    game->clock.tick++;
    weather_tick(&game->weather, game->clock.tick);
    uint32_t harmful_falls =
        game->gather_zones.harmful_tree_falls + game->designations.harmful_tree_falls;
    if (game->debug.instant_deconstruct) {
        deconstruct_now(game);
    }
    if (game->debug.instant_fell_and_mine) {
        fell_and_mine_now(game);
    }
    if (game->debug.instant_build) {
        finish_constructions_now(game);
    }
    regions_update(&game->regions, &game->map); /* Nothing to do unless the map changed. */
    if (stock_counted(game)) {
        game_update_stock(game);
    }
    update_plants(game);
    if (game->clock.tick % GAME_PLANT_UPDATE_INTERVAL == 0) {
        /* Every plant grew since the last time: zones may offer new jobs. */
        gather_zones_plants_changed(&game->gather_zones);
    }
    update_items(game);
    hearths_burn(&game->workstations, &game->map,
                 weather_wetting(&game->weather, game->clock.tick));
    heat_from_hearths(game);
    temperatures_tick(&game->temperatures, &game->map, game->clock.tick,
                      weather_outdoor(&game->weather, game->clock.tick));
    if (game->clock.tick % TICKS_PER_DAY == 0) {
        /* Bedding packs down: worn beds get a refresh site, or fall apart. */
        constructions_wear_day(&game->constructions, &game->map, &game->items);
        fade_knowledge(game);
    }
    beds_forget_missing(&game->beds, &game->map); /* Beds taken apart or fallen apart. */
    /* What gathering zones want, from what lies in the colony now. */
    gather_zones_update(&game->gather_zones, game->items.totals, game_daily_food(game));
    update_colonists(game);
    if (game->gather_zones.harmful_tree_falls + game->designations.harmful_tree_falls !=
        harmful_falls) {
        notices_push(&game->notices, NOTICE_TREE_FELL_ON_BUILDING, KNOWLEDGE_NONE, NULL,
                     game->clock.tick);
    }
    /* Walls and props taken apart or fallen apart, rock dug out: roofs and
     * ceilings left without support fall in. */
    CaveIns cave_ins;
    if (roofs_collapse(&game->roof_watch, &game->map, &game->items, &game->designations,
                       &cave_ins) > 0) {
        notices_push(&game->notices, NOTICE_ROOF_COLLAPSED, KNOWLEDGE_NONE, NULL,
                     game->clock.tick);
    }
    for (int i = 0; i < cave_ins.count && i < ROOF_CAVE_IN_MAX; i++) {
        clear_caved_in(game, cave_ins.tiles[i]);
    }
    if (cave_ins.count > 0) {
        notices_push(&game->notices, NOTICE_CAVE_IN, KNOWLEDGE_NONE, NULL, game->clock.tick);
    }
    if (game->clock.tick % CONVERSATION_CHECK_TICKS == 0) {
        conversations_update(game->colonists, &game->colonist_pool, game->clock.tick,
                             &game->rng);
    }
    if (game->clock.tick % GAME_KNOWLEDGE_CHECK_TICKS == 0) {
        game_count_knowledge(game);
    }
}

const Rooms *game_rooms(Game *game)
{
    rooms_update(&game->rooms, &game->map);
    return &game->rooms;
}

int game_update(Game *game, double real_seconds)
{
    int due = game_clock_consume(&game->clock, real_seconds);
    for (int i = 0; i < due; i++) {
        game_tick(game);
    }
    return due;
}

uint64_t game_hash(const Game *game)
{
    uint64_t hash = map_hash(&game->map);
    hash = designations_hash(&game->designations, hash);
    hash = dirt_hash(&game->dirt, hash);
    hash = gather_zones_hash(&game->gather_zones, hash);
    hash = items_hash(&game->items, hash);
    hash = stockpiles_hash(&game->stockpiles, hash);
    hash = workstations_hash(&game->workstations, hash);
    hash = constructions_hash(&game->constructions, hash);
    hash = beds_hash(&game->beds, hash);
    hash = reservations_hash(&game->reservations, hash);
    hash = hash_fnv1a64(hash, &game->clock.tick, sizeof(game->clock.tick));
    hash = hash_fnv1a64(hash, &game->rng.state, sizeof(game->rng.state));
    hash = hash_fnv1a64(hash, game->teach, sizeof(game->teach));
    hash = temperatures_hash(&game->temperatures, hash);
    hash = weather_hash(&game->weather, hash);
    if (game->debug.instant_build || game->debug.free_crafts || game->debug.no_tools ||
        game->debug.instant_deconstruct) {
        uint8_t debug[4] = {game->debug.instant_build, game->debug.free_crafts,
                            game->debug.no_tools, game->debug.instant_deconstruct};
        hash = hash_fnv1a64(hash, debug, sizeof(debug));
    }
    if (game->debug.instant_fell_and_mine) { /* Apart: earlier hashes stay as they were. */
        uint8_t fell_and_mine = 1;
        hash = hash_fnv1a64(hash, &fell_and_mine, sizeof(fell_and_mine));
    }
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id)) {
            continue;
        }
        const Colonist *c = &game->colonists[slot];
        int32_t ints[15] = {(int32_t)id.index,   (int32_t)id.generation, c->x,
                            c->y,                (int32_t)c->task,       c->timer,
                            c->target.x,         c->target.y,            c->carried.type,
                            c->carried.count,    c->carried.quality,     c->carried.hit_points,
                            c->carried.progress, c->haul_destination.x, c->haul_destination.y};
        hash = hash_fnv1a64(hash, ints, sizeof(ints));
        if (game->map.depth != 1) { /* Hashes of one-level games stay as they were. */
            int32_t levels[4] = {c->z, c->target.z, c->haul_destination.z, c->fetch.z};
            hash = hash_fnv1a64(hash, levels, sizeof(levels));
        }
        hash = hash_fnv1a64(hash, &c->move_progress, sizeof(c->move_progress));
        hash = hash_fnv1a64(hash, c->needs, sizeof(c->needs));
        hash = hash_fnv1a64(hash, &c->wetness, sizeof(c->wetness));
        hash = mood_hash(&c->mood, hash);
        int32_t sleep[3] = {c->sleep_ticks, c->sleep_cold_ticks, c->sleep_hot_ticks};
        hash = hash_fnv1a64(hash, sleep, sizeof(sleep));
        hash = hash_fnv1a64(hash, c->work_priorities, sizeof(c->work_priorities));
        hash = hash_fnv1a64(hash, c->knowledge, sizeof(c->knowledge));
        hash = hash_fnv1a64(hash, c->knowledge_peak, sizeof(c->knowledge_peak));
        hash = hash_fnv1a64(hash, c->knowledge_practice, sizeof(c->knowledge_practice));
        int32_t social[8] = {(int32_t)c->mentor.index,       (int32_t)c->mentor.generation,
                             c->assist_work,                 c->assist_wait,
                             c->chat_ticks,                  (int32_t)c->chat_partner.index,
                             (int32_t)c->chat_partner.generation, c->chat_topic};
        hash = hash_fnv1a64(hash, social, sizeof(social));
        hash = hash_fnv1a64(hash, &c->assisted_until, sizeof(c->assisted_until));
        int32_t fire[6] = {c->light_step,       c->fire_source, c->fire_source_number,
                           c->ember_ticks,      c->story_topic, (int32_t)c->fuel_promised};
        hash = hash_fnv1a64(hash, fire, sizeof(fire));
        hash = hash_fnv1a64(hash, &c->light_progress, sizeof(c->light_progress));
        hash = hash_fnv1a64(hash, &c->fuel_promised, sizeof(c->fuel_promised));
        hash = hash_fnv1a64(hash, &c->haul_delivering, sizeof(c->haul_delivering));
        hash = hash_fnv1a64(hash, &c->strength, sizeof(c->strength));
        hash = hash_fnv1a64(hash, &c->max_carry_weight, sizeof(c->max_carry_weight));
        hash = hash_fnv1a64(hash, &c->max_carry_bulk, sizeof(c->max_carry_bulk));
        hash = hash_fnv1a64(hash, c->name, strlen(c->name));
        int32_t job[14] = {c->station,     c->station_number, c->bill,       c->recipe,
                           c->station_phase, c->fetch.x,        c->fetch.y,     c->fetch_tool,
                           c->work_type,     c->gathering,      c->site,        c->site_number,
                           c->supply_slot,   c->supply_promised};
        hash = hash_fnv1a64(hash, job, sizeof(job));
        for (int i = 0; i < COLONIST_TOOL_MAX; i++) {
            ItemStack tool = c->tools[i];
            int32_t fields[5] = {tool.type, tool.count, tool.quality, tool.hit_points,
                                 c->tool_wear[i]};
            hash = hash_fnv1a64(hash, fields, sizeof(fields));
        }
    }
    return hash;
}

/* ---- Knowledge ----------------------------------------------------------- */

bool game_knowledge_discovered(const Game *game, KnowledgeType knowledge)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT) {
        return false;
    }
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            game->colonists[slot].knowledge[knowledge] >= KNOWLEDGE_DISCOVERED_LEVEL) {
            return true;
        }
    }
    return false;
}

int game_knowledge_masters(const Game *game, KnowledgeType knowledge)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT) {
        return 0;
    }
    int masters = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            knowledge_is_mastered(game->colonists[slot].knowledge[knowledge])) {
            masters++;
        }
    }
    return masters;
}

bool game_set_teach(Game *game, KnowledgeType knowledge, bool teach)
{
    if ((unsigned)knowledge >= KNOWLEDGE_COUNT) {
        return false;
    }
    game->teach[knowledge] = teach;
    return true;
}

EntityId game_apprentice_of(const Game *game, EntityId master)
{
    if (game_colonist_const(game, master) == NULL) {
        return ENTITY_NONE;
    }
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        const Colonist *colonist = &game->colonists[slot];
        if (!entity_id_is_none(id) && colonist->task == TASK_ASSIST &&
            entity_id_equal(colonist->mentor, master)) {
            return id;
        }
    }
    return ENTITY_NONE;
}

/* Notices for one knowledge, from the last count to this one. */
static void knowledge_notices(Game *game, KnowledgeType knowledge, int masters, bool known,
                              const Colonist *master, const Colonist *knower)
{
    Notices *notices = &game->notices;
    uint64_t tick = game->clock.tick;
    int before = game->knowledge_masters[knowledge];
    if (known && !game->knowledge_known[knowledge]) {
        notices_push(notices, NOTICE_KNOWLEDGE_DISCOVERED, knowledge, knower->name, tick);
    }
    if (masters == 1 && before >= 2) {
        notices_push(notices, NOTICE_KNOWLEDGE_LAST_MASTER, knowledge, master->name, tick);
    } else if (masters >= 1 && before == 0) {
        notices_push(notices, NOTICE_KNOWLEDGE_MASTERED, knowledge, master->name, tick);
    } else if (masters == 0 && before >= 1) {
        notices_push(notices, NOTICE_KNOWLEDGE_LOST, knowledge, NULL, tick);
    }
    if (!known && game->knowledge_known[knowledge]) {
        notices_push(notices, NOTICE_KNOWLEDGE_FORGOTTEN, knowledge, NULL, tick);
    }
}

void game_count_knowledge(Game *game)
{
    for (int knowledge = 0; knowledge < KNOWLEDGE_COUNT; knowledge++) {
        int masters = 0;
        const Colonist *master = NULL, *knower = NULL;
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (entity_id_is_none(game_colonist_id_at(game, slot))) {
                continue;
            }
            const Colonist *colonist = &game->colonists[slot];
            double level = colonist->knowledge[knowledge];
            if (level >= KNOWLEDGE_DISCOVERED_LEVEL && knower == NULL) {
                knower = colonist;
            }
            if (knowledge_is_mastered(level)) {
                masters++;
                if (master == NULL) {
                    master = colonist;
                }
            }
        }
        bool known = knower != NULL;
        if (game->knowledge_counted) {
            knowledge_notices(game, (KnowledgeType)knowledge, masters, known, master, knower);
        }
        game->knowledge_masters[knowledge] = (uint8_t)(masters < 255 ? masters : 255);
        game->knowledge_known[knowledge] = known;
    }
    game->knowledge_counted = true;
}

/* ---- Designations -------------------------------------------------------- */

bool game_set_work_priority(Game *game, EntityId id, WorkType type, int priority)
{
    Colonist *colonist = game_colonist(game, id);
    return colonist != NULL && colonist_set_work_priority(colonist, type, priority);
}

bool game_designation_available(const Game *game, DesignationType type)
{
    if (game->debug.instant_fell_and_mine &&
        (type == DESIGNATION_CHOP || type == DESIGNATION_MINE)) {
        return true; /* The cheat does it. */
    }
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            colonist_can_do(&game->colonists[slot], type)) {
            return true;
        }
    }
    return false;
}

bool game_gather_job_known(const Game *game, GatherJob job)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            colonist_can_gather(&game->colonists[slot], job)) {
            return true;
        }
    }
    return false;
}

bool game_gather_work_at(const Game *game, int x, int y, int z, GatherWorkInfo *info)
{
    *info = (GatherWorkInfo){GATHER_JOB_NONE, 0.0f, ENTITY_NONE};
    if (!map_in_bounds(&game->map, x, y, z)) {
        return false;
    }
    const GatherZones *zones = &game->gather_zones;
    size_t index = map_index(&game->map, x, y, z);
    float fraction = gather_zone_progress_fraction(zones, &game->map, x, y, z);
    uint8_t owner = reservations_owner(&game->reservations, RESERVATION_WORK,
                                       (TilePos){(int16_t)x, (int16_t)y, (int16_t)z});
    EntityId id = owner > 0 ? game_colonist_id_at(game, owner - 1u) : ENTITY_NONE;
    const Colonist *colonist = game_colonist_const(game, id);
    if (colonist != NULL && colonist->task == TASK_WORK && colonist->gathering &&
        colonist->target.x == x && colonist->target.y == y) {
        info->job = (GatherJob)colonist->work_type;
        info->worker = id;
        /* Work done on another job of the tile is lost once this one starts. */
        info->fraction = info->job == (GatherJob)zones->job[index] ? fraction : 0.0f;
        return true;
    }
    if (fraction > 0.0f) {
        info->job = (GatherJob)zones->job[index];
        info->fraction = fraction;
        return true;
    }
    return false;
}

int game_designate_rect(Game *game, int x0, int y0, int x1, int y1, int z, DesignationType type)
{
    if (type == DESIGNATION_NONE) {
        return designations_set_rect(&game->designations, &game->map, x0, y0, x1, y1, z, type);
    }
    int changed = 0;
    for (int y = (y0 < y1 ? y0 : y1); y <= (y0 < y1 ? y1 : y0); y++) {
        for (int x = (x0 < x1 ? x0 : x1); x <= (x0 < x1 ? x1 : x0); x++) {
            if (!designation_ground_free(&game->constructions, &game->workstations,
                                         &game->stockpiles, type, x, y, z)) {
                continue;
            }
            changed += designation_set(&game->designations, &game->map, x, y, z, type);
        }
    }
    return changed;
}

int game_cancel_rect(Game *game, int x0, int y0, int x1, int y1, int z)
{
    int removed = designations_set_rect(&game->designations, &game->map, x0, y0, x1, y1, z,
                                        DESIGNATION_NONE);
    return removed + game_remove_blueprints_rect(game, x0, y0, x1, y1, z);
}

/* ---- Forbidding ---------------------------------------------------------- */

/* The map bit of a target (0 for items and stations, kept elsewhere). */
static MapForbid forbid_bit(ForbidTarget target)
{
    switch (target) {
    case FORBID_PLANT:
        return MAP_FORBID_PLANT;
    case FORBID_STRUCTURE:
        return MAP_FORBID_STRUCTURE;
    case FORBID_ROOF:
        return MAP_FORBID_ROOF;
    case FORBID_ITEMS:
    case FORBID_STATION:
    case FORBID_TARGET_COUNT:
        break;
    }
    return (MapForbid)0;
}

/* The tile that holds the thing: the foot of the tree for a tile of its
 * trunk or foliage. */
static void forbid_tile(const Game *game, ForbidTarget target, int *x, int *y, int *z)
{
    TreeRef tree;
    if (target == FORBID_PLANT && map_in_bounds(&game->map, *x, *y, *z) &&
        game->map.plants[map_index(&game->map, *x, *y, *z)].type == PLANT_NONE &&
        tree_at(&game->map, *x, *y, *z, &tree)) {
        *x = tree.x;
        *y = tree.y;
        *z = tree.z;
    }
}

bool game_can_forbid(const Game *game, int x, int y, int z, ForbidTarget target)
{
    forbid_tile(game, target, &x, &y, &z);
    switch (target) {
    case FORBID_ITEMS:
        return items_get(&game->items, x, y, z).type != ITEM_NONE;
    case FORBID_STATION:
        return workstation_at(&game->workstations, x, y, z) != WORKSTATION_NONE;
    case FORBID_PLANT:
    case FORBID_STRUCTURE:
    case FORBID_ROOF:
        return map_can_forbid(&game->map, x, y, z, forbid_bit(target));
    case FORBID_TARGET_COUNT:
        break;
    }
    return false;
}

bool game_forbidden(const Game *game, int x, int y, int z, ForbidTarget target)
{
    forbid_tile(game, target, &x, &y, &z);
    switch (target) {
    case FORBID_ITEMS:
        return items_forbidden(&game->items, x, y, z);
    case FORBID_STATION: {
        const Workstation *station = workstation_get_const(
            &game->workstations, workstation_at(&game->workstations, x, y, z));
        return station != NULL && station->forbidden;
    }
    case FORBID_PLANT:
    case FORBID_STRUCTURE:
    case FORBID_ROOF:
        return map_forbidden(&game->map, x, y, z, forbid_bit(target));
    case FORBID_TARGET_COUNT:
        break;
    }
    return false;
}

bool game_set_forbidden(Game *game, int x, int y, int z, ForbidTarget target, bool forbidden)
{
    forbid_tile(game, target, &x, &y, &z);
    switch (target) {
    case FORBID_ITEMS:
        return items_set_forbidden(&game->items, x, y, z, forbidden); /* Counts as an addition. */
    case FORBID_STATION:
        return workstation_set_forbidden(&game->workstations,
                                         workstation_at(&game->workstations, x, y, z), forbidden);
    case FORBID_PLANT:
    case FORBID_STRUCTURE:
    case FORBID_ROOF:
        if (!map_set_forbidden(&game->map, x, y, z, forbid_bit(target), forbidden)) {
            return false;
        }
        if (!forbidden) { /* Orders, sites and gathering held up by it may go on. */
            game->designations.revision++;
            game->constructions.revision++;
            if (target == FORBID_PLANT) {
                gather_zones_plants_changed(&game->gather_zones);
            }
        }
        return true;
    case FORBID_TARGET_COUNT:
        break;
    }
    return false;
}

int game_set_forbidden_rect(Game *game, int x0, int y0, int x1, int y1, int z, bool forbidden)
{
    int changed = 0;
    for (int y = (y0 < y1 ? y0 : y1); y <= (y0 < y1 ? y1 : y0); y++) {
        for (int x = (x0 < x1 ? x0 : x1); x <= (x0 < x1 ? x1 : x0); x++) {
            for (int target = 0; target < FORBID_TARGET_COUNT; target++) {
                if (target == FORBID_PLANT && map_in_bounds(&game->map, x, y, z) &&
                    game->map.plants[map_index(&game->map, x, y, z)].type == PLANT_GRASS) {
                    continue; /* Ground cover: one tile at a time (game_set_forbidden()). */
                }
                changed += game_set_forbidden(game, x, y, z, (ForbidTarget)target, forbidden);
            }
        }
    }
    return changed;
}

/* ---- Zones --------------------------------------------------------------- */

int game_add_stockpile_rect(Game *game, int x0, int y0, int x1, int y1, int z, int preferred)
{
    const uint8_t *blocked[] = {game->gather_zones.zone, game->workstations.at,
                                game->constructions.workstation_tiles, NULL};
    return stockpiles_add_rect(&game->stockpiles, &game->map, x0, y0, x1, y1, z, preferred,
                               blocked);
}

int game_add_gather_zone_rect(Game *game, int x0, int y0, int x1, int y1, int z, int preferred)
{
    const uint8_t *blocked[] = {game->stockpiles.zone, game->workstations.at,
                                game->constructions.workstation_tiles, NULL};
    return gather_zones_add_rect(&game->gather_zones, &game->map, x0, y0, x1, y1, z, preferred,
                                 blocked);
}

/* ---- Picking ------------------------------------------------------------- */

EntityId game_colonist_at(const Game *game, float world_x, float world_y, int z)
{
    EntityId best = ENTITY_NONE;
    float best_distance = GAME_PICK_RADIUS * GAME_PICK_RADIUS;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id) || game->colonists[slot].z != z) {
            continue;
        }
        float x, y;
        colonist_position(&game->colonists[slot], &x, &y);
        float distance = (x - world_x) * (x - world_x) + (y - world_y) * (y - world_y);
        if (distance <= best_distance) {
            best_distance = distance;
            best = id;
        }
    }
    return best;
}

int game_colonists_in_rect(const Game *game, float x0, float y0, float x1, float y1, int z,
                           EntityId *out, int max_count)
{
    float min_x = fminf(x0, x1), max_x = fmaxf(x0, x1);
    float min_y = fminf(y0, y1), max_y = fmaxf(y0, y1);
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game) && count < max_count; slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id) || game->colonists[slot].z != z) {
            continue;
        }
        float x, y;
        colonist_position(&game->colonists[slot], &x, &y);
        if (x >= min_x && x <= max_x && y >= min_y && y <= max_y) {
            out[count++] = id;
        }
    }
    return count;
}

/* ---- Workstations -------------------------------------------------------- */

bool game_workstation_buildable(const Game *game, WorkstationType type)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            colonist_can_build(&game->colonists[slot], type)) {
            return true;
        }
    }
    return false;
}

bool game_recipe_known(const Game *game, RecipeType recipe)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            colonist_can_craft(&game->colonists[slot], recipe)) {
            return true;
        }
    }
    return false;
}

bool game_bill_has_worker(const Game *game, const Bill *bill)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            colonist_can_craft(colonist, (RecipeType)bill->recipe) &&
            colonist_level_fits_bill(colonist, bill)) {
            return true;
        }
    }
    return false;
}

static bool colonist_carries_tool(const Game *game, ItemTypeMask types)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            colonist_tool_index(&game->colonists[slot], types) >= 0) {
            return true;
        }
    }
    return false;
}

static bool items_lying(const Items *items, ItemTypeMask types, int count)
{
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if ((types & ITEM_TYPE_BIT(type)) && items->totals[type] >= count) {
            return true;
        }
    }
    return false;
}

ItemTypeMask game_recipe_missing_tools(const Game *game, RecipeType recipe)
{
    const RecipeDef *def = recipe_def(recipe);
    ItemTypeMask missing = 0;
    for (int slot = 0; def != NULL && !game->debug.no_tools && slot < RECIPE_TOOL_MAX; slot++) {
        ItemTypeMask types = def->tools[slot];
        if (types != 0 && !items_lying(&game->items, types, 1) &&
            !colonist_carries_tool(game, types)) {
            missing |= types;
        }
    }
    return missing;
}

bool game_bill_has_input(const Game *game, const Bill *bill)
{
    const RecipeDef *def = recipe_def((RecipeType)bill->recipe);
    if (def == NULL) {
        return false;
    }
    if (game->debug.free_crafts) {
        return bill_free_input(bill).count > 0;
    }
    /* Tools nobody carries yet will be taken from what lies around. */
    ItemTypeMask tools[RECIPE_TOOL_MAX] = {0};
    for (int slot = 0; !game->debug.no_tools && slot < RECIPE_TOOL_MAX; slot++) {
        if (def->tools[slot] != 0 && !colonist_carries_tool(game, def->tools[slot])) {
            tools[slot] = def->tools[slot];
        }
    }
    return bill_has_input(bill, game->items.totals, tools);
}

bool game_can_place_workstation(const Game *game, int x, int y, int z)
{
    /* Room in the list for every station whose site is finished. */
    return game->workstations.used_count + game->constructions.workstation_count <
               WORKSTATION_MAX &&
           map_structure(&game->map, x, y, z) == STRUCTURE_NONE &&
           construction_can_place(&game->constructions, &game->map, x, y, z) &&
           workstation_can_place(&game->workstations, &game->map, x, y, z);
}

bool game_place_workstation(Game *game, WorkstationType type, int x, int y, int z)
{
    if ((unsigned)type >= WORKSTATION_TYPE_COUNT || !game_can_place_workstation(game, x, y, z)) {
        return false;
    }
    /* Ready at once when there is nothing to bring, to build or to clear. */
    bool ready = !workstation_needs_construction(type) &&
                 game->map.plants[map_index(&game->map, x, y, z)].type == PLANT_NONE;
    bool placed = ready ? workstations_place(&game->workstations, &game->map, type, x, y, z) !=
                              WORKSTATION_NONE
                        : constructions_place_workstation(&game->constructions, &game->map,
                                                          type, x, y, z) != CONSTRUCTION_NONE;
    if (placed) {
        /* No zone on a station: the tile leaves the zone it was in. */
        stockpiles_remove_rect(&game->stockpiles, x, y, x, y, z);
        gather_zones_remove_rect(&game->gather_zones, x, y, x, y, z);
    }
    return placed;
}

bool game_remove_workstation(Game *game, int index)
{
    const Workstation *station = workstation_get_const(&game->workstations, index);
    if (station == NULL) {
        return false;
    }
    int x = station->x, y = station->y, z = station->z;
    ItemStack returned[1];
    int count = workstations_remove(&game->workstations, index, returned);
    for (int i = 0; i < count; i++) {
        items_drop(&game->items, &game->map, x, y, z, returned[i]);
    }
    return true;
}

int game_remove_blueprints_rect(Game *game, int x0, int y0, int x1, int y1, int z)
{
    int removed = 0;
    for (int y = (y0 < y1 ? y0 : y1); y <= (y0 < y1 ? y1 : y0); y++) {
        for (int x = (x0 < x1 ? x0 : x1); x <= (x0 < x1 ? x1 : x0); x++) {
            int site = construction_at(&game->constructions, x, y, z);
            if (game_remove_construction(game, site)) {
                removed++;
            }
        }
    }
    return removed;
}

bool game_remove_bill(Game *game, int station_index, int bill)
{
    const Workstation *station = workstation_get_const(&game->workstations, station_index);
    if (station == NULL || bill < 0 || bill >= station->bill_count) {
        return false;
    }
    int x = station->x, y = station->y, z = station->z;
    ItemStack returned;
    if (workstation_remove_bill(&game->workstations, station_index, bill, &returned)) {
        items_drop(&game->items, &game->map, x, y, z, returned);
    }
    return true;
}

/* ---- Structures ---------------------------------------------------------- */

bool game_structure_buildable(const Game *game, StructureType type)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            colonist_can_construct(&game->colonists[slot], type)) {
            return true;
        }
    }
    return false;
}

bool game_can_place_structure(const Game *game, StructureType type, int x, int y, int z,
                              Direction direction)
{
    int length = structure_length(type);
    if (length <= 0 || (unsigned)direction >= DIRECTION_COUNT) {
        return false;
    }
    for (int part = 0; part < length; part++) {
        int px = x + part * direction_dx(direction), py = y + part * direction_dy(direction);
        if (stockpile_at(&game->stockpiles, px, py, z) != STOCKPILE_NONE ||
            workstation_at(&game->workstations, px, py, z) != WORKSTATION_NONE) {
            return false;
        }
    }
    return construction_can_place_structure(&game->constructions, &game->map, type, x, y, z,
                                            direction);
}

int game_place_structure(Game *game, StructureType type, int x, int y, int z, Direction direction)
{
    if (!game_can_place_structure(game, type, x, y, z, direction)) {
        return CONSTRUCTION_NONE;
    }
    return constructions_place(&game->constructions, &game->map, type, x, y, z, direction);
}

void game_plan_structure_rect(const Game *game, StructureType type, Direction direction,
                              int x0, int y0, int x1, int y1, int z, StructurePlanVisit visit,
                              void *userdata)
{
    const StructureDef *def = structure_def(type);
    if (def == NULL || type == STRUCTURE_NONE || (unsigned)direction >= DIRECTION_COUNT) {
        return;
    }
    int min_x = x0 < x1 ? x0 : x1, max_x = x0 < x1 ? x1 : x0;
    int min_y = y0 < y1 ? y0 : y1, max_y = y0 < y1 ? y1 : y0;
    int length = def->length;
    int dx = direction_dx(direction), dy = direction_dy(direction);
    /* Stretched the way the structure goes, to hold at least one. */
    if (dx != 0 && max_x - min_x + 1 < length) {
        if (dx > 0) {
            max_x = min_x + length - 1;
        } else {
            min_x = max_x - length + 1;
        }
    }
    if (dy != 0 && max_y - min_y + 1 < length) {
        if (dy > 0) {
            max_y = min_y + length - 1;
        } else {
            min_y = max_y - length + 1;
        }
    }
    /* Cells of `length` tiles along the structure; the head at the end the
     * structure goes from. */
    int step_x = dx != 0 ? length : 1, step_y = dy != 0 ? length : 1;
    for (int y = min_y; y + step_y - 1 <= max_y; y += step_y) {
        for (int x = min_x; x + step_x - 1 <= max_x; x += step_x) {
            bool edge = x == min_x || x == max_x || y == min_y || y == max_y;
            if (!edge && def->drawn_as_outline) {
                continue;
            }
            int head_x = dx < 0 ? x + length - 1 : x;
            int head_y = dy < 0 ? y + length - 1 : y;
            visit(head_x, head_y, z,
                  game_can_place_structure(game, type, head_x, head_y, z, direction), userdata);
        }
    }
}

typedef struct StructurePlacing {
    Game *game;
    StructureType type;
    Direction direction;
    int placed;
} StructurePlacing;

static void place_planned(int x, int y, int z, bool allowed, void *userdata)
{
    StructurePlacing *placing = userdata;
    if (allowed && game_place_structure(placing->game, placing->type, x, y, z,
                                        placing->direction) != CONSTRUCTION_NONE) {
        placing->placed++;
    }
}

int game_place_structure_rect(Game *game, StructureType type, Direction direction, int x0,
                              int y0, int x1, int y1, int z)
{
    StructurePlacing placing = {game, type, direction, 0};
    game_plan_structure_rect(game, type, direction, x0, y0, x1, y1, z, place_planned, &placing);
    return placing.placed;
}

bool game_remove_construction(Game *game, int index)
{
    const ConstructionSite *site = construction_get(&game->constructions, index);
    if (site == NULL) {
        return false;
    }
    int x = site->x, y = site->y, z = site->z;
    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
    int count = constructions_remove(&game->constructions, index, returned);
    for (int i = 0; i < count; i++) {
        items_drop(&game->items, &game->map, x, y, z, returned[i]);
    }
    return true;
}
