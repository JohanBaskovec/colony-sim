/*
 * 2D camera math (pure, SDL-free): converts between world coordinates (in
 * tiles) and screen coordinates (in pixels).
 */
#ifndef COLONY_CORE_CAMERA_H
#define COLONY_CORE_CAMERA_H

#define CAMERA_MIN_ZOOM 4.0f /* Pixels per tile. */
#define CAMERA_MAX_ZOOM 96.0f

typedef struct Camera {
    float x; /* World position at the center of the viewport, in tiles. */
    float y;
    float zoom; /* Pixels per tile. */
    float viewport_width; /* In pixels. */
    float viewport_height;
} Camera;

/* Rectangle of tiles: x0 <= x < x1, y0 <= y < y1. */
typedef struct TileRect {
    int x0, y0, x1, y1;
} TileRect;

void camera_init(Camera *camera, float x, float y, float zoom);
void camera_set_viewport(Camera *camera, float width, float height);

void camera_world_to_screen(const Camera *camera, float world_x, float world_y, float *screen_x,
                            float *screen_y);
void camera_screen_to_world(const Camera *camera, float screen_x, float screen_y, float *world_x,
                            float *world_y);

/* Moves the camera by a distance expressed in screen pixels. */
void camera_pan_pixels(Camera *camera, float dx, float dy);

/* Multiplies the zoom, keeping the world point under (screen_x, screen_y) fixed. */
void camera_zoom_at(Camera *camera, float factor, float screen_x, float screen_y);

/* Keeps the camera center inside [0, max_x] x [0, max_y]. */
void camera_clamp(Camera *camera, float max_x, float max_y);

/* Tiles (at least partially) visible, clipped to a map_width x map_height map. */
TileRect camera_visible_tiles(const Camera *camera, int map_width, int map_height);

#endif /* COLONY_CORE_CAMERA_H */
