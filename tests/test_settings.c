#include "core/settings.h"
#include "test_framework.h"

#include <stdio.h>
#include <string.h>

static void parse(Settings *settings, const char *text)
{
    settings_parse(settings, text, strlen(text));
}

static void test_defaults(void)
{
    Settings settings;
    settings_default(&settings);
    CHECK_EQ_INT(settings.ui_scale, 100);
    CHECK_EQ_INT(settings.palette, SETTINGS_PALETTE_STANDARD);
    CHECK_STR_EQ(settings.language, ""); /* The system's. */
    CHECK(!settings.skip_menu);
}

static void test_parse_reads_every_key(void)
{
    Settings settings;
    settings_default(&settings);
    parse(&settings, "# A comment\n"
                     "ui_scale = 150\n"
                     "  palette=colorblind  \r\n"
                     "\n"
                     "language = pt_BR\n"
                     "SKIP_MENU=1\n");
    CHECK(settings.skip_menu);
    CHECK_EQ_INT(settings.ui_scale, 150);
    CHECK_EQ_INT(settings.palette, SETTINGS_PALETTE_COLORBLIND);
    CHECK_STR_EQ(settings.language, "pt_BR");
}

static void test_parse_ignores_what_it_does_not_understand(void)
{
    Settings settings;
    settings_default(&settings);
    snprintf(settings.language, sizeof(settings.language), "fr");
    parse(&settings, "ui_scale = 500\n"                        /* Out of range. */
                     "ui_scale = big\n"                        /* Not a number. */
                     "ui_scale = 120abc\n"                     /* Trailing garbage. */
                     "palette = purple\n"                      /* Unknown palette. */
                     "language = ../../etc\n"                  /* Not a language code. */
                     "language = abcdefghijklmnopqrstuvwxyz\n" /* Too long. */
                     "volume = 11\n"                           /* Unknown key. */
                     "no equals sign here\n");
    CHECK_EQ_INT(settings.ui_scale, 100);
    CHECK_EQ_INT(settings.palette, SETTINGS_PALETTE_STANDARD);
    CHECK_STR_EQ(settings.language, "fr");
}

static void test_parse_stops_at_the_given_length(void)
{
    Settings settings;
    settings_default(&settings);
    const char *text = "ui_scale = 125\nui_scale = 175\n";
    settings_parse(&settings, text, strlen("ui_scale = 125\n"));
    CHECK_EQ_INT(settings.ui_scale, 125);
    /* A last line without its newline still counts. */
    settings_parse(&settings, "ui_scale = 75", strlen("ui_scale = 75"));
    CHECK_EQ_INT(settings.ui_scale, 75);
}

static void test_empty_language_means_the_system_one(void)
{
    Settings settings;
    settings_default(&settings);
    snprintf(settings.language, sizeof(settings.language), "fr");
    parse(&settings, "language =\n");
    CHECK_STR_EQ(settings.language, "");
}

static void test_format_round_trips(void)
{
    Settings settings;
    settings_default(&settings);
    settings.ui_scale = 175;
    settings.palette = SETTINGS_PALETTE_COLORBLIND;
    snprintf(settings.language, sizeof(settings.language), "fr");
    settings.skip_menu = true;
    char text[512];
    size_t length = settings_format(&settings, text, sizeof(text));
    REQUIRE(length > 0 && length < sizeof(text));
    CHECK_EQ_INT((int)strlen(text), (int)length);

    Settings read;
    settings_default(&read);
    settings_parse(&read, text, length);
    CHECK_EQ_INT(read.ui_scale, 175);
    CHECK_EQ_INT(read.palette, SETTINGS_PALETTE_COLORBLIND);
    CHECK_STR_EQ(read.language, "fr");
    CHECK(read.skip_menu);
}

static void test_skip_menu_values(void)
{
    Settings settings;
    settings_default(&settings);
    parse(&settings, "skip_menu = yes\n");
    CHECK(settings.skip_menu);
    parse(&settings, "skip_menu = maybe\n"); /* Not a value: unchanged. */
    CHECK(settings.skip_menu);
    parse(&settings, "Skip_Menu = FALSE\n");
    CHECK(!settings.skip_menu);
    parse(&settings, "skip_menu = on\n");
    CHECK(settings.skip_menu);
    parse(&settings, "skip_menu = 0\n");
    CHECK(!settings.skip_menu);
}

static void test_format_reports_the_size_needed(void)
{
    Settings settings;
    settings_default(&settings);
    char full[512], small[8];
    size_t length = settings_format(&settings, full, sizeof(full));
    CHECK_EQ_INT((int)settings_format(&settings, small, sizeof(small)), (int)length);
    CHECK_EQ_INT((int)strlen(small), (int)sizeof(small) - 1); /* Cut short, terminated. */
}

static void test_palette_ids(void)
{
    CHECK_STR_EQ(settings_palette_id(SETTINGS_PALETTE_STANDARD), "standard");
    CHECK_STR_EQ(settings_palette_id(SETTINGS_PALETTE_COLORBLIND), "colorblind");
    CHECK(settings_palette_id(SETTINGS_PALETTE_COUNT) == NULL);
    for (int palette = 0; palette < SETTINGS_PALETTE_COUNT; palette++) {
        CHECK(settings_palette_name_key((SettingsPalette)palette) != NULL);
    }
}

int main(void)
{
    RUN_TEST(test_defaults);
    RUN_TEST(test_parse_reads_every_key);
    RUN_TEST(test_parse_ignores_what_it_does_not_understand);
    RUN_TEST(test_parse_stops_at_the_given_length);
    RUN_TEST(test_empty_language_means_the_system_one);
    RUN_TEST(test_format_round_trips);
    RUN_TEST(test_format_reports_the_size_needed);
    RUN_TEST(test_skip_menu_values);
    RUN_TEST(test_palette_ids);
    return test_summary();
}
