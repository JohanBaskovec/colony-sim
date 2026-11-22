#include "core/allowed_area.h"

#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static size_t tile_count(const AllowedAreas *areas)
{
    return (size_t)areas->width * (size_t)areas->height * (size_t)areas->depth;
}

/* Copies a name, cut to fit without splitting a UTF-8 character. */
static void copy_name(char *out, const char *name)
{
    size_t length = name != NULL ? strlen(name) : 0;
    if (length >= ALLOWED_AREA_NAME_SIZE) {
        length = ALLOWED_AREA_NAME_SIZE - 1;
        while (length > 0 && ((unsigned char)name[length] & 0xC0) == 0x80) {
            length--; /* name[length] continues a character: cut before it starts. */
        }
    }
    if (length > 0) {
        memcpy(out, name, length);
    }
    out[length] = '\0';
}

bool allowed_areas_init(AllowedAreas *areas, int width, int height, int depth)
{
    memset(areas, 0, sizeof(*areas));
    areas->width = width;
    areas->height = height;
    areas->depth = depth;
    size_t count = tile_count(areas);
    areas->masks = malloc(count * sizeof(*areas->masks));
    if (areas->masks == NULL) {
        return false;
    }
    for (size_t i = 0; i < count; i++) {
        areas->masks[i] = allowed_area_bit(0);
    }
    areas->count = 1;
    areas->list[0].tiles = (long)count;
    return true;
}

void allowed_areas_free(AllowedAreas *areas)
{
    free(areas->masks);
    areas->masks = NULL;
    areas->count = 0;
}

bool allowed_areas_valid(const AllowedAreas *areas, int index)
{
    return index >= 0 && index < areas->count;
}

const AllowedArea *allowed_areas_get(const AllowedAreas *areas, int index)
{
    return allowed_areas_valid(areas, index) ? &areas->list[index] : NULL;
}

bool allowed_areas_holds(const AllowedAreas *areas, int index, int x, int y, int z)
{
    if (!allowed_areas_valid(areas, index) || areas->masks == NULL || x < 0 || y < 0 || z < 0 ||
        x >= areas->width || y >= areas->height || z >= areas->depth) {
        return false;
    }
    size_t at = ((size_t)z * (size_t)areas->height + (size_t)y) * (size_t)areas->width + (size_t)x;
    return (areas->masks[at] & allowed_area_bit(index)) != 0;
}

int allowed_areas_add(AllowedAreas *areas, const char *name)
{
    if (areas->count >= ALLOWED_AREA_MAX) {
        return ALLOWED_AREA_NONE;
    }
    int index = areas->count++;
    AllowedArea *area = &areas->list[index];
    memset(area, 0, sizeof(*area));
    copy_name(area->name, name);
    areas->revision++;
    return index; /* No tile holds its bit: it was cleared when its last owner went. */
}

bool allowed_areas_remove(AllowedAreas *areas, int index)
{
    if (!allowed_areas_valid(areas, index) || areas->count <= 1) {
        return false;
    }
    /* Every bit above it moves down one. */
    const uint16_t below = (uint16_t)(allowed_area_bit(index) - 1u);
    size_t count = tile_count(areas);
    for (size_t i = 0; i < count; i++) {
        uint16_t mask = areas->masks[i];
        areas->masks[i] = (uint16_t)((mask & below) | ((mask >> (index + 1)) << index));
    }
    memmove(&areas->list[index], &areas->list[index + 1],
            sizeof(areas->list[0]) * (size_t)(areas->count - index - 1));
    areas->count--;
    memset(&areas->list[areas->count], 0, sizeof(areas->list[0]));
    areas->revision++;
    return true;
}

bool allowed_areas_rename(AllowedAreas *areas, int index, const char *name)
{
    if (!allowed_areas_valid(areas, index)) {
        return false;
    }
    copy_name(areas->list[index].name, name);
    areas->revision++;
    return true;
}

bool allowed_areas_invert(AllowedAreas *areas, int index)
{
    if (!allowed_areas_valid(areas, index)) {
        return false;
    }
    const uint16_t bit = allowed_area_bit(index);
    size_t count = tile_count(areas);
    for (size_t i = 0; i < count; i++) {
        areas->masks[i] ^= bit;
    }
    areas->list[index].tiles = (long)count - areas->list[index].tiles;
    areas->revision++;
    return true;
}

static int clamp_int(int value, int low, int high)
{
    return value < low ? low : (value > high ? high : value);
}

long allowed_areas_set_box(AllowedAreas *areas, int index, int x0, int y0, int z0, int x1, int y1,
                           int z1, bool held)
{
    if (!allowed_areas_valid(areas, index)) {
        return 0;
    }
    if (x1 < x0) {
        int swap = x0;
        x0 = x1;
        x1 = swap;
    }
    if (y1 < y0) {
        int swap = y0;
        y0 = y1;
        y1 = swap;
    }
    if (z1 < z0) {
        int swap = z0;
        z0 = z1;
        z1 = swap;
    }
    if (x1 < 0 || y1 < 0 || z1 < 0 || x0 >= areas->width || y0 >= areas->height ||
        z0 >= areas->depth) {
        return 0;
    }
    x0 = clamp_int(x0, 0, areas->width - 1);
    x1 = clamp_int(x1, 0, areas->width - 1);
    y0 = clamp_int(y0, 0, areas->height - 1);
    y1 = clamp_int(y1, 0, areas->height - 1);
    z0 = clamp_int(z0, 0, areas->depth - 1);
    z1 = clamp_int(z1, 0, areas->depth - 1);
    const uint16_t bit = allowed_area_bit(index);
    long changed = 0;
    for (int z = z0; z <= z1; z++) {
        for (int y = y0; y <= y1; y++) {
            uint16_t *row =
                areas->masks + ((size_t)z * (size_t)areas->height + (size_t)y) * areas->width;
            for (int x = x0; x <= x1; x++) {
                if (((row[x] & bit) != 0) != held) {
                    row[x] ^= bit;
                    changed++;
                }
            }
        }
    }
    areas->list[index].tiles += held ? changed : -changed;
    if (changed > 0) {
        areas->revision++;
    }
    return changed;
}

uint64_t allowed_areas_hash(const AllowedAreas *areas, uint64_t hash)
{
    int32_t count = areas->count;
    hash = hash_fnv1a64(hash, &count, sizeof(count));
    return hash_fnv1a64(hash, areas->masks, tile_count(areas) * sizeof(*areas->masks));
}

void allowed_areas_serialize(Serializer *s, AllowedAreas *areas)
{
    ser_int_in(s, &areas->count, 1, ALLOWED_AREA_MAX);
    for (int i = 0; i < ALLOWED_AREA_MAX; i++) {
        AllowedArea *area = &areas->list[i];
        ser_bytes(s, area->name, sizeof(area->name));
        area->name[sizeof(area->name) - 1] = '\0';
        if (s->reading) {
            area->tiles = 0; /* Counted below. */
        }
    }
    size_t count = tile_count(areas);
    ser_layer_u16(s, areas->masks, count);
    if (!s->reading || !ser_ok(s)) {
        return;
    }
    /* No bit for an area that does not exist; the counts again. */
    const uint16_t used = (uint16_t)((1u << (unsigned)areas->count) - 1u);
    for (size_t i = 0; i < count; i++) {
        uint16_t mask = areas->masks[i];
        if (!ser_check(s, (mask & ~used) == 0)) {
            return;
        }
        while (mask != 0) {
            areas->list[lowest_bit64(mask)].tiles++;
            mask &= (uint16_t)(mask - 1u);
        }
    }
}
