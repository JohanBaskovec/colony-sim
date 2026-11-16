#include "game/localization.h"

#include "core/log.h"

#include <SDL3/SDL.h>

static I18nCatalog *load_catalog(const Localization *loc, const char *code)
{
    char path[1200];
    SDL_snprintf(path, sizeof(path), "%s%s.lang", loc->lang_dir, code);

    size_t size = 0;
    void *data = SDL_LoadFile(path, &size);
    if (data == NULL) {
        LOG_ERROR("Cannot read '%s': %s", path, SDL_GetError());
        return NULL;
    }
    I18nCatalog *catalog = i18n_catalog_create();
    if (catalog != NULL && !i18n_catalog_parse(catalog, data, size, path)) {
        LOG_WARN("'%s' contains errors; valid entries were loaded", path);
    }
    SDL_free(data);
    return catalog;
}

static int compare_languages(const void *a, const void *b)
{
    return SDL_strcmp(((const LanguageInfo *)a)->code, ((const LanguageInfo *)b)->code);
}

static void discover_languages(Localization *loc)
{
    int count = 0;
    char **files = SDL_GlobDirectory(loc->lang_dir, "*.lang", 0, &count);
    if (files == NULL) {
        LOG_ERROR("Cannot list '%s': %s", loc->lang_dir, SDL_GetError());
        return;
    }

    for (int i = 0; i < count && loc->language_count < LOCALIZATION_MAX_LANGUAGES; i++) {
        LanguageInfo *info = &loc->languages[loc->language_count];
        SDL_strlcpy(info->code, files[i], sizeof(info->code));
        char *extension = SDL_strrchr(info->code, '.');
        if (extension == NULL || SDL_strcmp(extension, ".lang") != 0) {
            continue; /* Name too long for `code`: skip it. */
        }
        *extension = '\0';

        I18nCatalog *catalog = load_catalog(loc, info->code);
        if (catalog == NULL) {
            continue;
        }
        const char *name = i18n_catalog_get(catalog, I18N_KEY_LANGUAGE_NAME);
        SDL_strlcpy(info->name, name != NULL ? name : info->code, sizeof(info->name));
        i18n_catalog_destroy(catalog);
        loc->language_count++;
    }
    SDL_free(files);
    SDL_qsort(loc->languages, (size_t)loc->language_count, sizeof(LanguageInfo), compare_languages);
}

static int find_language(const Localization *loc, const char *code)
{
    for (int i = 0; i < loc->language_count; i++) {
        if (SDL_strcasecmp(loc->languages[i].code, code) == 0) {
            return i;
        }
    }
    return -1;
}

/* Tries "<language>_<COUNTRY>" then "<language>" for each preferred system locale. */
static int find_system_language(const Localization *loc)
{
    int count = 0;
    SDL_Locale **locales = SDL_GetPreferredLocales(&count);
    if (locales == NULL) {
        return -1;
    }
    int found = -1;
    for (int i = 0; i < count && found < 0; i++) {
        const SDL_Locale *locale = locales[i];
        if (locale->country != NULL) {
            char code[32];
            SDL_snprintf(code, sizeof(code), "%s_%s", locale->language, locale->country);
            found = find_language(loc, code);
        }
        if (found < 0) {
            found = find_language(loc, locale->language);
        }
    }
    SDL_free(locales);
    return found;
}

bool localization_init(Localization *loc, const char *assets_dir, const char *requested_code)
{
    SDL_zerop(loc);
    loc->active_index = -1;
    SDL_snprintf(loc->lang_dir, sizeof(loc->lang_dir), "%slang/", assets_dir);

    discover_languages(loc);
    loc->fallback = load_catalog(loc, LOCALIZATION_FALLBACK_CODE);
    if (loc->fallback == NULL) {
        LOG_ERROR("The reference language file '%s.lang' is required", LOCALIZATION_FALLBACK_CODE);
        return false;
    }

    int index = -1;
    if (requested_code != NULL && requested_code[0] != '\0') {
        index = find_language(loc, requested_code);
        if (index < 0) {
            LOG_WARN("Language '%s' is not available", requested_code);
        }
    }
    if (index < 0) {
        index = find_system_language(loc);
    }
    if (index < 0) {
        index = find_language(loc, LOCALIZATION_FALLBACK_CODE);
    }
    return index >= 0 && localization_select(loc, index);
}

void localization_shutdown(Localization *loc)
{
    i18n_set_catalogs(NULL, NULL);
    if (loc->active != loc->fallback) {
        i18n_catalog_destroy(loc->active);
    }
    i18n_catalog_destroy(loc->fallback);
    loc->active = NULL;
    loc->fallback = NULL;
}

bool localization_select(Localization *loc, int index)
{
    if (index < 0 || index >= loc->language_count) {
        return false;
    }
    const char *code = loc->languages[index].code;
    I18nCatalog *catalog = loc->fallback;
    if (SDL_strcmp(code, LOCALIZATION_FALLBACK_CODE) != 0) {
        catalog = load_catalog(loc, code);
        if (catalog == NULL) {
            return false;
        }
    }

    if (loc->active != NULL && loc->active != loc->fallback) {
        i18n_catalog_destroy(loc->active);
    }
    loc->active = catalog;
    loc->active_index = index;
    i18n_set_catalogs(loc->active, loc->fallback);
    LOG_INFO("Language: %s (%s)", loc->languages[index].name, code);
    return true;
}

void localization_cycle(Localization *loc)
{
    if (loc->language_count > 0) {
        localization_select(loc, (loc->active_index + 1) % loc->language_count);
    }
}

const LanguageInfo *localization_active_language(const Localization *loc)
{
    if (loc->active_index < 0 || loc->active_index >= loc->language_count) {
        return NULL;
    }
    return &loc->languages[loc->active_index];
}
