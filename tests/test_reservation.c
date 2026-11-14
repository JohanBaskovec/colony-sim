#include "core/reservation.h"
#include "core/util.h"

#include "test_framework.h"

static const TilePos A = {3, 4};
static const TilePos B = {5, 6};

static void test_add_and_query(void)
{
    Reservations r;
    reservations_init(&r);
    CHECK_EQ_INT(reservations_owner(&r, RESERVATION_WORK, A), RESERVATION_NO_OWNER);
    CHECK(reservations_add(&r, RESERVATION_WORK, A, 1));
    CHECK_EQ_INT(reservations_owner(&r, RESERVATION_WORK, A), 1);
    CHECK(!reservations_add(&r, RESERVATION_WORK, A, 2)); /* Already taken. */
    CHECK(!reservations_add(&r, RESERVATION_WORK, A, 1));
    /* Kinds are independent: the same tile can hold a food reservation. */
    CHECK(reservations_add(&r, RESERVATION_FOOD, A, 2));
    CHECK(!reservations_add(&r, RESERVATION_FOOD, B, RESERVATION_NO_OWNER));
    CHECK(!reservations_add(&r, RESERVATION_KIND_COUNT, B, 1));
    CHECK_EQ_INT(r.count, 2);
    CHECK_EQ_INT(r.count_by_kind[RESERVATION_WORK], 1);
    CHECK_EQ_INT(r.count_by_kind[RESERVATION_FOOD], 1);

    CHECK(reservations_taken_by_other(&r, RESERVATION_WORK, A, 2));
    CHECK(!reservations_taken_by_other(&r, RESERVATION_WORK, A, 1));
    CHECK(!reservations_taken_by_other(&r, RESERVATION_WORK, B, 2));
}

static void test_release_all(void)
{
    Reservations r;
    reservations_init(&r);
    reservations_add(&r, RESERVATION_WORK, A, 1);
    reservations_add(&r, RESERVATION_FOOD, B, 2);
    reservations_add(&r, RESERVATION_FOOD, A, 1);

    reservations_release_all(&r, 3); /* Holds nothing: no change. */
    CHECK_EQ_INT(r.count, 3);
    CHECK_EQ_UINT(r.releases, 0);

    reservations_release_all(&r, 1);
    CHECK_EQ_INT(r.count, 1);
    CHECK_EQ_INT(r.count_by_kind[RESERVATION_WORK], 0);
    CHECK_EQ_INT(r.count_by_kind[RESERVATION_FOOD], 1);
    CHECK_EQ_INT(reservations_owner(&r, RESERVATION_FOOD, B), 2);
    CHECK_EQ_INT(reservations_owner(&r, RESERVATION_WORK, A), RESERVATION_NO_OWNER);
    CHECK_EQ_UINT(r.releases, 1);
    CHECK(reservations_add(&r, RESERVATION_WORK, A, 2)); /* Free again. */
}

static void test_release_kind(void)
{
    Reservations r;
    reservations_init(&r);
    reservations_add(&r, RESERVATION_HAUL_SOURCE, A, 1);
    reservations_add(&r, RESERVATION_STORAGE, B, 1);
    reservations_add(&r, RESERVATION_STORAGE, A, 2);

    reservations_release_kind(&r, RESERVATION_WORK, 1); /* Holds none of that kind. */
    CHECK_EQ_INT(r.count, 3);
    CHECK_EQ_UINT(r.releases, 0);

    reservations_release_kind(&r, RESERVATION_STORAGE, 1);
    CHECK_EQ_INT(r.count, 2);
    CHECK_EQ_INT(r.count_by_kind[RESERVATION_STORAGE], 1);
    CHECK_EQ_INT(reservations_owner(&r, RESERVATION_HAUL_SOURCE, A), 1); /* Kept. */
    CHECK_EQ_INT(reservations_owner(&r, RESERVATION_STORAGE, A), 2);     /* Other owner. */
    CHECK_EQ_INT(reservations_owner(&r, RESERVATION_STORAGE, B), RESERVATION_NO_OWNER);
    CHECK_EQ_UINT(r.releases, 1);
}

static void test_table_full(void)
{
    Reservations r;
    reservations_init(&r);
    for (int i = 0; i < RESERVATION_MAX; i++) {
        REQUIRE(reservations_add(&r, RESERVATION_WORK, (TilePos){(int16_t)i, 0}, 1));
    }
    CHECK(!reservations_add(&r, RESERVATION_WORK, (TilePos){0, 1}, 1));
    reservations_release_all(&r, 1);
    CHECK_EQ_INT(r.count, 0);
}

/* Many reservations made and released in a mixed order: every lookup still
 * agrees with a plain scan of the entries. */
static void test_lookups_survive_churn(void)
{
    Reservations r;
    reservations_init(&r);
    uint32_t state = 12345;
    for (int round = 0; round < 2000; round++) {
        state = state * 1664525u + 1013904223u;
        ReservationKind kind = (ReservationKind)((state >> 8) % RESERVATION_KIND_COUNT);
        TilePos tile = {(int16_t)((state >> 12) % 24), (int16_t)((state >> 20) % 24)};
        uint8_t owner = (uint8_t)(1 + (state >> 28) % 12);
        if (round % 7 == 3) {
            reservations_release_all(&r, owner);
        } else if (round % 11 == 5) {
            reservations_release_kind(&r, kind, owner);
        } else {
            reservations_add(&r, kind, tile, owner);
        }

        uint8_t expected = RESERVATION_NO_OWNER;
        int by_kind[RESERVATION_KIND_COUNT] = {0};
        for (int i = 0; i < r.count; i++) {
            by_kind[r.entries[i].kind]++;
            if (r.entries[i].kind == kind && r.entries[i].tile.x == tile.x &&
                r.entries[i].tile.y == tile.y) {
                expected = r.entries[i].owner;
            }
        }
        if (reservations_owner(&r, kind, tile) != expected ||
            by_kind[kind] != r.count_by_kind[kind]) {
            CHECK(false);
            return;
        }
        /* Every entry can be found again. */
        for (int i = 0; i < r.count; i++) {
            if (reservations_owner(&r, (ReservationKind)r.entries[i].kind, r.entries[i].tile) !=
                r.entries[i].owner) {
                CHECK(false);
                return;
            }
        }
    }
    CHECK(r.count > 0);
    CHECK(r.releases > 0);
}

static void test_hash_depends_on_content(void)
{
    Reservations a, b;
    reservations_init(&a);
    reservations_init(&b);
    CHECK_EQ_UINT(reservations_hash(&a, HASH_FNV1A64_INIT),
                  reservations_hash(&b, HASH_FNV1A64_INIT));
    reservations_add(&a, RESERVATION_WORK, A, 1);
    CHECK(reservations_hash(&a, HASH_FNV1A64_INIT) != reservations_hash(&b, HASH_FNV1A64_INIT));
    reservations_add(&b, RESERVATION_WORK, A, 2);
    CHECK(reservations_hash(&a, HASH_FNV1A64_INIT) != reservations_hash(&b, HASH_FNV1A64_INIT));
}

int main(void)
{
    RUN_TEST(test_add_and_query);
    RUN_TEST(test_release_all);
    RUN_TEST(test_release_kind);
    RUN_TEST(test_table_full);
    RUN_TEST(test_lookups_survive_churn);
    RUN_TEST(test_hash_depends_on_content);
    return test_summary();
}
