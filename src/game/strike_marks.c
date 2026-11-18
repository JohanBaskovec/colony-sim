#include "game/strike_marks.h"

/* Points along a stroke: the more, the smoother its taper. */
#define STROKE_STEPS 12
#define ARC_STEPS 16
#define JAW_TEETH 6
#define BURST_RAYS 8

static const SDL_FColor WHITE_INK = {1.0f, 1.0f, 1.0f, 1.0f};
/* Under the white, so that it reads on pale coats and snow. */
static const SDL_FColor SHADOW_INK = {0.16f, 0.12f, 0.12f, 0.55f};
static const SDL_FColor BLOOD_INK = {0.70f, 0.09f, 0.09f, 1.0f};

static SDL_FColor faded(SDL_FColor color, float opacity)
{
    color.a *= opacity;
    return color;
}

/* A band between two rows of points (left[i], right[i]), as quads. */
static void band(SDL_Renderer *renderer, const SDL_FPoint *left, const SDL_FPoint *right, int count,
                 SDL_FColor color)
{
    enum { MAX = ARC_STEPS + 1 };
    SDL_Vertex vertices[MAX * 2];
    int indices[(MAX - 1) * 6];
    count = SDL_min(count, MAX);
    for (int i = 0; i < count; i++) {
        vertices[i * 2] = (SDL_Vertex){left[i], color, {0.0f, 0.0f}};
        vertices[i * 2 + 1] = (SDL_Vertex){right[i], color, {0.0f, 0.0f}};
    }
    for (int i = 0; i + 1 < count; i++) {
        int quad[6] = {i * 2, i * 2 + 2, i * 2 + 3, i * 2 + 3, i * 2 + 1, i * 2};
        SDL_memcpy(&indices[i * 6], quad, sizeof(quad));
    }
    SDL_RenderGeometry(renderer, NULL, vertices, count * 2, indices, (count - 1) * 6);
}

static void triangle(SDL_Renderer *renderer, SDL_FPoint a, SDL_FPoint b, SDL_FPoint c,
                     SDL_FColor color)
{
    SDL_Vertex vertices[3] = {
        {a, color, {0.0f, 0.0f}}, {b, color, {0.0f, 0.0f}}, {c, color, {0.0f, 0.0f}}};
    SDL_RenderGeometry(renderer, NULL, vertices, 3, NULL, 0);
}

static void disc(SDL_Renderer *renderer, float cx, float cy, float radius, SDL_FColor color)
{
    enum { SEGMENTS = 10 };
    for (int i = 0; i < SEGMENTS; i++) {
        float a0 = (float)i * 2.0f * SDL_PI_F / SEGMENTS;
        float a1 = (float)(i + 1) * 2.0f * SDL_PI_F / SEGMENTS;
        triangle(renderer, (SDL_FPoint){cx, cy},
                 (SDL_FPoint){cx + radius * SDL_cosf(a0), cy + radius * SDL_sinf(a0)},
                 (SDL_FPoint){cx + radius * SDL_cosf(a1), cy + radius * SDL_sinf(a1)}, color);
    }
}

/* A stroke from a to b, tapered at both ends, `width` at its widest; drawn
 * up to `share` of its length (it grows from a). */
static void stroke(SDL_Renderer *renderer, SDL_FPoint a, SDL_FPoint b, float width, float share,
                   SDL_FColor color)
{
    if (share <= 0.0f) {
        return;
    }
    float dx = b.x - a.x, dy = b.y - a.y;
    float length = SDL_sqrtf(dx * dx + dy * dy);
    if (length <= 0.0f) {
        return;
    }
    float nx = -dy / length, ny = dx / length;
    SDL_FPoint left[STROKE_STEPS + 1], right[STROKE_STEPS + 1];
    for (int i = 0; i <= STROKE_STEPS; i++) {
        float u = share * (float)i / STROKE_STEPS;
        float half = 0.5f * width * SDL_powf(SDL_sinf(SDL_PI_F * u), 0.8f);
        float x = a.x + dx * u, y = a.y + dy * u;
        left[i] = (SDL_FPoint){x + nx * half, y + ny * half};
        right[i] = (SDL_FPoint){x - nx * half, y - ny * half};
    }
    band(renderer, left, right, STROKE_STEPS + 1, color);
}

/* A white stroke over its shadow. */
static void inked_stroke(SDL_Renderer *renderer, SDL_FPoint a, SDL_FPoint b, float width,
                         float share, float opacity)
{
    stroke(renderer, a, b, width * 1.5f, share, faded(SHADOW_INK, opacity));
    stroke(renderer, a, b, width, share, faded(WHITE_INK, opacity));
}

/* Three parallel scratches, down and across the body, drawn one after the
 * other. */
static void claws(SDL_Renderer *renderer, float cx, float cy, float s, float side, float drawn,
                  float opacity)
{
    for (int i = 0; i < 3; i++) {
        float off = (float)(i - 1) * 0.1f * s;
        SDL_FPoint a = {cx + side * 0.10f * s + off, cy - 0.24f * s + off * 0.25f};
        SDL_FPoint b = {cx - side * 0.12f * s + off, cy + 0.20f * s + off * 0.25f};
        float share = SDL_clamp(drawn * 1.35f - (float)i * 0.12f, 0.0f, 1.0f);
        inked_stroke(renderer, a, b, 0.05f * s, share, opacity);
    }
}

/* Half an ellipse (the upper jaw when `upper`), with teeth pointing to the
 * other jaw. */
static void jaw(SDL_Renderer *renderer, float cx, float cy, float rx, float ry, bool upper, float s,
                float opacity)
{
    float sign = upper ? -1.0f : 1.0f;
    float thick = 0.045f * s;
    SDL_FPoint outer[ARC_STEPS + 1], inner[ARC_STEPS + 1];
    SDL_FPoint shadow_outer[ARC_STEPS + 1], shadow_inner[ARC_STEPS + 1];
    for (int i = 0; i <= ARC_STEPS; i++) {
        float t = SDL_PI_F * (float)i / ARC_STEPS;
        float x = cx - rx * SDL_cosf(t), y = cy + sign * ry * SDL_sinf(t);
        outer[i] = (SDL_FPoint){x, y};
        inner[i] = (SDL_FPoint){x, y - sign * thick};
        shadow_outer[i] = (SDL_FPoint){x, y + sign * thick * 0.35f};
        shadow_inner[i] = (SDL_FPoint){x, y - sign * thick * 1.35f};
    }
    band(renderer, shadow_outer, shadow_inner, ARC_STEPS + 1, faded(SHADOW_INK, opacity));
    for (int pass = 0; pass < 2; pass++) {
        SDL_FColor color = faded(pass == 0 ? SHADOW_INK : WHITE_INK, opacity);
        float grow = pass == 0 ? 1.4f : 1.0f;
        for (int i = 0; i < JAW_TEETH; i++) {
            float t = SDL_PI_F * ((float)i + 0.5f) / JAW_TEETH;
            float x = cx - rx * 0.92f * SDL_cosf(t), y = cy + sign * ry * 0.92f * SDL_sinf(t);
            float size = 0.055f * s * (0.7f + 0.3f * SDL_sinf(t)) * grow;
            triangle(renderer, (SDL_FPoint){x - size * 0.5f, y}, (SDL_FPoint){x + size * 0.5f, y},
                     (SDL_FPoint){x, y - sign * size * 1.5f}, color);
        }
    }
    band(renderer, outer, inner, ARC_STEPS + 1, faded(WHITE_INK, opacity));
}

/* Two jaws closing; once shut, the punctures where the teeth met. */
static void bite(SDL_Renderer *renderer, float cx, float cy, float s, float drawn, float opacity)
{
    float rx = 0.21f * s, ry = 0.13f * s;
    float gap = (1.0f - drawn * drawn) * 0.18f * s + 0.02f * s;
    jaw(renderer, cx, cy - gap, rx, ry, true, s, opacity);
    jaw(renderer, cx, cy + gap, rx, ry, false, s, opacity);
    if (drawn >= 1.0f) {
        for (int i = 0; i < 5; i++) {
            disc(renderer, cx - rx * 0.75f + (float)i * rx * 0.375f, cy, 0.018f * s,
                 faded(BLOOD_INK, opacity));
        }
    }
}

/* One slash along the blow, rising a little, red at its heart; drops thrown
 * out the far side. */
static void cut(SDL_Renderer *renderer, float cx, float cy, float s, float side, float drawn,
                float opacity)
{
    SDL_FPoint a = {cx - side * 0.30f * s, cy + 0.07f * s};
    SDL_FPoint b = {cx + side * 0.30f * s, cy - 0.07f * s};
    float share = SDL_min(1.0f, drawn * 1.5f);
    stroke(renderer, a, b, 0.085f * s, share, faded(SHADOW_INK, opacity));
    stroke(renderer, a, b, 0.06f * s, share, faded(WHITE_INK, opacity));
    stroke(renderer, a, b, 0.02f * s, share, faded(BLOOD_INK, opacity));
    if (drawn >= 1.0f) {
        static const float DROPS[3][3] = {
            {0.36f, -0.12f, 0.022f}, {0.43f, -0.03f, 0.016f}, {0.33f, 0.04f, 0.013f}};
        for (int i = 0; i < 3; i++) {
            disc(renderer, cx + side * DROPS[i][0] * s,
                 cy + DROPS[i][1] * s + (1.0f - opacity) * 0.1f * s, DROPS[i][2] * s,
                 faded(BLOOD_INK, opacity));
        }
    }
}

/* Short rays bursting out from the point struck. */
static void burst(SDL_Renderer *renderer, float cx, float cy, float s, float drawn, float opacity)
{
    float inner = (0.07f + 0.08f * drawn) * s, outer = (0.13f + 0.13f * drawn) * s;
    for (int i = 0; i < BURST_RAYS; i++) {
        float angle = ((float)i + 0.25f) * 2.0f * SDL_PI_F / BURST_RAYS;
        float reach = i % 2 == 0 ? 1.0f : 0.7f;
        float c = SDL_cosf(angle), sn = SDL_sinf(angle);
        SDL_FPoint a = {cx + c * inner, cy + sn * inner};
        SDL_FPoint b = {cx + c * (inner + (outer - inner) * reach),
                        cy + sn * (inner + (outer - inner) * reach)};
        inked_stroke(renderer, a, b, 0.045f * s, 1.0f, opacity);
    }
}

void strike_mark_draw(SDL_Renderer *renderer, StrikeMark mark, float cx, float cy, float size,
                      float side, float drawn, float opacity)
{
    if (opacity <= 0.0f) {
        return;
    }
    drawn = SDL_clamp(drawn, 0.0f, 1.0f);
    switch (mark) {
    case STRIKE_MARK_CLAWS: claws(renderer, cx, cy, size, side, drawn, opacity); break;
    case STRIKE_MARK_BITE: bite(renderer, cx, cy, size, drawn, opacity); break;
    case STRIKE_MARK_CUT: cut(renderer, cx, cy, size, side, drawn, opacity); break;
    case STRIKE_MARK_BLOW:
    default: burst(renderer, cx, cy, size, drawn, opacity); break;
    }
}
