#include "core/attributes.h"

#include "core/i18n.h"
#include "core/util.h"

#include <math.h>

typedef struct AttributeDef {
    const char *name_key;
    const char *description_key;
    const char *low_keys[ATTRIBUTE_BANDS]; /* From "a little" to "exceptionally". */
    const char *high_keys[ATTRIBUTE_BANDS];
} AttributeDef;

static const AttributeDef ATTRIBUTE_DEFS[ATTRIBUTE_COUNT] = {
    [ATTRIBUTE_WALKING] = {N_("attribute.walking"),
                           N_("attribute.walking.description"),
                           {N_("attribute.walking.low.1"), N_("attribute.walking.low.2"),
                            N_("attribute.walking.low.3"), N_("attribute.walking.low.4")},
                           {N_("attribute.walking.high.1"), N_("attribute.walking.high.2"),
                            N_("attribute.walking.high.3"), N_("attribute.walking.high.4")}},
    [ATTRIBUTE_WORK_PACE] = {N_("attribute.work_pace"),
                             N_("attribute.work_pace.description"),
                             {N_("attribute.work_pace.low.1"), N_("attribute.work_pace.low.2"),
                              N_("attribute.work_pace.low.3"), N_("attribute.work_pace.low.4")},
                             {N_("attribute.work_pace.high.1"), N_("attribute.work_pace.high.2"),
                              N_("attribute.work_pace.high.3"), N_("attribute.work_pace.high.4")}},
    [ATTRIBUTE_LEARNING] = {N_("attribute.learning"),
                            N_("attribute.learning.description"),
                            {N_("attribute.learning.low.1"), N_("attribute.learning.low.2"),
                             N_("attribute.learning.low.3"), N_("attribute.learning.low.4")},
                            {N_("attribute.learning.high.1"), N_("attribute.learning.high.2"),
                             N_("attribute.learning.high.3"), N_("attribute.learning.high.4")}},
    [ATTRIBUTE_SLEEP] = {N_("attribute.sleep"),
                         N_("attribute.sleep.description"),
                         {N_("attribute.sleep.low.1"), N_("attribute.sleep.low.2"),
                          N_("attribute.sleep.low.3"), N_("attribute.sleep.low.4")},
                         {N_("attribute.sleep.high.1"), N_("attribute.sleep.high.2"),
                          N_("attribute.sleep.high.3"), N_("attribute.sleep.high.4")}},
    [ATTRIBUTE_APPETITE] = {N_("attribute.appetite"),
                            N_("attribute.appetite.description"),
                            {N_("attribute.appetite.low.1"), N_("attribute.appetite.low.2"),
                             N_("attribute.appetite.low.3"), N_("attribute.appetite.low.4")},
                            {N_("attribute.appetite.high.1"), N_("attribute.appetite.high.2"),
                             N_("attribute.appetite.high.3"), N_("attribute.appetite.high.4")}},
    [ATTRIBUTE_COLD_TOLERANCE] =
        {N_("attribute.cold_tolerance"),
         N_("attribute.cold_tolerance.description"),
         {N_("attribute.cold_tolerance.low.1"), N_("attribute.cold_tolerance.low.2"),
          N_("attribute.cold_tolerance.low.3"), N_("attribute.cold_tolerance.low.4")},
         {N_("attribute.cold_tolerance.high.1"), N_("attribute.cold_tolerance.high.2"),
          N_("attribute.cold_tolerance.high.3"), N_("attribute.cold_tolerance.high.4")}},
    [ATTRIBUTE_HEAT_TOLERANCE] =
        {N_("attribute.heat_tolerance"),
         N_("attribute.heat_tolerance.description"),
         {N_("attribute.heat_tolerance.low.1"), N_("attribute.heat_tolerance.low.2"),
          N_("attribute.heat_tolerance.low.3"), N_("attribute.heat_tolerance.low.4")},
         {N_("attribute.heat_tolerance.high.1"), N_("attribute.heat_tolerance.high.2"),
          N_("attribute.heat_tolerance.high.3"), N_("attribute.heat_tolerance.high.4")}},
    [ATTRIBUTE_OUTLOOK] = {N_("attribute.outlook"),
                           N_("attribute.outlook.description"),
                           {N_("attribute.outlook.low.1"), N_("attribute.outlook.low.2"),
                            N_("attribute.outlook.low.3"), N_("attribute.outlook.low.4")},
                           {N_("attribute.outlook.high.1"), N_("attribute.outlook.high.2"),
                            N_("attribute.outlook.high.3"), N_("attribute.outlook.high.4")}},
    [ATTRIBUTE_TEMPERAMENT] =
        {N_("attribute.temperament"),
         N_("attribute.temperament.description"),
         {N_("attribute.temperament.low.1"), N_("attribute.temperament.low.2"),
          N_("attribute.temperament.low.3"), N_("attribute.temperament.low.4")},
         {N_("attribute.temperament.high.1"), N_("attribute.temperament.high.2"),
          N_("attribute.temperament.high.3"), N_("attribute.temperament.high.4")}},
};

static const int BAND_STARTS[ATTRIBUTE_BANDS] = ATTRIBUTE_BAND_STARTS;

/* The attributes that are a factor. */
static bool is_factor(AttributeType type)
{
    return type == ATTRIBUTE_WALKING || type == ATTRIBUTE_WORK_PACE || type == ATTRIBUTE_LEARNING ||
           type == ATTRIBUTE_SLEEP || type == ATTRIBUTE_APPETITE;
}

void attributes_init(Attributes *attributes)
{
    for (int type = 0; type < ATTRIBUTE_COUNT; type++) {
        attributes->position[type] = 0;
    }
}

void attributes_roll(Attributes *attributes, Rng *rng)
{
    for (int type = 0; type < ATTRIBUTE_COUNT; type++) {
        /* Twelve uniform draws of 0..1000: mean 6000, standard deviation
         * 1000 (a normal curve, near enough). */
        int sum = 0;
        for (int draw = 0; draw < 12; draw++) {
            sum += rng_range_i32(rng, 0, ATTRIBUTE_POSITION_MAX);
        }
        int position =
            (sum - 6 * ATTRIBUTE_POSITION_MAX) * ATTRIBUTE_ROLL_DEVIATION / ATTRIBUTE_POSITION_MAX;
        attributes->position[type] =
            (int16_t)clampi(position, -ATTRIBUTE_POSITION_MAX, ATTRIBUTE_POSITION_MAX);
    }
}

bool attributes_set(Attributes *attributes, AttributeType type, int position)
{
    if ((unsigned)type >= ATTRIBUTE_COUNT || position < -ATTRIBUTE_POSITION_MAX ||
        position > ATTRIBUTE_POSITION_MAX) {
        return false;
    }
    attributes->position[type] = (int16_t)position;
    return true;
}

float attribute_position(const Attributes *attributes, AttributeType type)
{
    if ((unsigned)type >= ATTRIBUTE_COUNT) {
        return 0.0f;
    }
    return (float)attributes->position[type] / (float)ATTRIBUTE_POSITION_MAX;
}

float attribute_factor(const Attributes *attributes, AttributeType type)
{
    if (!is_factor(type)) {
        return 1.0f;
    }
    /* 2^p for ATTRIBUTE_FACTOR_MAX 2; written for any maximum. */
    return exp2f(attribute_position(attributes, type) * log2f(ATTRIBUTE_FACTOR_MAX));
}

float attribute_thought_weight(const Attributes *attributes)
{
    float p = attribute_position(attributes, ATTRIBUTE_TEMPERAMENT);
    return p >= 0.0f ? exp2f(p * log2f(ATTRIBUTE_SENSITIVE_MAX))
                     : exp2f(-p * log2f(ATTRIBUTE_STOIC_MIN));
}

float attribute_outlook_points(const Attributes *attributes)
{
    return ATTRIBUTE_OUTLOOK_RANGE * attribute_position(attributes, ATTRIBUTE_OUTLOOK);
}

float attribute_cold_shift(const Attributes *attributes)
{
    return ATTRIBUTE_TOLERANCE_RANGE * attribute_position(attributes, ATTRIBUTE_COLD_TOLERANCE);
}

float attribute_heat_shift(const Attributes *attributes)
{
    return ATTRIBUTE_TOLERANCE_RANGE * attribute_position(attributes, ATTRIBUTE_HEAT_TOLERANCE);
}

int attribute_band(int position)
{
    int distance = position < 0 ? -position : position;
    int band = 0;
    for (int i = 0; i < ATTRIBUTE_BANDS; i++) {
        if (distance >= BAND_STARTS[i]) {
            band = i + 1;
        }
    }
    return band;
}

AttributeGroup attribute_group(AttributeType type)
{
    return type == ATTRIBUTE_OUTLOOK || type == ATTRIBUTE_TEMPERAMENT ? ATTRIBUTE_GROUP_MIND
                                                                      : ATTRIBUTE_GROUP_BODY;
}

const char *attribute_name_key(AttributeType type)
{
    return (unsigned)type < ATTRIBUTE_COUNT ? ATTRIBUTE_DEFS[type].name_key : NULL;
}

const char *attribute_description_key(AttributeType type)
{
    return (unsigned)type < ATTRIBUTE_COUNT ? ATTRIBUTE_DEFS[type].description_key : NULL;
}

const char *attribute_word_key(AttributeType type, int position)
{
    int band = attribute_band(position);
    if ((unsigned)type >= ATTRIBUTE_COUNT || band == 0) {
        return NULL;
    }
    const AttributeDef *def = &ATTRIBUTE_DEFS[type];
    return position < 0 ? def->low_keys[band - 1] : def->high_keys[band - 1];
}

uint64_t attributes_hash(const Attributes *attributes, uint64_t hash)
{
    return hash_fnv1a64(hash, attributes->position, sizeof(attributes->position));
}
