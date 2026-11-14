#include "core/bed.h"

#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const TilePos NO_BED = {-1, -1};

bool beds_init(Beds *beds, int width, int height)
{
    memset(beds, 0, sizeof(*beds));
    if (width <= 0 || height <= 0) {
        return false;
    }
    beds->owner = calloc((size_t)width * (size_t)height, sizeof(*beds->owner));
    if (beds->owner == NULL) {
        return false;
    }
    beds->width = width;
    beds->height = height;
    for (int owner = 0; owner < BED_OWNER_COUNT; owner++) {
        beds->owned[owner] = NO_BED;
    }
    return true;
}

void beds_free(Beds *beds)
{
    free(beds->owner);
    memset(beds, 0, sizeof(*beds));
}

static bool in_bounds(const Beds *beds, int x, int y)
{
    return x >= 0 && y >= 0 && x < beds->width && y < beds->height;
}

static size_t index_of(const Beds *beds, int x, int y)
{
    return (size_t)y * (size_t)beds->width + (size_t)x;
}

uint8_t beds_owner(const Beds *beds, int x, int y)
{
    return in_bounds(beds, x, y) ? beds->owner[index_of(beds, x, y)] : BED_NO_OWNER;
}

bool beds_owned(const Beds *beds, uint8_t owner, TilePos *bed)
{
    if (owner == BED_NO_OWNER || beds->owned[owner].x < 0) {
        return false;
    }
    *bed = beds->owned[owner];
    return true;
}

void beds_release_owner(Beds *beds, uint8_t owner)
{
    TilePos bed;
    if (beds_owned(beds, owner, &bed)) {
        beds->owner[index_of(beds, bed.x, bed.y)] = BED_NO_OWNER;
        beds->owned[owner] = NO_BED;
    }
}

void beds_release_bed(Beds *beds, int x, int y)
{
    uint8_t owner = beds_owner(beds, x, y);
    if (owner != BED_NO_OWNER) {
        beds_release_owner(beds, owner);
    }
}

bool beds_assign(Beds *beds, uint8_t owner, int x, int y)
{
    if (owner == BED_NO_OWNER || !in_bounds(beds, x, y)) {
        return false;
    }
    beds_release_owner(beds, owner);
    beds_release_bed(beds, x, y);
    beds->owner[index_of(beds, x, y)] = owner;
    beds->owned[owner] = (TilePos){(int16_t)x, (int16_t)y};
    return true;
}

int beds_forget_missing(Beds *beds, const Map *map)
{
    int forgotten = 0;
    for (int owner = 1; owner < BED_OWNER_COUNT; owner++) {
        TilePos bed = beds->owned[owner];
        if (bed.x >= 0 && !(map_structure_is_origin(map, bed.x, bed.y) &&
                            structure_is_bed(map_structure(map, bed.x, bed.y)))) {
            beds_release_owner(beds, (uint8_t)owner);
            forgotten++;
        }
    }
    return forgotten;
}

uint64_t beds_hash(const Beds *beds, uint64_t hash)
{
    for (int owner = 1; owner < BED_OWNER_COUNT; owner++) {
        TilePos bed = beds->owned[owner];
        if (bed.x >= 0) {
            int32_t fields[3] = {owner, bed.x, bed.y};
            hash = hash_fnv1a64(hash, fields, sizeof(fields));
        }
    }
    return hash;
}
