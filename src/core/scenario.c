#include "core/scenario.h"

#include "core/tree.h"

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
    /* The fruit and nuts in the crowns, up the trees too. */
    for (int r = GATHER_HAZELNUTS; r <= GATHER_PEARS; r++) {
        gather_forever(game, land, (GatherResource)r);
    }
    gather_zone_set_climb(&game->gather_zones, land, true);
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

/* Dry grass for every site, in the stockpile (x0, y0)-(x1, y1): grass cut
 * takes three days to dry, longer in the rain, and the scenario is there to
 * measure building. The grass gathered meanwhile goes to the refreshing. */
static void stock_dry_grass(Game *game, int x0, int y0, int x1, int y1, int z)
{
    int wanted = 0;
    for (int index = 0; index < CONSTRUCTION_MAX; index++) {
        const ConstructionSite *site = construction_get(&game->constructions, index);
        for (int slot = 0; site != NULL && slot < STRUCTURE_MATERIAL_SLOTS; slot++) {
            if (construction_material(site, slot) == ITEM_GRASS) {
                wanted += construction_material_count(site, slot);
            }
        }
    }
    for (int y = y0; y <= y1 && wanted > 0; y++) {
        for (int x = x0; x <= x1 && wanted > 0; x++) {
            if (map_is_passable(&game->map, x, y, z)) {
                wanted -= items_add(&game->items, x, y, z, item_stack_pristine(ITEM_GRASS, wanted));
            }
        }
    }
}

/* Makes the ground of the rectangle (x0, y0)-(x1, y1) level z: the hills cut
 * away, the hollows filled (a scenario places things at a fixed spot of the
 * level the first settler stands on). Trees there are taken out, with what
 * lies about, and the water there drained. */
static void level_ground(Game *game, int x0, int y0, int x1, int y1, int z)
{
    Map *map = &game->map;
    for (int y = y0; y <= y1; y++) {
        for (int x = x0; x <= x1; x++) {
            if (!map_in_bounds(map, x, y, z)) {
                continue;
            }
            for (int zz = 0; zz < map->depth; zz++) {
                TreeRef tree;
                if (tree_at(map, x, y, zz, &tree)) {
                    tree_clear(map, &game->items, tree.x, tree.y, tree.z);
                }
            }
            for (int zz = 0; zz < map->depth; zz++) {
                map_remove_plant(map, x, y, zz);
                map_set_slope(map, x, y, zz, false);
                items_clear(&game->items, x, y, zz);
                map_set_water(map, x, y, zz, 0);
                if (zz > z) {
                    map_set_rock(map, x, y, zz, ROCK_NONE);
                    map_set_terrain(map, x, y, zz, TERRAIN_OPEN);
                } else if (zz == z) {
                    map_set_rock(map, x, y, zz, ROCK_NONE);
                    map_set_terrain(map, x, y, zz, TERRAIN_SOIL);
                } else if (map->rock[map_index(map, x, y, zz)] == ROCK_NONE) {
                    map_set_rock(map, x, y, zz, ROCK_EARTH); /* Filled. */
                    map_set_terrain(map, x, y, zz, TERRAIN_SOIL);
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
    level_ground(game, x - 9, y - 4, x + 11, y + 4, z);
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
    stock_dry_grass(game, x - 8, y - 3, x - 3, y + 3, z);
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

/* A built crafting spot near (x, y, z), -1 if no tile is free. */
static int crafting_spot(Game *game, int x, int y, int z)
{
    int sx, sy;
    if (!free_tile(game, x, y, z, 16, true, &sx, &sy)) {
        return -1;
    }
    map_remove_plant(&game->map, sx, sy, z);
    return workstations_place(&game->workstations, &game->map, WORKSTATION_CRAFTING_SPOT, sx, sy,
                              z);
}

static void setup_basketry(Game *game, const Colonist *first)
{
    if (first == NULL) {
        return;
    }
    int x = first->x, y = first->y, z = first->z;
    static const KnowledgeType NEEDED[] = {KNOWLEDGE_BASKETRY, KNOWLEDGE_CORDAGE,
                                           KNOWLEDGE_STONE_KNAPPING};
    for (uint32_t slot = 0; slot < game_colonist_slot_count(game); slot++) {
        Colonist *colonist = game_colonist(game, game_colonist_id_at(game, slot));
        for (size_t k = 0; colonist != NULL && k < sizeof(NEEDED) / sizeof(NEEDED[0]); k++) {
            if (colonist_knowledge_level(colonist, NEEDED[k]) < 70.0) {
                colonist_set_knowledge_level(colonist, NEEDED[k], 70.0);
            }
        }
    }
    Workstations *stations = &game->workstations;
    int spot = crafting_spot(game, x + 4, y, z);
    if (spot < 0) {
        return;
    }
    int trays = workstation_add_bill(stations, spot, RECIPE_BARK_TRAY);
    workstation_set_bill_mode(stations, spot, trays, BILL_DO_TIMES);
    workstation_set_bill_target(stations, spot, trays, 4); /* Bark left for the boxes. */
    static const RecipeType FEW[] = {RECIPE_WOODEN_SCRAPER, RECIPE_FLINT_BORER, RECIPE_BARK_BOX,
                                     RECIPE_FRAME_BASKET, RECIPE_PACK_BASKET};
    static const int TIMES[] = {3, 3, 2, 2, 1};
    for (size_t i = 0; i < sizeof(FEW) / sizeof(FEW[0]); i++) {
        int bill = workstation_add_bill(stations, spot, FEW[i]);
        workstation_set_bill_mode(stations, spot, bill, BILL_DO_TIMES);
        workstation_set_bill_target(stations, spot, bill, TIMES[i]);
    }
    /* Heaps around: bark (every fourth heap dried out), twigs, what the tools need, and
     * fresh rods and cord for the boxes and baskets of rods. */
    for (int i = 0; i < 24; i++) {
        if (i >= 16) {
            int hx, hy;
            if (free_tile(game, x + (i % 4) * 5 - 8, y + 12 + (i / 4 - 4) * 5, z, 6, false, &hx,
                          &hy)) {
                ItemStack stack = i % 2 == 0 ? item_stack_make(ITEM_HAZEL_RODS, 10, 50 + i, 0)
                                             : item_stack_make(ITEM_NETTLE_CORD, 3, 50 + i, 0);
                item_stack_set_fresh(&stack); /* Rods just cut: pliable. */
                items_add(&game->items, hx, hy, z, stack);
                /* Edges for the long weaving: the flakes wear out on the trays. */
                items_add(&game->items, hx, hy, z,
                          i % 2 == 0 ? item_stack_make(ITEM_HAND_AXE, 1, 60, 120)
                                     : item_stack_make(ITEM_FLAKE, 3, 60, 15));
            }
            continue;
        }
        int hx, hy;
        if (!free_tile(game, x + (i % 4) * 5 - 8, y + (i / 4) * 5 - 8, z, 6, false, &hx, &hy)) {
            continue;
        }
        switch (i % 4) {
        case 0: {
            ItemStack bark = item_stack_make(ITEM_BIRCH_BARK, 3 + i % 3, 40 + (i * 7) % 40, 0);
            if (i % 8 == 4) {
                item_stack_set_moisture(&bark, 0.1f);
            } else {
                item_stack_set_fresh(&bark); /* Just peeled: pliable. */
            }
            items_add(&game->items, hx, hy, z, bark);
            break;
        }
        case 1: items_add(&game->items, hx, hy, z, item_stack_pristine(ITEM_TWIGS, 8)); break;
        case 2:
            items_add(&game->items, hx, hy, z, item_stack_make(ITEM_FLAKE, 2, 50 + i, 0));
            items_add(&game->items, hx, hy, z, item_stack_pristine(ITEM_COBBLE, 2));
            break;
        default:
            items_add(&game->items, hx, hy, z,
                      item_stack_make(ITEM_BRANCHES, 2, ITEM_QUALITY_STANDARD, 0));
            break;
        }
    }
    int sx, sy;
    if (free_tile(game, x, y + 12, z, 10, false, &sx, &sy)) {
        stockpile_create(&game->stockpiles, &game->map, sx - 3, sy - 2, sx + 3, sy + 2, z);
    }
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
    case SCENARIO_BASKETRY: setup_basketry(game, first); break;
    default: break;
    }
    return true;
}
