#include "core/reservation.h"

#include "core/util.h"

#include <string.h>

void reservations_init(Reservations *reservations)
{
    memset(reservations, 0, sizeof(*reservations));
}

static int find(const Reservations *reservations, ReservationKind kind, TilePos tile)
{
    for (int i = 0; i < reservations->count; i++) {
        const Reservation *entry = &reservations->entries[i];
        if (entry->kind == kind && entry->tile.x == tile.x && entry->tile.y == tile.y) {
            return i;
        }
    }
    return -1;
}

bool reservations_add(Reservations *reservations, ReservationKind kind, TilePos tile,
                      uint8_t owner)
{
    if (owner == RESERVATION_NO_OWNER || (unsigned)kind >= RESERVATION_KIND_COUNT ||
        reservations->count >= RESERVATION_MAX || find(reservations, kind, tile) >= 0) {
        return false;
    }
    reservations->entries[reservations->count++] = (Reservation){(uint8_t)kind, owner, tile};
    reservations->count_by_kind[kind]++;
    return true;
}

uint8_t reservations_owner(const Reservations *reservations, ReservationKind kind, TilePos tile)
{
    int index = find(reservations, kind, tile);
    return index >= 0 ? reservations->entries[index].owner : RESERVATION_NO_OWNER;
}

bool reservations_taken_by_other(const Reservations *reservations, ReservationKind kind,
                                 TilePos tile, uint8_t self)
{
    uint8_t owner = reservations_owner(reservations, kind, tile);
    return owner != RESERVATION_NO_OWNER && owner != self;
}

void reservations_release_all(Reservations *reservations, uint8_t owner)
{
    /* Order-preserving removal keeps the table (and its hash) independent of
     * how entries are released. */
    int kept = 0;
    bool released = false;
    for (int i = 0; i < reservations->count; i++) {
        Reservation entry = reservations->entries[i];
        if (entry.owner == owner) {
            reservations->count_by_kind[entry.kind]--;
            released = true;
        } else {
            reservations->entries[kept++] = entry;
        }
    }
    reservations->count = kept;
    if (released) {
        reservations->releases++;
    }
}

uint64_t reservations_hash(const Reservations *reservations, uint64_t hash)
{
    for (int i = 0; i < reservations->count; i++) {
        const Reservation *entry = &reservations->entries[i];
        int32_t fields[4] = {entry->kind, entry->owner, entry->tile.x, entry->tile.y};
        hash = hash_fnv1a64(hash, fields, sizeof(fields));
    }
    return hash;
}
