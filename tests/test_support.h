/*
 * Helpers shared by tests and test tools (no test state, safe to include anywhere).
 */
#ifndef COLONY_TEST_SUPPORT_H
#define COLONY_TEST_SUPPORT_H

#include <stdio.h>
#include <stdlib.h>

/* Qualities are 0..100. The five grades the game once had, at their centers:
 * tests that need an item clearly worse or better than the standard (50). */
#define TEST_QUALITY_CRUDE 10
#define TEST_QUALITY_ROUGH 30
#define TEST_QUALITY_FINE 70
#define TEST_QUALITY_SUPERB 90

/* Reads a whole file into a NUL-terminated malloc'ed buffer. Returns NULL on error. */
static inline char *test_read_file(const char *path, size_t *length)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    char *data = NULL;
    size_t size = 0;
    if (fseek(file, 0, SEEK_END) == 0) {
        long end = ftell(file);
        if (end >= 0 && fseek(file, 0, SEEK_SET) == 0) {
            size = (size_t)end;
            data = malloc(size + 1);
            if (data != NULL && fread(data, 1, size, file) != size) {
                free(data);
                data = NULL;
            }
        }
    }
    fclose(file);
    if (data != NULL) {
        data[size] = '\0';
        if (length != NULL) {
            *length = size;
        }
    }
    return data;
}

#endif /* COLONY_TEST_SUPPORT_H */
