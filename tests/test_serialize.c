#include "core/serialize.h"
#include "test_framework.h"

#include <stdlib.h>
#include <string.h>

typedef enum Colour { COLOUR_RED, COLOUR_GREEN, COLOUR_BLUE, COLOUR_COUNT } Colour;

typedef struct Sample {
    uint8_t a;
    uint16_t b;
    uint32_t c;
    uint64_t d;
    int8_t e;
    int16_t f;
    int g;
    long h;
    size_t i;
    float j;
    double k;
    bool l;
    Colour colour;
    int list[5];
    float floats[3];
    uint16_t shorts[4];
    char name[8];
} Sample;

/* One function writes and reads: the pattern every module follows. */
static void sample_serialize(Serializer *s, Sample *x)
{
    ser_u8(s, &x->a);
    ser_u16(s, &x->b);
    ser_u32(s, &x->c);
    ser_u64(s, &x->d);
    ser_i8(s, &x->e);
    ser_i16(s, &x->f);
    ser_int(s, &x->g);
    ser_long(s, &x->h);
    ser_size(s, &x->i);
    ser_float(s, &x->j);
    ser_double(s, &x->k);
    ser_bool(s, &x->l);
    SER_ENUM(s, x->colour, COLOUR_COUNT);
    ser_ints(s, x->list, 5);
    ser_floats(s, x->floats, 3);
    ser_u16s(s, x->shorts, 4);
    ser_bytes(s, x->name, sizeof(x->name));
}

static Sample sample(void)
{
    Sample x = {
        .a = 200,
        .b = 0xBEEF,
        .c = 0xDEADBEEFu,
        .d = 0x0123456789ABCDEFull,
        .e = -5,
        .f = -30000,
        .g = -123456,
        .h = -9876543210L,
        .i = 4000000000u,
        .j = -1.5f,
        .k = 3.141592653589793,
        .l = true,
        .colour = COLOUR_BLUE,
        .list = {1, -2, 3, -4, 5},
        .floats = {0.25f, -0.0f, 1e30f},
        .shorts = {1, 2, 65535, 0},
        .name = "Thaw",
    };
    return x;
}

static void test_values_come_back_as_written(void)
{
    Serializer writer;
    ser_writer_init(&writer);
    Sample written = sample();
    sample_serialize(&writer, &written);
    REQUIRE(ser_ok(&writer));
    /* Fixed sizes: 1+2+4+8+1+2+4+8+8+4+8+1+4 + 5*4 + 3*4 + 4*2 + 8. */
    CHECK_EQ_UINT(writer.size, 103);
    Serializer reader;
    ser_reader_init(&reader, writer.data, writer.size);
    Sample read;
    memset(&read, 0, sizeof(read));
    sample_serialize(&reader, &read);
    CHECK(ser_ok(&reader));
    CHECK_EQ_UINT(reader.at, writer.size);
    CHECK_EQ_INT(read.a, written.a);
    CHECK_EQ_UINT(read.b, written.b);
    CHECK_EQ_UINT(read.c, written.c);
    CHECK_EQ_UINT(read.d, written.d);
    CHECK_EQ_INT(read.e, written.e);
    CHECK_EQ_INT(read.f, written.f);
    CHECK_EQ_INT(read.g, written.g);
    CHECK_EQ_INT(read.h, written.h);
    CHECK_EQ_UINT(read.i, written.i);
    CHECK(read.j == written.j);
    CHECK(read.k == written.k);
    CHECK(read.l);
    CHECK_EQ_INT(read.colour, COLOUR_BLUE);
    CHECK(memcmp(read.list, written.list, sizeof(read.list)) == 0);
    CHECK(memcmp(read.floats, written.floats, sizeof(read.floats)) == 0);
    CHECK(memcmp(read.shorts, written.shorts, sizeof(read.shorts)) == 0);
    CHECK_STR_EQ(read.name, "Thaw");
    ser_free(&writer);
}

static void test_little_endian_on_the_file(void)
{
    Serializer writer;
    ser_writer_init(&writer);
    uint32_t value = 0x11223344u;
    ser_u32(&writer, &value);
    REQUIRE(writer.size == 4);
    CHECK_EQ_UINT(writer.data[0], 0x44);
    CHECK_EQ_UINT(writer.data[3], 0x11);
    ser_free(&writer);
}

/* Reading past the end fails, gives zeros, and stays failed. */
static void test_truncated_data_is_refused(void)
{
    Serializer writer;
    ser_writer_init(&writer);
    Sample written = sample();
    sample_serialize(&writer, &written);
    for (size_t size = 0; size < writer.size; size += 7) {
        Serializer reader;
        ser_reader_init(&reader, writer.data, size);
        Sample read;
        memset(&read, 0, sizeof(read));
        sample_serialize(&reader, &read);
        CHECK(!ser_ok(&reader));
        CHECK_EQ_INT(read.name[0], 0);
    }
    ser_free(&writer);
}

static void test_values_out_of_range_are_refused(void)
{
    Serializer writer;
    ser_writer_init(&writer);
    uint32_t big = 7;
    uint8_t two = 2;
    int count = 50;
    ser_u32(&writer, &big);
    ser_u8(&writer, &two);
    ser_int(&writer, &count);

    Colour colour = COLOUR_RED;
    Serializer reader;
    ser_reader_init(&reader, writer.data, writer.size);
    SER_ENUM(&reader, colour, COLOUR_COUNT); /* 7 is past the colours. */
    CHECK(!ser_ok(&reader));
    CHECK_EQ_INT(colour, COLOUR_RED);

    bool flag = false;
    ser_reader_init(&reader, writer.data + 4, writer.size - 4);
    ser_bool(&reader, &flag); /* 2 is no bool. */
    CHECK(!ser_ok(&reader));

    int read_count = 0;
    ser_reader_init(&reader, writer.data + 5, writer.size - 5);
    ser_int_in(&reader, &read_count, 0, 10);
    CHECK(!ser_ok(&reader));
    CHECK_EQ_INT(read_count, 0);
    ser_free(&writer);
}

static void test_sections_check_their_tag_and_length(void)
{
    Serializer writer;
    ser_writer_init(&writer);
    size_t mark = ser_section_begin(&writer, "TEST");
    uint32_t values[3] = {1, 2, 3};
    ser_u32s(&writer, values, 3);
    ser_section_end(&writer, mark);
    REQUIRE(ser_ok(&writer));
    CHECK_EQ_UINT(writer.size, 4 + 4 + 12);
    CHECK_EQ_UINT(writer.data[4], 12); /* The length, little-endian. */

    Serializer reader;
    uint32_t read[3] = {0};
    ser_reader_init(&reader, writer.data, writer.size);
    mark = ser_section_begin(&reader, "TEST");
    ser_u32s(&reader, read, 3);
    ser_section_end(&reader, mark);
    CHECK(ser_ok(&reader));
    CHECK_EQ_UINT(read[2], 3);

    ser_reader_init(&reader, writer.data, writer.size); /* Another tag. */
    mark = ser_section_begin(&reader, "TSET");
    CHECK(!ser_ok(&reader));

    ser_reader_init(&reader, writer.data, writer.size); /* Not read to its end. */
    mark = ser_section_begin(&reader, "TEST");
    ser_u32s(&reader, read, 2);
    ser_section_end(&reader, mark);
    CHECK(!ser_ok(&reader));

    writer.data[4] = 200; /* A length past the end. */
    ser_reader_init(&reader, writer.data, writer.size);
    mark = ser_section_begin(&reader, "TEST");
    CHECK(!ser_ok(&reader));
    ser_free(&writer);
}

static void test_arrays_are_allocated_when_read(void)
{
    Serializer writer;
    ser_writer_init(&writer);
    uint32_t count = 4;
    uint16_t values[4] = {9, 8, 7, 6};
    ser_u32(&writer, &count);
    ser_u16s(&writer, values, count);

    Serializer reader;
    ser_reader_init(&reader, writer.data, writer.size);
    uint32_t read_count = 0;
    uint16_t *read = NULL;
    ser_u32(&reader, &read_count);
    REQUIRE(ser_alloc(&reader, (void **)&read, read_count, sizeof(*read), 100));
    ser_u16s(&reader, read, read_count);
    CHECK(ser_ok(&reader));
    CHECK_EQ_UINT(read[3], 6);
    free(read);
    read = NULL;

    ser_reader_init(&reader, writer.data, writer.size); /* More than allowed. */
    ser_u32(&reader, &read_count);
    CHECK(!ser_alloc(&reader, (void **)&read, read_count, sizeof(*read), 3));
    CHECK(read == NULL);
    CHECK(!ser_ok(&reader));
    ser_free(&writer);
}

static void test_the_buffer_is_handed_over(void)
{
    Serializer writer;
    ser_writer_init(&writer);
    uint64_t value = 42;
    for (int i = 0; i < 2000; i++) { /* Past the first 4 KiB. */
        ser_u64(&writer, &value);
    }
    size_t size = 0;
    uint8_t *data = ser_release(&writer, &size);
    REQUIRE(data != NULL);
    CHECK_EQ_UINT(size, 16000);
    CHECK(writer.data == NULL);
    CHECK_EQ_UINT(data[15992], 42);
    free(data);
    ser_free(&writer);
}

static void test_layers_are_packed_and_come_back(void)
{
    enum { COUNT = 5000 };
    uint16_t *layer = calloc(COUNT, sizeof(*layer));
    float *floats = calloc(COUNT, sizeof(*floats));
    REQUIRE(layer != NULL && floats != NULL);
    for (int i = 0; i < COUNT; i++) {
        layer[i] = i < 3000 ? 0 : (i < 3010 ? (uint16_t)(i * 7) : (i % 50 < 2 ? 9 : 1000));
        floats[i] = i % 3 == 0 ? 0.5f : (float)i;
    }
    layer[COUNT - 1] = 1; /* A last value on its own. */
    Serializer writer;
    ser_writer_init(&writer);
    ser_layer_u16(&writer, layer, COUNT);
    size_t packed = writer.size;
    ser_layer_float(&writer, floats, COUNT);
    REQUIRE(ser_ok(&writer));
    CHECK(packed < 1000); /* Instead of 10 000 bytes. */

    uint16_t *read = calloc(COUNT, sizeof(*read));
    float *read_floats = calloc(COUNT, sizeof(*read_floats));
    REQUIRE(read != NULL && read_floats != NULL);
    Serializer reader;
    ser_reader_init(&reader, writer.data, writer.size);
    ser_layer_u16(&reader, read, COUNT);
    ser_layer_float(&reader, read_floats, COUNT);
    CHECK(ser_ok(&reader));
    CHECK_EQ_UINT(reader.at, writer.size);
    CHECK(memcmp(read, layer, COUNT * sizeof(*read)) == 0);
    CHECK(memcmp(read_floats, floats, COUNT * sizeof(*floats)) == 0);

    /* Runs that would overflow the layer are refused. */
    ser_reader_init(&reader, writer.data, packed);
    ser_layer_u16(&reader, read, COUNT - 10);
    CHECK(!ser_ok(&reader));
    free(read);
    free(read_floats);
    free(layer);
    free(floats);
    ser_free(&writer);
}

static void test_varints(void)
{
    uint64_t values[] = {0, 1, 127, 128, 300, 1ull << 40, UINT64_MAX};
    Serializer writer;
    ser_writer_init(&writer);
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        ser_varint(&writer, &values[i]);
    }
    CHECK_EQ_UINT(writer.size, 1 + 1 + 1 + 2 + 2 + 6 + 10);
    Serializer reader;
    ser_reader_init(&reader, writer.data, writer.size);
    for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
        uint64_t read = 0;
        ser_varint(&reader, &read);
        CHECK_EQ_UINT(read, values[i]);
    }
    CHECK(ser_ok(&reader));
    ser_free(&writer);
}

int main(void)
{
    RUN_TEST(test_values_come_back_as_written);
    RUN_TEST(test_little_endian_on_the_file);
    RUN_TEST(test_truncated_data_is_refused);
    RUN_TEST(test_values_out_of_range_are_refused);
    RUN_TEST(test_sections_check_their_tag_and_length);
    RUN_TEST(test_arrays_are_allocated_when_read);
    RUN_TEST(test_the_buffer_is_handed_over);
    RUN_TEST(test_layers_are_packed_and_come_back);
    RUN_TEST(test_varints);
    return test_summary();
}
