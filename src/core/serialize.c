#include "core/serialize.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

void ser_writer_init(Serializer *s)
{
    memset(s, 0, sizeof(*s));
}

void ser_reader_init(Serializer *s, const void *data, size_t size)
{
    memset(s, 0, sizeof(*s));
    s->reading = true;
    s->data = (uint8_t *)(uintptr_t)data; /* Only read. */
    s->size = data != NULL ? size : 0;
}

void ser_free(Serializer *s)
{
    if (!s->reading) {
        free(s->data);
    }
    memset(s, 0, sizeof(*s));
}

uint8_t *ser_release(Serializer *s, size_t *size)
{
    if (s->reading || s->failed) {
        *size = 0;
        return NULL;
    }
    uint8_t *data = s->data;
    *size = s->size;
    s->data = NULL;
    s->size = s->capacity = 0;
    return data;
}

bool ser_check(Serializer *s, bool ok)
{
    if (!ok) {
        s->failed = true;
    }
    return ok && !s->failed;
}

/* Writing: room for `size` more bytes. */
static bool reserve(Serializer *s, size_t size)
{
    if (s->failed) {
        return false;
    }
    if (size <= s->capacity - s->size) {
        return true;
    }
    size_t capacity = s->capacity > 0 ? s->capacity : 4096;
    while (capacity - s->size < size) {
        if (capacity > SIZE_MAX / 2) {
            s->failed = s->out_of_memory = true;
            return false;
        }
        capacity *= 2;
    }
    uint8_t *data = realloc(s->data, capacity);
    if (data == NULL) {
        s->failed = s->out_of_memory = true;
        return false;
    }
    s->data = data;
    s->capacity = capacity;
    return true;
}

/* `size` raw bytes, written from or read into `bytes` (zeroed when reading
 * fails). */
static void raw(Serializer *s, void *bytes, size_t size)
{
    if (s->reading) {
        if (s->failed || size > s->size - s->at) {
            s->failed = true;
            memset(bytes, 0, size);
            return;
        }
        memcpy(bytes, s->data + s->at, size);
        s->at += size;
    } else if (reserve(s, size)) {
        memcpy(s->data + s->size, bytes, size);
        s->size += size;
    }
}

/* An unsigned value of `bytes` bytes, little-endian on the file. */
static uint64_t unsigned_value(Serializer *s, uint64_t value, int bytes)
{
    uint8_t buffer[8];
    if (!s->reading) {
        for (int i = 0; i < bytes; i++) {
            buffer[i] = (uint8_t)(value >> (8 * i));
        }
    }
    raw(s, buffer, (size_t)bytes);
    if (!s->reading) {
        return value;
    }
    uint64_t read = 0;
    for (int i = 0; i < bytes; i++) {
        read |= (uint64_t)buffer[i] << (8 * i);
    }
    return read;
}

void ser_u8(Serializer *s, uint8_t *value)
{
    *value = (uint8_t)unsigned_value(s, *value, 1);
}

void ser_u16(Serializer *s, uint16_t *value)
{
    *value = (uint16_t)unsigned_value(s, *value, 2);
}

void ser_u32(Serializer *s, uint32_t *value)
{
    *value = (uint32_t)unsigned_value(s, *value, 4);
}

void ser_u64(Serializer *s, uint64_t *value)
{
    *value = unsigned_value(s, *value, 8);
}

void ser_i8(Serializer *s, int8_t *value)
{
    uint8_t bits = (uint8_t)*value;
    ser_u8(s, &bits);
    *value = (int8_t)bits;
}

void ser_i16(Serializer *s, int16_t *value)
{
    uint16_t bits = (uint16_t)*value;
    ser_u16(s, &bits);
    *value = (int16_t)bits;
}

void ser_i32(Serializer *s, int32_t *value)
{
    uint32_t bits = (uint32_t)*value;
    ser_u32(s, &bits);
    *value = (int32_t)bits;
}

void ser_i64(Serializer *s, int64_t *value)
{
    uint64_t bits = (uint64_t)*value;
    ser_u64(s, &bits);
    *value = (int64_t)bits;
}

void ser_int(Serializer *s, int *value)
{
    int32_t bits = (int32_t)*value;
    ser_i32(s, &bits);
    *value = (int)bits;
}

void ser_long(Serializer *s, long *value)
{
    int64_t bits = (int64_t)*value;
    ser_i64(s, &bits);
    if (s->reading && (bits < (int64_t)LONG_MIN || bits > (int64_t)LONG_MAX)) {
        s->failed = true;
        bits = 0;
    }
    *value = (long)bits;
}

void ser_size(Serializer *s, size_t *value)
{
    uint64_t bits = (uint64_t)*value;
    ser_u64(s, &bits);
    if (s->reading && bits > (uint64_t)SIZE_MAX) {
        s->failed = true;
        bits = 0;
    }
    *value = (size_t)bits;
}

void ser_float(Serializer *s, float *value)
{
    uint32_t bits;
    memcpy(&bits, value, sizeof(bits));
    ser_u32(s, &bits);
    memcpy(value, &bits, sizeof(bits));
}

void ser_double(Serializer *s, double *value)
{
    uint64_t bits;
    memcpy(&bits, value, sizeof(bits));
    ser_u64(s, &bits);
    memcpy(value, &bits, sizeof(bits));
}

void ser_bool(Serializer *s, bool *value)
{
    uint8_t byte = *value ? 1 : 0;
    ser_u8(s, &byte);
    if (byte > 1) {
        s->failed = true;
        byte = 0;
    }
    *value = byte != 0;
}

void ser_bytes(Serializer *s, void *data, size_t size)
{
    raw(s, data, size);
}

void ser_u8s(Serializer *s, uint8_t *values, size_t count)
{
    raw(s, values, count);
}

/* Little-endian machines (all the targets) copy arrays of plain integers as
 * they lie; others go value by value. */
static bool little_endian(void)
{
    const uint16_t probe = 1;
    uint8_t first;
    memcpy(&first, &probe, 1);
    return first == 1;
}

#define SER_ARRAY(name, type, one)                                                                 \
    void name(Serializer *s, type *values, size_t count)                                           \
    {                                                                                              \
        if (little_endian()) {                                                                     \
            if (count > SIZE_MAX / sizeof(type)) {                                                 \
                s->failed = true;                                                                  \
                return;                                                                            \
            }                                                                                      \
            raw(s, values, count * sizeof(type));                                                  \
            return;                                                                                \
        }                                                                                          \
        for (size_t i = 0; i < count; i++) {                                                       \
            one(s, &values[i]);                                                                    \
        }                                                                                          \
    }

SER_ARRAY(ser_u16s, uint16_t, ser_u16)
SER_ARRAY(ser_u32s, uint32_t, ser_u32)
SER_ARRAY(ser_u64s, uint64_t, ser_u64)
SER_ARRAY(ser_i16s, int16_t, ser_i16)

void ser_i8s(Serializer *s, int8_t *values, size_t count)
{
    raw(s, values, count);
}

/* Floats: IEEE bits, as the integers of the same size. */
void ser_floats(Serializer *s, float *values, size_t count)
{
    if (little_endian() && sizeof(float) == 4) {
        if (count > SIZE_MAX / 4) {
            s->failed = true;
            return;
        }
        raw(s, values, count * 4);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        ser_float(s, &values[i]);
    }
}

void ser_doubles(Serializer *s, double *values, size_t count)
{
    if (little_endian() && sizeof(double) == 8) {
        if (count > SIZE_MAX / 8) {
            s->failed = true;
            return;
        }
        raw(s, values, count * 8);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        ser_double(s, &values[i]);
    }
}

void ser_ints(Serializer *s, int *values, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ser_int(s, &values[i]);
    }
}

void ser_longs(Serializer *s, long *values, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ser_long(s, &values[i]);
    }
}

void ser_bools(Serializer *s, bool *values, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        ser_bool(s, &values[i]);
    }
}

void ser_varint(Serializer *s, uint64_t *value)
{
    if (!s->reading) {
        uint64_t rest = *value;
        do {
            uint8_t byte = (uint8_t)(rest & 0x7f);
            rest >>= 7;
            if (rest != 0) {
                byte |= 0x80;
            }
            raw(s, &byte, 1);
        } while (rest != 0);
        return;
    }
    uint64_t read = 0;
    for (int shift = 0;; shift += 7) {
        uint8_t byte = 0;
        raw(s, &byte, 1);
        if (s->failed || shift > 63) {
            s->failed = true;
            *value = 0;
            return;
        }
        read |= (uint64_t)(byte & 0x7f) << shift;
        if ((byte & 0x80) == 0) {
            break;
        }
    }
    *value = read;
}

/* The element `i` of a layer of `width`-byte values, as an integer. */
static uint64_t element(const void *values, size_t i, int width)
{
    const uint8_t *at = (const uint8_t *)values + i * (size_t)width;
    switch (width) {
    case 1: return *at;
    case 2: {
        uint16_t v;
        memcpy(&v, at, 2);
        return v;
    }
    case 4: {
        uint32_t v;
        memcpy(&v, at, 4);
        return v;
    }
    default: {
        uint64_t v;
        memcpy(&v, at, 8);
        return v;
    }
    }
}

static void set_element(void *values, size_t i, int width, uint64_t value)
{
    uint8_t *at = (uint8_t *)values + i * (size_t)width;
    switch (width) {
    case 1: *at = (uint8_t)value; break;
    case 2: {
        uint16_t v = (uint16_t)value;
        memcpy(at, &v, 2);
        break;
    }
    case 4: {
        uint32_t v = (uint32_t)value;
        memcpy(at, &v, 4);
        break;
    }
    default: memcpy(at, &value, 8); break;
    }
}

/* Runs shorter than this are written as literals. */
#define LAYER_RUN_MIN 3

/* A layer: tokens (varint), each `(length - 1) << 1 | repeat`, then one
 * value repeated `length` times, or `length` values. */
static void layer(Serializer *s, void *values, size_t count, int width)
{
    if (s->reading) {
        size_t done = 0;
        while (done < count && !s->failed) {
            uint64_t token = 0;
            ser_varint(s, &token);
            uint64_t length = (token >> 1) + 1;
            if (length > count - done) {
                s->failed = true;
                break;
            }
            if (token & 1) {
                uint64_t value = unsigned_value(s, 0, width);
                for (size_t i = 0; i < length; i++) {
                    set_element(values, done + i, width, value);
                }
            } else {
                for (size_t i = 0; i < length; i++) {
                    set_element(values, done + i, width, unsigned_value(s, 0, width));
                }
            }
            done += (size_t)length;
        }
        if (s->failed) {
            memset(values, 0, count * (size_t)width);
        }
        return;
    }
    size_t i = 0;
    while (i < count && !s->failed) {
        /* The run starting here. */
        uint64_t value = element(values, i, width);
        size_t run = 1;
        while (i + run < count && element(values, i + run, width) == value) {
            run++;
        }
        if (run >= LAYER_RUN_MIN) {
            uint64_t token = ((uint64_t)(run - 1) << 1) | 1;
            ser_varint(s, &token);
            (void)unsigned_value(s, value, width);
            i += run;
            continue;
        }
        /* Literals up to the next run long enough. */
        size_t end = i + run;
        while (end < count) {
            uint64_t next = element(values, end, width);
            size_t next_run = 1;
            while (end + next_run < count && next_run < LAYER_RUN_MIN &&
                   element(values, end + next_run, width) == next) {
                next_run++;
            }
            if (next_run >= LAYER_RUN_MIN) {
                break;
            }
            end += next_run;
        }
        uint64_t token = (uint64_t)(end - i - 1) << 1;
        ser_varint(s, &token);
        for (; i < end; i++) {
            (void)unsigned_value(s, element(values, i, width), width);
        }
    }
}

void ser_layer_u8(Serializer *s, uint8_t *values, size_t count)
{
    layer(s, values, count, 1);
}

void ser_layer_u16(Serializer *s, uint16_t *values, size_t count)
{
    layer(s, values, count, 2);
}

void ser_layer_u32(Serializer *s, uint32_t *values, size_t count)
{
    layer(s, values, count, 4);
}

void ser_layer_u64(Serializer *s, uint64_t *values, size_t count)
{
    layer(s, values, count, 8);
}

void ser_layer_float(Serializer *s, float *values, size_t count)
{
    layer(s, values, count, 4); /* IEEE bits. */
}

void ser_layer_u8_below(Serializer *s, uint8_t *values, size_t count, unsigned limit)
{
    layer(s, values, count, 1);
    if (!s->reading || s->failed) {
        return;
    }
    for (size_t i = 0; i < count; i++) {
        if (values[i] >= limit) {
            s->failed = true;
            memset(values, 0, count);
            return;
        }
    }
}

void ser_int_in(Serializer *s, int *value, int min, int max)
{
    ser_int(s, value);
    if (*value < min || *value > max) {
        s->failed = true;
        *value = min;
    }
}

void ser_u32_upto(Serializer *s, uint32_t *value, uint32_t max)
{
    ser_u32(s, value);
    if (*value > max) {
        s->failed = true;
        *value = 0;
    }
}

void ser_u8_below(Serializer *s, uint8_t *value, unsigned count)
{
    ser_u8(s, value);
    if (*value >= count) {
        s->failed = true;
        *value = 0;
    }
}

size_t ser_section_begin(Serializer *s, const char tag[4])
{
    uint8_t found[4];
    memcpy(found, tag, 4);
    raw(s, found, 4);
    if (s->reading && memcmp(found, tag, 4) != 0) {
        s->failed = true;
    }
    uint32_t length = 0;
    size_t mark = s->reading ? s->at : s->size;
    ser_u32(s, &length);
    if (s->reading) {
        if (length > s->size - s->at) {
            s->failed = true;
            return s->at;
        }
        return s->at + length; /* Where the section ends. */
    }
    return mark; /* Where its length goes. */
}

void ser_section_end(Serializer *s, size_t mark)
{
    if (s->failed) {
        return;
    }
    if (s->reading) {
        if (s->at != mark) {
            s->failed = true;
        }
        return;
    }
    size_t length = s->size - mark - 4;
    if (length > UINT32_MAX) {
        s->failed = true;
        return;
    }
    for (int i = 0; i < 4; i++) {
        s->data[mark + (size_t)i] = (uint8_t)(length >> (8 * i));
    }
}

bool ser_alloc(Serializer *s, void **array, size_t count, size_t size, size_t max)
{
    if (!ser_check(s, count <= max && (size == 0 || count <= SIZE_MAX / size))) {
        return false;
    }
    if (!s->reading) {
        return true;
    }
    if (count == 0) {
        free(*array);
        *array = NULL;
        return true;
    }
    void *grown = realloc(*array, count * size);
    if (grown == NULL) {
        s->failed = true;
        return false;
    }
    memset(grown, 0, count * size);
    *array = grown;
    return true;
}
