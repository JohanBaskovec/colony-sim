#include "core/game.h"

#include "core/log.h"
#include "core/util.h"

#include <math.h>
#include <string.h>

/* Spawn area requirements: enough reachable land around the starting point. */
#define SPAWN_AREA_COST 30.0f
#define SPAWN_AREA_MIN_TILES 900
#define SPAWN_MAX_ATTEMPTS 400
#define SPAWN_PLACEMENT_MAX_COST 40.0f
/* Preferred: drinkable water within this travel cost of the spawn point. */
#define SPAWN_WATER_MAX_COST 60.0f

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
    for (int skill = 0; skill < SKILL_COUNT; skill++) {
        colonist_set_skill_level(colonist, (SkillType)skill, skill_random_level(&game->rng));
    }
    return id;
}

/* Reservation owners are colonist slots + 1 and must fit in a byte. */
_Static_assert(COLONIST_MAX < 255, "colonist slots must fit in reservation owners");

static ColonistContext colonist_context(Game *game, uint32_t slot)
{
    ColonistContext context = {
        .map = &game->map,
        .designations = &game->designations,
        .items = &game->items,
        .stockpiles = &game->stockpiles,
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
 * preferably close to drinkable water. */
static bool find_spawn_center(Game *game, TilePos *out)
{
    Map *map = &game->map;
    int center_x = map->width / 2;
    int center_y = map->height / 2;
    bool want_water = map_has_water(map);
    bool have_dry_candidate = false;
    TilePos dry_candidate = {0, 0};
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
        if (!want_water || pathfinder_find_nearest(&game->pathfinder, map, candidate,
                                                   is_drinking_spot, NULL, SPAWN_WATER_MAX_COST,
                                                   NULL, NULL)) {
            *out = candidate;
            return true;
        }
        if (!have_dry_candidate) {
            have_dry_candidate = true;
            dry_candidate = candidate;
        }
    }
    if (have_dry_candidate) {
        LOG_WARN("No spawn area close to water found");
        *out = dry_candidate;
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
        !items_init(&game->items, game->map.width, game->map.height) ||
        !stockpiles_init(&game->stockpiles, game->map.width, game->map.height) ||
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
    items_free(&game->items);
    stockpiles_free(&game->stockpiles);
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
        if (plant->type == PLANT_NONE || plant->growth >= 1.0f) {
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
    hash = items_hash(&game->items, hash);
    hash = stockpiles_hash(&game->stockpiles, hash);
    hash = reservations_hash(&game->reservations, hash);
    hash = hash_fnv1a64(hash, &game->clock.tick, sizeof(game->clock.tick));
    hash = hash_fnv1a64(hash, &game->rng.state, sizeof(game->rng.state));
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        EntityId id = game_colonist_id_at(game, slot);
        if (entity_id_is_none(id)) {
            continue;
        }
        const Colonist *c = &game->colonists[slot];
        int32_t ints[14] = {(int32_t)id.index,   (int32_t)id.generation, c->x,
                            c->y,                (int32_t)c->task,       c->timer,
                            c->target.x,         c->target.y,            c->carried.type,
                            c->carried.count,    c->carried.quality,     c->carried.hit_points,
                            c->haul_destination.x, c->haul_destination.y};
        hash = hash_fnv1a64(hash, ints, sizeof(ints));
        hash = hash_fnv1a64(hash, &c->move_progress, sizeof(c->move_progress));
        hash = hash_fnv1a64(hash, c->needs, sizeof(c->needs));
        hash = hash_fnv1a64(hash, c->work_priorities, sizeof(c->work_priorities));
        hash = hash_fnv1a64(hash, c->skills, sizeof(c->skills));
        hash = hash_fnv1a64(hash, c->skill_xp, sizeof(c->skill_xp));
        hash = hash_fnv1a64(hash, &c->max_carry_weight, sizeof(c->max_carry_weight));
        hash = hash_fnv1a64(hash, &c->max_carry_bulk, sizeof(c->max_carry_bulk));
        hash = hash_fnv1a64(hash, c->name, strlen(c->name));
    }
    return hash;
}

/* ---- Designations -------------------------------------------------------- */

bool game_set_work_priority(Game *game, EntityId id, WorkType type, int priority)
{
    Colonist *colonist = game_colonist(game, id);
    return colonist != NULL && colonist_set_work_priority(colonist, type, priority);
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
