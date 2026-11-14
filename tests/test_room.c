#include "core/room.h"

#include "test_framework.h"

/* Walls along the edges of a rectangle (corners included). */
static void wall_ring(Map *map, int x0, int y0, int x1, int y1)
{
    for (int x = x0; x <= x1; x++) {
        map_set_structure(map, x, y0, STRUCTURE_WALL);
        map_set_structure(map, x, y1, STRUCTURE_WALL);
    }
    for (int y = y0; y <= y1; y++) {
        map_set_structure(map, x0, y, STRUCTURE_WALL);
        map_set_structure(map, x1, y, STRUCTURE_WALL);
    }
}

static void test_open_map_is_outdoors(void)
{
    Map map;
    Rooms rooms;
    REQUIRE(map_init(&map, 32, 32));
    REQUIRE(rooms_init(&rooms, 32, 32));
    CHECK(rooms_at(&rooms, &map, 5, 5) == NULL); /* Not built yet. */
    rooms_update(&rooms, &map);
    CHECK(rooms_current(&rooms, &map));
    const Room *room = rooms_at(&rooms, &map, 5, 5);
    REQUIRE(room != NULL);
    CHECK(!room->closed);
    CHECK_EQ_INT(room->tiles, 32 * 32);
    CHECK(!room_is_roofed(room));
    CHECK(rooms_at(&rooms, &map, -1, 5) == NULL);
    CHECK(!room_is_roofed(NULL));
    rooms_free(&rooms);
    map_free(&map);
}

static void test_walled_hut(void)
{
    Map map;
    Rooms rooms;
    REQUIRE(map_init(&map, 32, 32));
    REQUIRE(rooms_init(&rooms, 32, 32));
    wall_ring(&map, 10, 10, 14, 14); /* 3 × 3 inside. */
    map_set_structure(&map, 12, 14, STRUCTURE_DOOR);
    map_set_structure(&map, 11, 11, STRUCTURE_GRASS_BED); /* Beds do not close rooms. */
    rooms_update(&rooms, &map);
    const Room *room = rooms_at(&rooms, &map, 12, 12);
    REQUIRE(room != NULL);
    CHECK(room->closed);
    CHECK_EQ_INT(room->tiles, 9);
    CHECK_EQ_INT(room->roofed, 0);
    CHECK(!room_is_roofed(room));
    CHECK(rooms_at(&rooms, &map, 10, 10) == NULL); /* A wall. */
    CHECK(rooms_at(&rooms, &map, 12, 14) == NULL); /* The door. */
    CHECK(!rooms_at(&rooms, &map, 2, 2)->closed);

    /* Roofed tile by tile: roofed once every tile is. */
    for (int y = 11; y <= 13; y++) {
        for (int x = 11; x <= 13; x++) {
            if (x != 13 || y != 13) {
                map_set_roof(&map, x, y, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
            }
        }
    }
    CHECK(!rooms_current(&rooms, &map)); /* The map changed: unknown until updated. */
    CHECK(rooms_at(&rooms, &map, 12, 12) == NULL);
    rooms_update(&rooms, &map);
    room = rooms_at(&rooms, &map, 12, 12);
    CHECK_EQ_INT(room->roofed, 8);
    CHECK(!room_is_roofed(room));
    map_set_roof(&map, 13, 13, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    rooms_update(&rooms, &map);
    CHECK(room_is_roofed(rooms_at(&rooms, &map, 12, 12)));

    /* An opening in the wall: the hut is outdoors now. */
    map_set_structure(&map, 10, 12, STRUCTURE_NONE);
    rooms_update(&rooms, &map);
    room = rooms_at(&rooms, &map, 12, 12);
    CHECK(!room->closed);
    CHECK(!room_is_roofed(room));
    rooms_free(&rooms);
    map_free(&map);
}

static void test_large_enclosure_is_outdoors(void)
{
    Map map;
    Rooms rooms;
    REQUIRE(map_init(&map, 48, 48));
    REQUIRE(rooms_init(&rooms, 48, 48));
    /* Exactly ROOM_MAX_TILES inside is still a room; one more row is not. */
    wall_ring(&map, 1, 1, 22, 22); /* 20 × 20 inside. */
    rooms_update(&rooms, &map);
    CHECK_EQ_INT(rooms_at(&rooms, &map, 5, 5)->tiles, ROOM_MAX_TILES);
    CHECK(rooms_at(&rooms, &map, 5, 5)->closed);
    wall_ring(&map, 25, 1, 46, 23); /* 20 × 21 inside. */
    rooms_update(&rooms, &map);
    CHECK(!rooms_at(&rooms, &map, 30, 5)->closed);
    /* Walls closing a small area against the edge of the map: outdoors. */
    for (int i = 0; i <= 3; i++) {
        map_set_structure(&map, i, 29, STRUCTURE_WALL);
        map_set_structure(&map, i, 33, STRUCTURE_WALL);
        map_set_structure(&map, 3, 29 + i, STRUCTURE_WALL);
    }
    rooms_update(&rooms, &map);
    const Room *corner = rooms_at(&rooms, &map, 1, 31);
    CHECK_EQ_INT(corner->tiles, 9);
    CHECK(!corner->closed);
    rooms_free(&rooms);
    map_free(&map);
}

static void test_natural_roof(void)
{
    Map map;
    Rooms rooms;
    REQUIRE(map_init(&map, 32, 32));
    REQUIRE(rooms_init(&rooms, 32, 32));
    /* Solid rock with a 3 × 3 cave and a 3-wide gallery leading out east. */
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            map.rock[map_index(&map, x, y)] = ROCK_GRANITE;
        }
    }
    for (int y = 10; y <= 12; y++) {
        for (int x = 10; x <= 12; x++) {
            map.rock[map_index(&map, x, y)] = ROCK_NONE;
        }
    }
    map_topology_changed(&map);
    CHECK(!map_natural_roof(&map, 9, 9)); /* Rock itself. */
    CHECK(!map_natural_roof(&map, -1, 0));
    for (int y = 10; y <= 12; y++) {
        for (int x = 10; x <= 12; x++) {
            CHECK(map_natural_roof(&map, x, y)); /* Corners too. */
            CHECK(map_is_roofed(&map, x, y));
        }
    }
    rooms_update(&rooms, &map);
    const Room *cave = rooms_at(&rooms, &map, 11, 11);
    REQUIRE(cave != NULL);
    CHECK(cave->closed);
    CHECK(room_is_roofed(cave));

    /* At the foot of a cliff, open sky. */
    for (int y = 20; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            map.rock[map_index(&map, x, y)] = ROCK_NONE;
        }
    }
    CHECK(!map_natural_roof(&map, 16, 20));
    CHECK(!map_natural_roof(&map, 16, 25));
    /* A built roof counts as well. */
    map_set_roof(&map, 16, 25, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK(map_is_roofed(&map, 16, 25));
    rooms_free(&rooms);
    map_free(&map);
}

int main(void)
{
    RUN_TEST(test_open_map_is_outdoors);
    RUN_TEST(test_walled_hut);
    RUN_TEST(test_large_enclosure_is_outdoors);
    RUN_TEST(test_natural_roof);
    return test_summary();
}
