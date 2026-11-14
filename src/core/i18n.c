#include "core/i18n.h"

#include "core/log.h"
#include "core/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct I18nEntry {
    char *key;
    char *value;
    uint32_t hash;
} I18nEntry;

struct I18nCatalog {
    I18nEntry *entries; /* Insertion order. */
    size_t count;
    size_t capacity;
    int32_t *slots; /* Open-addressing index into `entries`; -1 means empty. */
    size_t slot_count; /* Power of two (or 0 before the first insertion). */
    PluralRule plural_rule;
};

/* ---- String writer (snprintf-like, UTF-8 aware truncation) --------------- */

typedef struct StrWriter {
    char *out;
    size_t capacity;
    size_t written; /* Bytes actually stored in `out`. */
    size_t length; /* Bytes the complete result would have. */
    bool truncated;
} StrWriter;

static void writer_init(StrWriter *writer, char *out, size_t capacity)
{
    writer->out = out;
    writer->capacity = capacity;
    writer->written = 0;
    writer->length = 0;
    writer->truncated = false;
    if (out != NULL && capacity > 0) {
        out[0] = '\0';
    }
}

static void writer_append(StrWriter *writer, const char *text, size_t size)
{
    writer->length += size;
    if (writer->truncated || writer->out == NULL || writer->capacity == 0) {
        return;
    }
    size_t room = writer->capacity - 1 - writer->written;
    size_t copy = size;
    if (copy > room) {
        /* Back off so that we never cut a multi-byte UTF-8 sequence in half. */
        copy = room;
        while (copy > 0 && ((unsigned char)text[copy] & 0xC0u) == 0x80u) {
            copy--;
        }
        writer->truncated = true;
    }
    memcpy(writer->out + writer->written, text, copy);
    writer->written += copy;
    writer->out[writer->written] = '\0';
}

static void writer_append_str(StrWriter *writer, const char *text)
{
    writer_append(writer, text, strlen(text));
}

/* ---- Catalog ------------------------------------------------------------- */

static char *duplicate_range(const char *text, size_t size)
{
    char *copy = malloc(size + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, text, size);
    copy[size] = '\0';
    return copy;
}

I18nCatalog *i18n_catalog_create(void)
{
    I18nCatalog *catalog = calloc(1, sizeof(*catalog));
    if (catalog != NULL) {
        catalog->plural_rule = PLURAL_RULE_ONE_OTHER;
    }
    return catalog;
}

void i18n_catalog_destroy(I18nCatalog *catalog)
{
    if (catalog == NULL) {
        return;
    }
    for (size_t i = 0; i < catalog->count; i++) {
        free(catalog->entries[i].key);
        free(catalog->entries[i].value);
    }
    free(catalog->entries);
    free(catalog->slots);
    free(catalog);
}

/*
 * Returns the index of the entry holding `key`, or -1. `slot_out` receives the
 * slot where the key lives, or where it should be inserted.
 */
static int32_t find_entry(const I18nCatalog *catalog, const char *key, uint32_t hash,
                          size_t *slot_out)
{
    if (catalog->slot_count == 0) {
        *slot_out = 0;
        return -1;
    }
    size_t mask = catalog->slot_count - 1;
    size_t slot = hash & mask;
    for (;;) {
        int32_t index = catalog->slots[slot];
        if (index < 0) {
            *slot_out = slot;
            return -1;
        }
        const I18nEntry *entry = &catalog->entries[index];
        if (entry->hash == hash && strcmp(entry->key, key) == 0) {
            *slot_out = slot;
            return index;
        }
        slot = (slot + 1) & mask;
    }
}

static bool rebuild_slots(I18nCatalog *catalog, size_t slot_count)
{
    int32_t *slots = malloc(slot_count * sizeof(*slots));
    if (slots == NULL) {
        return false;
    }
    for (size_t i = 0; i < slot_count; i++) {
        slots[i] = -1;
    }
    size_t mask = slot_count - 1;
    for (size_t i = 0; i < catalog->count; i++) {
        size_t slot = catalog->entries[i].hash & mask;
        while (slots[slot] >= 0) {
            slot = (slot + 1) & mask;
        }
        slots[slot] = (int32_t)i;
    }
    free(catalog->slots);
    catalog->slots = slots;
    catalog->slot_count = slot_count;
    return true;
}

static void apply_metadata(I18nCatalog *catalog, const char *key, const char *value)
{
    if (strcmp(key, I18N_KEY_PLURAL_RULE) != 0) {
        return;
    }
    if (strcmp(value, "one_other") == 0) {
        catalog->plural_rule = PLURAL_RULE_ONE_OTHER;
    } else if (strcmp(value, "zero_one_other") == 0) {
        catalog->plural_rule = PLURAL_RULE_ZERO_ONE_OTHER;
    } else if (strcmp(value, "other_only") == 0) {
        catalog->plural_rule = PLURAL_RULE_OTHER_ONLY;
    } else {
        LOG_WARN("Unknown plural rule '%s', keeping the previous one", value);
    }
}

bool i18n_catalog_set(I18nCatalog *catalog, const char *key, const char *value)
{
    if (catalog == NULL || key == NULL || value == NULL) {
        return false;
    }
    if (!i18n_is_valid_key(key)) {
        LOG_WARN("Invalid translation key '%s'", key);
        return false;
    }

    uint32_t hash = hash_fnv1a32_str(key);
    size_t slot = 0;
    int32_t existing = find_entry(catalog, key, hash, &slot);

    char *value_copy = duplicate_range(value, strlen(value));
    if (value_copy == NULL) {
        return false;
    }

    if (existing >= 0) {
        free(catalog->entries[existing].value);
        catalog->entries[existing].value = value_copy;
        apply_metadata(catalog, key, value);
        return true;
    }

    if (catalog->count == catalog->capacity) {
        size_t capacity = catalog->capacity > 0 ? catalog->capacity * 2 : 64;
        I18nEntry *entries = realloc(catalog->entries, capacity * sizeof(*entries));
        if (entries == NULL) {
            free(value_copy);
            return false;
        }
        catalog->entries = entries;
        catalog->capacity = capacity;
    }

    /* Keep the load factor below 70%. */
    if ((catalog->count + 1) * 10 > catalog->slot_count * 7) {
        size_t slot_count = catalog->slot_count > 0 ? catalog->slot_count * 2 : 128;
        if (!rebuild_slots(catalog, slot_count)) {
            free(value_copy);
            return false;
        }
        find_entry(catalog, key, hash, &slot);
    }

    char *key_copy = duplicate_range(key, strlen(key));
    if (key_copy == NULL) {
        free(value_copy);
        return false;
    }

    catalog->entries[catalog->count].key = key_copy;
    catalog->entries[catalog->count].value = value_copy;
    catalog->entries[catalog->count].hash = hash;
    catalog->slots[slot] = (int32_t)catalog->count;
    catalog->count++;
    apply_metadata(catalog, key, value);
    return true;
}

const char *i18n_catalog_get(const I18nCatalog *catalog, const char *key)
{
    if (catalog == NULL || key == NULL) {
        return NULL;
    }
    size_t slot = 0;
    int32_t index = find_entry(catalog, key, hash_fnv1a32_str(key), &slot);
    return index >= 0 ? catalog->entries[index].value : NULL;
}

size_t i18n_catalog_count(const I18nCatalog *catalog)
{
    return catalog != NULL ? catalog->count : 0;
}

const char *i18n_catalog_key_at(const I18nCatalog *catalog, size_t index)
{
    return (catalog != NULL && index < catalog->count) ? catalog->entries[index].key : NULL;
}

const char *i18n_catalog_value_at(const I18nCatalog *catalog, size_t index)
{
    return (catalog != NULL && index < catalog->count) ? catalog->entries[index].value : NULL;
}

PluralRule i18n_catalog_plural_rule(const I18nCatalog *catalog)
{
    return catalog != NULL ? catalog->plural_rule : PLURAL_RULE_ONE_OTHER;
}

/* ---- Parsing ------------------------------------------------------------- */

static bool is_blank(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\r';
}

/* Decodes \n, \t, \s (space) and \\ escapes. The result is malloc'ed. */
static char *unescape_value(const char *text, size_t size, const char *source, int line,
                            bool *ok)
{
    char *out = malloc(size + 1);
    if (out == NULL) {
        return NULL;
    }
    size_t length = 0;
    for (size_t i = 0; i < size; i++) {
        char ch = text[i];
        if (ch != '\\' || i + 1 >= size) {
            out[length++] = ch;
            continue;
        }
        char next = text[++i];
        switch (next) {
        case 'n': out[length++] = '\n'; break;
        case 't': out[length++] = '\t'; break;
        case 's': out[length++] = ' '; break;
        case '\\': out[length++] = '\\'; break;
        default:
            LOG_WARN("%s:%d: unknown escape sequence '\\%c'", source, line, next);
            *ok = false;
            out[length++] = '\\';
            out[length++] = next;
            break;
        }
    }
    out[length] = '\0';
    return out;
}

bool i18n_catalog_parse(I18nCatalog *catalog, const char *text, size_t length,
                        const char *source_name)
{
    if (catalog == NULL || (text == NULL && length > 0)) {
        return false;
    }
    const char *source = source_name != NULL ? source_name : "<memory>";
    bool ok = true;
    size_t pos = 0;
    int line = 0;

    if (length >= 3 && memcmp(text, "\xEF\xBB\xBF", 3) == 0) {
        pos = 3; /* UTF-8 byte order mark. */
    }

    while (pos < length) {
        size_t start = pos;
        while (pos < length && text[pos] != '\n') {
            pos++;
        }
        size_t end = pos;
        if (pos < length) {
            pos++; /* Skip the newline. */
        }
        line++;

        while (start < end && is_blank(text[start])) {
            start++;
        }
        while (end > start && is_blank(text[end - 1])) {
            end--;
        }
        if (start == end || text[start] == '#') {
            continue;
        }

        const char *equals = memchr(text + start, '=', end - start);
        if (equals == NULL) {
            LOG_WARN("%s:%d: expected 'key = value'", source, line);
            ok = false;
            continue;
        }

        size_t key_end = (size_t)(equals - text);
        size_t value_start = key_end + 1;
        while (key_end > start && is_blank(text[key_end - 1])) {
            key_end--;
        }
        while (value_start < end && is_blank(text[value_start])) {
            value_start++;
        }

        size_t key_length = key_end - start;
        if (key_length == 0 || key_length >= I18N_MAX_KEY_LENGTH) {
            LOG_WARN("%s:%d: missing or too long key", source, line);
            ok = false;
            continue;
        }
        char key[I18N_MAX_KEY_LENGTH];
        memcpy(key, text + start, key_length);
        key[key_length] = '\0';
        if (!i18n_is_valid_key(key)) {
            LOG_WARN("%s:%d: invalid key '%s'", source, line, key);
            ok = false;
            continue;
        }

        char *value = unescape_value(text + value_start, end - value_start, source, line, &ok);
        if (value == NULL) {
            LOG_ERROR("%s:%d: out of memory", source, line);
            return false;
        }
        if (i18n_catalog_get(catalog, key) != NULL) {
            LOG_WARN("%s:%d: duplicate key '%s' (last definition wins)", source, line, key);
            ok = false;
        }
        if (!i18n_catalog_set(catalog, key, value)) {
            ok = false;
        }
        free(value);
    }
    return ok;
}

/* ---- Helpers ------------------------------------------------------------- */

PluralCategory i18n_plural_category(PluralRule rule, long long count)
{
    unsigned long long magnitude =
        count < 0 ? 0ULL - (unsigned long long)count : (unsigned long long)count;
    switch (rule) {
    case PLURAL_RULE_ONE_OTHER: return magnitude == 1 ? PLURAL_ONE : PLURAL_OTHER;
    case PLURAL_RULE_ZERO_ONE_OTHER: return magnitude <= 1 ? PLURAL_ONE : PLURAL_OTHER;
    case PLURAL_RULE_OTHER_ONLY: return PLURAL_OTHER;
    }
    return PLURAL_OTHER;
}

uint32_t i18n_placeholder_mask(const char *text)
{
    uint32_t mask = 0;
    if (text == NULL) {
        return 0;
    }
    for (const char *p = text; *p != '\0'; p++) {
        if (p[0] != '{') {
            continue;
        }
        if (p[1] == '{') {
            p++; /* Escaped brace. */
        } else if (p[1] >= '0' && p[1] <= '9' && p[2] == '}') {
            mask |= 1u << (p[1] - '0');
            p += 2;
        }
    }
    return mask;
}

bool i18n_is_valid_key(const char *key)
{
    if (key == NULL || key[0] == '\0' || key[0] == '.') {
        return false;
    }
    size_t length = 0;
    char previous = '\0';
    for (const char *p = key; *p != '\0'; p++, length++) {
        char ch = *p;
        bool allowed = (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_' ||
                       ch == '-' || ch == '.';
        if (!allowed || (ch == '.' && previous == '.')) {
            return false;
        }
        previous = ch;
    }
    return length < I18N_MAX_KEY_LENGTH && previous != '.';
}

/* ---- Global state and tr() ---------------------------------------------- */

static const I18nCatalog *s_active = NULL;
static const I18nCatalog *s_fallback = NULL;

void i18n_set_catalogs(const I18nCatalog *active, const I18nCatalog *fallback)
{
    s_active = active;
    s_fallback = fallback;
}

const I18nCatalog *i18n_active_catalog(void)
{
    return s_active;
}

static const char *lookup(const char *key)
{
    const char *value = i18n_catalog_get(s_active, key);
    if (value == NULL) {
        value = i18n_catalog_get(s_fallback, key);
    }
    return value;
}

/* Logs each missing key once (up to a limit) to avoid flooding every frame. */
static void report_missing_key(const char *key)
{
    static uint32_t reported[64];
    static size_t reported_count = 0;
    uint32_t hash = hash_fnv1a32_str(key);
    for (size_t i = 0; i < reported_count; i++) {
        if (reported[i] == hash) {
            return;
        }
    }
    if (reported_count < COLONY_ARRAY_COUNT(reported)) {
        reported[reported_count++] = hash;
        LOG_WARN("Missing translation for key '%s'", key);
    }
}

const char *tr(const char *key)
{
    if (key == NULL) {
        return "";
    }
    const char *value = lookup(key);
    if (value == NULL) {
        report_missing_key(key);
        return key;
    }
    return value;
}

static const char *lookup_plural_in(const I18nCatalog *catalog, const char *key, long long count)
{
    if (catalog == NULL) {
        return NULL;
    }
    char full_key[I18N_MAX_KEY_LENGTH + 8];
    if (i18n_plural_category(catalog->plural_rule, count) == PLURAL_ONE) {
        snprintf(full_key, sizeof(full_key), "%s.one", key);
        const char *value = i18n_catalog_get(catalog, full_key);
        if (value != NULL) {
            return value;
        }
    }
    snprintf(full_key, sizeof(full_key), "%s.other", key);
    return i18n_catalog_get(catalog, full_key);
}

const char *tr_plural(const char *key, long long count)
{
    if (key == NULL) {
        return "";
    }
    /* Each catalog uses its own plural rule: English text follows English rules. */
    const char *value = lookup_plural_in(s_active, key, count);
    if (value == NULL) {
        value = lookup_plural_in(s_fallback, key, count);
    }
    if (value == NULL) {
        report_missing_key(key);
        return key;
    }
    return value;
}

size_t tr_format(char *out, size_t out_size, const char *pattern, int arg_count,
                 const char *const *args)
{
    StrWriter writer;
    writer_init(&writer, out, out_size);
    if (pattern == NULL) {
        return 0;
    }

    const char *run = pattern; /* Start of the literal text not yet written. */
    const char *p = pattern;
    while (*p != '\0') {
        if ((p[0] == '{' && p[1] == '{') || (p[0] == '}' && p[1] == '}')) {
            writer_append(&writer, run, (size_t)(p - run));
            writer_append(&writer, p, 1);
            p += 2;
            run = p;
        } else if (p[0] == '{' && p[1] >= '0' && p[1] <= '9' && p[2] == '}') {
            writer_append(&writer, run, (size_t)(p - run));
            int index = p[1] - '0';
            if (index < arg_count && args[index] != NULL) {
                writer_append_str(&writer, args[index]);
            } else {
                writer_append(&writer, p, 3); /* Keep it visible: easier to spot. */
            }
            p += 3;
            run = p;
        } else {
            p++;
        }
    }
    writer_append(&writer, run, (size_t)(p - run));
    return writer.length;
}

/* ---- Number formatting --------------------------------------------------- */

static const char *separator(const char *key, const char *default_value)
{
    const char *value = lookup(key);
    return value != NULL ? value : default_value;
}

/* Appends a run of ASCII digits, inserting the thousands separator. */
static void append_grouped_digits(StrWriter *writer, const char *digits, size_t count)
{
    const char *thousands = separator("format.thousands_separator", ",");
    for (size_t i = 0; i < count; i++) {
        writer_append(writer, &digits[i], 1);
        size_t remaining = count - i - 1;
        if (remaining > 0 && remaining % 3 == 0) {
            writer_append_str(writer, thousands);
        }
    }
}

size_t tr_integer(char *out, size_t out_size, long long value)
{
    StrWriter writer;
    writer_init(&writer, out, out_size);

    unsigned long long magnitude =
        value < 0 ? 0ULL - (unsigned long long)value : (unsigned long long)value;
    char digits[32];
    int count = snprintf(digits, sizeof(digits), "%llu", magnitude);
    if (value < 0) {
        writer_append(&writer, "-", 1);
    }
    append_grouped_digits(&writer, digits, (size_t)count);
    return writer.length;
}

size_t tr_decimal(char *out, size_t out_size, double value, int decimals)
{
    StrWriter writer;
    writer_init(&writer, out, out_size);

    if (decimals < 0) {
        decimals = 0;
    } else if (decimals > 9) {
        decimals = 9;
    }
    char raw[64];
    snprintf(raw, sizeof(raw), "%.*f", decimals, value);

    const char *digits = raw;
    if (*digits == '-') {
        writer_append(&writer, "-", 1);
        digits++;
    }
    const char *dot = strchr(digits, '.');
    size_t integer_count = dot != NULL ? (size_t)(dot - digits) : strlen(digits);
    append_grouped_digits(&writer, digits, integer_count);
    if (dot != NULL) {
        writer_append_str(&writer, separator("format.decimal_separator", "."));
        writer_append_str(&writer, dot + 1);
    }
    return writer.length;
}
