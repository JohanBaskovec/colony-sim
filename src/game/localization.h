/*
 * Language discovery and switching: finds the available assets/lang/<code>.lang
 * files, picks the initial language (command line, then system locale, then
 * English) and feeds the catalogs to the core i18n module.
 */
#ifndef COLONY_GAME_LOCALIZATION_H
#define COLONY_GAME_LOCALIZATION_H

#include "core/i18n.h"

#include <stdbool.h>

#define LOCALIZATION_MAX_LANGUAGES 64
#define LOCALIZATION_FALLBACK_CODE "en"

typedef struct LanguageInfo {
    char code[16]; /* File name without extension, e.g. "fr" or "pt_BR". */
    char name[64]; /* Native name from "language.name", e.g. "Français". */
} LanguageInfo;

typedef struct Localization {
    char lang_dir[1024]; /* Ends with a path separator. */
    LanguageInfo languages[LOCALIZATION_MAX_LANGUAGES]; /* Sorted by code. */
    int language_count;
    int active_index;
    I18nCatalog *fallback; /* English, always loaded. */
    I18nCatalog *active; /* May point to `fallback`. */
} Localization;

/* `requested_code` may be NULL or empty to use the system locale. */
bool localization_init(Localization *loc, const char *assets_dir, const char *requested_code);
void localization_shutdown(Localization *loc);

bool localization_select(Localization *loc, int index);

/* Switches to the next available language (wraps around). */
void localization_cycle(Localization *loc);

const LanguageInfo *localization_active_language(const Localization *loc);

#endif /* COLONY_GAME_LOCALIZATION_H */
