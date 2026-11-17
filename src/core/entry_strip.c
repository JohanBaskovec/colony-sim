#include "core/entry_strip.h"

#include <math.h>

/* How many entries of a width fit in a row. */
static int fitting(float width, float entry_width, float gap)
{
    int n = (int)floorf((width + gap) / (entry_width + gap));
    return n < 1 ? 1 : n;
}

EntryStrip entry_strip_fit(int count, float width, float entry_width, float gap, float min_scale)
{
    EntryStrip strip = {1.0f, 1, 0};
    if (count <= 0 || entry_width <= 0.0f) {
        return strip;
    }
    if (min_scale > 1.0f) {
        min_scale = 1.0f;
    }
    /* The scale at which they all fit on one row. */
    float scale = (width - gap * (float)(count - 1)) / (entry_width * (float)count);
    if (scale >= 1.0f) {
        scale = 1.0f;
    } else if (scale < min_scale) {
        scale = min_scale;
    }
    strip.scale = scale;
    strip.per_row = fitting(width, entry_width * scale, gap);
    if (strip.per_row > count) {
        strip.per_row = count;
    }
    strip.rows = (count + strip.per_row - 1) / strip.per_row;
    return strip;
}
