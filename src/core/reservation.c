#include "core/reservation.h"

#include "core/util.h"

#include <string.h>

void reservations_init(Reservations *reservations)
{
    memset(reservations, 0, sizeof(*reservations));
}

static uint32_t slot_of(ReservationKind kind, TilePos tile)
{
    uint32_t hash = (uint32_t)(uint16_t)tile.x * 73856093u ^
                    (uint32_t)(uint16_t)tile.y * 19349663u ^
                    (uint32_t)(uint16_t)tile.z * 25165843u ^ ((uint32_t)kind + 1u) * 83492791u;
    hash ^= hash >> 15;
    return hash & (RESERVATION_INDEX_SIZE - 1);
}

static int find(const Reservations *reservations, ReservationKind kind, TilePos tile)
{
    if ((unsigned)kind >= RESERVATION_KIND_COUNT || reservations->count_by_kind[kind] == 0) {
        return -1;
    }
    for (uint32_t slot = slot_of(kind, tile); reservations->index[slot] != 0;
         slot = (slot + 1) & (RESERVATION_INDEX_SIZE - 1)) {
        int i = reservations->index[slot] - 1;
        const Reservation *entry = &reservations->entries[i];
        if (entry->kind == kind && entry->tile.x == tile.x && entry->tile.y == tile.y &&
            entry->tile.z == tile.z) {
            return i;
        }
    }
    return -1;
}

static void index_insert(Reservations *reservations, int i)
{
    const Reservation *entry = &reservations->entries[i];
    uint32_t slot = slot_of((ReservationKind)entry->kind, entry->tile);
    while (reservations->index[slot] != 0) {
        slot = (slot + 1) & (RESERVATION_INDEX_SIZE - 1);
    }
    reservations->index[slot] = (uint16_t)(i + 1);
}

bool reservations_add(Reservations *reservations, ReservationKind kind, TilePos tile,
                      uint8_t owner)
{
    if (owner == RESERVATION_NO_OWNER || (unsigned)kind >= RESERVATION_KIND_COUNT ||
        reservations->count >= RESERVATION_MAX || find(reservations, kind, tile) >= 0) {
        return false;
    }
    reservations->entries[reservations->count] = (Reservation){(uint8_t)kind, owner, tile};
    index_insert(reservations, reservations->count);
    reservations->count++;
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

/* Releases what `owner` holds, of one kind or (kind < 0) of every kind.
 * Order-preserving removal keeps the table (and its hash) independent of how
 * entries are released. */
static void release(Reservations *reservations, uint8_t owner, int kind)
{
    int kept = 0;
    bool released = false;
    bool kinds[RESERVATION_KIND_COUNT] = {false};
    for (int i = 0; i < reservations->count; i++) {
        Reservation entry = reservations->entries[i];
        if (entry.owner == owner && (kind < 0 || entry.kind == kind)) {
            reservations->count_by_kind[entry.kind]--;
            kinds[entry.kind] = true;
            released = true;
        } else {
            reservations->entries[kept++] = entry;
        }
    }
    reservations->count = kept;
    if (released) {
        reservations->releases++;
        for (int k = 0; k < RESERVATION_KIND_COUNT; k++) {
            reservations->releases_by_kind[k] += kinds[k];
        }
        /* Entries moved: rebuild the lookup table (releases are rare next to lookups). */
        memset(reservations->index, 0, sizeof(reservations->index));
        for (int i = 0; i < kept; i++) {
            index_insert(reservations, i);
        }
    }
}

void reservations_release_all(Reservations *reservations, uint8_t owner)
{
    release(reservations, owner, -1);
}

void reservations_release_kind(Reservations *reservations, ReservationKind kind, uint8_t owner)
{
    release(reservations, owner, (int)kind);
}

uint64_t reservations_hash(const Reservations *reservations, uint64_t hash)
{
    for (int i = 0; i < reservations->count; i++) {
        const Reservation *entry = &reservations->entries[i];
        int32_t fields[4] = {entry->kind, entry->owner, entry->tile.x, entry->tile.y};
        hash = hash_fnv1a64(hash, fields, sizeof(fields));
        if (entry->tile.z != 0) { /* Hashes of one-level games stay as they were. */
            hash = hash_fnv1a64(hash, &entry->tile.z, sizeof(entry->tile.z));
        }
    }
    return hash;
}
