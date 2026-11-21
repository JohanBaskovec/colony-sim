#include "core/river.h"

#include "test_framework.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* The column of the edge tile at `at` along a side. */
static void edge_tile(MapSide side, float at, int width, int height, int *x, int *y)
{
    int along_x = (int)lroundf(at * (float)(width - 1));
    int along_y = (int)lroundf(at * (float)(height - 1));
    switch (side) {
    case MAP_SIDE_NORTH: *x = along_x, *y = 0; break;
    case MAP_SIDE_SOUTH: *x = along_x, *y = height - 1; break;
    case MAP_SIDE_WEST: *x = 0, *y = along_y; break;
    default: *x = width - 1, *y = along_y; break;
    }
}

static void test_the_seed_chooses_the_edges(void)
{
    int opposite = 0, neighbour = 0;
    for (uint64_t seed = 1; seed <= 200; seed++) {
        RiverParams river, a, b;
        river_default_params(&river);
        river.width = 6;
        river_resolve(&river, seed, &a);
        river_resolve(&river, seed, &b);
        CHECK(memcmp(&a, &b, sizeof(a)) == 0); /* From the seed alone. */
        CHECK((unsigned)a.from < MAP_SIDE_COUNT);
        CHECK((unsigned)a.to < MAP_SIDE_COUNT);
        CHECK(a.from != a.to);
        CHECK(a.from_at >= 0.2f && a.from_at <= 0.8f);
        CHECK(a.to_at >= 0.2f && a.to_at <= 0.8f);
        CHECK_EQ_INT(a.width, 6);
        bool across = (a.from + 2) % MAP_SIDE_COUNT == a.to;
        opposite += across;
        neighbour += !across;
    }
    CHECK(opposite > neighbour); /* More often across than round a corner. */
    CHECK(neighbour > 20);
}

static void test_edges_given_are_kept(void)
{
    RiverParams river, out;
    river_default_params(&river);
    river.width = 40;
    river.from = MAP_SIDE_WEST;
    river.to = MAP_SIDE_SOUTH;
    river.from_at = 0.0f;
    river.to_at = 0.5f;
    river_resolve(&river, 9, &out);
    CHECK_EQ_INT(out.width, RIVER_MAX_WIDTH);
    CHECK_EQ_INT(out.from, MAP_SIDE_WEST);
    CHECK_EQ_INT(out.to, MAP_SIDE_SOUTH);
    CHECK(fabsf(out.from_at - 0.2f) < 1e-6f); /* Brought within the middle of the edge. */
    CHECK(fabsf(out.to_at - 0.5f) < 1e-6f);
    river.to = MAP_SIDE_WEST; /* Not out by the edge it comes in by. */
    river_resolve(&river, 9, &out);
    CHECK(out.to != MAP_SIDE_WEST);
}

/* The channel (columns nearer the middle than half its width) runs whole
 * from the point it comes in by to the one it leaves by. */
static bool channel_joins(const float *distance, int width, int height, float half, int x0, int y0,
                          int x1, int y1)
{
    size_t columns = (size_t)width * (size_t)height;
    uint8_t *seen = calloc(columns, 1);
    int *queue = malloc(columns * sizeof(*queue));
    if (seen == NULL || queue == NULL) {
        free(seen);
        free(queue);
        return false;
    }
    int head = 0, tail = 0;
    queue[tail++] = y0 * width + x0;
    seen[y0 * width + x0] = 1;
    bool reached = false;
    while (head < tail && !reached) {
        int c = queue[head++];
        int x = c % width, y = c / width;
        reached = x == x1 && y == y1;
        static const int DX[4] = {1, -1, 0, 0}, DY[4] = {0, 0, 1, -1};
        for (int d = 0; d < 4; d++) {
            int nx = x + DX[d], ny = y + DY[d];
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) {
                continue;
            }
            int n = ny * width + nx;
            if (!seen[n] && distance[n] < half) {
                seen[n] = 1;
                queue[tail++] = n;
            }
        }
    }
    free(seen);
    free(queue);
    return reached;
}

static void test_the_course_runs_from_edge_to_edge(void)
{
    const int width = 96, height = 80;
    float *distance = malloc((size_t)width * height * sizeof(*distance));
    REQUIRE(distance != NULL);
    for (uint64_t seed = 1; seed <= 24; seed++) {
        RiverParams river, course;
        river_default_params(&river);
        river.width = RIVER_STREAM_WIDTH + (int)(seed % 4) * 3;
        river_resolve(&river, seed, &course);
        REQUIRE(river_distances(&course, seed, width, height, distance));
        float half = (float)course.width * 0.5f;
        int x0, y0, x1, y1;
        edge_tile(course.from, course.from_at, width, height, &x0, &y0);
        edge_tile(course.to, course.to_at, width, height, &x1, &y1);
        CHECK(distance[y0 * width + x0] < 1.0f);
        CHECK(distance[y1 * width + x1] < 1.0f);
        CHECK(channel_joins(distance, width, height, half, x0, y0, x1, y1));
        /* Every column has its distance, and the channel is not everywhere:
         * at least its width along the shortest way across a corner. */
        size_t channel = 0, unknown = 0;
        for (int i = 0; i < width * height; i++) {
            unknown += !(distance[i] >= 0.0f && distance[i] < FLT_MAX);
            channel += distance[i] < half;
        }
        CHECK_EQ_UINT(unknown, 0);
        CHECK(channel < (size_t)(width * height) / 4);
        CHECK(channel >= (size_t)(course.width * 30));
    }
    free(distance);
}

static void test_no_river_no_course(void)
{
    RiverParams river;
    river_default_params(&river);
    float distance[16 * 16];
    distance[0] = 7.0f;
    CHECK(!river_distances(&river, 1, 16, 16, distance)); /* No width. */
    river.width = 3;                                      /* Edges not resolved. */
    CHECK(!river_distances(&river, 1, 16, 16, distance));
    CHECK(distance[0] == 7.0f);
}

int main(void)
{
    RUN_TEST(test_the_seed_chooses_the_edges);
    RUN_TEST(test_edges_given_are_kept);
    RUN_TEST(test_the_course_runs_from_edge_to_edge);
    RUN_TEST(test_no_river_no_course);
    return test_summary();
}
