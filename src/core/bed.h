/*
 * Beds: which settler owns which bed.
 *
 * A bed (core/structure.h) is owned by one settler at most, a settler owns
 * one bed at most. Ownership is kept on the bed's head tile, by owner id (the
 * colonist's slot + 1, as for reservations), with the reverse table for the
 * bed of each owner. A settler without a bed takes the nearest free one the
 * first night (core/colonist.h) and sleeps there from then on.
 *
 * The map does not tell this module when a bed goes: beds_forget_missing()
 * drops the beds no longer on the map (the game calls it every tick).
 */
#ifndef COLONY_CORE_BED_H
#define COLONY_CORE_BED_H

#include "core/serialize.h"

#include "core/map.h"
#include "core/pathfinding.h"

#include <stdbool.h>
#include <stdint.h>

#define BED_NO_OWNER 0
#define BED_OWNER_COUNT 256 /* Owner ids are 1..255. */

typedef struct Beds {
    int width;
    int height;
    int depth;
    uint8_t *owner; /* Per tile: owner of the bed whose head lies there, or BED_NO_OWNER. */
    TilePos owned[BED_OWNER_COUNT]; /* Per owner id: head of its bed, x = -1 when none. */
} Beds;

bool beds_init(Beds *beds, int width, int height, int depth);
void beds_free(Beds *beds);

/* Owner of the bed whose head is at (x, y), or BED_NO_OWNER. */
uint8_t beds_owner(const Beds *beds, int x, int y, int z);

/* Head of the bed of an owner; false when it has none. */
bool beds_owned(const Beds *beds, uint8_t owner, TilePos *bed);

/* Gives the bed whose head is at (x, y) to an owner: the bed it had is freed,
 * and the bed's former owner loses it. Fails for BED_NO_OWNER or out of
 * bounds. */
bool beds_assign(Beds *beds, uint8_t owner, int x, int y, int z);

/* Frees the bed of an owner (the settler is gone), if any. */
void beds_release_owner(Beds *beds, uint8_t owner);

/* Frees the bed whose head is at (x, y), if owned. */
void beds_release_bed(Beds *beds, int x, int y, int z);

/* Frees the owned beds whose head no longer holds the head of a bed. Returns
 * how many. */
int beds_forget_missing(Beds *beds, const Map *map);

uint64_t beds_hash(const Beds *beds, uint64_t hash);

/* Saves or loads (docs/SAVE.md) who owns which bed, already allocated at the
 * size of the map. */
void beds_serialize(Serializer *s, Beds *beds);

#endif /* COLONY_CORE_BED_H */
