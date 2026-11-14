#include "core/room.h"

#include <stdlib.h>
#include <string.h>

bool rooms_init(Rooms *rooms, int width, int height)
{
    memset(rooms, 0, sizeof(*rooms));
    if (width <= 0 || height <= 0) {
        return false;
    }
    size_t count = (size_t)width * (size_t)height;
    rooms->id = calloc(count, sizeof(*rooms->id));
    rooms->stack = malloc(count * sizeof(*rooms->stack));
    /* At most one area per tile, plus ROOM_NONE. */
    rooms->list = calloc(count + 1, sizeof(*rooms->list));
    if (rooms->id == NULL || rooms->stack == NULL || rooms->list == NULL) {
        rooms_free(rooms);
        return false;
    }
    rooms->width = width;
    rooms->height = height;
    return true;
}

void rooms_free(Rooms *rooms)
{
    free(rooms->id);
    free(rooms->stack);
    free(rooms->list);
    memset(rooms, 0, sizeof(*rooms));
}

bool rooms_current(const Rooms *rooms, const Map *map)
{
    return rooms->built && rooms->topology_revision == map->topology_revision &&
           rooms->build_revision == map->build_revision && rooms->width == map->width &&
           rooms->height == map->height;
}

/* Rock, a wall or a door: what closes a room. */
static bool closes_rooms(const Map *map, size_t tile)
{
    StructureType structure = (StructureType)map->structure[tile];
    return map->rock[tile] != ROCK_NONE || structure_blocks(structure) ||
           structure_is_door(structure);
}

/* Flood fill over the four orthogonal neighbors (walls meeting at a corner
 * close a room). */
static void fill(Rooms *rooms, const Map *map, uint32_t start, uint32_t area)
{
    const uint32_t width = (uint32_t)map->width;
    const uint32_t height = (uint32_t)map->height;
    Room *room = &rooms->list[area];
    *room = (Room){0, 0, true};
    uint32_t top = 0;
    rooms->stack[top++] = start;
    rooms->id[start] = area;
    while (top > 0) {
        uint32_t tile = rooms->stack[--top];
        uint32_t x = tile % width;
        uint32_t y = tile / width;
        room->tiles++;
        if (x == 0 || y == 0 || x + 1 == width || y + 1 == height) {
            room->closed = false; /* Open to the rest of the world. */
        }
        uint32_t neighbors[4];
        int count = 0;
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
            if (rooms->id[next] == ROOM_NONE && !closes_rooms(map, next)) {
                rooms->id[next] = area; /* Marked when pushed: pushed once. */
                rooms->stack[top++] = next;
            }
        }
    }
    if (room->tiles > ROOM_MAX_TILES) {
        room->closed = false;
    }
}

void rooms_update(Rooms *rooms, const Map *map)
{
    if (rooms_current(rooms, map) || rooms->id == NULL || rooms->width != map->width ||
        rooms->height != map->height) {
        return;
    }
    size_t tiles = map_tile_count(map);
    memset(rooms->id, 0, tiles * sizeof(*rooms->id));
    rooms->count = 0;
    for (size_t tile = 0; tile < tiles; tile++) {
        if (rooms->id[tile] == ROOM_NONE && !closes_rooms(map, tile)) {
            fill(rooms, map, (uint32_t)tile, ++rooms->count);
        }
    }
    /* Roofs, for rooms only: outdoors is mostly open sky. */
    for (size_t tile = 0; tile < tiles; tile++) {
        Room *room = &rooms->list[rooms->id[tile]];
        if (rooms->id[tile] != ROOM_NONE && room->closed &&
            map_is_roofed(map, (int)(tile % (size_t)map->width),
                          (int)(tile / (size_t)map->width))) {
            room->roofed++;
        }
    }
    rooms->topology_revision = map->topology_revision;
    rooms->build_revision = map->build_revision;
    rooms->built = true;
}

const Room *rooms_at(const Rooms *rooms, const Map *map, int x, int y)
{
    if (!rooms_current(rooms, map) || !map_in_bounds(map, x, y)) {
        return NULL;
    }
    uint32_t area = rooms->id[map_index(map, x, y)];
    return area != ROOM_NONE ? &rooms->list[area] : NULL;
}

bool room_is_roofed(const Room *room)
{
    return room != NULL && room->closed && room->roofed == room->tiles;
}
