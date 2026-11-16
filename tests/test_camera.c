#include "core/camera.h"

#include "test_framework.h"

static Camera make_camera(void)
{
    Camera camera;
    camera_init(&camera, 50.0f, 40.0f, 16.0f);
    camera_set_viewport(&camera, 800.0f, 600.0f);
    return camera;
}

static void test_center_of_viewport_is_camera_position(void)
{
    Camera camera = make_camera();
    CHECK_EQ_INT(camera.level, 0); /* The level shown. */
    float wx, wy;
    camera_screen_to_world(&camera, 400.0f, 300.0f, &wx, &wy);
    CHECK_NEAR(wx, 50.0, 1e-4);
    CHECK_NEAR(wy, 40.0, 1e-4);
}

static void test_world_screen_round_trip(void)
{
    Camera camera = make_camera();
    float sx, sy, wx, wy;
    camera_world_to_screen(&camera, 12.25f, 77.5f, &sx, &sy);
    camera_screen_to_world(&camera, sx, sy, &wx, &wy);
    CHECK_NEAR(wx, 12.25, 1e-3);
    CHECK_NEAR(wy, 77.5, 1e-3);
}

static void test_zoom_is_clamped(void)
{
    Camera camera = make_camera();
    camera_zoom_at(&camera, 1000.0f, 400.0f, 300.0f);
    CHECK_NEAR(camera.zoom, CAMERA_MAX_ZOOM, 1e-4);
    camera_zoom_at(&camera, 0.0001f, 400.0f, 300.0f);
    CHECK_NEAR(camera.zoom, CAMERA_MIN_ZOOM, 1e-4);

    Camera other;
    camera_init(&other, 0.0f, 0.0f, 1.0f);
    CHECK_NEAR(other.zoom, CAMERA_MIN_ZOOM, 1e-4);
}

static void test_zoom_keeps_point_under_cursor(void)
{
    Camera camera = make_camera();
    float before_x, before_y, after_x, after_y;
    camera_screen_to_world(&camera, 120.0f, 500.0f, &before_x, &before_y);
    camera_zoom_at(&camera, 2.0f, 120.0f, 500.0f);
    camera_screen_to_world(&camera, 120.0f, 500.0f, &after_x, &after_y);
    CHECK_NEAR(camera.zoom, 32.0, 1e-4);
    CHECK_NEAR(after_x, before_x, 1e-3);
    CHECK_NEAR(after_y, before_y, 1e-3);
}

static void test_pan_moves_by_screen_distance(void)
{
    Camera camera = make_camera();
    camera_pan_pixels(&camera, 32.0f, -16.0f);
    CHECK_NEAR(camera.x, 52.0, 1e-4);
    CHECK_NEAR(camera.y, 39.0, 1e-4);
}

static void test_clamp(void)
{
    Camera camera = make_camera();
    camera.x = -10.0f;
    camera.y = 500.0f;
    camera_clamp(&camera, 100.0f, 100.0f);
    CHECK_NEAR(camera.x, 0.0, 1e-6);
    CHECK_NEAR(camera.y, 100.0, 1e-6);
}

static void test_visible_tiles(void)
{
    Camera camera = make_camera();
    /* 800x600 px at 16 px/tile = 50x37.5 tiles centered on (50, 40). */
    TileRect rect = camera_visible_tiles(&camera, 200, 200);
    CHECK_EQ_INT(rect.x0, 25);
    CHECK_EQ_INT(rect.x1, 75);
    CHECK_EQ_INT(rect.y0, 21);
    CHECK_EQ_INT(rect.y1, 59);

    /* Clipped to the map. */
    rect = camera_visible_tiles(&camera, 60, 30);
    CHECK_EQ_INT(rect.x1, 60);
    CHECK_EQ_INT(rect.y1, 30);

    camera.x = -1000.0f;
    rect = camera_visible_tiles(&camera, 60, 30);
    CHECK_EQ_INT(rect.x0, 0);
    CHECK_EQ_INT(rect.x1, 0);
}

int main(void)
{
    RUN_TEST(test_center_of_viewport_is_camera_position);
    RUN_TEST(test_world_screen_round_trip);
    RUN_TEST(test_zoom_is_clamped);
    RUN_TEST(test_zoom_keeps_point_under_cursor);
    RUN_TEST(test_pan_moves_by_screen_distance);
    RUN_TEST(test_clamp);
    RUN_TEST(test_visible_tiles);
    return test_summary();
}
