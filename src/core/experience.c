#include "core/experience.h"

#include "core/util.h"

#include <math.h>
#include <string.h>

void experiences_init(Experiences *experiences)
{
    memset(experiences, 0, sizeof(*experiences));
}

const Experience *experiences_recent(const Experiences *experiences, int index)
{
    if (index < 0 || index >= experiences->count) {
        return NULL;
    }
    int slot = (experiences->next - 1 - index + 2 * EXPERIENCE_MAX) % EXPERIENCE_MAX;
    return &experiences->list[slot];
}

static bool same_name(const char *a, const char *b)
{
    return strncmp(a, b != NULL ? b : "", EXPERIENCE_NAME_SIZE) == 0;
}

void experiences_add(Experiences *experiences, ExperienceType type, int species, int sex,
                     const char *name, uint32_t hour)
{
    if (type <= EXPERIENCE_NONE || (unsigned)type >= EXPERIENCE_TYPE_COUNT) {
        return;
    }
    for (int i = 0; i < experiences->count; i++) {
        Experience *old = (Experience *)experiences_recent(experiences, i);
        if (old->type == type && old->species == species && same_name(old->name, name) &&
            hour >= old->hour && hour - old->hour <= EXPERIENCE_REPEAT_HOURS) {
            old->hour = hour;
            old->sex = (uint8_t)sex;
            if (old->count < UINT8_MAX) {
                old->count++;
            }
            return;
        }
    }
    Experience *entry = &experiences->list[experiences->next];
    memset(entry, 0, sizeof(*entry));
    entry->type = (uint8_t)type;
    entry->species = (uint8_t)species;
    entry->sex = (uint8_t)sex;
    entry->count = 1;
    entry->hour = hour;
    if (name != NULL) {
        strncpy(entry->name, name, EXPERIENCE_NAME_SIZE - 1);
    }
    experiences->next = (uint8_t)((experiences->next + 1) % EXPERIENCE_MAX);
    if (experiences->count < EXPERIENCE_MAX) {
        experiences->count++;
    }
}

static double appeal(const Experience *experience, uint32_t hour)
{
    double age = hour > experience->hour ? (double)(hour - experience->hour) : 0.0;
    double weight = exp2(-age / EXPERIENCE_FRESH_HOURS);
    return weight > EXPERIENCE_OLD_WEIGHT ? weight : EXPERIENCE_OLD_WEIGHT;
}

const Experience *experiences_pick(const Experiences *experiences, uint32_t hour, Rng *rng)
{
    if (experiences->count == 0) {
        return NULL;
    }
    double total = 0.0;
    for (int i = 0; i < experiences->count; i++) {
        total += appeal(experiences_recent(experiences, i), hour);
    }
    double roll = (double)rng_next_float(rng) * total;
    for (int i = 0; i < experiences->count; i++) {
        const Experience *experience = experiences_recent(experiences, i);
        double weight = appeal(experience, hour);
        if (roll < weight) {
            return experience;
        }
        roll -= weight;
    }
    return experiences_recent(experiences, experiences->count - 1); /* Rounding. */
}

bool experience_is_feat(ExperienceType type)
{
    return type == EXPERIENCE_KILLED_ANIMAL;
}

uint64_t experiences_hash(const Experiences *experiences, uint64_t hash)
{
    for (int i = 0; i < experiences->count; i++) {
        const Experience *experience = experiences_recent(experiences, i);
        uint8_t bytes[4] = {experience->type, experience->species, experience->sex,
                            experience->count};
        hash = hash_fnv1a64(hash, bytes, sizeof(bytes));
        hash = hash_fnv1a64(hash, &experience->hour, sizeof(experience->hour));
        size_t length = 0;
        while (length < EXPERIENCE_NAME_SIZE && experience->name[length] != '\0') {
            length++;
        }
        hash = hash_fnv1a64(hash, experience->name, length);
    }
    return hash;
}

void experiences_serialize(Serializer *s, Experiences *experiences)
{
    for (int i = 0; i < EXPERIENCE_MAX; i++) {
        Experience *experience = &experiences->list[i];
        ser_u8_below(s, &experience->type, EXPERIENCE_TYPE_COUNT);
        ser_u8(s, &experience->species);
        ser_u8(s, &experience->sex);
        ser_u8(s, &experience->count);
        ser_u32(s, &experience->hour);
        ser_bytes(s, experience->name, EXPERIENCE_NAME_SIZE);
        experience->name[EXPERIENCE_NAME_SIZE - 1] = 0;
    }
    ser_u8_below(s, &experiences->next, EXPERIENCE_MAX);
    ser_u8_below(s, &experiences->count, EXPERIENCE_MAX + 1);
}
