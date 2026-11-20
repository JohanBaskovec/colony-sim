#include "core/settings.h"

#include "core/i18n.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#define LINE_SIZE 128

static const struct {
    const char *id;
    const char *name_key;
} PALETTES[SETTINGS_PALETTE_COUNT] = {
    [SETTINGS_PALETTE_STANDARD] = {"standard", N_("settings.palette.standard")},
    [SETTINGS_PALETTE_COLORBLIND] = {"colorblind", N_("settings.palette.colorblind")},
};

void settings_default(Settings *settings)
{
    memset(settings, 0, sizeof(*settings));
    settings->ui_scale = SETTINGS_UI_SCALE_DEFAULT;
    settings->palette = SETTINGS_PALETTE_STANDARD;
}

const char *settings_palette_id(SettingsPalette palette)
{
    return (unsigned)palette < SETTINGS_PALETTE_COUNT ? PALETTES[palette].id : NULL;
}

const char *settings_palette_name_key(SettingsPalette palette)
{
    return (unsigned)palette < SETTINGS_PALETTE_COUNT ? PALETTES[palette].name_key : NULL;
}

static bool is_space(char c)
{
    return c == ' ' || c == '\t' || c == '\r';
}

/* Strips the spaces around a string, in place. */
static char *trim(char *text)
{
    while (is_space(*text)) {
        text++;
    }
    size_t length = strlen(text);
    while (length > 0 && is_space(text[length - 1])) {
        text[--length] = '\0';
    }
    return text;
}

/* A whole decimal number of at most a few digits; false otherwise. */
static bool parse_int(const char *text, int *out)
{
    int value = 0;
    int digits = 0;
    for (; *text >= '0' && *text <= '9'; text++) {
        if (++digits > 6) {
            return false;
        }
        value = value * 10 + (*text - '0');
    }
    if (digits == 0 || *text != '\0') {
        return false;
    }
    *out = value;
    return true;
}

/* Letters, digits, '_' and '-' only, short enough (a file name in assets/lang). */
static bool is_language_code(const char *text)
{
    size_t length = strlen(text);
    if (length >= SETTINGS_LANGUAGE_SIZE) {
        return false;
    }
    for (size_t i = 0; i < length; i++) {
        char c = text[i];
        bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
                  c == '_' || c == '-';
        if (!ok) {
            return false;
        }
    }
    return true;
}

static bool equal_ignoring_case(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) {
            return false;
        }
    }
    return *a == *b;
}

/* 1/0, true/false, yes/no, on/off; false when it is none of them. */
static bool parse_bool(const char *text, bool *out)
{
    static const char *const YES[] = {"1", "true", "yes", "on"};
    static const char *const NO[] = {"0", "false", "no", "off"};
    for (size_t i = 0; i < sizeof(YES) / sizeof(YES[0]); i++) {
        if (equal_ignoring_case(text, YES[i])) {
            *out = true;
            return true;
        }
        if (equal_ignoring_case(text, NO[i])) {
            *out = false;
            return true;
        }
    }
    return false;
}

static void parse_line(Settings *settings, char *line)
{
    line = trim(line);
    if (line[0] == '\0' || line[0] == '#') {
        return;
    }
    char *equals = strchr(line, '=');
    if (equals == NULL) {
        return;
    }
    *equals = '\0';
    const char *key = trim(line);
    const char *value = trim(equals + 1);
    if (equal_ignoring_case(key, "ui_scale")) {
        int scale;
        if (parse_int(value, &scale) && scale >= CLI_UI_SCALE_MIN && scale <= CLI_UI_SCALE_MAX) {
            settings->ui_scale = scale;
        }
    } else if (equal_ignoring_case(key, "palette")) {
        for (int palette = 0; palette < SETTINGS_PALETTE_COUNT; palette++) {
            if (strcmp(value, PALETTES[palette].id) == 0) {
                settings->palette = (SettingsPalette)palette;
            }
        }
    } else if (equal_ignoring_case(key, "language")) {
        if (is_language_code(value)) {
            snprintf(settings->language, sizeof(settings->language), "%s", value);
        }
    } else if (equal_ignoring_case(key, "skip_menu")) {
        bool skip;
        if (parse_bool(value, &skip)) {
            settings->skip_menu = skip;
        }
    }
}

void settings_parse(Settings *settings, const char *text, size_t length)
{
    size_t start = 0;
    while (start < length) {
        size_t end = start;
        while (end < length && text[end] != '\n') {
            end++;
        }
        /* Longer lines are no setting of ours: skipped. */
        if (end - start < LINE_SIZE) {
            char line[LINE_SIZE];
            memcpy(line, text + start, end - start);
            line[end - start] = '\0';
            parse_line(settings, line);
        }
        start = end + 1;
    }
}

size_t settings_format(const Settings *settings, char *out, size_t size)
{
    const char *palette = settings_palette_id(settings->palette);
    int length = snprintf(out, size,
                          "# Colony Sim settings (written by the game)\n"
                          "ui_scale = %d\n"
                          "palette = %s\n"
                          "language = %s\n"
                          "skip_menu = %d\n",
                          settings->ui_scale, palette != NULL ? palette : PALETTES[0].id,
                          settings->language, settings->skip_menu ? 1 : 0);
    return length > 0 ? (size_t)length : 0;
}
