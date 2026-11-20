#include "core/game.h"
#include "core/exploration.h"

#include "core/conversation.h"
#include "core/hauling.h"
#include "core/log.h"
#include "core/shadow.h"
#include "core/sun.h"
#include "core/tree.h"
#include "core/util.h"
#include "core/weapon.h"

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
#define GAME_ATTRIBUTES_SEED 0xa77a1b07e5c0de5bULL
/* Relationships are brought up to date once an hour, at this tick of it. */
#define GAME_SOCIAL_UPDATE_PHASE 7
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
/* Mushrooms (docs/KNOWLEDGE.md §7.3): each cool night (MUSHROOM_MIN_CELSIUS), a patch
 * appears with MUSHROOM_CHANCE on open soil within MUSHROOM_TREE_RANGE tiles
 * of a mature tree, ×(1 + recent rain / MUSHROOM_RAIN_HOURS), at most
 * ×MUSHROOM_RAIN_MAX after much rain (Weather.recent_rain), and an existing
 * one goes with MUSHROOM_VANISH_CHANCE (five days on average). */
#define MUSHROOM_CHANCE 0.0015f
#define MUSHROOM_RAIN_HOURS 4.0f
#define MUSHROOM_RAIN_MAX 4.0f
#define MUSHROOM_VANISH_CHANCE 0.2f
#define MUSHROOM_TREE_RANGE 2
/* They come up only in the cool, damp nights between these air temperatures
 * (degrees Celsius at the first hour of the day): not in the heat of summer
 * nor in the frost. */
#define MUSHROOM_MIN_CELSIUS 4.0f
#define MUSHROOM_MAX_CELSIUS 16.0f
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
    params->wildlife = 0.0f;
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
    social_clear_slot(&game->social, (int)id.index); /* Nobody knows the newcomer. */
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
    /* Attributes come from their own stream: the colonists made later must
     * not depend on how many were made before, nor the rest of the game on
     * them. */
    Rng character;
    rng_seed(&character, game->seed ^ GAME_ATTRIBUTES_SEED,
             ((uint64_t)id.generation << 32) | id.index);
    Attributes attributes;
    attributes_roll(&attributes, &character);
    colonist_set_attributes(colonist, &attributes);
    /* Looks come from their own stream: adding a sprite variant must not
     * change what the simulation does. */
    Rng looks;
    rng_seed(&looks, game->seed ^ GAME_APPEARANCE_SEED, ((uint64_t)id.generation << 32) | id.index);
    colonist->appearance = appearance_roll(&looks, colonist->age);
    return id;
}

_Static_assert(COLONIST_MAX == OCCUPANCY_SETTLER_SLOTS, "settlers are numbered by slot");
_Static_assert(ANIMAL_MAX == OCCUPANCY_ANIMAL_SLOTS, "animals are numbered by slot");

/* Reservation owners are colonist slots + 1 and must fit in a byte. */
_Static_assert(COLONIST_MAX < 255, "colonist slots must fit in reservation owners");

static ColonistContext colonist_context(Game *game, uint32_t slot)
{
    ColonistContext context = {
        .map = &game->map,
        .plan_map = game_view_map(game),
        .plan_items = game_view_items(game),
        .vision = game->fog_on ? &game->vision : NULL,
        .designations = &game->designations,
        .dirt = &game->dirt,
        .gather_zones = &game->gather_zones,
        .items = &game->items,
        .stockpiles = &game->stockpiles,
        .workstations = &game->workstations,
        .constructions = &game->constructions,
        .beds = &game->beds,
        .reservations = &game->reservations,
        .item_moves = &game->item_moves,
        .pathfinder = &game->pathfinder,
        .regions = &game->regions,
        .stock = game->stock,
        .rng = &game->rng,
        .tick = game->clock.tick,
        .night = colonist_is_night_for(game->clock.tick, (uint8_t)(slot + 1)),
        .worker_id = (uint8_t)(slot + 1),
        .colonists = game->colonists,
        .colonist_pool = &game->colonist_pool,
        .teach = game->teach,
        .temperatures = &game->temperatures,
        .wetting = weather_wetting(&game->weather, game->clock.tick),
        .soil = &game->soil,
        .free_crafts = game->debug.free_crafts,
        .no_tools = game->debug.no_tools,
        .animals = &game->animals,
        .combat = &game->combat,
        .occupancy = &game->occupancy,
        .self = game_colonist_id_at(game, slot),
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
    occupancy_remove(&game->occupancy, occupancy_settler(id.index));
    social_clear_slot(&game->social, (int)id.index);
    return pool_release(&game->colonist_pool, id);
}

/* Tiles round a spot, nearest first (the spot's own first): where a group
 * ordered to move spreads out. */
#define MOVE_SPREAD_RADIUS 4
#define MOVE_SPREAD_TILES ((2 * MOVE_SPREAD_RADIUS + 1) * (2 * MOVE_SPREAD_RADIUS + 1))

typedef struct SpreadOffset {
    int dx, dy;
} SpreadOffset;

static int compare_spread(const void *a, const void *b)
{
    const SpreadOffset *oa = a, *ob = b;
    int da = oa->dx * oa->dx + oa->dy * oa->dy, db = ob->dx * ob->dx + ob->dy * ob->dy;
    if (da != db) {
        return da - db;
    }
    return oa->dy != ob->dy ? oa->dy - ob->dy : oa->dx - ob->dx;
}

int game_order_move(Game *game, const EntityId *ids, int count, int x, int y, int z)
{
    SpreadOffset offsets[MOVE_SPREAD_TILES];
    int offset_count = 0;
    for (int dy = -MOVE_SPREAD_RADIUS; dy <= MOVE_SPREAD_RADIUS; dy++) {
        for (int dx = -MOVE_SPREAD_RADIUS; dx <= MOVE_SPREAD_RADIUS; dx++) {
            offsets[offset_count++] = (SpreadOffset){dx, dy};
        }
    }
    qsort(offsets, (size_t)offset_count, sizeof(offsets[0]), compare_spread);

    TilePos taken[COLONIST_MAX];
    int taken_count = 0;
    int sent = 0;
    for (int i = 0; i < count && taken_count < COLONIST_MAX; i++) {
        Colonist *colonist = game_colonist(game, ids[i]);
        if (colonist == NULL) {
            continue;
        }
        ColonistContext context = colonist_context(game, ids[i].index);
        for (int o = 0; o < offset_count; o++) {
            TilePos goal = {(int16_t)(x + offsets[o].dx), (int16_t)(y + offsets[o].dy), (int16_t)z};
            bool free = true;
            for (int t = 0; t < taken_count && free; t++) {
                free = taken[t].x != goal.x || taken[t].y != goal.y;
            }
            OccupancyStopper stopper = {&game->occupancy, occupancy_settler(ids[i].index)};
            if (!free || !game_tile_explored(game, goal.x, goal.y, goal.z) ||
                !occupancy_stop_filter(&game->map, goal.x, goal.y, goal.z, &stopper) ||
                !colonist_order_move(colonist, &context, goal)) {
                continue;
            }
            taken[taken_count++] = goal;
            sent++;
            break;
        }
    }
    return sent;
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
        if (fertile && (!want_water ||
                        pathfinder_find_nearest(&game->pathfinder, map, candidate, is_drinking_spot,
                                                NULL, SPAWN_WATER_MAX_COST, NULL, NULL))) {
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
    /* A tree counts once it is grown (though only the young ones, with no
     * trunk yet, can be reached by the search). */
    float grown = plant_is_tree(count->type) ? TREE_CROWN_FROM
                                             : plant_def(count->type)->products[0].min_growth;
    if (plant->type == count->type && plant->growth >= grown) {
        count->count++;
    }
    return false;
}

typedef struct PlantSpotSearch {
    TilePos center;
    Rng *rng;
    float chance;   /* Of taking a suitable spot. */
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
    if (map->rock[index] != ROCK_NONE || map_is_water(map, x, y, z) || plant->type != PLANT_NONE ||
        terrain_def((Terrain)map->terrain[index])->fertility <
            plant_def(search->type)->min_fertility ||
        abs(x - search->center.x) + abs(y - search->center.y) < SPAWN_PLANT_MIN_DISTANCE) {
        return false;
    }
    if (plant_is_tree(search->type)) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (map_in_bounds(map, x + dx, y + dy, z) &&
                    plant_is_tree(map->plants[map_index(map, x + dx, y + dy, z)].type)) {
                    return false; /* Trunks never touch. */
                }
            }
        }
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
        bool found = pathfinder_find_nearest(&game->pathfinder, &game->map, center, is_plant_spot,
                                             &search, SPAWN_PLANT_MAX_COST, NULL, &spot);
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
        TreeShape shape;
        if (tree_shape(plant, spot.x, spot.y, spot.z, &shape) &&
            tree_room_to_grow(&game->map, spot.x, spot.y, spot.z, &shape)) {
            tree_build(&game->map, spot.x, spot.y, spot.z); /* A tree has its volume. */
        }
    }
}

/* A tile of a rich enough flint patch, on stony ground. */
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

/* Where a flint patch can be laid: stony ground (gather_stone_ground()), not
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
            if (distance > SPAWN_FLINT_PATCH_RADIUS || !gather_stone_ground(map, x, y, z)) {
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
    game->arrival = center;
    game->has_arrival = true;
    ensure_food_plants_near(game, center, PLANT_BRAMBLE, GAME_SPAWN_BRAMBLES);
    ensure_food_plants_near(game, center, PLANT_HAZEL, GAME_SPAWN_HAZELS);
    ensure_flint_near(game, center);
    for (int i = 0; i < count; i++) {
        TilePos tile;
        if (!pathfinder_find_nearest(&game->pathfinder, &game->map, center, is_free_tile, game,
                                     SPAWN_PLACEMENT_MAX_COST, NULL, &tile)) {
            LOG_WARN("Not enough room to place every starting colonist");
            break;
        }
        game_spawn_colonist(game, tile.x, tile.y, tile.z);
    }
    /* They came together: families among them, and the journey. */
    social_start_together(&game->social, game->colonists, &game->colonist_pool, game->clock.tick);
}

/* ---- Lifecycle ----------------------------------------------------------- */

/* The leaves of the crowns follow the climate the trees live in (the running
 * mean of the air, see plant_bare_target()): bare in winter, in leaf from late
 * spring to the middle of autumn. Each hourly pass over the plants adds up the
 * climates of the deciduous trees with a crown, and at its end sets Map.bare
 * from their mean. */
static void leaf_meet(Game *game, const Plant *plant)
{
    if (plant_def((PlantType)plant->type)->deciduous && (plant->state & PLANT_CLIMATE_KNOWN) &&
        plant->growth >= TREE_CROWN_FROM) {
        game->leaf_climate_sum += (double)plant->climate / PLANT_CLIMATE_SCALE;
        game->leaf_tree_count++;
    }
}

static void leaf_conclude(Game *game)
{
    if (game->leaf_tree_count > 0) {
        float mean = (float)(game->leaf_climate_sum / (double)game->leaf_tree_count);
        map_set_bare(&game->map, (int)lroundf(plant_bare_target(mean) * (float)MAP_BARE_STEPS));
        if (game->known_ready) { /* The season shows on every crown, seen or not. */
            map_set_bare(&game->known.map, game->map.bare);
        }
    }
    game->leaf_climate_sum = 0.0;
    game->leaf_tree_count = 0;
}

/* The crowns of a new world: from the trees as generated. */
static void leaf_settle(Game *game)
{
    uint64_t count =
        (uint64_t)map_level_tile_count(&game->map) * (uint64_t)(game->map.top_ground_level + 1);
    for (size_t i = 0; i < count; i++) {
        if (game->map.plants[i].type != PLANT_NONE) {
            leaf_meet(game, &game->map.plants[i]);
        }
    }
    leaf_conclude(game);
}

/* A tile's share of the light of open ground at noon on a clear day. */
static float noon_light(const Game *game, int x, int y, int z)
{
    int day = game_day_of_year(game->clock.tick);
    float elevation = sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f).elevation;
    return game_tile_light(game, x, y, z, shadow_direct_share(elevation, 1.0f), true);
}

bool game_init(Game *game, const NewGameParams *params)
{
    memset(game, 0, sizeof(*game));
    if (!worldgen_generate_lit(&game->map, &params->world, &game->light) ||
        !designations_init(&game->designations, game->map.width, game->map.height,
                           game->map.depth) ||
        !dirt_init(&game->dirt, game->map.width, game->map.height, game->map.depth) ||
        !gather_zones_init(&game->gather_zones, game->map.width, game->map.height,
                           game->map.depth) ||
        !items_init(&game->items, game->map.width, game->map.height, game->map.depth) ||
        !crowns_init(&game->crowns, game->map.width, game->map.height, game->map.depth) ||
        !stockpiles_init(&game->stockpiles, game->map.width, game->map.height, game->map.depth) ||
        !workstations_init(&game->workstations, game->map.width, game->map.height,
                           game->map.depth) ||
        !constructions_init(&game->constructions, game->map.width, game->map.height,
                            game->map.depth) ||
        !beds_init(&game->beds, game->map.width, game->map.height, game->map.depth) ||
        !pathfinder_init(&game->pathfinder, game->map.width, game->map.height, game->map.depth) ||
        !regions_init(&game->regions, game->map.width, game->map.height, game->map.depth) ||
        !pool_init(&game->colonist_pool, COLONIST_MAX) || !animals_init(&game->animals) ||
        !occupancy_init(&game->occupancy, game->map.width, game->map.height, game->map.depth)) {
        game_shutdown(game);
        return false;
    }
    game->seed = params->world.seed;
    /* Items dropped keep off storage they would mix up. */
    items_set_drop_filter(&game->items, haul_drop_filter, &game->stockpiles);
    /* Stones, branches and twigs lie as ordinary items from now on. */
    map_litter_to_items(&game->map, &game->items);
    /* Fruit and dead wood hang in the crowns from now on. */
    game->map.crowns = &game->crowns;
    corpses_init(&game->corpses);
    reservations_init(&game->reservations);
    item_moves_init(&game->item_moves);
    game_clock_init(&game->clock);
    rng_seed(&game->rng, params->world.seed, 0x73696dULL /* "sim" */);
    rng_seed(&game->wildlife_rng, params->world.seed, 0x77696c64ULL /* "wild" */);
    rng_seed(&game->combat_rng, params->world.seed, 0x666967687455ULL /* "fight" */);
    rng_seed(&game->blood_rng, params->world.seed, 0x626c6f6f64ULL /* "blood" */);
    social_init(&game->social, params->world.seed);
    roof_watch_init(&game->roof_watch, &game->map);
    game->tree_falls_seen = game->map.tree_fall_revision;
    weather_init(&game->weather, game->seed, game->clock.tick);
    if (!temperatures_init(&game->temperatures, &game->map,
                           weather_outdoor(&game->weather, game->clock.tick))) {
        game_shutdown(game);
        return false;
    }
    temperatures_settle_ground(&game->temperatures,
                               temperature_season_mean(game->clock.tick) + game->weather.spell);
    if (!soil_init(&game->soil, &game->map, GAME_SOIL_START_SHARE)) {
        game_shutdown(game);
        return false;
    }
    leaf_settle(game);
    /* The light of the world was worked out with it (the plants are set where
     * they can live all year); brought up to date at the end. */
    spawn_starting_colonists(game, params->colonist_count);
    regions_update(&game->regions, &game->map); /* Built here, not during the first tick. */
    if (params->wildlife > 0.0f) {
        animals_populate(&game->animals, &game->map, &game->pathfinder, &game->regions,
                         &game->wildlife_rng, params->wildlife,
                         game->has_arrival ? &game->arrival : NULL);
    }
    game_update_stock(game);
    game_count_knowledge(game); /* Who knows what at the start: no notice for it. */
    /* Last: the light where the arrival's camp changed the map. */
    tile_light_refresh(&game->light, &game->map, game->clock.tick);
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

void game_count_stock(const Game *game, long stock[ITEM_TYPE_COUNT])
{
    /* What the colony has is what it knows of: the known world under the fog,
     * so that items out of view count as they were last seen. */
    memset(stock, 0, sizeof(long) * ITEM_TYPE_COUNT);
    stockpiles_count_stored(&game->stockpiles, game_view_items(game), stock);
}

void game_update_stock(Game *game)
{
    game_count_stock(game, game->stock);
}

void game_shutdown(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        colonist_free(&game->colonists[slot]);
    }
    pool_free(&game->colonist_pool);
    animals_free(&game->animals);
    occupancy_free(&game->occupancy);
    pathfinder_free(&game->pathfinder);
    regions_free(&game->regions);
    game->items.mirror = NULL;
    vision_free(&game->vision);
    game->vision_on = false;
    known_free(&game->known);
    game->known_ready = false;
    game->fog_on = false;
    temperatures_free(&game->temperatures);
    soil_free(&game->soil);
    tile_light_free(&game->light);
    designations_free(&game->designations);
    dirt_free(&game->dirt);
    gather_zones_free(&game->gather_zones);
    items_free(&game->items);
    crowns_free(&game->crowns);
    corpses_free(&game->corpses);
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
    if (plant->type != PLANT_NONE ||
        world_roll(game->seed, index, day, 0) >= mushroom_chance(&game->weather)) {
        return;
    }
    float air = weather_outdoor(&game->weather, game->clock.tick);
    if (air < MUSHROOM_MIN_CELSIUS || air > MUSHROOM_MAX_CELSIUS) {
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

/* Once a day per plant (docs/PLANTS.md §13): a grown plant that is awake
 * sends out a runner or a seed with its `spread_chance` (rolled from the seed,
 * the tile and the day, leaving the game's random stream alone), and a young
 * plant takes root where it lands if the tile suits it, its ground is neither
 * too dry nor too wet for it now (docs/CLIMATE.md §6), and nothing of the
 * colony is on it (a stockpile, a workstation, a building site, an order;
 * items lying about do not stop it). */

static void spread_day(Game *game, size_t index, int day)
{
    Map *map = &game->map;
    const Plant *plant = &map->plants[index];
    const PlantDef *def = plant_def((PlantType)plant->type);
    if (plant->type == PLANT_NONE || def->spread_chance <= 0.0f ||
        (plant->state & ~PLANT_CLIMATE_KNOWN) != PLANT_STATE_AWAKE ||
        plant->growth < def->spread_growth ||
        world_roll(game->seed, index, day, 30) >= def->spread_chance) {
        return;
    }
    int x, y, z, tx, ty;
    map_tile_of(map, index, &x, &y, &z);
    if (!plant_spread_target(map, x, y, z, world_roll(game->seed, index, day, 31), &tx, &ty) ||
        !plant_can_take_root(map, (PlantType)plant->type, tx, ty, z, noon_light(game, tx, ty, z)) ||
        stockpile_at(&game->stockpiles, tx, ty, z) != STOCKPILE_NONE ||
        workstation_at(&game->workstations, tx, ty, z) >= 0 ||
        construction_at(&game->constructions, tx, ty, z) != CONSTRUCTION_NONE ||
        designation_get(&game->designations, tx, ty, z) != DESIGNATION_NONE) {
        return;
    }
    float moisture = soil_moisture(&game->soil, map, tx, ty, z);
    if (moisture >= -0.5f && plant_water_response((PlantType)plant->type, moisture) <= 0.0f) {
        return;
    }
    map->plants[map_index(map, tx, ty, z)] = plant_offspring(plant);
}

/* Whether a settler is on a tile the trunk of a tree of `shape` on (x, y, z)
 * would grow into. */
static bool tree_in_the_way(const Game *game, int x, int y, int z, const TreeShape *shape)
{
    const Map *map = &game->map;
    for (int dz = 0; dz < shape->trunk; dz++) {
        for (int dy = 0; dy < shape->size; dy++) {
            for (int dx = 0; dx < shape->size; dx++) {
                int tx = x + dx, ty = y + dy, tz = z + dz;
                if (map_in_bounds(map, tx, ty, tz) &&
                    map->rock[map_index(map, tx, ty, tz)] != ROCK_WOOD &&
                    game_tile_has_colonist(game, tx, ty, tz)) {
                    return true;
                }
            }
        }
    }
    return false;
}

/* What lies on the tiles a trunk of `shape` on (x, y, z) has just grown into
 * is pushed next door (the first open tile beside it, as a wall built there
 * does); with no room around, it stays. */
static void push_items_off_trunk(Game *game, int x, int y, int z, const TreeShape *shape)
{
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    Map *map = &game->map;
    for (int dz = 0; dz < shape->trunk; dz++) {
        for (int dy = 0; dy < shape->size; dy++) {
            for (int dx = 0; dx < shape->size; dx++) {
                int tx = x + dx, ty = y + dy, tz = z + dz;
                if (!map_in_bounds(map, tx, ty, tz) ||
                    items_get(&game->items, tx, ty, tz).count == 0) {
                    continue;
                }
                for (int i = 0; i < 4; i++) {
                    if (map_is_passable(map, tx + DX[i], ty + DY[i], tz)) {
                        items_move_all(&game->items, map, tx, ty, tz, tx + DX[i], ty + DY[i], tz,
                                       true);
                        break;
                    }
                }
            }
        }
    }
}

/* A tree whose shape changed as it grew (or that takes room and is not in
 * the map yet) is written into the map; one whose trunk has no room, or
 * would grow into something, waits at its stage (`old_growth`). */
static void grow_tree(Game *game, size_t index, float old_growth)
{
    Map *map = &game->map;
    Plant *plant = &map->plants[index];
    int x, y, z;
    map_tile_of(map, index, &x, &y, &z);
    TreeShape shape, before;
    if (!tree_shape(plant, x, y, z, &shape)) {
        return;
    }
    Plant old = *plant;
    old.growth = old_growth;
    bool had_shape = tree_shape(&old, x, y, z, &before);
    bool built = map->rock[index] == ROCK_WOOD;
    /* Nothing to write while the crown stays as it was. */
    if (built && had_shape && before.trunk == shape.trunk && before.reach == shape.reach &&
        before.growth == shape.growth) {
        return;
    }
    if (!tree_room_to_grow(map, x, y, z, &shape) || tree_in_the_way(game, x, y, z, &shape)) {
        if (built || !had_shape) {
            plant->growth = old_growth;
        }
        return;
    }
    tree_build(map, x, y, z);
    push_items_off_trunk(game, x, y, z, &shape);
    /* No stockpile on a trunk. */
    stockpiles_remove_rect(&game->stockpiles, x, y, x + shape.size - 1, y + shape.size - 1, z);
}

/* Once an hour per tree: twigs, and branches from a grown tree, fall at
 * random (TREE_SHED_*_CHANCE, rolled from the seed, the tile and the hour so
 * that the game's random stream is left alone), and in their season its nuts
 * or fruit (tree_fall_chance()). */
static void shed_litter(Game *game, size_t index)
{
    /* ITEM_NONE: what the tree bears, if any. */
    static const ItemType KINDS[] = {ITEM_BRANCHES, ITEM_TWIGS, ITEM_NONE};
    if (game->map.rock[index] != ROCK_WOOD) {
        return; /* No crown yet: the trunk has no room. */
    }
    int hour = (int)(game->clock.tick / GAME_PLANT_UPDATE_INTERVAL);
    for (uint32_t kind = 0; kind < sizeof(KINDS) / sizeof(KINDS[0]); kind++) {
        ItemType item = KINDS[kind];
        float chance = item == ITEM_NONE
                           ? tree_fall_chance(&game->map.plants[index],
                                              game_day_of_year(game->clock.tick), &item)
                           : tree_shed_chance(&game->map.plants[index], item);
        if (chance <= 0.0f || world_roll(game->seed, index, hour, 2 * kind + 10) >= chance) {
            continue;
        }
        int x, y, z;
        map_tile_of(&game->map, index, &x, &y, &z);
        float roll = world_roll(game->seed, index, hour, 2 * kind + 11);
        uint32_t pick = (uint32_t)(roll * 16777216.0f);
        tree_shed(&game->map, x, y, z, item, pick);
    }
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

/* The water in the ground (docs/CLIMATE.md §3): the same slice of tiles as
 * the plants, once an hour each; the snowpack and the banks once an hour for
 * the map. */
static void update_soil(Game *game, size_t begin, size_t end, uint64_t phase)
{
    Soil *soil = &game->soil;
    uint64_t tick = game->clock.tick;
    float air = weather_outdoor(&game->weather, tick);
    float rain = weather_rainfall(&game->weather, tick);
    if (phase == 0) {
        soil_refresh_banks(soil, &game->map);
        soil->melt = soil_snow_hour(soil, weather_snowfall(&game->weather, tick), air, rain > 0.0f);
    }
    float dew = weather_dew_point(&game->weather, tick);
    float sun = game_sunlight(tick);
    SoilHour hour = {
        .rain = rain,
        .melt = soil->melt,
        .pet = soil_potential_evaporation(sun, weather_light(&game->weather), air,
                                          weather_outdoor_humidity(&game->weather, tick)),
        .dew_point = dew,
        .night = sun <= 0.0f,
        .look_up = (tick / TICKS_PER_HOUR) % SOIL_CANOPY_HOURS == 0,
        .light = &game->light,
        .direct_share = game_direct_share(
            game, sun_at_tick(tick - phase + TICKS_PER_HOUR / 2, SUN_LATITUDE_DEFAULT).elevation),
    };
    soil_update(soil, &game->map, begin, end, &hour, game->temperatures.celsius);
}

/* What hangs in the crowns: a slice of the pool each tick, every item once
 * an hour (core/crown.h); what falls falls. */
static void update_crowns(Game *game, uint64_t phase)
{
    Crowns *crowns = &game->crowns;
    uint64_t tick = game->clock.tick;
    crowns->day = (int)(tick / TICKS_PER_DAY);
    if (crowns->count == 0) {
        return;
    }
    uint64_t used = crowns->pool_used;
    uint32_t begin = (uint32_t)(phase * used / GAME_PLANT_UPDATE_INTERVAL);
    uint32_t end = (uint32_t)((phase + 1) * used / GAME_PLANT_UPDATE_INTERVAL);
    CrownHour hour = {.seed = game->seed,
                      .hour = tick / TICKS_PER_HOUR,
                      .day = crowns->day,
                      .day_of_year = game_day_of_year(tick),
                      .storm = game->weather.type == WEATHER_STORM};
    crowns_hour(crowns, &game->map, &game->items, begin, end, &hour);
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
            int x, y, z;
            map_tile_of(map, i, &x, &y, &z);
            PlantConditions conditions = game_plant_conditions(game, x, y, z);
            plant_advance(plant, &conditions, GAME_PLANT_UPDATE_INTERVAL / TICKS_PER_HOUR);
            leaf_meet(game, plant);
            if (first_hour) {
                spread_day(game, i, day);
            }
            if (plant_is_tree(plant->type)) {
                grow_tree(game, i, old_growth);
                shed_litter(game, i);
            } else if (plant->type == PLANT_NONE && map->forbidden_count > 0) {
                /* Vanished or dead: what was forbidden of it goes too. */
                map_remove_plant(map, x, y, z);
            }
        }
    }
    update_soil(game, begin, end, phase);
    update_crowns(game, phase);
    if (phase == GAME_PLANT_UPDATE_INTERVAL - 1) {
        leaf_conclude(game); /* The whole pass is done. */
    }
}

void game_refresh_light(Game *game)
{
    tile_light_refresh(&game->light, &game->map, game->clock.tick);
}

float game_direct_share(const Game *game, float elevation)
{
    return shadow_direct_share(elevation, game->weather.type == WEATHER_CLEAR ? 1.0f : 0.0f);
}

float game_tile_light(const Game *game, int x, int y, int z, float direct_share, bool noon)
{
    const Map *map = &game->map;
    if (map->rock[map_index(map, x, y, z)] == ROCK_WOOD) {
        return 1.0f; /* A tree takes its light at the top of its crown. */
    }
    if (z >= game->light.levels) {
        return map_light_cover(map, x, y, z);
    }
    float open = tile_light_open(&game->light, x, y, z);
    float lit =
        noon ? tile_light_noon(&game->light, x, y, z) : tile_light_direct(&game->light, x, y, z);
    return (1.0f - direct_share) * open + direct_share * lit;
}

float game_light_at(const Game *game, int x, int y, int z)
{
    if (!map_in_bounds(&game->map, x, y, z)) {
        return 0.0f;
    }
    float sun = game_sunlight(game->clock.tick);
    if (sun <= 0.0f) {
        return 0.0f;
    }
    float share =
        game_direct_share(game, sun_at_tick(game->clock.tick, SUN_LATITUDE_DEFAULT).elevation);
    return sun * weather_light(&game->weather) * game_tile_light(game, x, y, z, share, false);
}

float game_light_peak_at(const Game *game, int x, int y, int z)
{
    if (!map_in_bounds(&game->map, x, y, z)) {
        return 0.0f;
    }
    int day = game_day_of_year(game->clock.tick);
    float share = game_direct_share(game, sun_position(SUN_LATITUDE_DEFAULT, day, 12.0f).elevation);
    return game_sun_noon(day) * weather_light(&game->weather) *
           game_tile_light(game, x, y, z, share, true);
}

PlantConditions game_plant_conditions(const Game *game, int x, int y, int z)
{
    const Map *map = &game->map;
    size_t index = map_index(map, x, y, z);
    float moisture = soil_moisture(&game->soil, map, x, y, z);
    return (PlantConditions){
        .temperature = temperature_for_plant(&game->temperatures, map, x, y, z),
        .fertility = terrain_def((Terrain)map->terrain[index])->fertility,
        .light = game_light_at(game, x, y, z),
        .light_peak = game_light_peak_at(game, x, y, z),
        .moisture = moisture,
        .moisture_known = moisture >= -0.5f,
    };
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
    int hours = GAME_ITEM_UPDATE_INTERVAL / TICKS_PER_HOUR;
    items_advance(&game->items, begin, end, hours, tile_conditions, game);
}

/* Whether a settler is carrying out the order and holds what it took. */
static bool item_move_busy(const void *context, const ItemMove *order)
{
    const Game *game = context;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) && colonist->task == TASK_HAUL &&
            colonist->haul_forced && colonist->carried.count > 0 &&
            colonist->target.x == order->source.x && colonist->target.y == order->source.y &&
            colonist->target.z == order->source.z && colonist->haul_item.slot == order->item.slot &&
            colonist->haul_item.generation == order->item.generation) {
            return true;
        }
    }
    return false;
}

/* A settler died: its body is left where it fell, the player told, and the
 * others remember it (worse for those who saw it). When it was the last,
 * the colony is lost and the game pauses. */
static void settler_died(Game *game, uint32_t slot)
{
    Colonist *dead = &game->colonists[slot];
    EntityId id = game_colonist_id_at(game, slot);
    int x = dead->x, y = dead->y, z = dead->z;
    _Static_assert(NOTICE_NAME_SIZE >= COLONIST_NAME_SIZE, "a settler's name fits in a notice");
    char name[NOTICE_NAME_SIZE] = {0};
    memcpy(name, dead->name, sizeof(dead->name));
    static const NoticeType DEATH_NOTICES[DEATH_CAUSE_COUNT] = {
        [DEATH_BLOOD_LOSS] = NOTICE_DEATH_BLOOD_LOSS,
        [DEATH_VITAL_PART] = NOTICE_DEATH_INJURY,
        [DEATH_STARVATION] = NOTICE_DEATH_STARVATION,
        [DEATH_DEHYDRATION] = NOTICE_DEATH_DEHYDRATION,
        [DEATH_HYPOTHERMIA] = NOTICE_DEATH_HYPOTHERMIA,
        [DEATH_HEATSTROKE] = NOTICE_DEATH_HEATSTROKE,
    };
    NoticeType notice = (unsigned)dead->health.death < DEATH_CAUSE_COUNT
                            ? DEATH_NOTICES[dead->health.death]
                            : NOTICE_DEATH_INJURY;
    for (uint32_t other = 0; other < game_colonist_slot_count(game); other++) {
        Colonist *mourner = &game->colonists[other];
        if (other == slot || entity_id_is_none(game_colonist_id_at(game, other)) ||
            health_is_dead(&mourner->health)) {
            continue;
        }
        /* Family and friends grieve more, and remember it. */
        ThoughtType grief =
            social_grief(&game->social, game->colonists, (int)other, (int)slot, game->clock.tick);
        mood_remember(&mourner->mood, grief);
        uint32_t hour = (uint32_t)(game->clock.tick / TICKS_PER_HOUR);
        if (grief != THOUGHT_DEATH) {
            experiences_add(&mourner->experiences, EXPERIENCE_LOST_CLOSE_ONE, 0, 0, dead->name,
                            hour);
        }
        /* Without vision (core/vision.h), whoever stands near on the level. */
        int dx = mourner->x - x, dy = mourner->y - y;
        bool saw =
            game->vision_on
                ? vision_viewer_sees(&game->vision, (int)other, x, y, z)
                : mourner->z == z && dx * dx + dy * dy <= GAME_WITNESS_RADIUS * GAME_WITNESS_RADIUS;
        if (saw) {
            mood_remember(&mourner->mood, THOUGHT_SAW_DEATH);
            experiences_add(&mourner->experiences, EXPERIENCE_SAW_DEATH, 0, 0, dead->name, hour);
        }
    }
    /* The body first, where it fell; what it carried around it. */
    ItemStack body = item_stack_pristine(ITEM_SETTLER_BODY, 1);
    body.corpse = corpses_add_settler(&game->corpses, dead, game->clock.tick);
    items_drop(&game->items, &game->map, x, y, z, body);
    game_remove_colonist(game, id);
    vision_viewer_remove(&game->vision, (int)slot);
    notices_push(&game->notices, notice, KNOWLEDGE_NONE, name, game->clock.tick);
    if (game->colonist_pool.count == 0) {
        game->colony_lost = true;
        notices_push(&game->notices, NOTICE_COLONY_LOST, KNOWLEDGE_NONE, NULL, game->clock.tick);
        game_clock_set_speed(&game->clock, GAME_SPEED_PAUSED);
    }
}

static void update_colonists(Game *game);
/* ---- Fights --------------------------------------------------------------- */

/* What the resolver needs of someone in a fight. */
typedef struct Fighter {
    Health *health;
    Journal *journal;
    JournalParty party; /* Who it is, for the journals. */
    TilePos pos;
    float agility;
} Fighter;

static bool fighter_of(Game *game, Combatant who, Fighter *out)
{
    if (who.kind == COMBATANT_SETTLER) {
        Colonist *colonist = game_colonist(game, who.id);
        if (colonist == NULL || health_is_dead(&colonist->health)) {
            return false;
        }
        *out = (Fighter){&colonist->health,
                         &colonist->journal,
                         {colonist->name, 0, 0},
                         {(int16_t)colonist->x, (int16_t)colonist->y, (int16_t)colonist->z},
                         COMBAT_SETTLER_AGILITY};
        return true;
    }
    Animal *animal = who.kind == COMBATANT_ANIMAL ? animals_get(&game->animals, who.id) : NULL;
    if (animal == NULL || health_is_dead(&animal->health)) {
        return false;
    }
    *out = (Fighter){&animal->health,
                     &animal->journal,
                     {NULL, animal->species + 1, animal->sex},
                     {(int16_t)animal->x, (int16_t)animal->y, (int16_t)animal->z},
                     animal_def((AnimalSpecies)animal->species)->agility};
    return true;
}

/* The target of a blow reacts: a settler will fight or flee at its next turn
 * (the player is told when it is the first blow), an animal fights back or
 * runs. */
static void provoke(Game *game, Combatant target, Combatant attacker)
{
    if (target.kind == COMBATANT_SETTLER) {
        Colonist *colonist = game_colonist(game, target.id);
        if (colonist != NULL && colonist_provoke(colonist, attacker)) {
            notices_push(&game->notices, NOTICE_SETTLER_ATTACKED, KNOWLEDGE_NONE, colonist->name,
                         game->clock.tick);
        }
    } else if (target.kind == COMBATANT_ANIMAL) {
        animals_provoke(&game->animals, target.id, attacker);
    }
}

/* Which way someone walking along `path` goes from its tile (0, 0 when still). */
static void walk_direction(const Path *path, int index, bool moving, int x, int y, float *dx,
                           float *dy)
{
    *dx = 0.0f;
    *dy = 0.0f;
    if (moving && index < path->count) {
        *dx = (float)(path->steps[index].x - x);
        *dy = (float)(path->steps[index].y - y);
    }
}

/* Those who bleed leave blood on the ground (core/blood.h). */
static void bleed_on_ground(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        if (entity_id_is_none(game_colonist_id_at(game, slot)) ||
            colonist->health.injury_count == 0) {
            continue;
        }
        BloodSource source = {
            .z = colonist->z, .moving = colonist_is_moving(colonist), .slot = slot};
        colonist_position(colonist, &source.x, &source.y);
        walk_direction(&colonist->path, colonist->path_index, source.moving, colonist->x,
                       colonist->y, &source.dir_x, &source.dir_y);
        blood_bleed(&game->blood, &game->blood_rng, &game->map, &colonist->health, &source,
                    game->clock.tick);
    }
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        const Animal *animal = &game->animals.list[slot];
        if (entity_id_is_none(animals_id_at(&game->animals, slot)) ||
            animal->health.injury_count == 0) {
            continue;
        }
        BloodSource source = {
            .z = animal->z, .moving = animal_is_moving(animal), .slot = COLONIST_MAX + slot};
        animal_position(animal, &source.x, &source.y);
        walk_direction(&animal->path, animal->path_index, source.moving, animal->x, animal->y,
                       &source.dir_x, &source.dir_y);
        blood_bleed(&game->blood, &game->blood_rng, &game->map, &animal->health, &source,
                    game->clock.tick);
    }
}

/* Resolves the blows asked for during the tick, in the order they were asked. */
/* The wounds dealt since the last call that no blow dealt (a fall in a
 * settler's turn...) go in the journals, each with its cause. */
static void note_pending_hurts(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
            Colonist *colonist = &game->colonists[slot];
            journal_note_hurts(&colonist->journal, &colonist->health, game->clock.tick);
        }
    }
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        if (!entity_id_is_none(animals_id_at(&game->animals, slot))) {
            Animal *animal = &game->animals.list[slot];
            journal_note_hurts(&animal->journal, &animal->health, game->clock.tick);
        }
    }
}

/* A blow that wounded or killed: a story to tell for a settler (an animal
 * it killed, one that wounded it), a memory between two settlers. */
static void note_fight_lived(Game *game, Combatant attacker, Combatant target, bool killed)
{
    uint32_t hour = (uint32_t)(game->clock.tick / TICKS_PER_HOUR);
    Colonist *hunter = attacker.kind == COMBATANT_SETTLER ? game_colonist(game, attacker.id) : NULL;
    Colonist *victim = target.kind == COMBATANT_SETTLER ? game_colonist(game, target.id) : NULL;
    const Animal *beast = target.kind == COMBATANT_ANIMAL ? animals_get(&game->animals, target.id)
                          : attacker.kind == COMBATANT_ANIMAL
                              ? animals_get(&game->animals, attacker.id)
                              : NULL;
    if (hunter != NULL && beast != NULL && killed) {
        experiences_add(&hunter->experiences, EXPERIENCE_KILLED_ANIMAL, beast->species, beast->sex,
                        NULL, hour);
    }
    if (victim != NULL && beast != NULL && attacker.kind == COMBATANT_ANIMAL) {
        experiences_add(&victim->experiences, EXPERIENCE_ATTACKED_BY_ANIMAL, beast->species,
                        beast->sex, NULL, hour);
    }
    if (hunter != NULL && victim != NULL) {
        social_note_attack(&game->social, game->colonists, &game->colonist_pool,
                           (int)attacker.id.index, (int)target.id.index, killed, game->clock.tick);
    }
}

static void resolve_strikes(Game *game)
{
    CombatQueue *queue = &game->combat;
    for (int i = 0; i < queue->count; i++) {
        const CombatStrike *strike = &queue->list[i];
        Fighter attacker, target;
        const AttackDef *def = attack_def((AttackType)strike->attack);
        if (def == NULL || !fighter_of(game, strike->attacker, &attacker) ||
            !fighter_of(game, strike->target, &target) || health_is_collapsed(attacker.health) ||
            !combat_can_strike(&game->map, attacker.pos, target.pos, def->reach)) {
            continue; /* Gone, dead, down or out of reach by now. */
        }
        BlowResult blow =
            combat_blow((AttackType)strike->attack, combat_aim(strike->skill, attacker.health),
                        strike->power, target.health, target.agility, &game->combat_rng);
        game->strikes_resolved++;
        health_take_notes(target.health, NULL); /* Noted below, with the attacker. */
        JournalBlow outcome = !blow.hit ? (blow.dodged ? JOURNAL_BLOW_DODGED : JOURNAL_BLOW_MISSED)
                              : blow.killed        ? JOURNAL_BLOW_KILLED
                              : blow.damage > 0.0f ? JOURNAL_BLOW_WOUNDED
                                                   : JOURNAL_BLOW_HARMLESS;
        uint64_t tick = game->clock.tick;
        if (outcome == JOURNAL_BLOW_WOUNDED || outcome == JOURNAL_BLOW_KILLED) {
            journal_note_wound(target.journal, tick, strike->attack, target.health, blow.part,
                               def->injury, attacker.party);
        } else {
            journal_note_struck(target.journal, tick, strike->attack, outcome, target.health,
                                blow.part, attacker.party);
        }
        journal_note_strike(attacker.journal, tick, strike->attack, outcome, target.health,
                            blow.part, def->injury, target.party);
        if (outcome == JOURNAL_BLOW_WOUNDED || outcome == JOURNAL_BLOW_KILLED) {
            note_fight_lived(game, strike->attacker, strike->target,
                             outcome == JOURNAL_BLOW_KILLED);
        }
        combat_log_push(&game->combat_log,
                        (CombatEvent){game->clock.tick, strike->attacker, strike->target,
                                      target.pos, strike->attack, blow.hit});
        provoke(game, strike->target, strike->attacker);
    }
    queue->count = 0;
}

Combatant game_combatant_at(const Game *game, int x, int y, int z)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            !health_is_dead(&colonist->health) && colonist->x == x && colonist->y == y &&
            colonist->z == z) {
            return (Combatant){COMBATANT_SETTLER, game_colonist_id_at(game, slot)};
        }
    }
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        const Animal *animal = &game->animals.list[slot];
        if (!entity_id_is_none(animals_id_at(&game->animals, slot)) &&
            !health_is_dead(&animal->health) && animal->x == x && animal->y == y &&
            animal->z == z) {
            return (Combatant){COMBATANT_ANIMAL, animals_id_at(&game->animals, slot)};
        }
    }
    return combatant_none();
}

bool game_debug_attack(Game *game, Combatant attacker, Combatant target)
{
    Fighter ignored;
    if (!fighter_of(game, attacker, &ignored) || !fighter_of(game, target, &ignored) ||
        combatant_equal(attacker, target)) {
        return false;
    }
    if (attacker.kind == COMBATANT_ANIMAL) {
        return animals_attack(&game->animals, attacker.id, target);
    }
    Colonist *colonist = game_colonist(game, attacker.id);
    if (target.kind != COMBATANT_ANIMAL || colonist == NULL) {
        return false;
    }
    colonist->enemy = target;
    return true;
}

/* Before the turns: those gone (removed, dead) hold no tile, and newcomers
 * are counted where they stand, so that the first to act already sees them
 * (afterwards, each keeps its own state up to date on its turn). */
static void sweep_one(Occupancy *occupancy, BeingId being, bool here, TilePos at)
{
    const OccupancyBeing *state = &occupancy->beings[being];
    if (!here) {
        if (state->placed || state->claiming) {
            occupancy_remove(occupancy, being);
        }
    } else if (!state->placed) {
        occupancy_place(occupancy, being, at);
    }
}

static void sweep_occupancy(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        sweep_one(&game->occupancy, occupancy_settler(slot),
                  !entity_id_is_none(game_colonist_id_at(game, slot)) &&
                      !health_is_dead(&colonist->health),
                  (TilePos){(int16_t)colonist->x, (int16_t)colonist->y, (int16_t)colonist->z});
    }
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        const Animal *animal = &game->animals.list[slot];
        sweep_one(&game->occupancy, occupancy_animal(slot),
                  !entity_id_is_none(animals_id_at(&game->animals, slot)) &&
                      !health_is_dead(&animal->health),
                  (TilePos){(int16_t)animal->x, (int16_t)animal->y, (int16_t)animal->z});
    }
}

static void update_colonists(Game *game)
{
    /* Fixed slot order keeps the simulation deterministic. */
    ColonistContext context = colonist_context(game, 0); /* The same for everyone... */
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
            context.worker_id = (uint8_t)(slot + 1); /* ...but for who is acting. */
            context.self = game_colonist_id_at(game, slot);
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
            /* Told once each time it collapses (struck down by a blow too: a
             * tick later). */
            bool collapsed = health_is_collapsed(&colonist->health);
            if (collapsed && !colonist->collapse_noticed) {
                notices_push(&game->notices, NOTICE_SETTLER_COLLAPSED, KNOWLEDGE_NONE,
                             colonist->name, game->clock.tick);
            }
            colonist->collapse_noticed = collapsed;
            if (colonist->draft_end != DRAFT_END_NONE) {
                static const NoticeType UNDRAFTED[DRAFT_END_COUNT] = {
                    [DRAFT_END_BREAK] = NOTICE_UNDRAFTED_BREAK,
                    [DRAFT_END_FLED] = NOTICE_UNDRAFTED_FLED,
                    [DRAFT_END_DOWN] = NOTICE_UNDRAFTED_DOWN,
                };
                if (colonist->draft_end < DRAFT_END_COUNT) {
                    notices_push(&game->notices, UNDRAFTED[colonist->draft_end], KNOWLEDGE_NONE,
                                 colonist->name, game->clock.tick);
                }
                colonist->draft_end = DRAFT_END_NONE;
            }
            if (health_is_dead(&colonist->health)) {
                settler_died(game, slot);
            }
        }
    }
}

/* Unpractised knowledges fade a little, once a day. */
/* The animals' turn, after the settlers': they fear every settler alive. */
static void update_animals(Game *game)
{
    if (game->animals.pool.count == 0) {
        return;
    }
    TilePos threats[COLONIST_MAX];
    AnimalSettler settlers[COLONIST_MAX];
    int threat_count = 0, settler_count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        const Colonist *colonist = &game->colonists[slot];
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            !health_is_dead(&colonist->health)) {
            settlers[settler_count++] =
                (AnimalSettler){game_colonist_id_at(game, slot),
                                {(int16_t)colonist->x, (int16_t)colonist->y, (int16_t)colonist->z}};
        }
        /* (Someone lying collapsed frightens nobody.) */
        if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
            !health_is_collapsed(&colonist->health) && !health_is_dead(&colonist->health)) {
            threats[threat_count++] =
                (TilePos){(int16_t)colonist->x, (int16_t)colonist->y, (int16_t)colonist->z};
        }
    }
    regions_update(&game->regions, &game->map); /* A settler may have changed the map. */
    AnimalContext context = {
        .map = &game->map,
        .pathfinder = &game->pathfinder,
        .regions = &game->regions,
        .rng = &game->wildlife_rng,
        .tick = game->clock.tick,
        .threats = threats,
        .threat_count = threat_count,
        .settlers = settlers,
        .settler_count = settler_count,
        .combat = &game->combat,
        .items = &game->items,
        .temperatures = &game->temperatures,
        .occupancy = &game->occupancy,
    };
    animals_tick(&game->animals, &context);
    /* The dead leave their carcass where they fell; their herd follows
     * another leader. */
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        EntityId id = animals_id_at(&game->animals, slot);
        const Animal *animal = &game->animals.list[slot];
        if (entity_id_is_none(id) || !health_is_dead(&animal->health)) {
            continue;
        }
        int x = animal->x, y = animal->y, z = animal->z;
        ItemStack carcass =
            item_stack_pristine(animal_def((AnimalSpecies)animal->species)->carcass, 1);
        carcass.corpse = corpses_add_animal(&game->corpses, animal, game->clock.tick);
        animals_remove(&game->animals, id);
        occupancy_remove(&game->occupancy, occupancy_animal(slot)); /* A carcass: walked over. */
        items_drop(&game->items, &game->map, x, y, z, carcass);
    }
}

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
            temperatures_heat_source(&game->temperatures, station->x, station->y, station->z,
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

bool game_set_vision(Game *game, bool on)
{
    if (on == game->vision_on) {
        return true;
    }
    if (!on) {
        game->items.mirror = NULL;
        vision_free(&game->vision);
        game->vision_on = false;
        game->fog_on = false; /* The fog needs the vision. */
        game->fog_started = false;
        return true;
    }
    if (!vision_init(&game->vision, &game->map)) {
        return false;
    }
    vision_limit_casts(&game->vision, true);
    game->vision_on = true;
    return true;
}

/* Slices over which the tiles in view are copied into the known world again
 * (they change: plants grow, items are carried off). */
#define KNOWN_SYNC_SLICES 48

/* Copies the tiles of a field of bits into the known world, a run of
 * consecutive tiles at a time. */
static void copy_known_bits(Game *game, size_t word, uint64_t bits)
{
    while (bits != 0) {
        int start = lowest_bit64(bits);
        uint64_t rest = ~(bits >> start);
        int length = rest == 0 ? 64 - start : lowest_bit64(rest);
        known_copy_run(&game->known, &game->map, &game->items, &game->dirt, &game->workstations,
                       word * 64 + (size_t)start, (size_t)length);
        uint64_t run = length == 64 ? ~(uint64_t)0 : (((uint64_t)1 << length) - 1) << start;
        bits &= ~run;
    }
}

/* Copies into the known world the tiles that came into view, went out of it
 * or were explored since the last call (a last time for those leaving, so
 * that the known world holds what was seen last), and a slice of those in
 * view. */
static void sync_known(Game *game)
{
    Vision *vision = &game->vision;
    if (vision->snapshot_pending) {
        for (size_t word = 0; word < vision->words; word++) {
            uint64_t bits = vision->snapshot[word];
            vision->snapshot[word] = 0;
            copy_known_bits(game, word, bits);
        }
        vision->snapshot_pending = false;
    }
    size_t slice = (size_t)(game->clock.tick % KNOWN_SYNC_SLICES);
    for (size_t word = vision->words * slice / KNOWN_SYNC_SLICES;
         word < vision->words * (slice + 1) / KNOWN_SYNC_SLICES; word++) {
        copy_known_bits(game, word, vision->seen_bits[word]);
    }
}

/* Puts every settler where it stands in the vision (fields are recast when it
 * moved or fell asleep), and takes away those who are gone or unconscious. */
/* Doors somebody stands in are open for the light and the sight, the others
 * close behind them (Map.doors); the settlers who see through are told. */
static void update_doors(Game *game)
{
    Map *map = &game->map;
    if (map->structure_counts[STRUCTURE_DOOR] == 0 && game->passing_door_count == 0) {
        return;
    }
    uint32_t now[OCCUPANCY_BEINGS];
    int count = 0;
    for (int being = 1; being <= OCCUPANCY_BEINGS; being++) {
        const OccupancyBeing *state = &game->occupancy.beings[being];
        TilePos at = state->at;
        if (!state->placed || !map_in_bounds(map, at.x, at.y, at.z)) {
            continue;
        }
        uint32_t index = (uint32_t)map_index(map, at.x, at.y, at.z);
        bool known = map->structure[index] != STRUCTURE_DOOR;
        for (int i = 0; i < count && !known; i++) {
            known = now[i] == index;
        }
        if (!known) {
            now[count++] = index;
        }
    }
    size_t level = map_level_tile_count(map);
    for (int pass = 0; pass < 2; pass++) { /* Those left behind, then those entered. */
        const uint32_t *from = pass == 0 ? game->passing_doors : now;
        const uint32_t *to = pass == 0 ? now : game->passing_doors;
        int from_count = pass == 0 ? game->passing_door_count : count;
        int to_count = pass == 0 ? count : game->passing_door_count;
        for (int i = 0; i < from_count; i++) {
            bool kept = false;
            for (int j = 0; j < to_count && !kept; j++) {
                kept = to[j] == from[i];
            }
            if (kept || (pass == 0 && map->structure[from[i]] != STRUCTURE_DOOR)) {
                continue;
            }
            map_set_door_passing(map, from[i], pass == 1);
            if (game->vision_on) {
                size_t column = from[i] % level;
                vision_tile_changed(&game->vision, (int)(column % (size_t)map->width),
                                    (int)(column / (size_t)map->width), (int)(from[i] / level));
            }
        }
    }
    memcpy(game->passing_doors, now, (size_t)count * sizeof(now[0]));
    game->passing_door_count = count;
}

static void update_vision(Game *game)
{
    vision_update_map(&game->vision, &game->map);
    _Static_assert(COLONIST_MAX <= VISION_MAX_VIEWERS, "a viewer for each settler");
    /* Starting from a different settler each tick, so that none always waits. */
    uint32_t slots = game_colonist_slot_count(game);
    for (uint32_t turn = 0; turn < slots; turn++) {
        uint32_t slot = (turn + (uint32_t)game->clock.tick) % slots;
        const Colonist *colonist = &game->colonists[slot];
        if (entity_id_is_none(game_colonist_id_at(game, slot)) ||
            !pool_is_alive(&game->colonist_pool, game_colonist_id_at(game, slot))) {
            vision_viewer_remove(&game->vision, (int)slot);
            continue;
        }
        if (health_is_unconscious(&colonist->health) || health_is_dead(&colonist->health)) {
            vision_viewer_remove(&game->vision, (int)slot); /* Sees nothing. */
            continue;
        }
        vision_viewer_set_limited(&game->vision, &game->map, (int)slot, colonist->x, colonist->y,
                                  colonist->z, colonist->task == TASK_SLEEP,
                                  health_sight_reach(&colonist->health), game->clock.tick);
    }
    if (game->fog_on) {
        sync_known(game);
        known_update_animals(&game->known, &game->animals, &game->vision, game->clock.tick);
    }
}

bool game_set_fog(Game *game, bool on)
{
    if (!on) {
        game->fog_on = false;
        game->items.mirror = NULL;
        return true;
    }
    if (game->fog_on) {
        return true;
    }
    if (!game_set_vision(game, true)) {
        return false;
    }
    if (!game->known_ready) {
        if (!known_init(&game->known, &game->map)) {
            return false;
        }
        game->known_ready = true;
    }
    if (!game->fog_started) {
        game->fog_started = true;
        TilePos center = game->arrival;
        if (!game->has_arrival) {
            int x = game->map.width / 2, y = game->map.height / 2;
            center = (TilePos){(int16_t)x, (int16_t)y, (int16_t)map_ground_level(&game->map, x, y)};
        }
        exploration_start(&game->vision, &game->map, &game->pathfinder, center, game->seed);
    }
    /* Everything explored is copied afresh, then what the settlers see now. */
    Vision *vision = &game->vision;
    for (size_t word = 0; word < vision->words; word++) {
        vision->snapshot[word] |= vision->explored[word];
    }
    vision->snapshot_pending = true;
    game->fog_on = true;
    game->items.mirror = &game->known.items;
    game->items.mirror_when = game->vision.seen_bits;
    update_vision(game);
    return true;
}

void game_reveal_map(Game *game)
{
    if (game->vision_on) {
        vision_explore_all(&game->vision, &game->map);
    }
}

const Items *game_items_at(const Game *game, int x, int y, int z)
{
    return !game->fog_on || vision_seen(&game->vision, x, y, z) ? &game->items : &game->known.items;
}

bool game_tile_explored(const Game *game, int x, int y, int z)
{
    return !game->fog_on ? map_in_bounds(&game->map, x, y, z)
                         : vision_explored(&game->vision, x, y, z);
}

bool game_tile_seen(const Game *game, int x, int y, int z)
{
    return !game->fog_on ? map_in_bounds(&game->map, x, y, z) : vision_seen(&game->vision, x, y, z);
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

bool game_debug_colonist_need(Game *game, EntityId id, NeedType need, int direction)
{
    Colonist *colonist = game_colonist(game, id);
    if (colonist == NULL || (unsigned)need >= NEED_COUNT) {
        return false;
    }
    float step = direction > 0 ? GAME_DEBUG_NEED_STEP : -GAME_DEBUG_NEED_STEP;
    colonist->needs[need] = clampf(colonist->needs[need] + step, 0.0f, 1.0f);
    return true;
}

bool game_debug_animal_need(Game *game, EntityId id, AnimalNeed need, int direction)
{
    Animal *animal = animals_get(&game->animals, id);
    if (animal == NULL || (unsigned)need >= ANIMAL_NEED_COUNT) {
        return false;
    }
    float step = direction > 0 ? GAME_DEBUG_NEED_STEP : -GAME_DEBUG_NEED_STEP;
    animal->needs[need] = clampf(animal->needs[need] + step, 0.0f, 1.0f);
    return true;
}

bool game_debug_colonist_attribute(Game *game, EntityId id, AttributeType type, int direction)
{
    Colonist *colonist = game_colonist(game, id);
    if (colonist == NULL || (unsigned)type >= ATTRIBUTE_COUNT) {
        return false;
    }
    Attributes attributes = colonist->attributes;
    int position = attributes.position[type] +
                   (direction > 0 ? GAME_DEBUG_ATTRIBUTE_STEP : -GAME_DEBUG_ATTRIBUTE_STEP);
    position = position > ATTRIBUTE_POSITION_MAX    ? ATTRIBUTE_POSITION_MAX
               : position < -ATTRIBUTE_POSITION_MAX ? -ATTRIBUTE_POSITION_MAX
                                                    : position;
    if (!attributes_set(&attributes, type, position)) {
        return false;
    }
    colonist_set_attributes(colonist, &attributes);
    return true;
}

bool game_debug_colonist_knowledge(Game *game, EntityId id, KnowledgeType knowledge, int direction)
{
    Colonist *colonist = game_colonist(game, id);
    if (colonist == NULL || (unsigned)knowledge >= KNOWLEDGE_COUNT) {
        return false;
    }
    double level = colonist->knowledge[knowledge] +
                   (direction > 0 ? GAME_DEBUG_KNOWLEDGE_STEP : -GAME_DEBUG_KNOWLEDGE_STEP);
    level = level < KNOWLEDGE_LEVEL_MIN   ? KNOWLEDGE_LEVEL_MIN
            : level > KNOWLEDGE_LEVEL_MAX ? KNOWLEDGE_LEVEL_MAX
                                          : level;
    colonist->knowledge[knowledge] = level;
    colonist->knowledge_peak[knowledge] = level;
    colonist->knowledge_practice[knowledge] = game->clock.tick;
    return true;
}

void game_debug_temperature(Game *game, int direction)
{
    float offset = game->weather.debug_offset +
                   (direction > 0 ? GAME_DEBUG_TEMPERATURE_STEP : -GAME_DEBUG_TEMPERATURE_STEP);
    game->weather.debug_offset =
        clampf(offset, -GAME_DEBUG_TEMPERATURE_LIMIT, GAME_DEBUG_TEMPERATURE_LIMIT);
}

void game_debug_next_weather(Game *game)
{
    game->weather.type = (uint8_t)((game->weather.type + 1u) % WEATHER_TYPE_COUNT);
}

void game_debug_cold_snap(Game *game)
{
    game->weather.spell = -WEATHER_SPELL_MAX;
}

/* ---- Debug: social life --------------------------------------------------------- */

static ConversationWorld conversation_world(Game *game)
{
    return (ConversationWorld){
        .colonists = game->colonists,
        .pool = &game->colonist_pool,
        .social = &game->social,
        .tick = game->clock.tick,
        .weather = weather_precipitation(&game->weather, game->clock.tick) == PRECIPITATION_SNOW
                       ? JOURNAL_WEATHER_SNOW
                       : game->weather.type,
        .counts = &game->chat_counts,
    };
}

/* The slot of a living settler, -1 otherwise. */
static int living_slot(const Game *game, EntityId id)
{
    const Colonist *colonist = game_colonist_const(game, id);
    return colonist != NULL && !health_is_dead(&colonist->health) ? (int)id.index : -1;
}

/* A living settler other than `a` and `b`, -1 when none. */
static int third_settler(const Game *game, int a, int b)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        int c = (int)slot;
        if (c != a && c != b && living_slot(game, game_colonist_id_at(game, slot)) == c) {
            return c;
        }
    }
    return -1;
}

static void update_relationships(Game *game, bool notes)
{
    social_update(&game->social, game->colonists, &game->colonist_pool, game->clock.tick, notes);
}

bool game_debug_chat(Game *game, EntityId a_id, EntityId b_id, int kind, ChatResult *out)
{
    int a = living_slot(game, a_id), b = living_slot(game, b_id);
    if (a < 0 || b < 0 || a == b || kind < GAME_DEBUG_CHAT_ANY || kind > GAME_DEBUG_CHAT_BRAWL) {
        return false;
    }
    ConversationWorld world = conversation_world(game);
    uint64_t tick = game->clock.tick;
    int c = third_settler(game, a, b);
    if (kind == CHAT_GOSSIP && conversation_gossip_subject(&world, a, b) < 0 && c >= 0) {
        social_debug_set_opinion(&game->social, game->colonists, a, c,
                                 -CONVERSATION_GOSSIP_OPINION - 20.0f, tick);
    }
    if (kind == CHAT_REPORT && social_tale_about(&game->social, a, b, tick) == NULL && c >= 0) {
        social_hear_tale(&game->social, a, c, b, false, tick);
    }
    if (kind == CHAT_STORY && game->colonists[a].experiences.count == 0) {
        experiences_add(&game->colonists[a].experiences, EXPERIENCE_KILLED_ANIMAL, ANIMAL_WOLF,
                        ANIMAL_MALE, NULL, (uint32_t)(tick / TICKS_PER_HOUR));
    }
    ChatResult result = kind == GAME_DEBUG_CHAT_ANY ? conversation_hold(&world, a, b)
                        : kind == GAME_DEBUG_CHAT_BRAWL
                            ? conversation_brawl(&world, a, b)
                            : conversation_hold_as(&world, a, b, (ChatKind)kind);
    if (out != NULL) {
        *out = result;
    }
    return true;
}

bool game_debug_opinion(Game *game, EntityId who_id, EntityId whom_id, int direction)
{
    int who = living_slot(game, who_id), whom = living_slot(game, whom_id);
    if (who < 0 || whom < 0 || who == whom) {
        return false;
    }
    social_debug_nudge(&game->social, who, whom,
                       direction > 0 ? GAME_DEBUG_OPINION_STEP : -GAME_DEBUG_OPINION_STEP,
                       game->clock.tick);
    update_relationships(game, true);
    return true;
}

static float level_opinion(RelationLevel level)
{
    switch (level) {
    case RELATION_ENEMY: return GAME_DEBUG_ENEMY_OPINION;
    case RELATION_RIVAL: return GAME_DEBUG_RIVAL_OPINION;
    case RELATION_FRIEND: return GAME_DEBUG_FRIEND_OPINION;
    case RELATION_CLOSE_FRIEND: return GAME_DEBUG_CLOSE_FRIEND_OPINION;
    default: return 0.0f;
    }
}

bool game_debug_relation(Game *game, EntityId a_id, EntityId b_id, RelationLevel level)
{
    int a = living_slot(game, a_id), b = living_slot(game, b_id);
    if (a < 0 || b < 0 || a == b || (unsigned)level >= RELATION_LEVEL_COUNT) {
        return false;
    }
    float target = level_opinion(level);
    social_debug_set_opinion(&game->social, game->colonists, a, b, target, game->clock.tick);
    social_debug_set_opinion(&game->social, game->colonists, b, a, target, game->clock.tick);
    update_relationships(game, true);
    return true;
}

bool game_debug_family(Game *game, EntityId a_id, EntityId b_id)
{
    int a = living_slot(game, a_id), b = living_slot(game, b_id);
    if (a < 0 || b < 0 || a == b) {
        return false;
    }
    FamilyTie tie = (FamilyTie)social_bond(&game->social, a, b)->family;
    social_set_family(&game->social, a, b, (FamilyTie)((tie + 1) % FAMILY_TIE_COUNT));
    update_relationships(game, true);
    return true;
}

void game_debug_discord(Game *game)
{
    uint32_t slots = game_colonist_slot_count(game);
    for (uint32_t who = 0; who < slots; who++) {
        for (uint32_t whom = 0; whom < slots; whom++) {
            if (who == whom || living_slot(game, game_colonist_id_at(game, who)) < 0 ||
                living_slot(game, game_colonist_id_at(game, whom)) < 0) {
                continue;
            }
            bool enemy = rng_chance(&game->social.rng, 1.0f / GAME_DEBUG_DISCORD_ENEMIES);
            social_debug_set_opinion(&game->social, game->colonists, (int)who, (int)whom,
                                     enemy ? GAME_DEBUG_ENEMY_OPINION : GAME_DEBUG_RIVAL_OPINION,
                                     game->clock.tick);
        }
    }
    update_relationships(game, true);
}

/* Sets an attribute of a settler to a fraction of its range (-1..1). */
static void set_attribute(Colonist *colonist, AttributeType type, float fraction)
{
    Attributes attributes = colonist->attributes;
    attributes_set(&attributes, type, (int)(fraction * (float)ATTRIBUTE_POSITION_MAX));
    colonist_set_attributes(colonist, &attributes);
}

void game_debug_feud(Game *game)
{
    int living[COLONIST_MAX];
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (living_slot(game, game_colonist_id_at(game, slot)) >= 0) {
            living[count++] = (int)slot;
        }
    }
    for (int i = 0; i < count; i++) {
        Colonist *colonist = &game->colonists[living[i]];
        bool abrasive = (i / 2) % 2 == 0; /* Every other settler of its clan. */
        set_attribute(colonist, ATTRIBUTE_KINDNESS, abrasive ? -0.8f : 0.6f);
        set_attribute(colonist, ATTRIBUTE_CONFIDENCE, abrasive ? 0.6f : 0.0f);
    }
    for (int i = 0; i < count; i++) {
        for (int j = 0; j < count; j++) {
            if (i != j) {
                float target =
                    i % 2 == j % 2 ? GAME_DEBUG_FRIEND_OPINION : GAME_DEBUG_ENEMY_OPINION;
                social_debug_set_opinion(&game->social, game->colonists, living[i], living[j],
                                         target, game->clock.tick);
            }
        }
    }
    update_relationships(game, false);
}

void game_debug_forget_social(Game *game)
{
    social_debug_clear(&game->social, (int)game_colonist_slot_count(game));
    update_relationships(game, true);
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
    game_settle_climate(game);
}

void game_settle_climate(Game *game)
{
    uint64_t tick = game->clock.tick;
    float outdoor = weather_outdoor(&game->weather, tick);
    Temperatures *temperatures = &game->temperatures;
    size_t count = map_tile_count(&game->map);
    for (size_t i = 0; temperatures->celsius != NULL && i < count; i++) {
        if (temperatures->kind[i] / TEMPERATURE_COVER_COUNT != TEMPERATURE_MATERIAL_ROCK) {
            temperatures->celsius[i] = outdoor;
        }
    }
    temperatures->outdoor = outdoor;
    temperatures_settle_ground(temperatures, temperature_season_mean(tick) + game->weather.spell);
}

ItemId game_item_at_point(const Game *game, float x, float y, int z)
{
    return items_at_point(&game->items, x, y, z);
}

int game_items_under_point(const Game *game, float x, float y, int z, ItemPick *out, int capacity)
{
    int tx = (int)floorf(x), ty = (int)floorf(y), count = 0;
    /* The tiles drawn last first: the row below, then this one, then the
     * row above, each from the right. */
    for (int dy = 1; dy >= -1; dy--) {
        for (int dx = 1; dx >= -1; dx--) {
            int cx = tx + dx, cy = ty + dy;
            if (!map_in_bounds(&game->map, cx, cy, z)) {
                continue;
            }
            const Items *items = game_items_at(game, cx, cy, z);
            int drawn = 0;
            for (const Item *item = items_first(items, map_index(&game->map, cx, cy, z));
                 item != NULL && drawn < ITEM_DRAWN_PER_TILE && count < capacity;
                 item = items_next(items, item), drawn++) {
                float ix = (float)cx + ((float)item->spot_x + 0.5f) / 256.0f;
                float iy = (float)cy + ((float)item->spot_y + 0.5f) / 256.0f;
                float radius = item_drawn_size((ItemType)item->type) * 0.5f;
                if ((ix - x) * (ix - x) + (iy - y) * (iy - y) <= radius * radius) {
                    out[count++] =
                        (ItemPick){items_id(items, item), (int16_t)cx, (int16_t)cy, (int16_t)z};
                }
            }
        }
    }
    return count;
}

/* game_items_in_rect() of the items of one type (ITEM_NONE: any). */
static int items_in_rect_of(const Game *game, float x0, float y0, float x1, float y1, int z,
                            int type, ItemPick *out, int capacity)
{
    float min_x = fminf(x0, x1), max_x = fmaxf(x0, x1);
    float min_y = fminf(y0, y1), max_y = fmaxf(y0, y1);
    int first_x = (int)floorf(min_x), last_x = (int)floorf(max_x);
    int first_y = (int)floorf(min_y), last_y = (int)floorf(max_y);
    first_x = first_x < 0 ? 0 : first_x;
    first_y = first_y < 0 ? 0 : first_y;
    last_x = last_x >= game->map.width ? game->map.width - 1 : last_x;
    last_y = last_y >= game->map.height ? game->map.height - 1 : last_y;
    int count = 0;
    for (int y = first_y; y <= last_y; y++) {
        for (int x = first_x; x <= last_x; x++) {
            if (!map_in_bounds(&game->map, x, y, z)) {
                continue;
            }
            const Items *items = game_items_at(game, x, y, z);
            int drawn = 0;
            for (const Item *item = items_first(items, map_index(&game->map, x, y, z));
                 item != NULL && drawn < ITEM_DRAWN_PER_TILE && count < capacity;
                 item = items_next(items, item), drawn++) {
                float ix = (float)x + ((float)item->spot_x + 0.5f) / 256.0f;
                float iy = (float)y + ((float)item->spot_y + 0.5f) / 256.0f;
                if (ix >= min_x && ix <= max_x && iy >= min_y && iy <= max_y &&
                    (type == ITEM_NONE || item->type == type)) {
                    out[count++] =
                        (ItemPick){items_id(items, item), (int16_t)x, (int16_t)y, (int16_t)z};
                }
            }
        }
    }
    return count;
}

int game_items_in_rect(const Game *game, float x0, float y0, float x1, float y1, int z,
                       ItemPick *out, int capacity)
{
    return items_in_rect_of(game, x0, y0, x1, y1, z, ITEM_NONE, out, capacity);
}

int game_items_of_type_in_rect(const Game *game, float x0, float y0, float x1, float y1, int z,
                               ItemType type, ItemPick *out, int capacity)
{
    if (type == ITEM_NONE) {
        return 0;
    }
    return items_in_rect_of(game, x0, y0, x1, y1, z, type, out, capacity);
}

/* The pick's item among `items`, NULL when not there; follows it onto
 * another tile. */
static const Item *pick_in(const Game *game, const Items *items, ItemPick *pick)
{
    const Item *item = items_by_id(items, pick->id);
    if (item == NULL) {
        return NULL;
    }
    size_t index = map_index(&game->map, pick->x, pick->y, pick->z);
    if (item->tile != index) { /* Moved (the debug tool). */
        size_t level = map_level_tile_count(&game->map);
        pick->z = (int16_t)(item->tile / level);
        pick->y = (int16_t)(item->tile % level / (size_t)game->map.width);
        pick->x = (int16_t)(item->tile % (size_t)game->map.width);
    }
    return item;
}

const Item *game_pick_item(const Game *game, ItemPick *pick)
{
    if (pick->id.slot == UINT32_MAX || !map_in_bounds(&game->map, pick->x, pick->y, pick->z)) {
        return NULL;
    }
    const Items *items = game_items_at(game, pick->x, pick->y, pick->z);
    const Item *item = pick_in(game, items, pick);
    if (item != NULL || !game->fog_on || !game->known_ready) {
        return item;
    }
    /* Its tile came into view or left it: the same item among the others. */
    const Items *other = items == &game->items ? &game->known.items : &game->items;
    const Item *before = pick_in(game, other, pick);
    if (before == NULL) {
        return NULL;
    }
    items = game_items_at(game, pick->x, pick->y, pick->z);
    ItemId id = items_find_like(items, pick->x, pick->y, pick->z, before);
    if (id.slot == UINT32_MAX) {
        return NULL;
    }
    pick->id = id;
    return items_by_id(items, id);
}

bool game_debug_move_item(Game *game, ItemId id, float x, float y, int z)
{
    int tx = (int)floorf(x), ty = (int)floorf(y);
    if (!map_is_passable(&game->map, tx, ty, z)) {
        return false;
    }
    int spot_x = (int)floorf((x - (float)tx) * 256.0f);
    int spot_y = (int)floorf((y - (float)ty) * 256.0f);
    return items_move(&game->items, id, tx, ty, z, spot_x, spot_y);
}

bool game_debug_turn_item(Game *game, ItemId id, int steps)
{
    return items_turn(&game->items, id, steps);
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
        ItemStack stack =
            item_stack_make(type, batch, ITEM_QUALITY_STANDARD, item_def(type)->max_hit_points);
        int dropped = items_drop(&game->items, &game->map, x, y, z, stack);
        placed += dropped;
        if (dropped < batch) {
            break; /* No room left around. */
        }
    }
    return placed;
}

bool game_debug_plant_tree(Game *game, int x, int y, int z)
{
    Map *map = &game->map;
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    size_t index = map_index(map, x, y, z);
    if (map->rock[index] != ROCK_NONE || map->structure[index] != STRUCTURE_NONE ||
        map_is_slope(map, x, y, z) ||
        terrain_def((Terrain)map->terrain[index])->fertility <
            plant_def(PLANT_OAK)->min_fertility) {
        return false;
    }
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if ((dx != 0 || dy != 0) && map_in_bounds(map, x + dx, y + dy, z) &&
                plant_is_tree(map->plants[map_index(map, x + dx, y + dy, z)].type)) {
                return false; /* Trunks never touch (see worldgen). */
            }
        }
    }
    if (plant_is_tree(map->plants[index].type)) {
        return false;
    }
    map->plants[index] = (Plant){.type = PLANT_OAK}; /* The commonest tree. */
    return true;
}

bool game_debug_grow_tree(Game *game, int x, int y, int z, bool fully)
{
    Map *map = &game->map;
    TreeRef tree;
    if (!map_in_bounds(map, x, y, z)) {
        return false;
    }
    if (plant_is_tree(map->plants[map_index(map, x, y, z)].type)) {
        tree = (TreeRef){.x = x, .y = y, .z = z}; /* A seedling or the foot. */
    } else if (!tree_at(map, x, y, z, &tree)) {
        return false;
    }
    size_t index = map_index(map, tree.x, tree.y, tree.z);
    Plant *plant = &map->plants[index];
    float old_growth = plant->growth;
    if (old_growth >= 1.0f) {
        return false;
    }
    /* To the next multiple of the step (no drift from adding 0.1 again and again). */
    float steps = floorf(old_growth / GAME_DEBUG_TREE_GROWTH_STEP + 0.01f) + 1.0f;
    float growth = fully ? 1.0f : steps * GAME_DEBUG_TREE_GROWTH_STEP;
    plant->growth = growth < 1.0f ? growth : 1.0f;
    grow_tree(game, index, old_growth); /* Back to `old_growth` if it has no room. */
    return plant->growth > old_growth;
}

/* A number between `min` and `max` from the game's draws. */
static float roll_between(Game *game, float min, float max)
{
    return min + (max - min) * rng_next_float(&game->rng);
}

/* A blow of `min` to `max` points (times the body's size when `sized`) on
 * every settler and animal on the tile; `type` INJURY_TYPE_COUNT: of a random
 * kind for each; `cause` goes in their journals. Returns how many were hurt. */
static int hurt_beings_at(Game *game, int x, int y, int z, InjuryType type, float min, float max,
                          bool sized, HurtCause cause)
{
    int hurt = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        Colonist *colonist = &game->colonists[slot];
        if (entity_id_is_none(game_colonist_id_at(game, slot)) || colonist->x != x ||
            colonist->y != y || colonist->z != z) {
            continue;
        }
        InjuryType kind = type == INJURY_TYPE_COUNT
                              ? (InjuryType)rng_range_i32(&game->rng, 0, INJURY_TYPE_COUNT - 1)
                              : type;
        float damage = roll_between(game, min, max) * (sized ? colonist->health.size : 1.0f);
        colonist->health.hurt_cause = (uint8_t)cause;
        hurt += health_blow(&colonist->health, &game->rng, kind, damage) > 0.0f;
        colonist->health.hurt_cause = HURT_OTHER;
        journal_note_hurts(&colonist->journal, &colonist->health, game->clock.tick);
    }
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        Animal *animal = &game->animals.list[slot];
        if (entity_id_is_none(animals_id_at(&game->animals, slot)) || animal->x != x ||
            animal->y != y || animal->z != z) {
            continue;
        }
        InjuryType kind = type == INJURY_TYPE_COUNT
                              ? (InjuryType)rng_range_i32(&game->rng, 0, INJURY_TYPE_COUNT - 1)
                              : type;
        float damage = roll_between(game, min, max) * (sized ? animal->health.size : 1.0f);
        animal->health.hurt_cause = (uint8_t)cause;
        hurt += health_blow(&animal->health, &game->rng, kind, damage) > 0.0f;
        animal->health.hurt_cause = HURT_OTHER;
        journal_note_hurts(&animal->journal, &animal->health, game->clock.tick);
    }
    return hurt;
}

int game_debug_hurt(Game *game, int x, int y, int z)
{
    return hurt_beings_at(game, x, y, z, INJURY_TYPE_COUNT, GAME_DEBUG_BLOW_MIN,
                          GAME_DEBUG_BLOW_MAX, true, HURT_OTHER);
}

/* Trees felled since the last call came down on whoever stood there: the
 * trunk crushes, the crown bruises; the woodcutter, beside the foot, steps
 * back (the first tiles of the band are spared). */
static void hurt_under_felled_trees(Game *game)
{
    const Map *map = &game->map;
    uint32_t falls = map->tree_fall_revision - game->tree_falls_seen;
    if (falls > MAP_TREE_FALL_LOG_SIZE) {
        falls = MAP_TREE_FALL_LOG_SIZE; /* (Older ones are lost.) */
    }
    for (uint32_t r = map->tree_fall_revision - falls + 1; falls > 0; r++, falls--) {
        MapTreeFall fall = map->tree_fall_log[r % MAP_TREE_FALL_LOG_SIZE];
        for (int along = 2; along <= fall.length; along++) {
            bool trunk = along <= fall.trunk;
            for (int across = 0; across < fall.size; across++) {
                int tx, ty;
                tree_fall_band_tile(fall.x, fall.y, fall.size, (Direction)fall.direction, along,
                                    across, &tx, &ty);
                hurt_beings_at(game, tx, ty, fall.z, trunk ? INJURY_CRUSH : INJURY_BRUISE,
                               trunk ? GAME_TRUNK_DAMAGE_MIN : GAME_CROWN_DAMAGE_MIN,
                               trunk ? GAME_TRUNK_DAMAGE_MAX : GAME_CROWN_DAMAGE_MAX, false,
                               trunk ? HURT_TREE_TRUNK : HURT_TREE_CROWN);
            }
        }
    }
    game->tree_falls_seen = map->tree_fall_revision;
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

/* Drops a load on the first passable tile beside (x, y, z), if any: lost
 * otherwise. */
static void drop_beside(Game *game, int x, int y, int z, ItemStack load)
{
    static const int DX[4] = {0, 1, 0, -1};
    static const int DY[4] = {-1, 0, 1, 0};
    for (int i = 0; load.count > 0 && i < 4; i++) {
        if (map_is_passable(&game->map, x + DX[i], y + DY[i], z)) {
            items_drop_load(&game->items, &game->map, x + DX[i], y + DY[i], z, &load);
            return;
        }
    }
    items_use_up(&game->items, &load, load.count);
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

/* Whether anyone sees one of the tiles of a collapse (always without the fog):
 * the colony only hears of what happens to the map when someone sees it. */
static bool collapse_seen(const Game *game, const TilePos *tiles, int count)
{
    if (!game->fog_on) {
        return true;
    }
    for (int i = 0; i < count && i < ROOF_CAVE_IN_MAX; i++) {
        if (vision_seen(&game->vision, tiles[i].x, tiles[i].y, tiles[i].z)) {
            return true;
        }
    }
    return false;
}

/* Hardships that just began make stories to tell (core/experience.h). */
static void note_hardships(Colonist *colonist, uint64_t tick)
{
    static const struct {
        ThoughtType thought;
        ExperienceType experience;
    } HARDSHIPS[] = {
        {THOUGHT_STARVING, EXPERIENCE_STARVED},
        {THOUGHT_PARCHED, EXPERIENCE_PARCHED},
        {THOUGHT_SLEPT_COLD, EXPERIENCE_COLD_NIGHT},
    };
    for (size_t i = 0; i < COLONY_ARRAY_COUNT(HARDSHIPS); i++) {
        if ((colonist->mood.news & THOUGHT_BIT(HARDSHIPS[i].thought)) != 0) {
            experiences_add(&colonist->experiences, HARDSHIPS[i].experience, 0, 0, NULL,
                            (uint32_t)(tick / TICKS_PER_HOUR));
        }
    }
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
    /* Every tick for the bills that count it, else now and then. */
    if (game->clock.tick % GAME_STOCK_INTERVAL == 0 || stock_counted(game)) {
        game_update_stock(game);
    }
    tile_light_tick(&game->light, &game->map, game->clock.tick);
    update_plants(game);
    if (game->clock.tick % GAME_PLANT_UPDATE_INTERVAL == 0) {
        /* Every plant grew since the last time: zones may offer new jobs. */
        gather_zones_plants_changed(&game->gather_zones);
    }
    update_items(game);
    hearths_burn(&game->workstations, &game->map,
                 weather_wetting(&game->weather, game->clock.tick));
    heat_from_hearths(game);
    float sun = game_sunlight(game->clock.tick);
    const WeatherDef *sky = weather_def((WeatherType)game->weather.type);
    TemperatureSurface surface = {
        .canopy = game->soil.canopy,
        .evap = game->soil.evap,
        .sun = game->soil.sun,
        .sun_heat = TEMPERATURE_SUN_HEAT * sun * weather_light(&game->weather),
        .night_cooling =
            sun <= 0.0f && sky != NULL ? TEMPERATURE_NIGHT_COOLING * sky->clearness : 0.0f,
    };
    temperatures_set_surface(&game->temperatures, &surface);
    temperatures_tick(&game->temperatures, &game->map, game->clock.tick,
                      weather_outdoor(&game->weather, game->clock.tick));
    if (game->clock.tick % TICKS_PER_DAY == 0) {
        /* Bedding packs down: worn beds get a refresh site, or fall apart. */
        constructions_wear_day(&game->constructions, &game->map, &game->items);
        fade_knowledge(game);
    }
    beds_forget_missing(&game->beds, &game->map); /* Beds taken apart or fallen apart. */
    /* What gathering zones want, from what the colony has in store. */
    gather_zones_update(&game->gather_zones, game->stock, game_daily_food(game));
    item_moves_prune(&game->item_moves, &game->items, item_move_busy, game); /* Emptied sources. */
    hurt_under_felled_trees(game); /* (Felled by the debug tools.) */
    sweep_occupancy(game);
    update_colonists(game);
    hurt_under_felled_trees(game); /* Felled by the woodcutters: they die on their next turn. */
    update_animals(game);
    update_doors(game);
    note_pending_hurts(game);
    resolve_strikes(game);
    bleed_on_ground(game);
    if (game->vision_on) {
        update_vision(game);
    }
    if (game->gather_zones.harmful_tree_falls + game->designations.harmful_tree_falls !=
        harmful_falls) {
        notices_push(&game->notices, NOTICE_TREE_FELL_ON_BUILDING, KNOWLEDGE_NONE, NULL,
                     game->clock.tick);
    }
    /* Walls and props taken apart or fallen apart, rock dug out: roofs and
     * ceilings left without support fall in. */
    CaveIns cave_ins;
    if (roofs_collapse(&game->roof_watch, &game->map, &game->items, &game->designations,
                       &cave_ins) > 0 &&
        collapse_seen(game, cave_ins.roofs, cave_ins.roof_count)) {
        notices_push(&game->notices, NOTICE_ROOF_COLLAPSED, KNOWLEDGE_NONE, NULL, game->clock.tick);
    }
    for (int i = 0; i < cave_ins.count && i < ROOF_CAVE_IN_MAX; i++) {
        clear_caved_in(game, cave_ins.tiles[i]);
        /* Whoever stood there is crushed (and pushed out on its next turn). */
        hurt_beings_at(game, cave_ins.tiles[i].x, cave_ins.tiles[i].y, cave_ins.tiles[i].z,
                       INJURY_CRUSH, GAME_CAVE_IN_DAMAGE_MIN, GAME_CAVE_IN_DAMAGE_MAX, false,
                       HURT_CAVE_IN);
    }
    if (cave_ins.count > 0 && collapse_seen(game, cave_ins.tiles, cave_ins.count)) {
        notices_push(&game->notices, NOTICE_CAVE_IN, KNOWLEDGE_NONE, NULL, game->clock.tick);
    }
    if (game->clock.tick % CONVERSATION_CHECK_TICKS == 0) {
        ConversationWorld world = conversation_world(game);
        conversations_update(&world);
        social_company_update(&game->social, game->colonists, &game->colonist_pool);
    }
    if (game->clock.tick % TICKS_PER_HOUR == GAME_SOCIAL_UPDATE_PHASE) {
        social_update(&game->social, game->colonists, &game->colonist_pool, game->clock.tick, true);
    }
    if (game->clock.tick % GAME_KNOWLEDGE_CHECK_TICKS == 0) {
        game_count_knowledge(game);
    }
    /* The thoughts that came this tick go in the settlers' journals. */
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
            Colonist *colonist = &game->colonists[slot];
            note_hardships(colonist, game->clock.tick);
            journal_note_thoughts(&colonist->journal, &colonist->mood, game->clock.tick);
        }
    }
}

int game_update(Game *game, double real_seconds)
{
    int due = game_clock_consume(&game->clock, real_seconds);
    for (int i = 0; i < due; i++) {
        game_tick(game);
        if (game->clock.speed == GAME_SPEED_PAUSED) {
            return i + 1; /* The game paused itself (the colony was lost). */
        }
    }
    return due;
}

ItemsCheck game_check_items(const Game *game)
{
    ItemsCheck check = items_check(&game->items);
    if (check.problems == 0 && game->known_ready) {
        check = items_check(&game->known.items);
    }
    long held = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (entity_id_is_none(game_colonist_id_at(game, slot))) {
            continue;
        }
        const ItemStack *load = &game->colonists[slot].carried;
        if (!items_check_load(&game->items, *load)) {
            check.problems++;
            check.first = check.first != NULL ? check.first : "a settler's load is not its items";
        }
        held += load->held != 0 ? load->count : 0;
    }
    /* The inputs of crafts in progress and the materials delivered to sites. */
    for (int index = 0; index < WORKSTATION_MAX; index++) {
        const Workstation *station = &game->workstations.list[index];
        if (station->used && station->work_input.held != 0) {
            if (!items_check_load(&game->items, station->work_input)) {
                check.problems++;
                check.first =
                    check.first != NULL ? check.first : "a craft's input is not its items";
            }
            held += station->work_input.count;
        }
    }
    for (int index = 0; index < CONSTRUCTION_MAX; index++) {
        const ConstructionSite *site = construction_get(&game->constructions, index);
        for (int slot = 0; site != NULL && slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            if (site->delivered[slot].held != 0) {
                if (!items_check_load(&game->items, site->delivered[slot])) {
                    check.problems++;
                    check.first =
                        check.first != NULL ? check.first : "a site's materials are not its items";
                }
                held += site->delivered[slot].count;
            }
        }
    }
    if (held != game->items.held_count) {
        check.problems++;
        check.first =
            check.first != NULL ? check.first : "items in loads are lost or counted twice";
    }
    return check;
}

uint64_t game_hash(const Game *game)
{
    uint64_t hash = map_hash(&game->map);
    hash = designations_hash(&game->designations, hash);
    hash = dirt_hash(&game->dirt, hash);
    hash = gather_zones_hash(&game->gather_zones, hash);
    hash = items_hash(&game->items, hash);
    hash = crowns_hash(&game->crowns, hash);
    hash = stockpiles_hash(&game->stockpiles, hash);
    hash = workstations_hash(&game->workstations, hash);
    hash = constructions_hash(&game->constructions, hash);
    hash = beds_hash(&game->beds, hash);
    hash = reservations_hash(&game->reservations, hash);
    if (game->item_moves.count > 0) { /* Hashes without orders stay as they were. */
        hash = item_moves_hash(&game->item_moves, hash);
    }
    hash = hash_fnv1a64(hash, &game->clock.tick, sizeof(game->clock.tick));
    hash = hash_fnv1a64(hash, &game->rng.state, sizeof(game->rng.state));
    hash = hash_fnv1a64(hash, game->teach, sizeof(game->teach));
    hash = temperatures_hash(&game->temperatures, hash);
    hash = soil_hash(&game->soil, hash);
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
    if (game->fog_on) { /* What the colony has seen: apart too. */
        uint64_t explored = vision_explored_hash(&game->vision);
        hash = hash_fnv1a64(hash, &explored, sizeof(explored));
    }
    if (game->strikes_resolved > 0) { /* Likewise. */
        hash = hash_fnv1a64(hash, &game->strikes_resolved, sizeof(game->strikes_resolved));
        hash = hash_fnv1a64(hash, &game->combat_rng.state, sizeof(game->combat_rng.state));
    }
    if (game->colony_lost) { /* Only then: hashes stay as they were. */
        hash = hash_fnv1a64(hash, &game->colony_lost, sizeof(game->colony_lost));
    }
    if (animals_slot_count(&game->animals) > 0) { /* Apart: hashes without animals stay. */
        hash = animals_hash(&game->animals, hash);
        hash = hash_fnv1a64(hash, &game->wildlife_rng.state, sizeof(game->wildlife_rng.state));
    }
    hash = social_hash(&game->social, &game->colonist_pool, hash);
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id)) {
            continue;
        }
        const Colonist *c = &game->colonists[slot];
        int32_t ints[15] = {(int32_t)id.index,
                            (int32_t)id.generation,
                            c->x,
                            c->y,
                            (int32_t)c->task,
                            c->timer,
                            c->target.x,
                            c->target.y,
                            c->carried.type,
                            c->carried.count,
                            c->carried.quality,
                            c->carried.hit_points,
                            c->carried.progress,
                            c->haul_destination.x,
                            c->haul_destination.y};
        hash = hash_fnv1a64(hash, ints, sizeof(ints));
        if (game->map.depth != 1) { /* Hashes of one-level games stay as they were. */
            int32_t levels[4] = {c->z, c->target.z, c->haul_destination.z, c->fetch.z};
            hash = hash_fnv1a64(hash, levels, sizeof(levels));
        }
        hash = hash_fnv1a64(hash, &c->move_progress, sizeof(c->move_progress));
        hash = hash_fnv1a64(hash, c->needs, sizeof(c->needs));
        hash = hash_fnv1a64(hash, &c->company, sizeof(c->company));
        hash = hash_fnv1a64(hash, &c->social_states, sizeof(c->social_states));
        hash = hash_fnv1a64(hash, &c->friend_slots, sizeof(c->friend_slots));
        hash = hash_fnv1a64(hash, &c->close_friend_slots, sizeof(c->close_friend_slots));
        hash = hash_fnv1a64(hash, &c->rival_slots, sizeof(c->rival_slots));
        hash = experiences_hash(&c->experiences, hash);
        hash = hash_fnv1a64(hash, &c->wetness, sizeof(c->wetness));
        hash = mood_hash(&c->mood, hash);
        hash = attributes_hash(&c->attributes, hash);
        int32_t sleep[3] = {c->sleep_ticks, c->sleep_cold_ticks, c->sleep_hot_ticks};
        hash = hash_fnv1a64(hash, sleep, sizeof(sleep));
        hash = hash_fnv1a64(hash, c->work_priorities, sizeof(c->work_priorities));
        hash = hash_fnv1a64(hash, c->knowledge, sizeof(c->knowledge));
        hash = hash_fnv1a64(hash, c->knowledge_peak, sizeof(c->knowledge_peak));
        hash = hash_fnv1a64(hash, c->knowledge_practice, sizeof(c->knowledge_practice));
        int32_t social[8] = {(int32_t)c->mentor.index,
                             (int32_t)c->mentor.generation,
                             c->assist_work,
                             c->assist_wait,
                             c->chat_ticks,
                             (int32_t)c->chat_partner.index,
                             (int32_t)c->chat_partner.generation,
                             c->chat_topic};
        hash = hash_fnv1a64(hash, social, sizeof(social));
        hash = hash_fnv1a64(hash, &c->assisted_until, sizeof(c->assisted_until));
        int32_t fire[6] = {c->light_step,  c->fire_source, c->fire_source_number,
                           c->ember_ticks, c->story_topic, (int32_t)c->fuel_promised};
        hash = hash_fnv1a64(hash, fire, sizeof(fire));
        hash = hash_fnv1a64(hash, &c->light_progress, sizeof(c->light_progress));
        hash = hash_fnv1a64(hash, &c->fuel_promised, sizeof(c->fuel_promised));
        hash = hash_fnv1a64(hash, &c->haul_delivering, sizeof(c->haul_delivering));
        hash = hash_fnv1a64(hash, &c->haul_type, sizeof(c->haul_type));
        if (c->haul_item.slot != UINT32_MAX) { /* Hashes without item moves stay as they were. */
            hash = hash_fnv1a64(hash, &c->haul_item.slot, sizeof(c->haul_item.slot));
            hash = hash_fnv1a64(hash, &c->haul_item.generation, sizeof(c->haul_item.generation));
        }
        hash = hash_fnv1a64(hash, &c->strength, sizeof(c->strength));
        hash = hash_fnv1a64(hash, &c->max_carry_weight, sizeof(c->max_carry_weight));
        hash = health_hash(&c->health, hash);
        if (c->drafted) { /* Only then: hashes of games without it stay as they were. */
            hash = hash_fnv1a64(hash, &c->drafted, sizeof(c->drafted));
        }
        if (c->wielding) {
            int32_t wield[2] = {c->wielding, c->wielded};
            hash = hash_fnv1a64(hash, wield, sizeof(wield));
        }
        if (!combatant_is_none(c->attack_target)) {
            int32_t target[3] = {c->attack_target.kind, (int32_t)c->attack_target.id.index,
                                 (int32_t)c->attack_target.id.generation};
            hash = hash_fnv1a64(hash, target, sizeof(target));
        }
        if (!combatant_is_none(c->enemy) || c->attack_cooldown > 0) {
            /* Only then: hashes of games without fights stay as they were. */
            int32_t fight[4] = {c->enemy.kind, (int32_t)c->enemy.id.index,
                                (int32_t)c->enemy.id.generation, c->attack_cooldown};
            hash = hash_fnv1a64(hash, fight, sizeof(fight));
        }
        hash = hash_fnv1a64(hash, c->name, strlen(c->name));
        int32_t job[14] = {c->station,       c->station_number, c->bill,    c->recipe,
                           c->station_phase, c->fetch.x,        c->fetch.y, c->fetch_tool,
                           c->work_type,     c->gathering,      c->site,    c->site_number,
                           c->supply_slot,   c->supply_promised};
        hash = hash_fnv1a64(hash, job, sizeof(job));
        for (int i = 0; i < COLONIST_TOOL_MAX; i++) {
            ItemStack tool = c->tools[i];
            int32_t fields[7] = {
                tool.type,       tool.count,       tool.quality,          tool.hit_points,
                c->tool_wear[i], c->tool_locks[i], c->primary_weapon == i};
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
            (colonist_can_do(&game->colonists[slot], type) ||
             (type == DESIGNATION_MINE &&
              colonist_can_do(&game->colonists[slot], DESIGNATION_DIG)))) {
            return true; /* Mining covers earth too. */
        }
    }
    return false;
}

bool game_designation_available_at(const Game *game, DesignationType type, int x, int y, int z)
{
    if (type == DESIGNATION_HARVEST || type == DESIGNATION_UPROOT) {
        for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
            if (!entity_id_is_none(game_colonist_id_at(game, slot)) &&
                colonist_can_do_at(&game->colonists[slot], game_view_map(game), x, y, z, type)) {
                return true;
            }
        }
        return false;
    }
    return game_designation_available(game, type);
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
            if (!game_tile_explored(game, x, y, z) ||
                !designation_ground_free(&game->constructions, &game->workstations,
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
    int removed =
        designations_set_rect(&game->designations, &game->map, x0, y0, x1, y1, z, DESIGNATION_NONE);
    return removed + game_remove_blueprints_rect(game, x0, y0, x1, y1, z);
}

/* ---- Forbidding ---------------------------------------------------------- */

/* The map bit of a target (0 for items and stations, kept elsewhere). */
static MapForbid forbid_bit(ForbidTarget target)
{
    switch (target) {
    case FORBID_PLANT: return MAP_FORBID_PLANT;
    case FORBID_STRUCTURE: return MAP_FORBID_STRUCTURE;
    case FORBID_ROOF: return MAP_FORBID_ROOF;
    case FORBID_ITEMS:
    case FORBID_STATION:
    case FORBID_SITE:
    case FORBID_STOCKPILE:
    case FORBID_GATHER_ZONE:
    case FORBID_TARGET_COUNT: break;
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
    case FORBID_ITEMS: return items_get(&game->items, x, y, z).type != ITEM_NONE;
    case FORBID_STATION: return workstation_at(&game->workstations, x, y, z) != WORKSTATION_NONE;
    case FORBID_PLANT:
    case FORBID_STRUCTURE:
    case FORBID_ROOF: return map_can_forbid(&game->map, x, y, z, forbid_bit(target));
    case FORBID_SITE: return construction_at(&game->constructions, x, y, z) != CONSTRUCTION_NONE;
    case FORBID_STOCKPILE: return stockpile_at(&game->stockpiles, x, y, z) != STOCKPILE_NONE;
    case FORBID_GATHER_ZONE:
        return gather_zone_at(&game->gather_zones, x, y, z) != GATHER_ZONE_NONE;
    case FORBID_TARGET_COUNT: break;
    }
    return false;
}

bool game_forbidden(const Game *game, int x, int y, int z, ForbidTarget target)
{
    forbid_tile(game, target, &x, &y, &z);
    switch (target) {
    case FORBID_ITEMS: return items_forbidden(&game->items, x, y, z);
    case FORBID_STATION: {
        const Workstation *station = workstation_get_const(
            &game->workstations, workstation_at(&game->workstations, x, y, z));
        return station != NULL && station->forbidden;
    }
    case FORBID_PLANT:
    case FORBID_STRUCTURE:
    case FORBID_ROOF: return map_forbidden(&game->map, x, y, z, forbid_bit(target));
    case FORBID_SITE: {
        const ConstructionSite *site =
            construction_get(&game->constructions, construction_at(&game->constructions, x, y, z));
        return site != NULL && site->forbidden;
    }
    case FORBID_STOCKPILE: return stockpile_forbidden_at(&game->stockpiles, x, y, z);
    case FORBID_GATHER_ZONE: {
        const GatherZone *zone =
            gather_zone_get(&game->gather_zones, gather_zone_at(&game->gather_zones, x, y, z));
        return zone != NULL && zone->forbidden;
    }
    case FORBID_TARGET_COUNT: break;
    }
    return false;
}

bool game_door_kept_open(const Game *game, int x, int y, int z)
{
    const Map *map = game_view_map(game);
    return map_in_bounds(map, x, y, z) && map_door_kept_open_at(map, map_index(map, x, y, z));
}

bool game_set_door_kept_open(Game *game, int x, int y, int z, bool open)
{
    if (!game_tile_explored(game, x, y, z)) {
        return false; /* Nobody has seen it. */
    }
    return map_set_door_kept_open(&game->map, x, y, z, open);
}

bool game_set_forbidden(Game *game, int x, int y, int z, ForbidTarget target, bool forbidden)
{
    forbid_tile(game, target, &x, &y, &z);
    if (!game_tile_explored(game, x, y, z)) {
        return false; /* Nobody has seen it: nothing to forbid. */
    }
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
    case FORBID_SITE: {
        int index = construction_at(&game->constructions, x, y, z);
        if (index == CONSTRUCTION_NONE || game_forbidden(game, x, y, z, target) == forbidden) {
            return false;
        }
        construction_set_forbidden(&game->constructions, index, forbidden);
        return true;
    }
    case FORBID_STOCKPILE: {
        int index = stockpile_at(&game->stockpiles, x, y, z);
        if (index == STOCKPILE_NONE || game_forbidden(game, x, y, z, target) == forbidden) {
            return false;
        }
        stockpile_set_forbidden(&game->stockpiles, index, forbidden);
        return true;
    }
    case FORBID_GATHER_ZONE: {
        int index = gather_zone_at(&game->gather_zones, x, y, z);
        if (index == GATHER_ZONE_NONE || game_forbidden(game, x, y, z, target) == forbidden) {
            return false;
        }
        gather_zone_set_forbidden(&game->gather_zones, index, forbidden);
        return true;
    }
    case FORBID_TARGET_COUNT: break;
    }
    return false;
}

int game_set_forbidden_rect(Game *game, int x0, int y0, int x1, int y1, int z, bool forbidden)
{
    int changed = 0;
    for (int y = (y0 < y1 ? y0 : y1); y <= (y0 < y1 ? y1 : y0); y++) {
        for (int x = (x0 < x1 ? x0 : x1); x <= (x0 < x1 ? x1 : x0); x++) {
            for (int target = 0; target <= FORBID_SITE; target++) { /* Whole zones are left out. */
                changed += game_set_forbidden(game, x, y, z, (ForbidTarget)target, forbidden);
            }
        }
    }
    return changed;
}

bool game_order_item_move(Game *game, int x, int y, int z, int dx, int dy, int dz)
{
    TilePos source = {(int16_t)x, (int16_t)y, (int16_t)z};
    TilePos destination = {(int16_t)dx, (int16_t)dy, (int16_t)dz};
    ItemStack stack = items_get(&game->items, x, y, z);
    if (stack.type == ITEM_NONE || !game_tile_explored(game, x, y, z) ||
        !game_tile_explored(game, dx, dy, dz) ||
        !path_walker_can_stand(PATH_WALKER_SETTLER, game_view_map(game), dx, dy, dz) ||
        item_move_room(&game->items, destination, stack) == 0) {
        return false;
    }
    if (!item_moves_add(&game->item_moves, source, destination, ITEM_ID_NONE)) {
        return false;
    }
    item_moves_remove_items_on(&game->item_moves, source); /* The tile's order takes them. */
    items_set_forbidden(&game->items, x, y, z, true);
    return true;
}

bool game_cancel_item_move(Game *game, int x, int y, int z)
{
    return item_moves_remove(&game->item_moves, (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                             ITEM_ID_NONE);
}

bool game_item_move_at(const Game *game, int x, int y, int z, TilePos *destination)
{
    int index = item_moves_find(&game->item_moves, (TilePos){(int16_t)x, (int16_t)y, (int16_t)z},
                                ITEM_ID_NONE);
    if (index < 0) {
        return false;
    }
    *destination = game->item_moves.orders[index].destination;
    return true;
}

/* The tile an item lies on; false when it is not on the ground. */
static bool item_tile(const Game *game, ItemId id, TilePos *tile)
{
    const Item *item = items_by_id(&game->items, id);
    if (item == NULL) {
        return false;
    }
    size_t level = map_level_tile_count(&game->map);
    *tile = (TilePos){(int16_t)(item->tile % (size_t)game->map.width),
                      (int16_t)(item->tile % level / (size_t)game->map.width),
                      (int16_t)(item->tile / level)};
    return true;
}

ItemId game_pick_real_item(const Game *game, ItemPick *pick)
{
    const Item *item = game_pick_item(game, pick);
    if (item == NULL) {
        return ITEM_ID_NONE;
    }
    const Items *items = game_items_at(game, pick->x, pick->y, pick->z);
    if (items == &game->items) {
        return pick->id;
    }
    return items_find_like(&game->items, pick->x, pick->y, pick->z, item);
}

bool game_item_forbidden(const Game *game, ItemId id)
{
    const Item *item = items_by_id(&game->items, id);
    return item != NULL && item->forbidden;
}

bool game_set_item_forbidden(Game *game, ItemId id, bool forbidden)
{
    TilePos tile;
    if (!item_tile(game, id, &tile) || !game_tile_explored(game, tile.x, tile.y, tile.z)) {
        return false;
    }
    return items_set_item_forbidden(&game->items, id, forbidden); /* Counts as an addition. */
}

bool game_order_single_item_move(Game *game, ItemId id, int dx, int dy, int dz)
{
    TilePos source, destination = {(int16_t)dx, (int16_t)dy, (int16_t)dz};
    if (!item_tile(game, id, &source) || !game_tile_explored(game, source.x, source.y, source.z) ||
        !game_tile_explored(game, dx, dy, dz) ||
        !path_walker_can_stand(PATH_WALKER_SETTLER, game_view_map(game), dx, dy, dz) ||
        item_move_room(&game->items, destination, item_as_stack(items_by_id(&game->items, id))) ==
            0 ||
        !item_moves_add(&game->item_moves, source, destination, id)) {
        return false;
    }
    items_set_item_forbidden(&game->items, id, true);
    return true;
}

bool game_order_carry(Game *game, EntityId colonist_id, ItemId item)
{
    Colonist *colonist = game_colonist(game, colonist_id);
    TilePos tile;
    if (colonist == NULL || game_carry_refusal(game, colonist_id, item) != CARRY_REFUSAL_NONE ||
        !item_tile(game, item, &tile)) {
        return false;
    }
    ColonistContext context = colonist_context(game, colonist_id.index);
    return colonist_order_carry(colonist, &context, item, tile);
}

CarryRefusal game_carry_refusal(const Game *game, EntityId colonist_id, ItemId item)
{
    const Colonist *colonist = game_colonist_const(game, colonist_id);
    if (colonist == NULL) {
        return CARRY_REFUSAL_DOWN;
    }
    if (items_by_id(&game->items, item) == NULL) {
        return items_id_held(&game->items, item) ? CARRY_REFUSAL_HELD : CARRY_REFUSAL_GONE;
    }
    TilePos tile;
    item_tile(game, item, &tile);
    CarryRefusal refusal = colonist_carry_refusal(colonist, &game->items, &game->map,
                                                  game_view_map(game), &game->regions, item, tile);
    if (refusal == CARRY_REFUSAL_NONE && !game_tile_explored(game, tile.x, tile.y, tile.z)) {
        return CARRY_REFUSAL_UNREACHABLE;
    }
    return refusal;
}

bool game_can_carry(const Game *game, EntityId colonist_id, ItemId item)
{
    return game_carry_refusal(game, colonist_id, item) == CARRY_REFUSAL_NONE;
}

bool game_put_down(Game *game, EntityId colonist_id, int index)
{
    Colonist *colonist = game_colonist(game, colonist_id);
    return colonist != NULL && colonist_put_down(colonist, &game->items, &game->map, index);
}

bool game_set_carry_lock(Game *game, EntityId colonist_id, int index, CarryLock lock)
{
    Colonist *colonist = game_colonist(game, colonist_id);
    return colonist != NULL && colonist_set_lock(colonist, index, lock);
}

bool game_set_primary_weapon(Game *game, EntityId colonist_id, int index, bool primary)
{
    Colonist *colonist = game_colonist(game, colonist_id);
    return colonist != NULL && colonist_set_primary_weapon(colonist, index, primary);
}

bool game_can_draft(const Game *game, EntityId colonist_id)
{
    const Colonist *colonist = game_colonist_const(game, colonist_id);
    return colonist != NULL && colonist_can_draft(colonist);
}

bool game_is_drafted(const Game *game, EntityId colonist_id)
{
    const Colonist *colonist = game_colonist_const(game, colonist_id);
    return colonist != NULL && colonist->drafted;
}

int game_set_drafted(Game *game, const EntityId *ids, int count, bool drafted)
{
    int changed = 0;
    for (int i = 0; i < count; i++) {
        Colonist *colonist = game_colonist(game, ids[i]);
        if (colonist == NULL || colonist->drafted == drafted) {
            continue;
        }
        ColonistContext context = colonist_context(game, ids[i].index);
        changed += colonist_set_drafted(colonist, &context, drafted) ? 1 : 0;
    }
    return changed;
}

int game_order_attack(Game *game, const EntityId *ids, int count, Combatant target)
{
    if (target.kind != COMBATANT_ANIMAL) {
        return 0;
    }
    const Animal *animal = animals_get_const(&game->animals, target.id);
    if (animal == NULL || !game_tile_seen(game, animal->x, animal->y, animal->z)) {
        return 0; /* Only what the colony sees. */
    }
    int sent = 0;
    for (int i = 0; i < count; i++) {
        Colonist *colonist = game_colonist(game, ids[i]);
        if (colonist != NULL) {
            ColonistContext context = colonist_context(game, ids[i].index);
            sent += colonist_order_attack(colonist, &context, target) ? 1 : 0;
        }
    }
    return sent;
}

int game_weapon_choices(const Game *game, EntityId colonist_id, WeaponChoice *out, int capacity)
{
    const Colonist *colonist = game_colonist_const(game, colonist_id);
    if (colonist == NULL || capacity <= 0) {
        return 0;
    }
    int count = 0;
    out[count++] = (WeaponChoice){-1, ITEM_ID_NONE, ITEM_NONE}; /* Its fists. */
    for (int i = 0; i < COLONIST_TOOL_MAX && count < capacity; i++) {
        if (item_is_weapon((ItemType)colonist->tools[i].type) && colonist_can_wield(colonist, i)) {
            out[count++] = (WeaponChoice){i, ITEM_ID_NONE, (ItemType)colonist->tools[i].type};
        }
    }
    /* Then those lying on its tile and around it, row by row. */
    const Items *items = &game->items;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            int x = colonist->x + dx, y = colonist->y + dy, z = colonist->z;
            if (!map_in_bounds(&game->map, x, y, z) || !game_tile_explored(game, x, y, z)) {
                continue;
            }
            for (const Item *item = items_first(items, map_index(&game->map, x, y, z));
                 item != NULL && count < capacity; item = items_next(items, item)) {
                ItemId id = items_id(items, item);
                if (!colonist_can_wield_item(colonist, items, id)) {
                    continue;
                }
                bool alike = false; /* One line for items alike (a heap of cobbles). */
                for (int i = 0; i < count && !alike; i++) {
                    const Item *listed = items_by_id(items, out[i].item);
                    alike = listed != NULL && listed->type == item->type &&
                            listed->quality == item->quality &&
                            listed->hit_points == item->hit_points;
                }
                if (!alike) {
                    out[count++] = (WeaponChoice){-1, id, (ItemType)item->type};
                }
            }
        }
    }
    return count;
}

int game_wielded_choice(const Game *game, EntityId colonist_id)
{
    const Colonist *colonist = game_colonist_const(game, colonist_id);
    return colonist != NULL && colonist->wielding ? colonist_wielded(colonist) : -1;
}

bool game_wield(Game *game, EntityId colonist_id, WeaponChoice choice)
{
    Colonist *colonist = game_colonist(game, colonist_id);
    if (colonist == NULL) {
        return false;
    }
    ColonistContext context = colonist_context(game, colonist_id.index);
    return choice.item.slot != UINT32_MAX ? colonist_wield_item(colonist, &context, choice.item)
                                          : colonist_wield(colonist, &context, choice.slot);
}

bool game_can_keep_load_item(const Game *game, EntityId colonist_id, CarryLock lock)
{
    const Colonist *colonist = game_colonist_const(game, colonist_id);
    return colonist != NULL && !colonist_is_down(colonist) && colonist->carried.count > 0 &&
           colonist_can_keep(colonist, (ItemType)colonist->carried.type, 1, lock);
}

int game_keep_load_item(Game *game, EntityId colonist_id, int index, CarryLock lock)
{
    Colonist *colonist = game_colonist(game, colonist_id);
    if (colonist == NULL) {
        return -1;
    }
    ColonistContext context = colonist_context(game, colonist_id.index);
    return colonist_keep_load_item(colonist, &context, index, lock);
}

bool game_drop_load_item(Game *game, EntityId colonist_id, int index)
{
    Colonist *colonist = game_colonist(game, colonist_id);
    if (colonist == NULL) {
        return false;
    }
    ColonistContext context = colonist_context(game, colonist_id.index);
    return colonist_drop_load_item(colonist, &context, index);
}

bool game_cancel_single_item_move(Game *game, ItemId id)
{
    TilePos source;
    return item_tile(game, id, &source) && item_moves_remove(&game->item_moves, source, id);
}

bool game_single_item_move_at(const Game *game, ItemId id, TilePos *destination)
{
    TilePos source;
    int index = item_tile(game, id, &source) ? item_moves_find(&game->item_moves, source, id) : -1;
    if (index < 0) {
        return false;
    }
    *destination = game->item_moves.orders[index].destination;
    return true;
}

/* ---- Zones --------------------------------------------------------------- */

int game_add_stockpile_rect(Game *game, int x0, int y0, int x1, int y1, int z, int preferred)
{
    const uint8_t *blocked[] = {game->gather_zones.zone, game->workstations.at,
                                game->constructions.workstation_tiles, NULL};
    /* Through the known world: tiles nobody has seen are not there. */
    return stockpiles_add_view_rect(&game->stockpiles, game_view_map(game), x0, y0, x1, y1, z,
                                    preferred, blocked);
}

int game_add_gather_zone_rect(Game *game, int x0, int y0, int x1, int y1, int z, int preferred)
{
    const uint8_t *blocked[] = {game->stockpiles.zone, game->workstations.at,
                                game->constructions.workstation_tiles, NULL};
    return gather_zones_add_view_rect(&game->gather_zones, game_view_map(game), x0, y0, x1, y1, z,
                                      preferred, blocked);
}

int game_remove_stockpile_rect(Game *game, int x0, int y0, int x1, int y1, int z)
{
    return stockpiles_remove_view_rect(&game->stockpiles, &game->map, x0, y0, x1, y1, z);
}

int game_remove_gather_zone_rect(Game *game, int x0, int y0, int x1, int y1, int z)
{
    return gather_zones_remove_view_rect(&game->gather_zones, &game->map, x0, y0, x1, y1, z);
}

int game_extend_stockpile_rect(Game *game, int index, int x0, int y0, int x1, int y1, int z)
{
    const uint8_t *blocked[] = {game->gather_zones.zone, game->workstations.at,
                                game->constructions.workstation_tiles, NULL};
    return stockpile_extend_view(&game->stockpiles, game_view_map(game), index, x0, y0, x1, y1, z,
                                 blocked);
}

int game_shrink_stockpile_rect(Game *game, int index, int x0, int y0, int x1, int y1, int z)
{
    return stockpile_shrink_view(&game->stockpiles, &game->map, index, x0, y0, x1, y1, z);
}

int game_extend_gather_zone_rect(Game *game, int index, int x0, int y0, int x1, int y1, int z)
{
    const uint8_t *blocked[] = {game->stockpiles.zone, game->workstations.at,
                                game->constructions.workstation_tiles, NULL};
    return gather_zone_extend_view(&game->gather_zones, game_view_map(game), index, x0, y0, x1, y1,
                                   z, blocked);
}

int game_shrink_gather_zone_rect(Game *game, int index, int x0, int y0, int x1, int y1, int z)
{
    return gather_zone_shrink_view(&game->gather_zones, &game->map, index, x0, y0, x1, y1, z);
}

/* ---- Picking ------------------------------------------------------------- */

/* Whether the level shown, z, lets one see a colonist: it is on that level, or
 * on the level seen through the open space over its tile (see map_view_level). */
static bool colonist_in_view(const Game *game, const Colonist *colonist, int z)
{
    if (colonist->z == z) {
        return true;
    }
    if (colonist->z > z) {
        return false;
    }
    float x, y;
    colonist_position(colonist, &x, &y);
    return map_view_level(&game->map, (int)floorf(x), (int)floorf(y), z) == colonist->z;
}

EntityId game_colonist_at(const Game *game, float world_x, float world_y, int z)
{
    EntityId best = ENTITY_NONE;
    float best_distance = GAME_PICK_RADIUS * GAME_PICK_RADIUS;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id) || !colonist_in_view(game, &game->colonists[slot], z)) {
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
        if (entity_id_is_none(id) || !colonist_in_view(game, &game->colonists[slot], z)) {
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

/* ---- Animals ------------------------------------------------------------- */

EntityId game_spawn_animal(Game *game, AnimalSpecies species, AnimalSex sex, int x, int y, int z)
{
    return animals_spawn(&game->animals, &game->map, species, sex, x, y, z, &game->wildlife_rng);
}

EntityId game_debug_spawn_animal(Game *game, AnimalSpecies species, AnimalSex sex, int x, int y,
                                 int z)
{
    Animals *animals = &game->animals;
    EntityId nearest = ENTITY_NONE;
    int best = ANIMAL_HERD_RADIUS * ANIMAL_HERD_RADIUS + 1;
    for (uint32_t slot = 0; slot < animals_slot_count(animals); slot++) {
        const Animal *other = &animals->list[slot];
        int dx = other->x - x, dy = other->y - y;
        if (!entity_id_is_none(animals_id_at(animals, slot)) && other->species == species &&
            other->sex == sex && other->z == z && dx * dx + dy * dy < best) {
            best = dx * dx + dy * dy;
            nearest = animals_id_at(animals, slot);
        }
    }
    EntityId id = game_spawn_animal(game, species, sex, x, y, z);
    if (!entity_id_is_none(id) && !entity_id_is_none(nearest)) {
        animals_set_leader(animals, id, nearest);
    }
    return id;
}

const Animal *game_animal_const(const Game *game, EntityId id)
{
    return animals_get_const(&game->animals, id);
}

bool game_animal_seen(const Game *game, EntityId id)
{
    const Animal *animal = animals_get_const(&game->animals, id);
    if (animal == NULL) {
        return false;
    }
    float x, y;
    animal_position(animal, &x, &y);
    return game_tile_seen(game, (int)floorf(x), (int)floorf(y), animal->z);
}

EntityId game_animal_at(const Game *game, float world_x, float world_y, int z)
{
    EntityId best = ENTITY_NONE;
    float best_distance = GAME_PICK_RADIUS * GAME_PICK_RADIUS;
    for (uint32_t slot = 0; slot < animals_slot_count(&game->animals); slot++) {
        EntityId id = animals_id_at(&game->animals, slot);
        if (entity_id_is_none(id)) {
            continue;
        }
        const Animal *animal = &game->animals.list[slot];
        float x, y;
        animal_position(animal, &x, &y);
        if (animal->z > z ||
            (animal->z < z &&
             map_view_level(&game->map, (int)floorf(x), (int)floorf(y), z) != animal->z) ||
            !game_animal_seen(game, id)) {
            continue;
        }
        float distance = (x - world_x) * (x - world_x) + (y - world_y) * (y - world_y);
        if (distance <= best_distance) {
            best_distance = distance;
            best = id;
        }
    }
    return best;
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
            colonist_fits_bill(colonist, game_colonist_id_at(game, slot), bill)) {
            return true;
        }
    }
    return false;
}

BillEstimate game_bill_estimate(const Game *game, const Bill *bill)
{
    BillEstimate estimate = {0};
    RecipeType recipe = (RecipeType)bill->recipe;
    const RecipeDef *def = recipe_def(recipe);
    if (def == NULL) {
        return estimate;
    }
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        const Colonist *colonist = &game->colonists[slot];
        if (entity_id_is_none(id) || !colonist_can_craft(colonist, recipe) ||
            !colonist_fits_bill(colonist, id, bill)) {
            continue;
        }
        double level = colonist_work_level(colonist, def->knowledge);
        if (estimate.crafters == 0 || level < estimate.low_level) {
            estimate.low_level = level;
            estimate.low_crafter = id;
        }
        if (estimate.crafters == 0 || level > estimate.high_level) {
            estimate.high_level = level;
            estimate.high_crafter = id;
        }
        estimate.crafters++;
    }
    if (estimate.crafters == 0) {
        return estimate;
    }
    ItemType input = ITEM_NONE;
    for (int i = 0; i < RECIPE_INPUT_ALTERNATIVES && input == ITEM_NONE; i++) {
        if (def->inputs[i] != ITEM_NONE && (bill->inputs & ITEM_TYPE_BIT(def->inputs[i]))) {
            input = def->inputs[i];
        }
    }
    estimate.low =
        recipe_average_product(recipe, input, bill->max_quality, bill->woods, estimate.low_level);
    estimate.high =
        recipe_average_product(recipe, input, bill->max_quality, bill->woods, estimate.high_level);
    return estimate;
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
    return bill_has_input(bill, game_view_items(game)->totals, tools);
}

bool game_hearth_lacks(const Game *game, const Workstation *station, HearthIngredient ingredient)
{
    if (!hearth_is(station) || (unsigned)ingredient >= HEARTH_INGREDIENT_COUNT ||
        hearth_laid(station, ingredient)) {
        return false;
    }
    if (ingredient == HEARTH_TINDER) {
        const Workstations *stations = &game->workstations;
        int seen = 0;
        for (int i = 0; i < WORKSTATION_MAX && seen < stations->used_count; i++) {
            const Workstation *other = &stations->list[i];
            seen += other->used ? 1 : 0;
            if (other->used && other != station && other->burning && !other->forbidden) {
                return false; /* Embers. */
            }
        }
    }
    const long *totals = game_view_items(game)->totals;
    ItemTypeMask types = hearth_ingredient_types(ingredient);
    for (int type = ITEM_NONE + 1; type < ITEM_TYPE_COUNT; type++) {
        if ((types & ITEM_TYPE_BIT(type)) && totals[type] > 0) {
            return false;
        }
    }
    return true;
}

/* What is built at once leaves the plants under it alone: they are ordered
 * uprooted (a settler does it, as for any plant in the way). */
static void order_uprooting(Game *game, int x, int y, int z)
{
    if (gather_plant_uprootable(&game->map, x, y, z)) {
        game_designate_rect(game, x, y, x, y, z, DESIGNATION_UPROOT);
    }
}

bool game_can_place_workstation(const Game *game, int x, int y, int z)
{
    /* Room in the list for every station whose site is finished. */
    return game_tile_explored(game, x, y, z) &&
           game->workstations.used_count + game->constructions.workstation_count <
               WORKSTATION_MAX &&
           map_structure(&game->map, x, y, z) == STRUCTURE_NONE &&
           construction_can_replace(&game->constructions, &game->map, x, y, z) &&
           workstation_can_place(&game->workstations, &game->map, x, y, z);
}

bool game_place_workstation(Game *game, WorkstationType type, int x, int y, int z)
{
    if ((unsigned)type >= WORKSTATION_TYPE_COUNT || !game_can_place_workstation(game, x, y, z) ||
        construction_workstation_is_planned(&game->constructions, type, x, y, z)) {
        return false;
    }
    /* What was planned there is given up, its materials dropped. */
    game_remove_construction(game, construction_at(&game->constructions, x, y, z));
    /* Ready at once when there is nothing to bring nor to build. */
    bool ready = !workstation_needs_construction(type);
    bool placed = ready ? workstations_place(&game->workstations, &game->map, type, x, y, z) !=
                              WORKSTATION_NONE
                        : constructions_place_workstation(&game->constructions, &game->map, type, x,
                                                          y, z) != CONSTRUCTION_NONE;
    if (placed && ready) {
        order_uprooting(game, x, y, z); /* The plant under it is left to a settler. */
    }
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
        items_drop_load(&game->items, &game->map, x, y, z, &returned[i]);
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
        items_drop_load(&game->items, &game->map, x, y, z, &returned);
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

int game_build_level(const Game *game, int x, int y, int shown, bool roof)
{
    int seen = map_view_level(game_view_map(game), x, y, shown);
    if (seen < 0 || seen > shown) {
        return shown;
    }
    return roof && seen < shown ? seen + 1 : seen;
}

bool game_can_place_structure(const Game *game, StructureType type, int x, int y, int z,
                              Direction direction)
{
    int length = structure_length(type);
    if (length <= 0 || (unsigned)direction >= DIRECTION_COUNT) {
        return false;
    }
    for (int part = 0; part < length; part++) {
        if (!game_tile_explored(game, x + part * direction_dx(direction),
                                y + part * direction_dy(direction), z)) {
            return false;
        }
        int px = x + part * direction_dx(direction), py = y + part * direction_dy(direction);
        if (stockpile_at(&game->stockpiles, px, py, z) != STOCKPILE_NONE ||
            workstation_at(&game->workstations, px, py, z) != WORKSTATION_NONE) {
            return false;
        }
    }
    return !construction_is_planned(&game->constructions, type, x, y, z, direction) &&
           construction_can_replace_structure(&game->constructions, &game->map, type, x, y, z,
                                              direction);
}

int game_place_structure(Game *game, StructureType type, int x, int y, int z, Direction direction)
{
    if (!game_can_place_structure(game, type, x, y, z, direction)) {
        return CONSTRUCTION_NONE;
    }
    /* What was planned on its tiles is given up, its materials dropped. */
    for (int part = 0; part < structure_length(type); part++) {
        game_remove_construction(game, construction_at(&game->constructions,
                                                       x + part * direction_dx(direction),
                                                       y + part * direction_dy(direction), z));
    }
    if (!structure_needs_construction(type)) { /* Nothing to bring nor to do: built at once. */
        /* The map clears the plants of the tiles it builds on (as builders do);
         * these are left as they are, and ordered uprooted. */
        Plant plants[STRUCTURE_MAX_LENGTH];
        int length = structure_length(type);
        _Static_assert(STRUCTURE_MAX_LENGTH >= 2, "a bed is two tiles long");
        for (int part = 0; part < length && part < STRUCTURE_MAX_LENGTH; part++) {
            plants[part] =
                game->map.plants[map_index(&game->map, x + part * direction_dx(direction),
                                           y + part * direction_dy(direction), z)];
        }
        map_place_structure(&game->map, x, y, z, type, direction, ITEM_QUALITY_STANDARD);
        for (int part = 0; part < length && part < STRUCTURE_MAX_LENGTH; part++) {
            int px = x + part * direction_dx(direction), py = y + part * direction_dy(direction);
            game->map.plants[map_index(&game->map, px, py, z)] = plants[part];
            order_uprooting(game, px, py, z);
        }
        return CONSTRUCTION_BUILT_AT_ONCE;
    }
    return constructions_place(&game->constructions, &game->map, type, x, y, z, direction);
}

void game_plan_structure_rect(const Game *game, StructureType type, Direction direction, int x0,
                              int y0, int x1, int y1, int z, StructurePlanVisit visit,
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
            int level = game_build_level(game, head_x, head_y, z, structure_is_roof(type));
            visit(head_x, head_y, level,
                  game_can_place_structure(game, type, head_x, head_y, level, direction), userdata);
        }
    }
}

typedef struct StructurePlacing {
    Game *game;
    StructureType type;
    Direction direction;
    WoodMask woods;
    int placed;
} StructurePlacing;

static void place_planned(int x, int y, int z, bool allowed, void *userdata)
{
    StructurePlacing *placing = userdata;
    int index =
        allowed ? game_place_structure(placing->game, placing->type, x, y, z, placing->direction)
                : CONSTRUCTION_NONE;
    if (index != CONSTRUCTION_NONE) {
        construction_set_wood_choice(&placing->game->constructions, index, placing->woods);
        placing->placed++;
    }
}

int game_place_structure_rect(Game *game, StructureType type, Direction direction, int x0, int y0,
                              int x1, int y1, int z)
{
    return game_place_structure_rect_wood(game, type, direction, x0, y0, x1, y1, z, 0);
}

int game_place_structure_rect_wood(Game *game, StructureType type, Direction direction, int x0,
                                   int y0, int x1, int y1, int z, WoodMask woods)
{
    StructurePlacing placing = {game, type, direction, woods, 0};
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
        items_drop_load(&game->items, &game->map, x, y, z, &returned[i]);
    }
    return true;
}
