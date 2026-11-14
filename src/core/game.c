#include "core/game.h"

#include <string.h>

bool game_init(Game *game, const WorldGenParams *params)
{
    memset(game, 0, sizeof(*game));
    if (!worldgen_generate(&game->map, params)) {
        return false;
    }
    game->seed = params->seed;
    game_clock_init(&game->clock);
    rng_seed(&game->rng, params->seed, 0x73696dULL /* "sim" */);
    return true;
}

void game_shutdown(Game *game)
{
    map_free(&game->map);
    memset(game, 0, sizeof(*game));
}

static void update_plants(Game *game)
{
    const float growth_scale = (float)GAME_PLANT_UPDATE_INTERVAL / (float)TICKS_PER_DAY;
    Map *map = &game->map;
    size_t count = map_tile_count(map);

    for (size_t i = 0; i < count; i++) {
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

void game_tick(Game *game)
{
    game->clock.tick++;
    if (game->clock.tick % GAME_PLANT_UPDATE_INTERVAL == 0) {
        update_plants(game);
    }
}

int game_update(Game *game, double real_seconds)
{
    int due = game_clock_consume(&game->clock, real_seconds);
    for (int i = 0; i < due; i++) {
        game_tick(game);
    }
    return due;
}
