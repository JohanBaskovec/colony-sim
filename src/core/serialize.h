/*
 * Serializer (docs/SAVE.md §4): writes a module's fields into a growing
 * block of memory, or reads them back from one, through the same calls, so
 * that writing and reading cannot drift apart. Fixed sizes whatever the
 * platform, little-endian, no padding: `int` as 32 bits, `long` and
 * `size_t` as 64, floats as their IEEE bits, bools as a byte. Reading
 * checks everything: past the end, a count past its bound, an enum past its
 * count, a bool other than 0 or 1 set a sticky error, after which every read
 * gives zeros and nothing more is written. No file I/O.
 */
#ifndef COLONY_CORE_SERIALIZE_H
#define COLONY_CORE_SERIALIZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct Serializer {
    bool reading;
    bool failed;        /* Sticky: out of memory, past the end or a value refused. */
    bool out_of_memory; /* Writing only: why it failed. */
    uint8_t *data;      /* Writing: owned, `capacity` bytes. Reading: not owned. */
    size_t size;        /* Writing: bytes written. Reading: bytes there are. */
    size_t capacity;
    size_t at; /* Reading: where the next read starts. */
} Serializer;

/* An empty writer (owns its buffer: ser_free()). */
void ser_writer_init(Serializer *s);
/* A reader over `size` bytes of `data` (not copied: kept alive meanwhile). */
void ser_reader_init(Serializer *s, const void *data, size_t size);
/* Frees a writer's buffer (nothing for a reader). */
void ser_free(Serializer *s);
/* Hands the writer's buffer over to the caller (who frees it with free()),
 * leaving the writer empty. NULL when it failed. */
uint8_t *ser_release(Serializer *s, size_t *size);

/* Marks the data refused (reading) whenever `ok` is false; returns `ok` and
 * not failed. */
bool ser_check(Serializer *s, bool ok);
/* Whether reading has failed. */
static inline bool ser_ok(const Serializer *s)
{
    return !s->failed;
}

void ser_u8(Serializer *s, uint8_t *value);
void ser_u16(Serializer *s, uint16_t *value);
void ser_u32(Serializer *s, uint32_t *value);
void ser_u64(Serializer *s, uint64_t *value);
void ser_i8(Serializer *s, int8_t *value);
void ser_i16(Serializer *s, int16_t *value);
void ser_i32(Serializer *s, int32_t *value);
void ser_i64(Serializer *s, int64_t *value);
void ser_int(Serializer *s, int *value);       /* 32 bits on the file. */
void ser_long(Serializer *s, long *value);     /* 64 bits on the file. */
void ser_size(Serializer *s, size_t *value);   /* 64 bits on the file. */
void ser_float(Serializer *s, float *value);   /* IEEE bits. */
void ser_double(Serializer *s, double *value); /* IEEE bits. */
void ser_bool(Serializer *s, bool *value);     /* One byte, 0 or 1. */
void ser_bytes(Serializer *s, void *data, size_t size);

/* Arrays of `count` values. */
void ser_u8s(Serializer *s, uint8_t *values, size_t count);
void ser_u16s(Serializer *s, uint16_t *values, size_t count);
void ser_u32s(Serializer *s, uint32_t *values, size_t count);
void ser_u64s(Serializer *s, uint64_t *values, size_t count);
void ser_i8s(Serializer *s, int8_t *values, size_t count);
void ser_i16s(Serializer *s, int16_t *values, size_t count);
void ser_ints(Serializer *s, int *values, size_t count);
void ser_longs(Serializer *s, long *values, size_t count);
void ser_floats(Serializer *s, float *values, size_t count);
void ser_doubles(Serializer *s, double *values, size_t count);
void ser_bools(Serializer *s, bool *values, size_t count);

/* An unsigned value in as few bytes as it needs (7 bits a byte). */
void ser_varint(Serializer *s, uint64_t *value);

/* Layers of the map (`count` values, mostly runs of the same value):
 * run-length encoded, a literal stretch where values change. Reading
 * refuses runs that do not add up to `count`. */
void ser_layer_u8(Serializer *s, uint8_t *values, size_t count);
void ser_layer_u16(Serializer *s, uint16_t *values, size_t count);
void ser_layer_u32(Serializer *s, uint32_t *values, size_t count);
void ser_layer_u64(Serializer *s, uint64_t *values, size_t count);
void ser_layer_float(Serializer *s, float *values, size_t count);
/* A layer of values that index tables: each must be below `limit`. */
void ser_layer_u8_below(Serializer *s, uint8_t *values, size_t count, unsigned limit);

/* An `int` that must lie in min..max (a count, an index): refused otherwise. */
void ser_int_in(Serializer *s, int *value, int min, int max);
/* A `uint32_t` that must be at most `max`. */
void ser_u32_upto(Serializer *s, uint32_t *value, uint32_t max);
/* A `uint8_t` that must be below `count` (a stored enum). */
void ser_u8_below(Serializer *s, uint8_t *value, unsigned count);

/* An enum field below `count`, stored as 32 bits: SER_ENUM(s, x->task, TASK_COUNT). */
#define SER_ENUM(s, field, count)                                                                  \
    do {                                                                                           \
        uint32_t ser_enum_value_ = (uint32_t)(field);                                              \
        ser_u32_upto((s), &ser_enum_value_, (uint32_t)(count) - 1u);                               \
        (field) = ser_enum_value_;                                                                 \
    } while (0)

/* Sections (docs/SAVE.md §2): a 4-character tag and the length of what
 * follows. ser_section_begin() returns a mark for ser_section_end(), which
 * writes the length (writing) or checks that the section was read to its
 * end (reading). A tag other than the one expected refuses the data. */
size_t ser_section_begin(Serializer *s, const char tag[4]);
void ser_section_end(Serializer *s, size_t mark);

/* Reading: makes room for `count` items of `size` bytes in `*array`
 * (realloc, contents zeroed), refusing counts past `max`. Writing: checks
 * only. Returns false when it failed. */
bool ser_alloc(Serializer *s, void **array, size_t count, size_t size, size_t max);

#endif
