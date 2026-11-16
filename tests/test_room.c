#include "core/room.h"

#include "test_framework.h"

/* Walls along the edges of a rectangle (corners included). */
static void wall_ring(Map *map, int x0, int y0, int x1, int y1)
{
    for (int x = x0; x <= x1; x++) {
        map_set_structure(map, x, y0, 0, STRUCTURE_WALL);
        map_set_structure(map, x, y1, 0, STRUCTURE_WALL);
    }
    for (int y = y0; y <= y1; y++) {
        map_set_structure(map, x0, y, 0, STRUCTURE_WALL);
        map_set_structure(map, x1, y, 0, STRUCTURE_WALL);
    }
}

static void test_open_map_is_outdoors(void)
{
    Map map;
    Rooms rooms;
    REQUIRE(map_init(&map, 32, 32, 1));
    REQUIRE(rooms_init(&rooms, 32, 32, 1));
    CHECK(rooms_at(&rooms, &map, 5, 5, 0) == NULL); /* Not built yet. */
    rooms_update(&rooms, &map);
    CHECK(rooms_current(&rooms, &map));
    const Room *room = rooms_at(&rooms, &map, 5, 5, 0);
    REQUIRE(room != NULL);
    CHECK(!room->closed);
    CHECK_EQ_INT(room->tiles, 32 * 32);
    CHECK(!room_is_roofed(room));
    CHECK(rooms_at(&rooms, &map, -1, 5, 0) == NULL);
    CHECK(!room_is_roofed(NULL));
    rooms_free(&rooms);
    map_free(&map);
}

static void test_walled_hut(void)
{
    Map map;
    Rooms rooms;
    REQUIRE(map_init(&map, 32, 32, 2));
    REQUIRE(rooms_init(&rooms, 32, 32, 2));
    wall_ring(&map, 10, 10, 14, 14); /* 3 × 3 inside. */
    map_set_structure(&map, 12, 14, 0, STRUCTURE_DOOR);
    map_set_structure(&map, 11, 11, 0, STRUCTURE_GRASS_BED); /* Beds do not close rooms. */
    rooms_update(&rooms, &map);
    const Room *room = rooms_at(&rooms, &map, 12, 12, 0);
    REQUIRE(room != NULL);
    CHECK(room->closed);
    CHECK_EQ_INT(room->tiles, 9);
    CHECK_EQ_INT(room->roofed, 0);
    CHECK(!room_is_roofed(room));
    CHECK(rooms_at(&rooms, &map, 10, 10, 0) == NULL); /* A wall. */
    CHECK(rooms_at(&rooms, &map, 12, 14, 0) == NULL); /* The door. */
    CHECK(!rooms_at(&rooms, &map, 2, 2, 0)->closed);

    /* Roofed tile by tile (on the level above): roofed once every tile is. */
    for (int y = 11; y <= 13; y++) {
        for (int x = 11; x <= 13; x++) {
            if (x != 13 || y != 13) {
                map_set_roof(&map, x, y, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
            }
        }
    }
    CHECK(!rooms_current(&rooms, &map)); /* The map changed: unknown until updated. */
    CHECK(rooms_at(&rooms, &map, 12, 12, 0) == NULL);
    rooms_update(&rooms, &map);
    room = rooms_at(&rooms, &map, 12, 12, 0);
    CHECK_EQ_INT(room->roofed, 8);
    CHECK(!room_is_roofed(room));
    map_set_roof(&map, 13, 13, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    rooms_update(&rooms, &map);
    CHECK(room_is_roofed(rooms_at(&rooms, &map, 12, 12, 0)));

    /* An opening in the wall: the hut is outdoors now. */
    map_set_structure(&map, 10, 12, 0, STRUCTURE_NONE);
    rooms_update(&rooms, &map);
    room = rooms_at(&rooms, &map, 12, 12, 0);
    CHECK(!room->closed);
    CHECK(!room_is_roofed(room));
    rooms_free(&rooms);
    map_free(&map);
}

static void test_large_enclosure_is_outdoors(void)
{
    Map map;
    Rooms rooms;
    REQUIRE(map_init(&map, 48, 48, 1));
    REQUIRE(rooms_init(&rooms, 48, 48, 1));
    /* Exactly ROOM_MAX_TILES inside is still a room; one more row is not. */
    wall_ring(&map, 1, 1, 22, 22); /* 20 × 20 inside. */
    rooms_update(&rooms, &map);
    CHECK_EQ_INT(rooms_at(&rooms, &map, 5, 5, 0)->tiles, ROOM_MAX_TILES);
    CHECK(rooms_at(&rooms, &map, 5, 5, 0)->closed);
    wall_ring(&map, 25, 1, 46, 23); /* 20 × 21 inside. */
    rooms_update(&rooms, &map);
    CHECK(!rooms_at(&rooms, &map, 30, 5, 0)->closed);
    /* Walls closing a small area against the edge of the map: outdoors. */
    for (int i = 0; i <= 3; i++) {
        map_set_structure(&map, i, 29, 0, STRUCTURE_WALL);
        map_set_structure(&map, i, 33, 0, STRUCTURE_WALL);
        map_set_structure(&map, 3, 29 + i, 0, STRUCTURE_WALL);
    }
    rooms_update(&rooms, &map);
    const Room *corner = rooms_at(&rooms, &map, 1, 31, 0);
    CHECK_EQ_INT(corner->tiles, 9);
    CHECK(!corner->closed);
    rooms_free(&rooms);
    map_free(&map);
}

static void test_natural_roof(void)
{
    Map map;
    Rooms rooms;
    REQUIRE(map_init(&map, 32, 32, 3));
    REQUIRE(rooms_init(&rooms, 32, 32, 3));
    /* A hill: solid rock on level 0 under a floor on level 1, with a 3 × 3
     * cave dug into it. */
    for (int y = 0; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            map.rock[map_index(&map, x, y, 0)] = ROCK_GRANITE;
            map.terrain[map_index(&map, x, y, 1)] = TERRAIN_SOIL;
        }
    }
    for (int y = 10; y <= 12; y++) {
        for (int x = 10; x <= 12; x++) {
            map.rock[map_index(&map, x, y, 0)] = ROCK_NONE;
        }
    }
    map_topology_changed(&map);
    CHECK(!map_natural_roof(&map, 9, 9, 0)); /* Rock itself. */
    CHECK(!map_natural_roof(&map, -1, 0, 0));
    CHECK(!map_natural_roof(&map, 11, 11, 1)); /* The hilltop: open sky. */
    for (int y = 10; y <= 12; y++) {
        for (int x = 10; x <= 12; x++) {
            CHECK(map_natural_roof(&map, x, y, 0));
            CHECK(map_is_roofed(&map, x, y, 0));
        }
    }
    rooms_update(&rooms, &map);
    const Room *cave = rooms_at(&rooms, &map, 11, 11, 0);
    REQUIRE(cave != NULL);
    CHECK(cave->closed);
    CHECK(room_is_roofed(cave));

    /* At the foot of a cliff, open sky. */
    for (int y = 20; y < 32; y++) {
        for (int x = 0; x < 32; x++) {
            map.rock[map_index(&map, x, y, 0)] = ROCK_NONE;
            map.terrain[map_index(&map, x, y, 1)] = TERRAIN_OPEN;
        }
    }
    map_topology_changed(&map);
    CHECK(!map_natural_roof(&map, 16, 20, 0));
    CHECK(!map_natural_roof(&map, 16, 25, 0));
    /* Anything higher up covers too: rock two levels up. */
    map.rock[map_index(&map, 16, 25, 2)] = ROCK_SANDSTONE;
    CHECK(map_natural_roof(&map, 16, 25, 0));
    CHECK(map_natural_roof(&map, 16, 25, 1));
    /* A built roof counts as well, but is not natural. */
    CHECK(!map_is_roofed(&map, 17, 25, 0));
    map_set_roof(&map, 17, 25, 1, STRUCTURE_ROOF, ITEM_QUALITY_STANDARD);
    CHECK(map_is_roofed(&map, 17, 25, 0));
    CHECK(!map_natural_roof(&map, 17, 25, 0));
    CHECK_EQ_INT(map_roof_over(&map, 17, 25, 0), STRUCTURE_ROOF);
    CHECK_EQ_INT(map_roof_over(&map, 16, 25, 0), STRUCTURE_NONE); /* Rock. */
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
