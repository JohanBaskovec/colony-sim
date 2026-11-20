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
        stockpile_create(&game->stockpiles, &game->map, x - 6, y - 6, x + 6, y + 6, z);
        int better =
            stockpile_create(&game->stockpiles, &game->map, x + 10, y - 4, x + 16, y + 4, z);
        stockpile_set_priority(&game->stockpiles, better, 2);
    }
    /* Everything else the land offers, on the whole map. */
    int land = whole_map_zone(game);
    gather_forever(game, land, GATHER_GRASS);
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
    gather_forever(game, land, GATHER_WOOD);
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
    /* The screen first, then the walls around it (7 × 6 tiles), then two
     * rows of beds inside. */
    game_place_structure(game, STRUCTURE_DOOR, x + 7, y + 3, z, DIRECTION_SOUTH);
    game_place_structure_rect(game, STRUCTURE_WALL, DIRECTION_SOUTH, x + 4, y - 2, x + 10, y + 3,
                              z);
    game_place_structure_rect(game, STRUCTURE_GRASS_BED, DIRECTION_EAST, x + 5, y - 1, x + 8, y + 1,
                              z);
}

/* The nearest free tile (passable, without tree, structure, station or
 * items; where a workstation may stand when `station`) to (x, y, z) within
 * `radius`, ring by ring in a fixed order. */
static bool free_tile(const Game *game, int x, int y, int z, int radius, bool station, int *out_x,
                      int *out_y)
{
    const Map *map = &game->map;
    for (int ring = 0; ring <= radius; ring++) {
        for (int dy = -ring; dy <= ring; dy++) {
            for (int dx = -ring; dx <= ring; dx++) {
                int tx = x + dx, ty = y + dy;
                if ((dx != -ring && dx != ring && dy != -ring && dy != ring) ||
                    !map_is_passable(map, tx, ty, z) ||
                    plant_is_tree(map->plants[map_index(map, tx, ty, z)].type) ||
                    map_structure(map, tx, ty, z) != STRUCTURE_NONE ||
                    workstation_at(&game->workstations, tx, ty, z) >= 0 ||
                    items_get(&game->items, tx, ty, z).type != ITEM_NONE ||
                    (station && !workstation_can_place(&game->workstations, map, tx, ty, z))) {
                    continue;
                }
                *out_x = tx;
                *out_y = ty;
                return true;
            }
        }
    }
    return false;
}

/* A lit hearth on a free tile near (x, y, z) with a bill for `recipe`. */
static int cooking_hearth(Game *game, int x, int y, int z, RecipeType recipe)
{
    int hx, hy;
    if (!free_tile(game, x, y, z, 16, true, &hx, &hy)) {
        return -1;
    }
    Workstations *stations = &game->workstations;
    map_remove_plant(&game->map, hx, hy, z); /* Grass or a bush cleared. */
    int index = workstations_place(stations, &game->map, WORKSTATION_OPEN_HEARTH, hx, hy, z);
    Workstation *station = workstation_get(stations, index);
    if (station == NULL) {
        return -1;
    }
    station->fuel = HEARTH_FUEL_CAPACITY;
    hearth_light(stations, index);
    hearth_set_keep_burning(stations, index, true);
    int bill = workstation_add_bill(stations, index, recipe);
    workstation_set_bill_mode(stations, index, bill, BILL_DO_TIMES);
    workstation_set_bill_target(stations, index, bill, BILL_TARGET_MAX);
    return index;
}

static void setup_cooking(Game *game, const Colonist *first)
{
    if (first == NULL) {
        return;
    }
    int x = first->x, y = first->y, z = first->z;
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        Colonist *colonist = game_colonist(game, game_colonist_id_at(game, slot));
        if (colonist != NULL && colonist_knowledge_level(colonist, KNOWLEDGE_FIRE) < 60.0) {
            colonist_set_knowledge_level(colonist, KNOWLEDGE_FIRE, 60.0);
        }
    }
    cooking_hearth(game, x + 4, y, z, RECIPE_ROAST_CARROTS);
    int rhizomes = cooking_hearth(game, x - 4, y, z, RECIPE_ROAST_RHIZOMES);
    if (rhizomes >= 0) {
        workstation_set_bill_quality_range(&game->workstations, rhizomes, 0, 40, ITEM_QUALITY_BEST);
    }
    /* Heaps: fresh and half rotten, good and poor, alone or with others. */
    static const int PERCENT[] = {100, 30, 70, 15, 50, 90, 25, 60};
    static const int QUALITY[] = {70, 20, 45, 90, 30, 60, 80, 50};
    for (int i = 0; i < 16; i++) {
        int hx, hy;
        if (!free_tile(game, x + (i % 4) * 5 - 8, y + (i / 4) * 5 - 8, z, 6, false, &hx, &hy)) {
            continue;
        }
        if (i % 4 == 1) {
            items_add(&game->items, hx, hy, z, item_stack_pristine(ITEM_BRANCHES, 3)); /* Under. */
        }
        ItemType food = i % 2 == 0 ? ITEM_WILD_CARROTS : ITEM_CATTAIL_RHIZOMES;
        int percent = PERCENT[i % 8], quality = QUALITY[(i * 3) % 8];
        items_add(
            &game->items, hx, hy, z,
            item_stack_make(food, 6 + i % 5, quality, item_hit_points_at_percent(food, percent)));
        if (i % 3 == 0) {
            items_add(&game->items, hx, hy, z, item_stack_pristine(ITEM_COBBLE, 2)); /* Over. */
        }
    }
    int sx, sy;
    if (free_tile(game, x, y + 12, z, 10, false, &sx, &sy)) {
        /* For food only: the stones and wood lying about stay where they are. */
        int pile =
            stockpile_create(&game->stockpiles, &game->map, sx - 3, sy - 2, sx + 3, sy + 2, z);
        stockpile_set_allowed(&game->stockpiles, pile, item_types_food());
    }
    int land =
        gather_zone_create(&game->gather_zones, &game->map, x - 30, y - 30, x + 30, y + 30, z);
    gather_forever(game, land, GATHER_WILD_CARROTS);
    gather_forever(game, land, GATHER_CATTAIL_RHIZOMES);
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
    case SCENARIO_COOKING: setup_cooking(game, first); break;
    default: break;
    }
    return true;
}
