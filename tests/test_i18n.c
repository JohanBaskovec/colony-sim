#include "core/i18n.h"
#include "core/log.h"

#include "test_framework.h"

static I18nCatalog *parse_text(const char *text, bool *ok)
{
    I18nCatalog *catalog = i18n_catalog_create();
    bool result = i18n_catalog_parse(catalog, text, strlen(text), "test");
    if (ok != NULL) {
        *ok = result;
    }
    return catalog;
}

/* ---- Parsing ---- */

static void test_parse_basic_entries(void)
{
    bool ok = false;
    I18nCatalog *catalog = parse_text("# comment\n"
                                      "\n"
                                      "hello = Hello\n"
                                      "   menu.start_game   =   Start a new game   \n"
                                      "\t# indented comment\n",
                                      &ok);
    CHECK(ok);
    CHECK_EQ_UINT(i18n_catalog_count(catalog), 2);
    CHECK_STR_EQ(i18n_catalog_get(catalog, "hello"), "Hello");
    CHECK_STR_EQ(i18n_catalog_get(catalog, "menu.start_game"), "Start a new game");
    CHECK(i18n_catalog_get(catalog, "missing") == NULL);
    CHECK_STR_EQ(i18n_catalog_key_at(catalog, 0), "hello");
    CHECK_STR_EQ(i18n_catalog_value_at(catalog, 1), "Start a new game");
    CHECK(i18n_catalog_key_at(catalog, 2) == NULL);
    i18n_catalog_destroy(catalog);
}

static void test_parse_handles_bom_crlf_and_missing_final_newline(void)
{
    bool ok = false;
    I18nCatalog *catalog = parse_text("\xEF\xBB\xBF"
                                      "a = one\r\nb = two",
                                      &ok);
    CHECK(ok);
    CHECK_STR_EQ(i18n_catalog_get(catalog, "a"), "one");
    CHECK_STR_EQ(i18n_catalog_get(catalog, "b"), "two");
    i18n_catalog_destroy(catalog);
}

static void test_parse_escapes(void)
{
    bool ok = false;
    I18nCatalog *catalog = parse_text("k = line1\\nline2\\tx\\\\y\\s\n"
                                      "lead = \\s padded\n",
                                      &ok);
    CHECK(ok);
    CHECK_STR_EQ(i18n_catalog_get(catalog, "k"), "line1\nline2\tx\\y ");
    CHECK_STR_EQ(i18n_catalog_get(catalog, "lead"), "  padded");
    i18n_catalog_destroy(catalog);
}

static void test_parse_value_may_contain_equals_and_be_empty(void)
{
    I18nCatalog *catalog = parse_text("formula = a = b\nempty =\n", NULL);
    CHECK_STR_EQ(i18n_catalog_get(catalog, "formula"), "a = b");
    CHECK_STR_EQ(i18n_catalog_get(catalog, "empty"), "");
    i18n_catalog_destroy(catalog);
}

static void test_parse_reports_invalid_lines_but_keeps_valid_ones(void)
{
    bool ok = true;
    I18nCatalog *catalog = parse_text("good = yes\n"
                                      "no equals sign here\n"
                                      "Bad Key = uppercase and spaces\n"
                                      " = no key\n"
                                      "also.good = yes\n",
                                      &ok);
    CHECK(!ok);
    CHECK_EQ_UINT(i18n_catalog_count(catalog), 2);
    CHECK_STR_EQ(i18n_catalog_get(catalog, "also.good"), "yes");
    i18n_catalog_destroy(catalog);
}

static void test_parse_duplicate_key_last_wins(void)
{
    bool ok = true;
    I18nCatalog *catalog = parse_text("k = first\nk = second\n", &ok);
    CHECK(!ok);
    CHECK_EQ_UINT(i18n_catalog_count(catalog), 1);
    CHECK_STR_EQ(i18n_catalog_get(catalog, "k"), "second");
    i18n_catalog_destroy(catalog);
}

static void test_catalog_grows_to_many_keys(void)
{
    I18nCatalog *catalog = i18n_catalog_create();
    char key[32], value[32];
    for (int i = 0; i < 2000; i++) {
        snprintf(key, sizeof(key), "key.%d", i);
        snprintf(value, sizeof(value), "value %d", i);
        CHECK(i18n_catalog_set(catalog, key, value));
    }
    CHECK_EQ_UINT(i18n_catalog_count(catalog), 2000);
    for (int i = 0; i < 2000; i++) {
        snprintf(key, sizeof(key), "key.%d", i);
        snprintf(value, sizeof(value), "value %d", i);
        CHECK_STR_EQ(i18n_catalog_get(catalog, key), value);
    }
    i18n_catalog_destroy(catalog);
}

static void test_key_validation(void)
{
    CHECK(i18n_is_valid_key("hud.tile.terrain"));
    CHECK(i18n_is_valid_key("a-b_c.d9"));
    CHECK(!i18n_is_valid_key(""));
    CHECK(!i18n_is_valid_key(NULL));
    CHECK(!i18n_is_valid_key("Upper"));
    CHECK(!i18n_is_valid_key("with space"));
    CHECK(!i18n_is_valid_key(".leading"));
    CHECK(!i18n_is_valid_key("trailing."));
    CHECK(!i18n_is_valid_key("double..dot"));
    CHECK(!i18n_is_valid_key("accent.\xC3\xA9"));
}

/* ---- Plurals ---- */

static void test_plural_categories(void)
{
    CHECK_EQ_INT(i18n_plural_category(PLURAL_RULE_ONE_OTHER, 0), PLURAL_OTHER);
    CHECK_EQ_INT(i18n_plural_category(PLURAL_RULE_ONE_OTHER, 1), PLURAL_ONE);
    CHECK_EQ_INT(i18n_plural_category(PLURAL_RULE_ONE_OTHER, 2), PLURAL_OTHER);
    CHECK_EQ_INT(i18n_plural_category(PLURAL_RULE_ONE_OTHER, -1), PLURAL_ONE);
    CHECK_EQ_INT(i18n_plural_category(PLURAL_RULE_ZERO_ONE_OTHER, 0), PLURAL_ONE);
    CHECK_EQ_INT(i18n_plural_category(PLURAL_RULE_ZERO_ONE_OTHER, 1), PLURAL_ONE);
    CHECK_EQ_INT(i18n_plural_category(PLURAL_RULE_ZERO_ONE_OTHER, 2), PLURAL_OTHER);
    CHECK_EQ_INT(i18n_plural_category(PLURAL_RULE_OTHER_ONLY, 1), PLURAL_OTHER);
}

static void test_plural_rule_comes_from_metadata(void)
{
    I18nCatalog *catalog = parse_text("x = y\n", NULL);
    CHECK_EQ_INT(i18n_catalog_plural_rule(catalog), PLURAL_RULE_ONE_OTHER);
    i18n_catalog_set(catalog, I18N_KEY_PLURAL_RULE, "zero_one_other");
    CHECK_EQ_INT(i18n_catalog_plural_rule(catalog), PLURAL_RULE_ZERO_ONE_OTHER);
    i18n_catalog_set(catalog, I18N_KEY_PLURAL_RULE, "other_only");
    CHECK_EQ_INT(i18n_catalog_plural_rule(catalog), PLURAL_RULE_OTHER_ONLY);
    i18n_catalog_set(catalog, I18N_KEY_PLURAL_RULE, "nonsense");
    CHECK_EQ_INT(i18n_catalog_plural_rule(catalog), PLURAL_RULE_OTHER_ONLY);
    i18n_catalog_destroy(catalog);
}

/* ---- tr() ---- */

static void test_tr_falls_back_to_fallback_then_key(void)
{
    I18nCatalog *english = parse_text("a = A (en)\nb = B (en)\n", NULL);
    I18nCatalog *french = parse_text("a = A (fr)\n", NULL);
    i18n_set_catalogs(french, english);

    CHECK_STR_EQ(tr("a"), "A (fr)");
    CHECK_STR_EQ(tr("b"), "B (en)");
    CHECK_STR_EQ(tr("c.missing"), "c.missing");
    CHECK_STR_EQ(tr(NULL), "");
    CHECK(i18n_active_catalog() == french);

    i18n_set_catalogs(NULL, NULL);
    CHECK_STR_EQ(tr("a"), "a");
    i18n_catalog_destroy(french);
    i18n_catalog_destroy(english);
}

static void test_tr_plural_uses_rule_of_providing_catalog(void)
{
    I18nCatalog *english = parse_text("language.plural_rule = one_other\n"
                                      "tree.one = {0} tree\n"
                                      "tree.other = {0} trees\n"
                                      "rock.one = {0} rock\n"
                                      "rock.other = {0} rocks\n",
                                      NULL);
    I18nCatalog *french = parse_text("language.plural_rule = zero_one_other\n"
                                     "tree.one = {0} arbre\n"
                                     "tree.other = {0} arbres\n",
                                     NULL);
    i18n_set_catalogs(french, english);

    CHECK_STR_EQ(tr_plural("tree", 0), "{0} arbre");
    CHECK_STR_EQ(tr_plural("tree", 1), "{0} arbre");
    CHECK_STR_EQ(tr_plural("tree", 2), "{0} arbres");
    /* Missing in French: English text with the English rule (0 -> other). */
    CHECK_STR_EQ(tr_plural("rock", 0), "{0} rocks");
    CHECK_STR_EQ(tr_plural("rock", 1), "{0} rock");
    CHECK_STR_EQ(tr_plural("nothing", 3), "nothing");

    i18n_set_catalogs(NULL, NULL);
    i18n_catalog_destroy(french);
    i18n_catalog_destroy(english);
}

static void test_tr_plural_falls_back_to_other_form(void)
{
    I18nCatalog *catalog = parse_text("item.other = items\n", NULL);
    i18n_set_catalogs(catalog, NULL);
    CHECK_STR_EQ(tr_plural("item", 1), "items");
    i18n_set_catalogs(NULL, NULL);
    i18n_catalog_destroy(catalog);
}

/* ---- Formatting ---- */

static void test_format_positional_and_reordered(void)
{
    char out[128];
    size_t length = TR_FORMAT(out, sizeof(out), "{0} and {1}", "A", "B");
    CHECK_STR_EQ(out, "A and B");
    CHECK_EQ_UINT(length, 7);

    TR_FORMAT(out, sizeof(out), "{1}, then {0}, then {1}", "first", "second");
    CHECK_STR_EQ(out, "second, then first, then second");
}

static void test_format_escaped_braces_and_missing_args(void)
{
    char out[128];
    TR_FORMAT(out, sizeof(out), "{{0}} {0} }} {", "x");
    CHECK_STR_EQ(out, "{0} x } {");

    TR_FORMAT(out, sizeof(out), "{0} {2} {x}", "a", "b");
    CHECK_STR_EQ(out, "a {2} {x}");

    CHECK_EQ_UINT(tr_format(out, sizeof(out), "plain", 0, NULL), 5);
    CHECK_STR_EQ(out, "plain");
}

static void test_format_truncation_never_splits_utf8(void)
{
    char out[4];
    /* "a" + "é" (2 bytes) fills 3 bytes; the next "é" cannot fit. */
    size_t length = TR_FORMAT(out, sizeof(out), "a\xC3\xA9{0}", "\xC3\xA9");
    CHECK_STR_EQ(out, "a\xC3\xA9");
    CHECK_EQ_UINT(length, 5);

    char small[3];
    /* Only 2 bytes of room: "é" would be split, so only "a" is kept. */
    TR_FORMAT(small, sizeof(small), "a\xC3\xA9z", "unused");
    CHECK_STR_EQ(small, "a");

    CHECK_EQ_UINT(TR_FORMAT(NULL, 0, "{0}!", "abc"), 4);
}

static void test_placeholder_mask(void)
{
    CHECK_EQ_UINT(i18n_placeholder_mask("no placeholders"), 0);
    CHECK_EQ_UINT(i18n_placeholder_mask("{0} and {2}"), 0x5);
    CHECK_EQ_UINT(i18n_placeholder_mask("{{0}} escaped, {1} real"), 0x2);
    CHECK_EQ_UINT(i18n_placeholder_mask("{9}{9}"), 1u << 9);
    CHECK_EQ_UINT(i18n_placeholder_mask(NULL), 0);
}

/* ---- Numbers ---- */

static void test_number_formatting_follows_language(void)
{
    char out[64];
    I18nCatalog *english = parse_text("format.thousands_separator = ,\n"
                                      "format.decimal_separator = .\n",
                                      NULL);
    I18nCatalog *french = parse_text("format.thousands_separator = \xC2\xA0\n"
                                     "format.decimal_separator = ,\n",
                                     NULL);

    i18n_set_catalogs(english, english);
    tr_integer(out, sizeof(out), 1234567);
    CHECK_STR_EQ(out, "1,234,567");
    tr_integer(out, sizeof(out), -1234);
    CHECK_STR_EQ(out, "-1,234");
    tr_integer(out, sizeof(out), 999);
    CHECK_STR_EQ(out, "999");
    tr_integer(out, sizeof(out), 0);
    CHECK_STR_EQ(out, "0");
    tr_decimal(out, sizeof(out), 1234.5, 2);
    CHECK_STR_EQ(out, "1,234.50");
    tr_decimal(out, sizeof(out), -1.26, 1);
    CHECK_STR_EQ(out, "-1.3");
    tr_decimal(out, sizeof(out), 42.7, 0);
    CHECK_STR_EQ(out, "43");

    i18n_set_catalogs(french, english);
    tr_integer(out, sizeof(out), 1234567);
    CHECK_STR_EQ(out, "1\xC2\xA0"
                      "234\xC2\xA0"
                      "567");
    tr_decimal(out, sizeof(out), 1234.5, 2);
    CHECK_STR_EQ(out, "1\xC2\xA0"
                      "234,50");

    i18n_set_catalogs(NULL, NULL);
    tr_integer(out, sizeof(out), 1000);
    CHECK_STR_EQ(out, "1,000"); /* Defaults when no catalog is loaded. */

    i18n_catalog_destroy(french);
    i18n_catalog_destroy(english);
}

/* ---- Real language files ---- */

static void check_language_file(const char *code, PluralRule expected_rule)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/lang/%s.lang", COLONY_SOURCE_ASSETS_DIR, code);
    size_t length = 0;
    char *text = test_read_file(path, &length);
    REQUIRE(text != NULL);

    I18nCatalog *catalog = i18n_catalog_create();
    CHECK(i18n_catalog_parse(catalog, text, length, path));
    CHECK_STR_EQ(i18n_catalog_get(catalog, I18N_KEY_LANGUAGE_CODE), code);
    CHECK(i18n_catalog_get(catalog, I18N_KEY_LANGUAGE_NAME) != NULL);
    CHECK_EQ_INT(i18n_catalog_plural_rule(catalog), expected_rule);
    i18n_catalog_destroy(catalog);
    free(text);
}

static void test_shipped_language_files_parse_cleanly(void)
{
    check_language_file("en", PLURAL_RULE_ONE_OTHER);
    check_language_file("fr", PLURAL_RULE_ZERO_ONE_OTHER);
}

int main(void)
{
    /* Invalid input is tested on purpose: keep expected warnings out of the output. */
    log_set_min_level(LOG_LEVEL_ERROR);

    RUN_TEST(test_parse_basic_entries);
    RUN_TEST(test_parse_handles_bom_crlf_and_missing_final_newline);
    RUN_TEST(test_parse_escapes);
    RUN_TEST(test_parse_value_may_contain_equals_and_be_empty);
    RUN_TEST(test_parse_reports_invalid_lines_but_keeps_valid_ones);
    RUN_TEST(test_parse_duplicate_key_last_wins);
    RUN_TEST(test_catalog_grows_to_many_keys);
    RUN_TEST(test_key_validation);
    RUN_TEST(test_plural_categories);
    RUN_TEST(test_plural_rule_comes_from_metadata);
    RUN_TEST(test_tr_falls_back_to_fallback_then_key);
    RUN_TEST(test_tr_plural_uses_rule_of_providing_catalog);
    RUN_TEST(test_tr_plural_falls_back_to_other_form);
    RUN_TEST(test_format_positional_and_reordered);
    RUN_TEST(test_format_escaped_braces_and_missing_args);
    RUN_TEST(test_format_truncation_never_splits_utf8);
    RUN_TEST(test_placeholder_mask);
    RUN_TEST(test_number_formatting_follows_language);
    RUN_TEST(test_shipped_language_files_parse_cleanly);
    return test_summary();
}
