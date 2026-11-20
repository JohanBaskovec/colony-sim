#include "core/new_game.h"

#include "core/i18n.h"
#include "core/util.h"

#include <math.h>
#include <string.h>

static const NewGameOptionDef OPTION_DEFS[NEW_GAME_OPTION_COUNT] = {
    [NEW_GAME_WIDTH] = {"width", N_("new_game.width"), N_("new_game.width.note"),
                        NEW_GAME_SECTION_WORLD, NEW_GAME_UNIT_TILES, 64, MAP_MAX_SIZE, 8},
    [NEW_GAME_HEIGHT] = {"height", N_("new_game.height"), N_("new_game.height.note"),
                         NEW_GAME_SECTION_WORLD, NEW_GAME_UNIT_TILES, 64, MAP_MAX_SIZE, 8},
    [NEW_GAME_FEATURE_SCALE] = {"feature_scale", N_("new_game.feature_scale"),
                                N_("new_game.feature_scale.note"), NEW_GAME_SECTION_WORLD,
                                NEW_GAME_UNIT_TILES, 16, 128, 8},
    [NEW_GAME_WATER] = {"water", N_("new_game.water"), N_("new_game.water.note"),
                        NEW_GAME_SECTION_RELIEF, NEW_GAME_UNIT_PERCENT, 0, 60, 1},
    [NEW_GAME_HILLS] = {"hills", N_("new_game.hills"), N_("new_game.hills.note"),
                        NEW_GAME_SECTION_RELIEF, NEW_GAME_UNIT_PERCENT, 30, 100, 1},
    [NEW_GAME_HILL_STEP] = {"hill_step", N_("new_game.hill_step"), N_("new_game.hill_step.note"),
                            NEW_GAME_SECTION_RELIEF, NEW_GAME_UNIT_PERCENT, 2, 20, 1},
    [NEW_GAME_ROCK] = {"rock", N_("new_game.rock"), N_("new_game.rock.note"),
                       NEW_GAME_SECTION_RELIEF, NEW_GAME_UNIT_PERCENT, 30, 100, 1},
    [NEW_GAME_UNDERGROUND] = {"underground", N_("new_game.underground"),
                              N_("new_game.underground.note"), NEW_GAME_SECTION_RELIEF,
                              NEW_GAME_UNIT_LEVELS, 2, 16, 1},
    [NEW_GAME_TREES] = {"trees", N_("new_game.trees"), N_("new_game.trees.note"),
                        NEW_GAME_SECTION_LIFE, NEW_GAME_UNIT_PERCENT, 0, 100, 5},
    [NEW_GAME_COLONISTS] = {"colonists", N_("new_game.colonists"), N_("new_game.colonists.note"),
                            NEW_GAME_SECTION_LIFE, NEW_GAME_UNIT_COUNT, 1, COLONIST_MAX, 1},
    [NEW_GAME_WILDLIFE] = {"wildlife", N_("new_game.wildlife"), N_("new_game.wildlife.note"),
                           NEW_GAME_SECTION_LIFE, NEW_GAME_UNIT_PERCENT, 0, 400, 25},
};

static const char *const SECTION_KEYS[NEW_GAME_SECTION_COUNT] = {
    [NEW_GAME_SECTION_WORLD] = N_("new_game.section.world"),
    [NEW_GAME_SECTION_RELIEF] = N_("new_game.section.relief"),
    [NEW_GAME_SECTION_LIFE] = N_("new_game.section.life"),
};

const NewGameOptionDef *new_game_option_def(NewGameOption option)
{
    return (unsigned)option < NEW_GAME_OPTION_COUNT ? &OPTION_DEFS[option] : NULL;
}

const char *new_game_section_key(NewGameSection section)
{
    return (unsigned)section < NEW_GAME_SECTION_COUNT ? SECTION_KEYS[section] : NULL;
}

void new_game_default_params(NewGameParams *params)
{
    game_default_params(params);
    params->wildlife = GAME_WILDLIFE_DENSITY;
}

/* An elevation (0 .. 1) in percent, and back. */
static int percent_of(float share)
{
    return (int)lroundf(share * 100.0f);
}

static float share_of(int percent)
{
    return (float)percent / 100.0f;
}

int new_game_option_value(const NewGameParams *params, NewGameOption option)
{
    const WorldGenParams *world = &params->world;
    switch (option) {
    case NEW_GAME_WIDTH: return world->width;
    case NEW_GAME_HEIGHT: return world->height;
    case NEW_GAME_FEATURE_SCALE: return (int)lroundf(world->feature_scale);
    case NEW_GAME_WATER: return percent_of(world->water_level);
    case NEW_GAME_HILLS: return percent_of(world->hill_level);
    case NEW_GAME_HILL_STEP: return percent_of(world->level_step);
    case NEW_GAME_ROCK: return percent_of(world->mountain_level);
    case NEW_GAME_UNDERGROUND: return world->underground_levels;
    case NEW_GAME_TREES: return percent_of(world->tree_density);
    case NEW_GAME_COLONISTS: return params->colonist_count;
    case NEW_GAME_WILDLIFE: return (int)lroundf(params->wildlife / GAME_WILDLIFE_DENSITY * 100.0f);
    case NEW_GAME_OPTION_COUNT: break;
    }
    return 0;
}

void new_game_option_set(NewGameParams *params, NewGameOption option, int value)
{
    const NewGameOptionDef *def = new_game_option_def(option);
    if (def == NULL) {
        return;
    }
    value = clampi(value, def->min, def->max);
    value = def->min + (value - def->min + def->step / 2) / def->step * def->step;
    value = clampi(value, def->min, def->max);
    WorldGenParams *world = &params->world;
    switch (option) {
    case NEW_GAME_WIDTH: world->width = value; break;
    case NEW_GAME_HEIGHT: world->height = value; break;
    case NEW_GAME_FEATURE_SCALE: world->feature_scale = (float)value; break;
    case NEW_GAME_WATER: world->water_level = share_of(value); break;
    case NEW_GAME_HILLS:
        world->hill_level = share_of(value);
        if (percent_of(world->mountain_level) < value) {
            world->mountain_level = world->hill_level; /* The rock moves up with them. */
        }
        break;
    case NEW_GAME_HILL_STEP: world->level_step = share_of(value); break;
    case NEW_GAME_ROCK:
        world->mountain_level = share_of(value);
        if (percent_of(world->hill_level) > value) {
            world->hill_level = world->mountain_level; /* The hills move down with it. */
        }
        break;
    case NEW_GAME_UNDERGROUND: world->underground_levels = value; break;
    case NEW_GAME_TREES: world->tree_density = share_of(value); break;
    case NEW_GAME_COLONISTS: params->colonist_count = value; break;
    case NEW_GAME_WILDLIFE: params->wildlife = GAME_WILDLIFE_DENSITY * share_of(value); break;
    case NEW_GAME_OPTION_COUNT: break;
    }
}

bool new_game_option_applies(const NewGameParams *params, NewGameOption option)
{
    switch (option) {
    case NEW_GAME_HILLS:
    case NEW_GAME_HILL_STEP:
    case NEW_GAME_UNDERGROUND: return !params->world.flat;
    default: return (unsigned)option < NEW_GAME_OPTION_COUNT;
    }
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

bool new_game_seed_from_text(const char *text, uint64_t *seed)
{
    if (text == NULL) {
        return false;
    }
    size_t begin = 0, end = strlen(text);
    while (begin < end && is_space(text[begin])) {
        begin++;
    }
    while (end > begin && is_space(text[end - 1])) {
        end--;
    }
    if (begin == end) {
        return false;
    }
    /* A number, if it is one and fits. */
    uint64_t number = 0;
    bool digits = true;
    for (size_t i = begin; i < end && digits; i++) {
        char c = text[i];
        uint64_t digit = (uint64_t)(c - '0');
        if (c < '0' || c > '9' || number > (UINT64_MAX - digit) / 10u) {
            digits = false;
        } else {
            number = number * 10u + digit;
        }
    }
    if (digits) {
        *seed = number;
        return true;
    }
    /* Any other text: FNV-1a over its bytes. */
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = begin; i < end; i++) {
        hash ^= (uint64_t)(unsigned char)text[i];
        hash *= 0x100000001b3ULL;
    }
    *seed = hash;
    return true;
}
