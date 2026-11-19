#include "core/scenario.h"

static void gather_forever(Game *game, int zone, GatherResource resource)
{
    gather_zone_set_enabled(&game->gather_zones, zone, resource, true);
    gather_zone_set_mode(&game->gather_zones, zone, resource, GATHER_FOREVER);
}

/* A gathering zone over the whole map, every level. */
static int whole_map_zone(Game *game)
{
    const Map *map = &game->map;
    int zone = GATHER_ZONE_NONE;
    for (int z = 0; z < map->depth; z++) {
        if (zone == GATHER_ZONE_NONE) {
            zone = gather_zone_create(&game->gather_zones, map, 0, 0, map->width - 1,
                                      map->height - 1, z);
        } else {
            gather_zone_extend(&game->gather_zones, map, zone, 0, 0, map->width - 1,
                               map->height - 1, z);
        }
    }
    return zone;
}

static void setup_gathering(Game *game, const Colonist *first)
{
    if (first != NULL) {
        int x = first->x, y = first->y, z = first->z;
        /* Stones forever: keeps some colonists busy to the end. */
        int stones =
            gather_zone_create(&game->gather_zones, &game->map, x - 14, y + 8, x - 10, y + 12, z);
        gather_forever(game, stones, GATHER_COBBLES);
        gather_forever(game, stones, GATHER_FLINT);
        stockpile_create(&game->stockpiles, &game->map, x - 6, y - 6, x + 6, y + 6, z);
        int better =
            stockpile_create(&game->stockpiles, &game->map, x + 10, y - 4, x + 16, y + 4, z);
        stockpile_set_priority(&game->stockpiles, better, 2);
    }
    /* Everything else the land offers, on the whole map. */
    int land = whole_map_zone(game);
    gather_forever(game, land, GATHER_GRASS);
    gather_forever(game, land, GATHER_TWIGS);
    gather_forever(game, land, GATHER_BRANCHES);
    for (int r = GATHER_BLACKBERRIES; r <= GATHER_CATTAIL_RHIZOMES; r++) { /* Wild food. */
        gather_forever(game, land, (GatherResource)r);
    }
    gather_zone_set_group_mode(&game->gather_zones, land, GATHER_GROUP_FOOD, GATHER_FOREVER);
}

static void setup_unreachable(Game *game, const Colonist *first)
{
    const Map *map = &game->map;
    regions_update(&game->regions, map);
    int land = whole_map_zone(game);
    gather_forever(game, land, GATHER_BRANCHES);
    uint32_t home =
        first != NULL ? regions_at(&game->regions, map, first->x, first->y, first->z) : REGION_NONE;
    for (int z = 0; z < map->depth; z++) {
        for (int y = 0; y < map->height; y++) {
            for (int x = 0; x < map->width; x++) {
                if (regions_at(&game->regions, map, x, y, z) == home) {
                    gather_zones_remove_rect(&game->gather_zones, x, y, x, y, z);
                }
            }
        }
    }
}

static void setup_construction(Game *game, const Colonist *first)
{
    if (first == NULL) {
        return;
    }
    int x = first->x, y = first->y, z = first->z;
    stockpile_create(&game->stockpiles, &game->map, x - 8, y - 3, x - 3, y + 3, z);
    int land =
        gather_zone_create(&game->gather_zones, &game->map, x - 30, y - 30, x + 30, y + 30, z);
    gather_forever(game, land, GATHER_GRASS);
    gather_forever(game, land, GATHER_BRANCHES);
    gather_forever(game, land, GATHER_TWIGS);
    /* The screen first, then the walls around it (7 × 6 tiles), then two
     * rows of beds inside. */
    game_place_structure(game, STRUCTURE_DOOR, x + 7, y + 3, z, DIRECTION_SOUTH);
    game_place_structure_rect(game, STRUCTURE_WALL, DIRECTION_SOUTH, x + 4, y - 2, x + 10, y + 3,
                              z);
    game_place_structure_rect(game, STRUCTURE_GRASS_BED, DIRECTION_EAST, x + 5, y - 1, x + 8, y + 1,
                              z);
}

bool scenario_apply(Game *game, Scenario scenario)
{
    if ((unsigned)scenario >= SCENARIO_COUNT) {
        return false;
    }
    const Colonist *first = game_colonist_const(game, game_colonist_id_at(game, 0));
    switch (scenario) {
    case SCENARIO_GATHERING: setup_gathering(game, first); break;
    case SCENARIO_UNREACHABLE: setup_unreachable(game, first); break;
    case SCENARIO_CONSTRUCTION: setup_construction(game, first); break;
    case SCENARIO_FEUDS:
        setup_gathering(game, first);
        game_debug_feud(game);
        break;
    default: break;
    }
    return true;
}
