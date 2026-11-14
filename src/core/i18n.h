/*
 * Internationalization: translation catalogs, lookup, plurals and formatting.
 *
 * Every string shown to the player is identified by a stable key and looked up
 * at runtime with tr(). Translations live in UTF-8 text files, one per language
 * (assets/lang/<code>.lang), using a simple "key = value" format. See
 * docs/I18N.md for the file format and the translator guide.
 *
 * Lookup order: active catalog -> fallback catalog (English) -> the key itself,
 * so a missing translation is visible on screen instead of crashing.
 *
 * This module is pure C (no SDL, no file I/O): the caller loads the file
 * contents and hands them to i18n_catalog_parse().
 */
#ifndef COLONY_CORE_I18N_H
#define COLONY_CORE_I18N_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Marks a string literal as a translation key without translating it, for keys
 * stored in static tables (terrain names, season names...). The translation key
 * check (cmake/CheckTranslationKeys.cmake) scans for N_, tr and tr_plural calls
 * with a literal argument and verifies that each key exists in en.lang.
 */
#define N_(key) (key)

/* Maximum key length in bytes, including the terminating NUL. */
#define I18N_MAX_KEY_LENGTH 128

/* Well-known metadata keys present in every language file. */
#define I18N_KEY_LANGUAGE_CODE "language.code"
#define I18N_KEY_LANGUAGE_NAME "language.name"
#define I18N_KEY_PLURAL_RULE "language.plural_rule"

/* How a language picks between the ".one" and ".other" forms of a plural key. */
typedef enum PluralRule {
    PLURAL_RULE_ONE_OTHER,      /* "one" for 1, "other" otherwise (English, German...). */
    PLURAL_RULE_ZERO_ONE_OTHER, /* "one" for 0 and 1, "other" otherwise (French...). */
    PLURAL_RULE_OTHER_ONLY,     /* Always "other" (Japanese, Chinese...). */
} PluralRule;

typedef enum PluralCategory {
    PLURAL_ONE,
    PLURAL_OTHER,
} PluralCategory;

typedef struct I18nCatalog I18nCatalog;

/* ---- Catalogs ------------------------------------------------------------ */

I18nCatalog *i18n_catalog_create(void);
void i18n_catalog_destroy(I18nCatalog *catalog);

/*
 * Parses the contents of a language file and adds its entries to the catalog.
 * Invalid lines are reported through the log and skipped; the function then
 * returns false but every valid line is still loaded. `source_name` is only
 * used in diagnostics (may be NULL).
 */
bool i18n_catalog_parse(I18nCatalog *catalog, const char *text, size_t length,
                        const char *source_name);

/* Adds or replaces an entry. Returns false if the key is invalid. */
bool i18n_catalog_set(I18nCatalog *catalog, const char *key, const char *value);

/* Returns the value for `key`, or NULL when the catalog does not contain it. */
const char *i18n_catalog_get(const I18nCatalog *catalog, const char *key);

/* Entries are kept in insertion order and can be iterated by index. */
size_t i18n_catalog_count(const I18nCatalog *catalog);
const char *i18n_catalog_key_at(const I18nCatalog *catalog, size_t index);
const char *i18n_catalog_value_at(const I18nCatalog *catalog, size_t index);

/* Taken from the "language.plural_rule" entry (one_other, zero_one_other, other_only). */
PluralRule i18n_catalog_plural_rule(const I18nCatalog *catalog);

/* ---- Helpers ------------------------------------------------------------- */

PluralCategory i18n_plural_category(PluralRule rule, long long count);

/* Bit N is set when the text contains the placeholder {N} (N in 0..9). */
uint32_t i18n_placeholder_mask(const char *text);

/* Keys use lowercase ASCII letters, digits, '_', '-' and '.' separators. */
bool i18n_is_valid_key(const char *key);

/* ---- Global translation state ------------------------------------------- */

/*
 * Selects the catalogs used by tr(). Catalogs are borrowed, not owned: they
 * must outlive their use here. `active` may equal `fallback`; both may be NULL.
 */
void i18n_set_catalogs(const I18nCatalog *active, const I18nCatalog *fallback);
const I18nCatalog *i18n_active_catalog(void);

/* Translates a key. Never returns NULL: falls back to the key itself. */
const char *tr(const char *key);

/*
 * Translates a plural key: looks up "<key>.one" or "<key>.other" according to
 * the plural rule of the catalog providing the text. The count is not
 * substituted; combine with TR_FORMAT for that.
 */
const char *tr_plural(const char *key, long long count);

/*
 * Substitutes positional placeholders {0}..{9} in `pattern` with `args`.
 * Translators may reorder placeholders freely. "{{" and "}}" produce literal
 * braces. A placeholder without a matching argument is left untouched.
 *
 * Behaves like snprintf: always NUL-terminates (when out_size > 0), never
 * splits a UTF-8 sequence when truncating, and returns the length the full
 * result would have.
 */
size_t tr_format(char *out, size_t out_size, const char *pattern, int arg_count,
                 const char *const *args);

/* Convenience wrapper: TR_FORMAT(buffer, sizeof(buffer), pattern, arg0, arg1, ...). */
#define TR_FORMAT(out, out_size, pattern, ...)                                                   \
    tr_format((out), (out_size), (pattern),                                                      \
              (int)(sizeof((const char *const[]){__VA_ARGS__}) / sizeof(const char *)),          \
              (const char *const[]){__VA_ARGS__})

/*
 * Locale-aware number formatting, using the "format.thousands_separator" and
 * "format.decimal_separator" entries of the current language. Same return
 * convention as tr_format.
 */
size_t tr_integer(char *out, size_t out_size, long long value);
size_t tr_decimal(char *out, size_t out_size, double value, int decimals);

#endif /* COLONY_CORE_I18N_H */
