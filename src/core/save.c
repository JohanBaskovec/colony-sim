#include "core/save.h"

#include "core/serialize.h"
#include "core/util.h"

#include <stdlib.h>
#include <string.h>

static const char SAVE_MAGIC[4] = {'T', 'H', 'M', 'R'};

/* Header: magic, version, then the sections; a hash of everything before it
 * ends the save. */
#define SAVE_HEADER_SIZE 8
#define SAVE_FOOTER_SIZE 8

/* What the game holds apart from its modules, and the size of the map
 * (read first: the rest is allocated at that size). */
static void game_fields_serialize(Serializer *s, Game *game)
{
    ser_u64(s, &game->seed);
    ser_u64(s, &game->clock.tick);
    rng_serialize(s, &game->rng);
    rng_serialize(s, &game->combat_rng);
    rng_serialize(s, &game->blood_rng);
    rng_serialize(s, &game->wildlife_rng);
    ser_double(s, &game->leaf_climate_sum);
    ser_u32(s, &game->leaf_tree_count);
    ser_bool(s, &game->vision_on);
    ser_bool(s, &game->known_ready);
    ser_bool(s, &game->fog_on);
    ser_bool(s, &game->fog_started);
    tile_pos_serialize(s, &game->arrival);
    ser_bool(s, &game->has_arrival);
    ser_bool(s, &game->colony_lost);
    ser_u32(s, &game->strikes_resolved);
    ser_u32(s, &game->tree_falls_seen);
    ser_int_in(s, &game->passing_door_count, 0, OCCUPANCY_BEINGS);
    ser_u32s(s, game->passing_doors, OCCUPANCY_BEINGS);
    ser_longs(s, game->stock, ITEM_TYPE_COUNT);
    ser_u8s(s, game->knowledge_masters, KNOWLEDGE_COUNT);
    ser_bools(s, game->knowledge_known, KNOWLEDGE_COUNT);
    ser_bool(s, &game->knowledge_counted);
    ser_bools(s, game->teach, KNOWLEDGE_COUNT);
    ser_bool(s, &game->debug.instant_build);
    ser_bool(s, &game->debug.free_crafts);
    ser_bool(s, &game->debug.no_tools);
    ser_bool(s, &game->debug.instant_deconstruct);
    ser_bool(s, &game->debug.instant_fell_and_mine);
}

static void map_section(Serializer *s, Game *game)
{
    map_serialize(s, &game->map);
}

static void designations_section(Serializer *s, Game *game)
{
    designations_serialize(s, &game->designations);
}

static void dirt_section(Serializer *s, Game *game)
{
    dirt_serialize(s, &game->dirt);
}

static void gather_zones_section(Serializer *s, Game *game)
{
    gather_zones_serialize(s, &game->gather_zones);
}

static void stockpiles_section(Serializer *s, Game *game)
{
    stockpiles_serialize(s, &game->stockpiles);
}

static void workstations_section(Serializer *s, Game *game)
{
    workstations_serialize(s, &game->workstations);
}

static void constructions_section(Serializer *s, Game *game)
{
    constructions_serialize(s, &game->constructions);
}

static void beds_section(Serializer *s, Game *game)
{
    beds_serialize(s, &game->beds);
}

static void reservations_section(Serializer *s, Game *game)
{
    reservations_serialize(s, &game->reservations);
}

static void item_moves_section(Serializer *s, Game *game)
{
    item_moves_serialize(s, &game->item_moves);
}

static void items_section(Serializer *s, Game *game)
{
    items_serialize(s, &game->items);
}

static void crowns_section(Serializer *s, Game *game)
{
    crowns_serialize(s, &game->crowns);
}

static void corpses_section(Serializer *s, Game *game)
{
    corpses_serialize(s, &game->corpses);
}

static void temperatures_section(Serializer *s, Game *game)
{
    temperatures_serialize(s, &game->temperatures);
}

static void soil_section(Serializer *s, Game *game)
{
    soil_serialize(s, &game->soil);
}

static void water_section(Serializer *s, Game *game)
{
    water_serialize(s, &game->water);
}

static void light_section(Serializer *s, Game *game)
{
    tile_light_serialize(s, &game->light);
}

static void weather_section(Serializer *s, Game *game)
{
    weather_serialize(s, &game->weather);
    roof_watch_serialize(s, &game->roof_watch);
}

/* The settlers: their pool, then each live or dead slot below its high water. */
static void colonists_section(Serializer *s, Game *game)
{
    pool_serialize(s, &game->colonist_pool);
    for (uint32_t slot = 0; slot < game->colonist_pool.high_water && ser_ok(s); slot++) {
        colonist_serialize(s, &game->colonists[slot]);
    }
    social_serialize(s, &game->social);
}

static void animals_section(Serializer *s, Game *game)
{
    animals_serialize(s, &game->animals);
}

static void beings_section(Serializer *s, Game *game)
{
    occupancy_serialize(s, &game->occupancy);
    combat_queue_serialize(s, &game->combat);
    blood_serialize(s, &game->blood);
    notices_serialize(s, &game->notices);
}

/* What the settlers see, while it is followed (allocated first when reading). */
static void vision_section(Serializer *s, Game *game)
{
    bool on = game->vision_on;
    ser_bool(s, &on);
    if (s->reading && on && !game_set_vision(game, true)) {
        ser_check(s, false);
        return;
    }
    if (on) {
        vision_serialize(s, &game->vision);
    }
}

/* The known world, once made (allocated first when reading). */
static void known_section(Serializer *s, Game *game)
{
    bool ready = game->known_ready;
    ser_bool(s, &ready);
    if (s->reading && ready) {
        if (!ser_check(s, known_init(&game->known, &game->map))) {
            return;
        }
        game->known_ready = true;
    }
    if (ready) {
        known_serialize(s, &game->known);
    }
}

typedef struct SaveSection {
    char tag[4];
    void (*serialize)(Serializer *s, Game *game);
} SaveSection;

/* The sections after GAME, in their order on the file. */
static const SaveSection SECTIONS[] = {
    {{'M', 'A', 'P', ' '}, map_section},           {{'D', 'S', 'G', 'N'}, designations_section},
    {{'D', 'I', 'R', 'T'}, dirt_section},          {{'G', 'Z', 'O', 'N'}, gather_zones_section},
    {{'S', 'T', 'C', 'K'}, stockpiles_section},    {{'W', 'K', 'S', 'T'}, workstations_section},
    {{'C', 'N', 'S', 'T'}, constructions_section}, {{'B', 'E', 'D', 'S'}, beds_section},
    {{'R', 'S', 'R', 'V'}, reservations_section},  {{'M', 'O', 'V', 'E'}, item_moves_section},
    {{'I', 'T', 'E', 'M'}, items_section},         {{'C', 'R', 'W', 'N'}, crowns_section},
    {{'D', 'E', 'A', 'D'}, corpses_section},       {{'T', 'E', 'M', 'P'}, temperatures_section},
    {{'S', 'O', 'I', 'L'}, soil_section},          {{'W', 'A', 'T', 'R'}, water_section},
    {{'L', 'I', 'G', 'H'}, light_section},         {{'S', 'K', 'Y', ' '}, weather_section},
    {{'S', 'E', 'T', 'L'}, colonists_section},     {{'A', 'N', 'I', 'M'}, animals_section},
    {{'B', 'E', 'I', 'N'}, beings_section},        {{'V', 'I', 'S', 'N'}, vision_section},
    {{'K', 'N', 'O', 'W'}, known_section},
};

/* Writes or reads every section after the header. */
static void sections_serialize(Serializer *s, Game *game)
{
    for (size_t i = 0; i < COLONY_ARRAY_COUNT(SECTIONS) && ser_ok(s); i++) {
        size_t mark = ser_section_begin(s, SECTIONS[i].tag);
        if (ser_ok(s)) {
            SECTIONS[i].serialize(s, game);
        }
        ser_section_end(s, mark);
    }
}

/* The size of the map, written into the GAME section after its fields. */
static void map_size_serialize(Serializer *s, int *width, int *height, int *depth)
{
    ser_int_in(s, width, MAP_MIN_SIZE, MAP_MAX_SIZE);
    ser_int_in(s, height, MAP_MIN_SIZE, MAP_MAX_SIZE);
    ser_int_in(s, depth, 1, MAP_MAX_LEVELS);
}

uint8_t *game_save(const Game *game, size_t *size)
{
    Game *writing = (Game *)(uintptr_t)game; /* Only read: the serializer writes it out. */
    Serializer s;
    ser_writer_init(&s);
    uint8_t magic[4];
    memcpy(magic, SAVE_MAGIC, 4);
    ser_bytes(&s, magic, 4);
    uint32_t version = SAVE_VERSION;
    ser_u32(&s, &version);

    size_t mark = ser_section_begin(&s, "GAME");
    int width = game->map.width, height = game->map.height, depth = game->map.depth;
    map_size_serialize(&s, &width, &height, &depth);
    game_fields_serialize(&s, writing);
    ser_section_end(&s, mark);
    sections_serialize(&s, writing);

    if (ser_ok(&s)) {
        uint64_t hash = hash_fnv1a64(HASH_FNV1A64_INIT, s.data, s.size);
        ser_u64(&s, &hash);
    }
    uint8_t *data = ser_release(&s, size);
    ser_free(&s);
    return data;
}

int save_version_of(const void *data, size_t size)
{
    if (data == NULL || size < SAVE_HEADER_SIZE || memcmp(data, SAVE_MAGIC, 4) != 0) {
        return -1;
    }
    Serializer s;
    ser_reader_init(&s, (const uint8_t *)data + 4, 4);
    uint32_t version = 0;
    ser_u32(&s, &version);
    return version > (uint32_t)INT32_MAX ? -1 : (int)version;
}

SaveError game_load(Game *game, const void *data, size_t size)
{
    memset(game, 0, sizeof(*game));
    int version = save_version_of(data, size);
    if (version < 0) {
        return SAVE_ERROR_NOT_A_SAVE;
    }
    if (version != SAVE_VERSION) {
        return SAVE_ERROR_VERSION;
    }
    if (size < SAVE_HEADER_SIZE + SAVE_FOOTER_SIZE) {
        return SAVE_ERROR_DAMAGED;
    }
    size_t body = size - SAVE_FOOTER_SIZE;
    Serializer s;
    ser_reader_init(&s, (const uint8_t *)data + body, SAVE_FOOTER_SIZE);
    uint64_t hash = 0;
    ser_u64(&s, &hash);
    if (hash != hash_fnv1a64(HASH_FNV1A64_INIT, data, body)) {
        return SAVE_ERROR_DAMAGED;
    }

    ser_reader_init(&s, data, body);
    s.at = SAVE_HEADER_SIZE;
    size_t mark = ser_section_begin(&s, "GAME");
    int width = 0, height = 0, depth = 0;
    map_size_serialize(&s, &width, &height, &depth);
    game_fields_serialize(&s, game);
    ser_section_end(&s, mark);
    if (!ser_ok(&s)) {
        memset(game, 0, sizeof(*game));
        return SAVE_ERROR_DAMAGED;
    }
    /* What the fields read must not pass for allocated modules. */
    bool vision_on = game->vision_on, known_ready = game->known_ready, fog_on = game->fog_on;
    game->vision_on = game->known_ready = game->fog_on = false;
    if (!map_init(&game->map, width, height, depth)) {
        memset(game, 0, sizeof(*game));
        return SAVE_ERROR_MEMORY;
    }
    /* The map first: the other modules are allocated around it. */
    mark = ser_section_begin(&s, SECTIONS[0].tag);
    if (ser_ok(&s)) {
        SECTIONS[0].serialize(&s, game);
    }
    ser_section_end(&s, mark);
    if (!ser_ok(&s)) {
        game_shutdown(game);
        return SAVE_ERROR_DAMAGED;
    }
    uint64_t tick = game->clock.tick;
    if (!game_prepare_load(game)) {
        return SAVE_ERROR_MEMORY;
    }
    game->clock.tick = tick;
    for (size_t i = 1; i < COLONY_ARRAY_COUNT(SECTIONS) && ser_ok(&s); i++) {
        mark = ser_section_begin(&s, SECTIONS[i].tag);
        if (ser_ok(&s)) {
            SECTIONS[i].serialize(&s, game);
        }
        ser_section_end(&s, mark);
    }
    if (!ser_ok(&s) || s.at != body || game->vision_on != vision_on ||
        game->known_ready != known_ready || (fog_on && !(vision_on && known_ready))) {
        game_shutdown(game);
        return SAVE_ERROR_DAMAGED;
    }
    /* The pointers between modules, and the caches rebuilt. */
    game->fog_on = fog_on;
    if (fog_on) {
        game->items.mirror = &game->known.items;
        game->items.mirror_when = game->vision.seen_bits;
    }
    regions_update(&game->regions, &game->map);
    game->clock.speed = GAME_SPEED_PAUSED;
    return SAVE_OK;
}
