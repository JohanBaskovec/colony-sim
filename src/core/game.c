#include "core/game.h"

#include "core/log.h"
#include "core/util.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Spawn area requirements: enough reachable land around the starting point. */
#define SPAWN_AREA_COST 30.0f
#define SPAWN_AREA_MIN_TILES 900
#define SPAWN_MAX_ATTEMPTS 400
#define SPAWN_PLACEMENT_MAX_COST 40.0f
/* Preferred: drinkable water within this travel cost of the spawn point. */
#define SPAWN_WATER_MAX_COST 60.0f
/* Bushes for the first settlers are counted and planted within this travel
 * cost of the arrival point: closer than GAME_SPAWN_FOOD_MAX_COST, since the
 * settlers stand a few tiles away from it. Planted ones are at least
 * SPAWN_BUSH_MIN_DISTANCE tiles (Manhattan) from it, each candidate spot
 * being taken with SPAWN_BUSH_SPOT_CHANCE. */
#define SPAWN_BUSH_MAX_COST (GAME_SPAWN_FOOD_MAX_COST - 10.0f)
#define SPAWN_BUSH_MIN_DISTANCE 3
#define SPAWN_BUSH_SPOT_CHANCE 0.08f
/* Flint patch laid for the first settlers when there is none near them:
 * SPAWN_FLINT_PATCH_CHANCE percent in the middle, SPAWN_FLINT_PATCH_FALLOFF
 * less per step, over SPAWN_FLINT_PATCH_RADIUS steps (Manhattan). */
#define SPAWN_FLINT_PATCH_RADIUS 2
#define SPAWN_FLINT_PATCH_CHANCE 45
#define SPAWN_FLINT_PATCH_FALLOFF 10

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

bool game_tile_has_colonist(const Game *game, int x, int y)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (entity_id_is_none(game_colonist_id_at(game, slot))) {
            continue;
        }
        const Colonist *colonist = &game->colonists[slot];
        if (colonist->x == x && colonist->y == y) {
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

EntityId game_spawn_colonist(Game *game, int x, int y)
{
    if (!map_is_passable(&game->map, x, y)) {
        return ENTITY_NONE;
    }
    const char *name = pick_name(game);
    EntityId id = pool_alloc(&game->colonist_pool);
    if (entity_id_is_none(id)) {
        return ENTITY_NONE;
    }
    Colonist *colonist = &game->colonists[id.index];
    colonist_init(colonist, name, rng_range_i32(&game->rng, 18, 60), x, y);
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
    return id;
}

/* Reservation owners are colonist slots + 1 and must fit in a byte. */
_Static_assert(COLONIST_MAX < 255, "colonist slots must fit in reservation owners");

static ColonistContext colonist_context(Game *game, uint32_t slot)
{
    ColonistContext context = {
        .map = &game->map,
        .designations = &game->designations,
        .gather_zones = &game->gather_zones,
        .items = &game->items,
        .stockpiles = &game->stockpiles,
        .workstations = &game->workstations,
        .constructions = &game->constructions,
        .reservations = &game->reservations,
        .pathfinder = &game->pathfinder,
        .regions = &game->regions,
        .rng = &game->rng,
        .tick = game->clock.tick,
        .night = colonist_is_night(game->clock.tick),
        .worker_id = (uint8_t)(slot + 1),
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
    colonist_drop_tools(colonist, &game->items, &game->map);
    colonist_free(colonist);
    memset(colonist, 0, sizeof(*colonist));
    return pool_release(&game->colonist_pool, id);
}

/* ---- Starting colonists -------------------------------------------------- */

static bool is_drinking_spot(const Map *map, int x, int y, void *userdata)
{
    (void)userdata;
    return map_is_drinking_spot(map, x, y);
}

static bool map_has_water(const Map *map)
{
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            if (map_is_water(map, x, y)) {
                return true;
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
    TilePos fallback = {0, 0};
    for (int attempt = 0; attempt < SPAWN_MAX_ATTEMPTS; attempt++) {
        int radius = 2 + attempt / 2;
        int x = clampi(center_x + rng_range_i32(&game->rng, -radius, radius), 0, map->width - 1);
        int y = clampi(center_y + rng_range_i32(&game->rng, -radius, radius), 0, map->height - 1);
        if (!map_is_passable(map, x, y)) {
            continue;
        }
        TilePos candidate = {(int16_t)x, (int16_t)y};
        if (pathfinder_reachable_count(&game->pathfinder, map, candidate, SPAWN_AREA_COST) <
            SPAWN_AREA_MIN_TILES) {
            continue;
        }
        bool fertile = terrain_def((Terrain)map->terrain[map_index(map, x, y)])->fertility >=
                       plant_def(PLANT_BUSH)->min_fertility;
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
    for (int y = 0; y < map->height; y++) {
        for (int x = 0; x < map->width; x++) {
            if (map_is_passable(map, x, y)) {
                *out = (TilePos){(int16_t)x, (int16_t)y};
                return true;
            }
        }
    }
    return false;
}

static bool is_ripe_bush(const Plant *plant)
{
    return plant->type == PLANT_BUSH && plant->growth >= plant_def(PLANT_BUSH)->min_edible_growth;
}

/* Counts the ripe bushes it visits and never accepts a tile, so that the
 * search covers the whole area. */
static bool count_ripe_bush(const Map *map, int x, int y, void *userdata)
{
    if (is_ripe_bush(&map->plants[map_index(map, x, y)])) {
        (*(int *)userdata)++;
    }
    return false;
}

typedef struct BushSpotSearch {
    TilePos center;
    Rng *rng;
    float chance; /* Of taking a suitable spot. */
} BushSpotSearch;

/* Where a bush can be planted: fertile open ground with at most grass on it,
 * not next to the arrival point. Each suitable spot is only taken with some
 * chance (drawn in the search order, which is deterministic) so that bushes
 * end up scattered. */
static bool is_bush_spot(const Map *map, int x, int y, void *userdata)
{
    BushSpotSearch *search = userdata;
    size_t index = map_index(map, x, y);
    const Plant *plant = &map->plants[index];
    if (map->rock[index] != ROCK_NONE || map_is_water(map, x, y) ||
        (plant->type != PLANT_NONE && plant->type != PLANT_GRASS) ||
        terrain_def((Terrain)map->terrain[index])->fertility <
            plant_def(PLANT_BUSH)->min_fertility ||
        abs(x - search->center.x) + abs(y - search->center.y) < SPAWN_BUSH_MIN_DISTANCE) {
        return false;
    }
    return search->chance >= 1.0f || rng_next_float(search->rng) < search->chance;
}

/* The first settlers bring no food: plants ripe berry bushes near their
 * arrival point until there are GAME_SPAWN_RIPE_BUSHES within reach. */
static void ensure_food_near(Game *game, TilePos center)
{
    int ripe = 0;
    pathfinder_find_nearest(&game->pathfinder, &game->map, center, count_ripe_bush, &ripe,
                            SPAWN_BUSH_MAX_COST, NULL, NULL);
    BushSpotSearch search = {center, &game->rng, SPAWN_BUSH_SPOT_CHANCE};
    for (; ripe < GAME_SPAWN_RIPE_BUSHES; ripe++) {
        TilePos spot;
        bool found = pathfinder_find_nearest(&game->pathfinder, &game->map, center,
                                             is_bush_spot, &search, SPAWN_BUSH_MAX_COST, NULL,
                                             &spot);
        if (!found) {
            /* Few suitable spots: take the nearest one. */
            search.chance = 1.0f;
            found = pathfinder_find_nearest(&game->pathfinder, &game->map, center, is_bush_spot,
                                            &search, SPAWN_BUSH_MAX_COST, NULL, &spot);
        }
        if (!found) {
            LOG_WARN("No room for berry bushes near the starting colonists");
            return;
        }
        Plant *plant = &game->map.plants[map_index(&game->map, spot.x, spot.y)];
        memset(plant, 0, sizeof(*plant));
        plant->type = PLANT_BUSH;
        plant->growth = 1.0f;
        plant_fill_stocks(plant);
    }
}

/* A tile of a rich enough flint patch where stones can be gathered. */
static bool is_flint_spot(const Map *map, int x, int y, void *userdata)
{
    (void)userdata;
    return map_flint_chance(map, x, y) >= GAME_SPAWN_FLINT_CHANCE &&
           gather_stone_ground(map, x, y);
}

typedef struct PatchSpotSearch {
    TilePos center;
    Rng *rng;
    float chance; /* Of taking a suitable spot. */
} PatchSpotSearch;

/* Where a flint patch can be laid: ground where stones can be gathered, not
 * next to the arrival point; scattered like the bushes (see is_bush_spot()). */
static bool is_patch_spot(const Map *map, int x, int y, void *userdata)
{
    PatchSpotSearch *search = userdata;
    if (!gather_stone_ground(map, x, y) ||
        abs(x - search->center.x) + abs(y - search->center.y) < SPAWN_BUSH_MIN_DISTANCE) {
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
                                SPAWN_BUSH_MAX_COST, NULL, NULL)) {
        return;
    }
    PatchSpotSearch search = {center, &game->rng, SPAWN_BUSH_SPOT_CHANCE};
    TilePos spot;
    if (!pathfinder_find_nearest(&game->pathfinder, map, center, is_patch_spot, &search,
                                 SPAWN_BUSH_MAX_COST, NULL, &spot)) {
        search.chance = 1.0f;
        if (!pathfinder_find_nearest(&game->pathfinder, map, center, is_patch_spot, &search,
                                     SPAWN_BUSH_MAX_COST, NULL, &spot)) {
            LOG_WARN("No room for flint near the starting colonists");
            return;
        }
    }
    /* Richest in the middle, fading over SPAWN_FLINT_PATCH_RADIUS tiles. */
    for (int dy = -SPAWN_FLINT_PATCH_RADIUS; dy <= SPAWN_FLINT_PATCH_RADIUS; dy++) {
        for (int dx = -SPAWN_FLINT_PATCH_RADIUS; dx <= SPAWN_FLINT_PATCH_RADIUS; dx++) {
            int x = spot.x + dx, y = spot.y + dy;
            int distance = abs(dx) + abs(dy);
            if (distance > SPAWN_FLINT_PATCH_RADIUS ||
                !gather_stone_ground(map, x, y)) {
                continue;
            }
            int chance = SPAWN_FLINT_PATCH_CHANCE - SPAWN_FLINT_PATCH_FALLOFF * distance;
            if (chance > map_flint_chance(map, x, y)) {
                map_set_flint_chance(map, x, y, chance);
            }
        }
    }
}

static bool is_free_tile(const Map *map, int x, int y, void *userdata)
{
    return map_is_passable(map, x, y) && !game_tile_has_colonist(userdata, x, y);
}

static void spawn_starting_colonists(Game *game, int count)
{
    TilePos center;
    if (count <= 0 || !find_spawn_center(game, &center)) {
        return;
    }
    ensure_food_near(game, center);
    ensure_flint_near(game, center);
    for (int i = 0; i < count; i++) {
        TilePos tile;
        if (!pathfinder_find_nearest(&game->pathfinder, &game->map, center, is_free_tile, game,
                                     SPAWN_PLACEMENT_MAX_COST, NULL, &tile)) {
            LOG_WARN("Not enough room to place every starting colonist");
            return;
        }
        game_spawn_colonist(game, tile.x, tile.y);
    }
}

/* ---- Lifecycle ----------------------------------------------------------- */

bool game_init(Game *game, const NewGameParams *params)
{
    memset(game, 0, sizeof(*game));
    if (!worldgen_generate(&game->map, &params->world) ||
        !designations_init(&game->designations, game->map.width, game->map.height) ||
        !gather_zones_init(&game->gather_zones, game->map.width, game->map.height) ||
        !items_init(&game->items, game->map.width, game->map.height) ||
        !stockpiles_init(&game->stockpiles, game->map.width, game->map.height) ||
        !workstations_init(&game->workstations, game->map.width, game->map.height) ||
        !constructions_init(&game->constructions, game->map.width, game->map.height) ||
        !pathfinder_init(&game->pathfinder, game->map.width, game->map.height) ||
        !regions_init(&game->regions, game->map.width, game->map.height) ||
        !pool_init(&game->colonist_pool, COLONIST_MAX)) {
        game_shutdown(game);
        return false;
    }
    game->seed = params->world.seed;
    reservations_init(&game->reservations);
    game_clock_init(&game->clock);
    rng_seed(&game->rng, params->world.seed, 0x73696dULL /* "sim" */);
    spawn_starting_colonists(game, params->colonist_count);
    regions_update(&game->regions, &game->map); /* Built here, not during the first tick. */
    return true;
}

void game_shutdown(Game *game)
{
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        colonist_free(&game->colonists[slot]);
    }
    pool_free(&game->colonist_pool);
    pathfinder_free(&game->pathfinder);
    regions_free(&game->regions);
    designations_free(&game->designations);
    gather_zones_free(&game->gather_zones);
    items_free(&game->items);
    stockpiles_free(&game->stockpiles);
    workstations_free(&game->workstations);
    constructions_free(&game->constructions);
    map_free(&game->map);
    memset(game, 0, sizeof(*game));
}

/* ---- Simulation ---------------------------------------------------------- */

/*
 * Every plant grows once per GAME_PLANT_UPDATE_INTERVAL ticks, but not all in
 * the same tick: each tick handles the next slice of the map, so that a large
 * map does not stall the simulation once per in-game hour.
 */
static void update_plants(Game *game)
{
    const float growth_scale = (float)GAME_PLANT_UPDATE_INTERVAL / (float)TICKS_PER_DAY;
    Map *map = &game->map;
    uint64_t count = map_tile_count(map);
    uint64_t phase = game->clock.tick % GAME_PLANT_UPDATE_INTERVAL;
    size_t begin = (size_t)(phase * count / GAME_PLANT_UPDATE_INTERVAL);
    size_t end = (size_t)((phase + 1) * count / GAME_PLANT_UPDATE_INTERVAL);

    for (size_t i = begin; i < end; i++) {
        Plant *plant = &map->plants[i];
        if (plant->type == PLANT_NONE) {
            continue;
        }
        plant_regrow_stocks(plant, GAME_PLANT_UPDATE_INTERVAL / TICKS_PER_HOUR);
        if (plant->growth >= 1.0f) {
            continue;
        }
        const PlantDef *def = plant_def((PlantType)plant->type);
        float fertility = terrain_def((Terrain)map->terrain[i])->fertility;
        if (fertility < def->min_fertility) {
            continue;
        }
        plant->growth += def->growth_per_day * fertility * growth_scale;
        if (plant->growth > 1.0f) {
            plant->growth = 1.0f;
        }
    }
}

/* Items lying on the ground change over time (fresh grass dries), slice by
 * slice like plants. */
static void update_items(Game *game)
{
    uint64_t count = map_tile_count(&game->map);
    uint64_t phase = game->clock.tick % GAME_ITEM_UPDATE_INTERVAL;
    size_t begin = (size_t)(phase * count / GAME_ITEM_UPDATE_INTERVAL);
    size_t end = (size_t)((phase + 1) * count / GAME_ITEM_UPDATE_INTERVAL);
    items_advance(&game->items, begin, end, GAME_ITEM_UPDATE_INTERVAL / TICKS_PER_HOUR);
}

static void update_colonists(Game *game)
{
    /* Fixed slot order keeps the simulation deterministic. */
    ColonistContext context = colonist_context(game, 0); /* The same for everyone... */
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        if (!entity_id_is_none(game_colonist_id_at(game, slot))) {
            context.worker_id = (uint8_t)(slot + 1); /* ...but for who is acting. */
            colonist_tick(&game->colonists[slot], &context);
        }
    }
}

void game_tick(Game *game)
{
    game->clock.tick++;
    regions_update(&game->regions, &game->map); /* Nothing to do unless the map changed. */
    update_plants(game);
    if (game->clock.tick % GAME_PLANT_UPDATE_INTERVAL == 0) {
        /* Every plant grew since the last time: zones may offer new jobs. */
        gather_zones_plants_changed(&game->gather_zones);
    }
    update_items(game);
    /* What gathering zones want, from what lies in the colony now. */
    gather_zones_update(&game->gather_zones, game->items.totals);
    update_colonists(game);
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
    hash = gather_zones_hash(&game->gather_zones, hash);
    hash = items_hash(&game->items, hash);
    hash = stockpiles_hash(&game->stockpiles, hash);
    hash = workstations_hash(&game->workstations, hash);
    hash = constructions_hash(&game->constructions, hash);
    hash = reservations_hash(&game->reservations, hash);
    hash = hash_fnv1a64(hash, &game->clock.tick, sizeof(game->clock.tick));
    hash = hash_fnv1a64(hash, &game->rng.state, sizeof(game->rng.state));
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
        hash = hash_fnv1a64(hash, &c->move_progress, sizeof(c->move_progress));
        hash = hash_fnv1a64(hash, c->needs, sizeof(c->needs));
        hash = hash_fnv1a64(hash, c->work_priorities, sizeof(c->work_priorities));
        hash = hash_fnv1a64(hash, c->knowledge, sizeof(c->knowledge));
        hash = hash_fnv1a64(hash, &c->haul_delivering, sizeof(c->haul_delivering));
        hash = hash_fnv1a64(hash, &c->strength, sizeof(c->strength));
        hash = hash_fnv1a64(hash, &c->max_carry_weight, sizeof(c->max_carry_weight));
        hash = hash_fnv1a64(hash, &c->max_carry_bulk, sizeof(c->max_carry_bulk));
        hash = hash_fnv1a64(hash, c->name, strlen(c->name));
        int32_t job[10] = {c->station,  c->station_number, c->bill,       c->recipe,
                           c->station_phase, c->fetch.x,   c->fetch.y,     c->fetch_tool,
                           c->work_type, c->gathering};
        hash = hash_fnv1a64(hash, job, sizeof(job));
        for (int i = 0; i < COLONIST_TOOL_MAX; i++) {
            ItemStack tool = c->tools[i];
            int32_t fields[4] = {tool.type, tool.count, tool.quality, tool.hit_points};
            hash = hash_fnv1a64(hash, fields, sizeof(fields));
        }
    }
    return hash;
}

/* ---- Designations -------------------------------------------------------- */

bool game_set_work_priority(Game *game, EntityId id, WorkType type, int priority)
{
    Colonist *colonist = game_colonist(game, id);
    return colonist != NULL && colonist_set_work_priority(colonist, type, priority);
}

bool game_designation_available(const Game *game, DesignationType type)
{
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

int game_designate_rect(Game *game, int x0, int y0, int x1, int y1, DesignationType type)
{
    return designations_set_rect(&game->designations, &game->map, x0, y0, x1, y1, type);
}

/* ---- Picking ------------------------------------------------------------- */

EntityId game_colonist_at(const Game *game, float world_x, float world_y)
{
    EntityId best = ENTITY_NONE;
    float best_distance = GAME_PICK_RADIUS * GAME_PICK_RADIUS;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id)) {
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

int game_colonists_in_rect(const Game *game, float x0, float y0, float x1, float y1,
                           EntityId *out, int max_count)
{
    float min_x = fminf(x0, x1), max_x = fmaxf(x0, x1);
    float min_y = fminf(y0, y1), max_y = fmaxf(y0, y1);
    int count = 0;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game) && count < max_count; slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id)) {
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
    for (int slot = 0; def != NULL && slot < RECIPE_TOOL_MAX; slot++) {
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
    /* Tools nobody carries yet will be taken from what lies around. */
    ItemTypeMask tools[RECIPE_TOOL_MAX] = {0};
    for (int slot = 0; slot < RECIPE_TOOL_MAX; slot++) {
        if (def->tools[slot] != 0 && !colonist_carries_tool(game, def->tools[slot])) {
            tools[slot] = def->tools[slot];
        }
    }
    return bill_has_input(bill, game->items.totals, tools);
}

bool game_can_place_workstation(const Game *game, int x, int y)
{
    return stockpile_at(&game->stockpiles, x, y) == STOCKPILE_NONE &&
           map_structure(&game->map, x, y) == STRUCTURE_NONE &&
           construction_at(&game->constructions, x, y) == CONSTRUCTION_NONE &&
           workstation_can_place(&game->workstations, &game->map, x, y);
}

int game_place_workstation(Game *game, WorkstationType type, int x, int y)
{
    if (!game_can_place_workstation(game, x, y)) {
        return WORKSTATION_NONE;
    }
    return workstations_place(&game->workstations, &game->map, type, x, y);
}

bool game_remove_workstation(Game *game, int index)
{
    const Workstation *station = workstation_get_const(&game->workstations, index);
    if (station == NULL) {
        return false;
    }
    int x = station->x, y = station->y;
    ItemStack returned[2];
    int count = workstations_remove(&game->workstations, index, returned);
    for (int i = 0; i < count; i++) {
        items_drop(&game->items, &game->map, x, y, returned[i]);
    }
    return true;
}

int game_remove_blueprints_rect(Game *game, int x0, int y0, int x1, int y1)
{
    int removed = 0;
    for (int y = (y0 < y1 ? y0 : y1); y <= (y0 < y1 ? y1 : y0); y++) {
        for (int x = (x0 < x1 ? x0 : x1); x <= (x0 < x1 ? x1 : x0); x++) {
            int index = workstation_at(&game->workstations, x, y);
            const Workstation *station = workstation_get_const(&game->workstations, index);
            if (station != NULL && !station->built && game_remove_workstation(game, index)) {
                removed++;
            }
            if (game_remove_construction(game, construction_at(&game->constructions, x, y))) {
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
    int x = station->x, y = station->y;
    ItemStack returned;
    if (workstation_remove_bill(&game->workstations, station_index, bill, &returned)) {
        items_drop(&game->items, &game->map, x, y, returned);
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

bool game_can_place_structure(const Game *game, int x, int y)
{
    return stockpile_at(&game->stockpiles, x, y) == STOCKPILE_NONE &&
           workstation_at(&game->workstations, x, y) == WORKSTATION_NONE &&
           construction_can_place(&game->constructions, &game->map, x, y);
}

int game_place_structure(Game *game, StructureType type, int x, int y)
{
    if (!game_can_place_structure(game, x, y)) {
        return CONSTRUCTION_NONE;
    }
    return constructions_place(&game->constructions, &game->map, type, x, y);
}

int game_place_structure_rect(Game *game, StructureType type, int x0, int y0, int x1, int y1)
{
    const StructureDef *def = structure_def(type);
    if (def == NULL || type == STRUCTURE_NONE) {
        return 0;
    }
    int min_x = x0 < x1 ? x0 : x1, max_x = x0 < x1 ? x1 : x0;
    int min_y = y0 < y1 ? y0 : y1, max_y = y0 < y1 ? y1 : y0;
    int placed = 0;
    for (int y = min_y; y <= max_y; y++) {
        for (int x = min_x; x <= max_x; x++) {
            bool edge = x == min_x || x == max_x || y == min_y || y == max_y;
            if ((edge || !def->drawn_as_outline) &&
                game_place_structure(game, type, x, y) != CONSTRUCTION_NONE) {
                placed++;
            }
        }
    }
    return placed;
}

bool game_remove_construction(Game *game, int index)
{
    const ConstructionSite *site = construction_get(&game->constructions, index);
    if (site == NULL) {
        return false;
    }
    int x = site->x, y = site->y;
    ItemStack returned[STRUCTURE_MATERIAL_SLOTS];
    int count = constructions_remove(&game->constructions, index, returned);
    for (int i = 0; i < count; i++) {
        items_drop(&game->items, &game->map, x, y, returned[i]);
    }
    return true;
}
