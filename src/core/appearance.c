#include "core/appearance.h"

ColonistAppearance appearance_roll(Rng *rng, int age)
{
    ColonistAppearance appearance;
    appearance.skin = (uint8_t)rng_range_u32(rng, APPEARANCE_SKIN_COUNT);
    appearance.body = (uint8_t)rng_range_u32(rng, APPEARANCE_BODY_COUNT);
    appearance.hair_style =
        rng_chance(rng, APPEARANCE_BALD_CHANCE)
            ? 0
            : (uint8_t)(1 + rng_range_u32(rng, APPEARANCE_HAIR_STYLE_COUNT - 1));
    appearance.beard = rng_chance(rng, APPEARANCE_BEARD_CHANCE)
                           ? (uint8_t)(1 + rng_range_u32(rng, APPEARANCE_BEARD_COUNT - 1))
                           : 0;
    float gray_chance = age >= APPEARANCE_GRAY_AGE      ? 0.7f
                        : age >= APPEARANCE_GRAYING_AGE ? 0.25f
                                                        : 0.0f;
    if (rng_chance(rng, gray_chance)) {
        appearance.hair_color = HAIR_COLOR_GRAY;
    } else {
        /* Darker hair is the most common. */
        static const uint8_t WEIGHTS[HAIR_COLOR_GRAY] = {
            [HAIR_COLOR_BLACK] = 30, [HAIR_COLOR_DARK_BROWN] = 30, [HAIR_COLOR_BROWN] = 20,
            [HAIR_COLOR_AUBURN] = 8, [HAIR_COLOR_BLOND] = 12,
        };
        uint32_t total = 0;
        for (int color = 0; color < HAIR_COLOR_GRAY; color++) {
            total += WEIGHTS[color];
        }
        uint32_t pick = rng_range_u32(rng, total);
        appearance.hair_color = HAIR_COLOR_BLACK;
        for (int color = 0; color < HAIR_COLOR_GRAY; color++) {
            if (pick < WEIGHTS[color]) {
                appearance.hair_color = (uint8_t)color;
                break;
            }
            pick -= WEIGHTS[color];
        }
    }
    return appearance;
}

bool appearance_is_valid(ColonistAppearance appearance)
{
    return appearance.skin < APPEARANCE_SKIN_COUNT && appearance.body < APPEARANCE_BODY_COUNT &&
           appearance.hair_style < APPEARANCE_HAIR_STYLE_COUNT &&
           appearance.hair_color < HAIR_COLOR_COUNT && appearance.beard < APPEARANCE_BEARD_COUNT;
}

void appearance_serialize(Serializer *s, ColonistAppearance *appearance)
{
    ser_u8_below(s, &appearance->skin, APPEARANCE_SKIN_COUNT);
    ser_u8_below(s, &appearance->body, APPEARANCE_BODY_COUNT);
    ser_u8_below(s, &appearance->hair_style, APPEARANCE_HAIR_STYLE_COUNT);
    ser_u8_below(s, &appearance->hair_color, HAIR_COLOR_COUNT);
    ser_u8_below(s, &appearance->beard, APPEARANCE_BEARD_COUNT);
}
