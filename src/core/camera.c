#include "core/camera.h"

#include "core/util.h"

#include <math.h>

void camera_init(Camera *camera, float x, float y, float zoom)
{
    camera->x = x;
    camera->y = y;
    camera->zoom = clampf(zoom, CAMERA_MIN_ZOOM, CAMERA_MAX_ZOOM);
    camera->viewport_width = 0.0f;
    camera->viewport_height = 0.0f;
    camera->level = 0;
}

void camera_set_viewport(Camera *camera, float width, float height)
{
    camera->viewport_width = width;
    camera->viewport_height = height;
}

void camera_world_to_screen(const Camera *camera, float world_x, float world_y, float *screen_x,
                            float *screen_y)
{
    *screen_x = (world_x - camera->x) * camera->zoom + camera->viewport_width * 0.5f;
    *screen_y = (world_y - camera->y) * camera->zoom + camera->viewport_height * 0.5f;
}

void camera_screen_to_world(const Camera *camera, float screen_x, float screen_y, float *world_x,
                            float *world_y)
{
    *world_x = (screen_x - camera->viewport_width * 0.5f) / camera->zoom + camera->x;
    *world_y = (screen_y - camera->viewport_height * 0.5f) / camera->zoom + camera->y;
}

void camera_pan_pixels(Camera *camera, float dx, float dy)
{
    camera->x += dx / camera->zoom;
    camera->y += dy / camera->zoom;
}

void camera_zoom_at(Camera *camera, float factor, float screen_x, float screen_y)
{
    float anchor_x, anchor_y;
    camera_screen_to_world(camera, screen_x, screen_y, &anchor_x, &anchor_y);
    camera->zoom = clampf(camera->zoom * factor, CAMERA_MIN_ZOOM, CAMERA_MAX_ZOOM);
    /* Move the center so that the anchor maps back to the same screen point. */
    camera->x = anchor_x - (screen_x - camera->viewport_width * 0.5f) / camera->zoom;
    camera->y = anchor_y - (screen_y - camera->viewport_height * 0.5f) / camera->zoom;
}

void camera_clamp(Camera *camera, float max_x, float max_y)
{
    camera->x = clampf(camera->x, 0.0f, max_x);
    camera->y = clampf(camera->y, 0.0f, max_y);
}

TileRect camera_visible_tiles(const Camera *camera, int map_width, int map_height)
{
    float left, top, right, bottom;
    camera_screen_to_world(camera, 0.0f, 0.0f, &left, &top);
    camera_screen_to_world(camera, camera->viewport_width, camera->viewport_height, &right,
                           &bottom);

    TileRect rect;
    rect.x0 = clampi((int)floorf(left), 0, map_width);
    rect.y0 = clampi((int)floorf(top), 0, map_height);
    rect.x1 = clampi((int)ceilf(right), 0, map_width);
    rect.y1 = clampi((int)ceilf(bottom), 0, map_height);
    return rect;
}
