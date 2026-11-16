/*
 * Translation consistency checker, run by CTest for every language file:
 *
 *     check_translations <reference.lang> <translation.lang>
 *
 * Fails when the translation:
 *   - has a "language.code" different from its file name,
 *   - lacks a key of the reference file (English),
 *   - contains a key that the reference does not have (obsolete or misspelled),
 *   - uses a different set of {N} placeholders than the reference text.
 */
#include "core/i18n.h"
#include "core/log.h"

#include "test_support.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static I18nCatalog *load_catalog(const char *path, int *errors)
{
    size_t length = 0;
    char *text = test_read_file(path, &length);
    if (text == NULL) {
        fprintf(stderr, "error: cannot read '%s'\n", path);
        (*errors)++;
        return NULL;
    }
    I18nCatalog *catalog = i18n_catalog_create();
    if (!i18n_catalog_parse(catalog, text, length, path)) {
        fprintf(stderr, "error: '%s' contains invalid lines (see warnings above)\n", path);
        (*errors)++;
    }
    free(text);
    return catalog;
}

/* File name without directory nor extension: "assets/lang/fr.lang" -> "fr". */
static void file_stem(const char *path, char *out, size_t out_size)
{
    const char *name = path;
    for (const char *p = path; *p != '\0'; p++) {
        if (*p == '/' || *p == '\\') {
            name = p + 1;
        }
    }
    snprintf(out, out_size, "%s", name);
    char *dot = strrchr(out, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
}

static bool ends_with(const char *text, const char *suffix)
{
    size_t text_length = strlen(text);
    size_t suffix_length = strlen(suffix);
    return text_length >= suffix_length && strcmp(text + text_length - suffix_length, suffix) == 0;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: %s <reference.lang> <translation.lang>\n", argv[0]);
        return EXIT_FAILURE;
    }
    log_set_min_level(LOG_LEVEL_WARN);

    int errors = 0;
    I18nCatalog *reference = load_catalog(argv[1], &errors);
    I18nCatalog *translation = load_catalog(argv[2], &errors);
    if (reference == NULL || translation == NULL) {
        i18n_catalog_destroy(reference);
        i18n_catalog_destroy(translation);
        return EXIT_FAILURE;
    }

    char stem[64];
    file_stem(argv[2], stem, sizeof(stem));
    const char *code = i18n_catalog_get(translation, I18N_KEY_LANGUAGE_CODE);
    if (code == NULL || strcmp(code, stem) != 0) {
        fprintf(stderr, "error: %s must equal the file name '%s' (found '%s')\n",
                I18N_KEY_LANGUAGE_CODE, stem, code != NULL ? code : "(missing)");
        errors++;
    }

    PluralRule rule = i18n_catalog_plural_rule(translation);
    for (size_t i = 0; i < i18n_catalog_count(reference); i++) {
        const char *key = i18n_catalog_key_at(reference, i);
        const char *reference_text = i18n_catalog_value_at(reference, i);
        const char *translated_text = i18n_catalog_get(translation, key);

        if (translated_text == NULL) {
            /* Languages without a singular form only need ".other". */
            if (rule == PLURAL_RULE_OTHER_ONLY && ends_with(key, ".one")) {
                continue;
            }
            fprintf(stderr, "error: missing key '%s'\n", key);
            errors++;
            continue;
        }
        uint32_t expected = i18n_placeholder_mask(reference_text);
        uint32_t actual = i18n_placeholder_mask(translated_text);
        if (expected != actual) {
            fprintf(stderr, "error: key '%s' uses placeholders 0x%x, expected 0x%x\n", key,
                    (unsigned)actual, (unsigned)expected);
            errors++;
        }
    }

    for (size_t i = 0; i < i18n_catalog_count(translation); i++) {
        const char *key = i18n_catalog_key_at(translation, i);
        if (i18n_catalog_get(reference, key) == NULL) {
            fprintf(stderr, "error: key '%s' does not exist in the reference file\n", key);
            errors++;
        }
    }

    printf("%s: %zu keys, %d error(s)\n", argv[2], i18n_catalog_count(translation), errors);
    i18n_catalog_destroy(reference);
    i18n_catalog_destroy(translation);
    return errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
