#include "game/text.h"

#include "core/log.h"
#include "core/util.h"

#include <SDL3_ttf/SDL_ttf.h>

#define TEXT_CACHE_CAPACITY 256
#define TEXT_CACHE_MAX_IDLE_FRAMES 120

typedef struct TextCacheEntry {
    char *string; /* NULL when the slot is free. */
    uint64_t hash;
    TTF_Text *text;
    uint64_t last_used_frame;
} TextCacheEntry;

struct TextRenderer {
    TTF_TextEngine *engine;
    TTF_Font *font;
    TextCacheEntry entries[TEXT_CACHE_CAPACITY];
    uint64_t frame;
};

TextRenderer *text_renderer_create(SDL_Renderer *renderer, const char *font_path, float point_size)
{
    TextRenderer *text = SDL_calloc(1, sizeof(*text));
    if (text == NULL) {
        return NULL;
    }
    text->font = TTF_OpenFont(font_path, point_size);
    if (text->font == NULL) {
        LOG_ERROR("Cannot open font '%s': %s", font_path, SDL_GetError());
        text_renderer_destroy(text);
        return NULL;
    }
    /* Without HarfBuzz, SDL_ttf mispositions glyphs after kerned pairs ("Te rrain"). */
    TTF_SetFontKerning(text->font, false);
    text->engine = TTF_CreateRendererTextEngine(renderer);
    if (text->engine == NULL) {
        LOG_ERROR("Cannot create the text engine: %s", SDL_GetError());
        text_renderer_destroy(text);
        return NULL;
    }
    return text;
}

static void free_entry(TextCacheEntry *entry)
{
    if (entry->text != NULL) {
        TTF_DestroyText(entry->text);
    }
    SDL_free(entry->string);
    SDL_zerop(entry);
}

void text_renderer_destroy(TextRenderer *text)
{
    if (text == NULL) {
        return;
    }
    for (int i = 0; i < TEXT_CACHE_CAPACITY; i++) {
        free_entry(&text->entries[i]);
    }
    if (text->engine != NULL) {
        TTF_DestroyRendererTextEngine(text->engine);
    }
    if (text->font != NULL) {
        TTF_CloseFont(text->font);
    }
    SDL_free(text);
}

/* Returns the cached layout of `utf8`, creating it (and evicting the LRU entry) if needed. */
static TTF_Text *get_text(TextRenderer *text, const char *utf8)
{
    uint64_t hash = hash_fnv1a64(HASH_FNV1A64_INIT, utf8, SDL_strlen(utf8));
    int free_slot = -1;
    int oldest_slot = -1;

    for (int i = 0; i < TEXT_CACHE_CAPACITY; i++) {
        TextCacheEntry *entry = &text->entries[i];
        if (entry->string == NULL) {
            if (free_slot < 0) {
                free_slot = i;
            }
            continue;
        }
        if (entry->hash == hash && SDL_strcmp(entry->string, utf8) == 0) {
            entry->last_used_frame = text->frame;
            return entry->text;
        }
        if (oldest_slot < 0 ||
            entry->last_used_frame < text->entries[oldest_slot].last_used_frame) {
            oldest_slot = i;
        }
    }

    int slot = free_slot >= 0 ? free_slot : oldest_slot;
    TextCacheEntry *entry = &text->entries[slot];
    free_entry(entry);

    entry->text = TTF_CreateText(text->engine, text->font, utf8, 0);
    if (entry->text == NULL) {
        LOG_WARN("Cannot lay out text: %s", SDL_GetError());
        return NULL;
    }
    entry->string = SDL_strdup(utf8);
    entry->hash = hash;
    entry->last_used_frame = text->frame;
    return entry->text;
}

void text_draw(TextRenderer *text, const char *utf8, float x, float y, SDL_Color color)
{
    if (utf8 == NULL || utf8[0] == '\0') {
        return;
    }
    TTF_Text *layout = get_text(text, utf8);
    if (layout == NULL) {
        return;
    }
    TTF_SetTextColor(layout, color.r, color.g, color.b, color.a);
    TTF_DrawRendererText(layout, x, y);
}

void text_measure(TextRenderer *text, const char *utf8, int *width, int *height)
{
    *width = 0;
    *height = text_line_height(text);
    if (utf8 == NULL || utf8[0] == '\0') {
        return;
    }
    TTF_Text *layout = get_text(text, utf8);
    if (layout != NULL) {
        TTF_GetTextSize(layout, width, height);
    }
}

int text_line_height(const TextRenderer *text)
{
    return TTF_GetFontLineSkip(text->font);
}

void text_renderer_end_frame(TextRenderer *text)
{
    text->frame++;
    for (int i = 0; i < TEXT_CACHE_CAPACITY; i++) {
        TextCacheEntry *entry = &text->entries[i];
        if (entry->string != NULL &&
            text->frame - entry->last_used_frame > TEXT_CACHE_MAX_IDLE_FRAMES) {
            free_entry(entry);
        }
    }
}
