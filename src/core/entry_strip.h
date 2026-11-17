/*
 * Fitting a strip of same-sized entries (the colonist bar's portraits) into a
 * width: at full size on one row when they fit, else shrunk (not below a
 * minimum scale), then on as many rows as needed. Pure arithmetic, so that
 * the frontend's layout is tested here.
 */
#ifndef COLONY_CORE_ENTRY_STRIP_H
#define COLONY_CORE_ENTRY_STRIP_H

typedef struct EntryStrip {
    float scale; /* Of the entries, min_scale .. 1. */
    int per_row; /* Entries on a full row (at least 1). */
    int rows;    /* 0 when there is no entry. */
} EntryStrip;

/* `count` entries `entry_width` wide (at scale 1) with `gap` between them,
 * in `width`. */
EntryStrip entry_strip_fit(int count, float width, float entry_width, float gap, float min_scale);

#endif /* COLONY_CORE_ENTRY_STRIP_H */
