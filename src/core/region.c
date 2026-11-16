#include "core/region.h"

#include <stdlib.h>
#include <string.h>

bool regions_init(Regions *regions, int width, int height, int depth)
{
    memset(regions, 0, sizeof(*regions));
    if (width <= 0 || height <= 0 || depth <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height * (size_t)depth;
    regions->id = calloc(count, sizeof(*regions->id));
    regions->stack = malloc(count * sizeof(*regions->stack));
    /* At most one region per tile, plus REGION_NONE. */
    regions->drinkable = calloc(count + 1, sizeof(*regions->drinkable));
    if (regions->id == NULL || regions->stack == NULL || regions->drinkable == NULL) {
        regions_free(regions);
        return false;
    }
    regions->width = width;
    regions->height = height;
    regions->depth = depth;
    return true;
}

void regions_free(Regions *regions)
{
    free(regions->boxes);
    free(regions->id);
    free(regions->stack);
    free(regions->drinkable);
    memset(regions, 0, sizeof(*regions));
}

bool regions_current(const Regions *regions, const Map *map)
{
    return regions->built && regions->map_revision == map->topology_revision &&
           regions->width == map->width && regions->height == map->height &&
           regions->depth == map->depth;
}

/* Makes room for the box of a new region id; without memory, boxes are
 * given up (regions_box() then answers the whole map). */
static void new_region_box(Regions *regions, uint32_t region, uint32_t tile)
{
    if (regions->boxes_lost) {
        return;
    }
    if (region >= regions->box_capacity) {
        uint32_t capacity = regions->box_capacity > 0 ? regions->box_capacity * 2 : 256;
        while (capacity <= region) {
            capacity *= 2;
        }
        TileBox *grown = realloc(regions->boxes, (size_t)capacity * sizeof(*grown));
        if (grown == NULL) {
            regions->boxes_lost = true;
            return;
        }
        regions->boxes = grown;
        regions->box_capacity = capacity;
    }
    const uint32_t width = (uint32_t)regions->width, level = width * (uint32_t)regions->height;
    int x = (int)(tile % width), y = (int)(tile % level / width), z = (int)(tile / level);
    regions->boxes[region] = (TileBox){x, y, z, x, y, z};
}

/*
 * Flood fill over the four orthogonal neighbors and the slopes to other
 * levels (map_level_links()). Walking also allows diagonal steps, but only
 * between tiles whose two shared orthogonal neighbors are passable (no corner
 * cutting), so diagonals never connect more than orthogonal steps do.
 */
static void fill(Regions *regions, const Map *map, uint32_t start, uint32_t region)
{
    const uint32_t width = (uint32_t)map->width;
    const uint32_t height = (uint32_t)map->height;
    uint32_t top = 0;
    regions->stack[top++] = start;
    regions->id[start] = region;
    new_region_box(regions, region, start);
    TileBox *box = regions->boxes_lost ? NULL : &regions->boxes[region];
    while (top > 0) {
        uint32_t tile = regions->stack[--top];
        uint32_t x = tile % width;
        uint32_t y = tile % (width * height) / width;
        uint32_t z = tile / (width * height);
        if (map_is_drinking_spot(map, (int)x, (int)y, (int)z)) {
            regions->drinkable[region] = 1;
        }
        if (box != NULL) {
            tile_box_add(box, (int)x, (int)y, (int)z);
        }
        uint32_t neighbors[4 + MAP_MAX_LEVEL_LINKS];
        TilePos links[MAP_MAX_LEVEL_LINKS];
        int link_count = map_level_links(map, (int)x, (int)y, (int)z, links);
        int count = 0;
        for (int i = 0; i < link_count; i++) {
            neighbors[count++] = (uint32_t)map_index(map, links[i].x, links[i].y, links[i].z);
        }
        if (x > 0) {
            neighbors[count++] = tile - 1;
        }
        if (x + 1 < width) {
            neighbors[count++] = tile + 1;
        }
        if (y > 0) {
            neighbors[count++] = tile - width;
        }
        if (y + 1 < height) {
            neighbors[count++] = tile + width;
        }
        for (int i = 0; i < count; i++) {
            uint32_t next = neighbors[i];
            if (regions->id[next] == REGION_NONE && map_move_cost_at(map, next) > 0.0f) {
                regions->id[next] = region; /* Marked when pushed: pushed once. */
                regions->stack[top++] = next;
            }
        }
    }
}

/*
 * Adds a tile that just became passable. It joins the region of its passable
 * neighbors (on its level and up or down slopes), or starts a new one if it
 * has none. Returns false when it joins several regions together: the caller
 * has to rebuild.
 */
static bool open_tile(Regions *regions, const Map *map, int x, int y, int z)
{
    size_t tile = map_index(map, x, y, z);
    if (map_move_cost_at(map, tile) <= 0.0f) {
        return false; /* Not what the log says: start over. */
    }
    static const int DX[4] = {-1, 1, 0, 0};
    static const int DY[4] = {0, 0, -1, 1};
    TilePos neighbors[4 + MAP_MAX_LEVEL_LINKS];
    int count = map_level_links(map, x, y, z, neighbors);
    for (int i = 0; i < 4; i++) {
        neighbors[count++] = (TilePos){(int16_t)(x + DX[i]), (int16_t)(y + DY[i]), (int16_t)z};
    }
    uint32_t region = REGION_NONE;
    for (int i = 0; i < count; i++) {
        const TilePos *n = &neighbors[i];
        if (!map_in_bounds(map, n->x, n->y, n->z)) {
            continue;
        }
        uint32_t neighbor = regions->id[map_index(map, n->x, n->y, n->z)];
        if (neighbor == REGION_NONE) {
            continue;
        }
        if (region != REGION_NONE && neighbor != region) {
            return false;
        }
        region = neighbor;
    }
    if (region == REGION_NONE) {
        region = ++regions->count;
        regions->drinkable[region] = 0;
        new_region_box(regions, region, (uint32_t)tile);
    } else if (!regions->boxes_lost) {
        tile_box_add(&regions->boxes[region], x, y, z);
    }
    regions->id[tile] = region;
    if (map_is_drinking_spot(map, x, y, z)) {
        regions->drinkable[region] = 1;
    }
    return true;
}

/* Applies the map's recent changes one by one, if they are all still in its
 * log and all simple enough. */
static bool update_incrementally(Regions *regions, const Map *map)
{
    uint32_t pending = map->topology_revision - regions->map_revision;
    if (!regions->built || pending > MAP_TOPOLOGY_LOG_SIZE) {
        return false;
    }
    for (uint32_t i = 1; i <= pending; i++) {
        uint32_t revision = regions->map_revision + i;
        const MapTopologyChange *change = &map->topology_log[revision % MAP_TOPOLOGY_LOG_SIZE];
        if (!change->opened || !map_in_bounds(map, change->x, change->y, change->z) ||
            !open_tile(regions, map, change->x, change->y, change->z)) {
            return false;
        }
    }
    regions->map_revision = map->topology_revision;
    return true;
}

void regions_update(Regions *regions, const Map *map)
{
    if (regions_current(regions, map) || regions->id == NULL || regions->width != map->width ||
        regions->height != map->height || regions->depth != map->depth) {
        return;
    }
    if (update_incrementally(regions, map)) {
        return;
    }
    regions->rebuilds++;
    size_t tiles = map_tile_count(map);
    memset(regions->id, 0, tiles * sizeof(*regions->id));
    memset(regions->drinkable, 0, (size_t)regions->count + 1);
    regions->count = 0;
    regions->boxes_lost = false; /* Every box is made again. */
    for (size_t tile = 0; tile < tiles; tile++) {
        if (regions->id[tile] == REGION_NONE && map_move_cost_at(map, tile) > 0.0f) {
            fill(regions, map, (uint32_t)tile, ++regions->count);
        }
    }
    regions->map_revision = map->topology_revision;
    regions->built = true;
}

uint32_t regions_at(const Regions *regions, const Map *map, int x, int y, int z)
{
    if (!regions_current(regions, map) || !map_in_bounds(map, x, y, z)) {
        return REGION_NONE;
    }
    return regions->id[map_index(map, x, y, z)];
}

bool regions_connected(const Regions *regions, const Map *map, TilePos a, TilePos b)
{
    uint32_t region_a = regions_at(regions, map, a.x, a.y, a.z);
    uint32_t region_b = regions_at(regions, map, b.x, b.y, b.z);
    return region_a == REGION_NONE || region_b == REGION_NONE || region_a == region_b;
}

TileBox regions_box(const Regions *regions, const Map *map, uint32_t region)
{
    if (!regions_current(regions, map) || region == REGION_NONE || region > regions->count ||
        regions->boxes_lost) {
        return (TileBox){0, 0, 0, map->width - 1, map->height - 1, map->depth - 1};
    }
    return regions->boxes[region];
}

bool regions_has_drinking_spot(const Regions *regions, const Map *map, uint32_t region)
{
    if (!regions_current(regions, map) || region == REGION_NONE || region > regions->count) {
        return true; /* Unknown: the caller has to look. */
    }
    return regions->drinkable[region] != 0;
}
