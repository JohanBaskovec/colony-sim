/*
 * Reservations: which settler is busy with what, so that two settlers never
 * walk to the same job or the same berry bush.
 *
 * A reservation is (kind, tile, owner). A tile has at most one reservation of
 * each kind. Owners are small non-zero ids (colonist slot + 1). A settler
 * releases everything it holds whenever it changes task, so reservations
 * cannot leak.
 */
#ifndef COLONY_CORE_RESERVATION_H
#define COLONY_CORE_RESERVATION_H

#include "core/pathfinding.h"

#include <stdbool.h>
#include <stdint.h>

#define RESERVATION_MAX 256
#define RESERVATION_NO_OWNER 0

typedef enum ReservationKind {
    RESERVATION_WORK, /* A designated tile being worked on. */
    RESERVATION_FOOD, /* Food (plant or items) about to be eaten. */
    RESERVATION_HAUL_SOURCE, /* An item stack about to be picked up. */
    RESERVATION_STORAGE, /* A storage tile items are being brought to. */
    RESERVATION_KIND_COUNT
} ReservationKind;

typedef struct Reservation {
    uint8_t kind; /* ReservationKind */
    uint8_t owner;
    TilePos tile;
} Reservation;

typedef struct Reservations {
    Reservation entries[RESERVATION_MAX];
    int count;
    int count_by_kind[RESERVATION_KIND_COUNT];
    /* Incremented on every release: something may have become available. */
    uint32_t releases;
} Reservations;

void reservations_init(Reservations *reservations);

/* Fails if the tile is already reserved for this kind, the owner is invalid or
 * the table is full. */
bool reservations_add(Reservations *reservations, ReservationKind kind, TilePos tile,
                      uint8_t owner);

/* RESERVATION_NO_OWNER when the tile is free for this kind. */
uint8_t reservations_owner(const Reservations *reservations, ReservationKind kind, TilePos tile);

/* Whether someone other than `self` holds the tile for this kind. */
bool reservations_taken_by_other(const Reservations *reservations, ReservationKind kind,
                                 TilePos tile, uint8_t self);

/* Releases everything held by `owner`. */
void reservations_release_all(Reservations *reservations, uint8_t owner);

/* Releases what `owner` holds for one kind only. */
void reservations_release_kind(Reservations *reservations, ReservationKind kind, uint8_t owner);

uint64_t reservations_hash(const Reservations *reservations, uint64_t hash);

#endif /* COLONY_CORE_RESERVATION_H */
