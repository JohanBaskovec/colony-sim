#include "core/save_files.h"
#include "test_framework.h"

#include <string.h>

static void test_names_tell_their_kind(void)
{
    int number = -1;
    CHECK_EQ_INT(save_file_kind("save-001.sav", &number), SAVE_FILE_MANUAL);
    CHECK_EQ_INT(number, 1);
    CHECK_EQ_INT(save_file_kind("save-417.sav", &number), SAVE_FILE_MANUAL);
    CHECK_EQ_INT(number, 417);
    CHECK_EQ_INT(save_file_kind("quicksave.sav", &number), SAVE_FILE_QUICK);
    CHECK_EQ_INT(number, 0);
    CHECK_EQ_INT(save_file_kind("autosave.sav", NULL), SAVE_FILE_AUTO);
    CHECK_EQ_INT(save_file_kind("autosave-previous.sav", NULL), SAVE_FILE_AUTO_PREVIOUS);
    /* Not saves: a temporary file, other numbers, other names. */
    CHECK_EQ_INT(save_file_kind("save-001.sav.tmp", NULL), SAVE_FILE_NONE);
    CHECK_EQ_INT(save_file_kind("save-000.sav", NULL), SAVE_FILE_NONE);
    CHECK_EQ_INT(save_file_kind("save-1.sav", NULL), SAVE_FILE_NONE);
    CHECK_EQ_INT(save_file_kind("save-0a1.sav", NULL), SAVE_FILE_NONE);
    CHECK_EQ_INT(save_file_kind("save-0001.sav", NULL), SAVE_FILE_NONE);
    CHECK_EQ_INT(save_file_kind("settings.cfg", NULL), SAVE_FILE_NONE);
    CHECK_EQ_INT(save_file_kind("", NULL), SAVE_FILE_NONE);
    CHECK_EQ_INT(save_file_kind(NULL, &number), SAVE_FILE_NONE);
}

static void test_names_are_made_back(void)
{
    char name[SAVE_FILE_NAME_SIZE];
    for (int kind = SAVE_FILE_MANUAL; kind < SAVE_FILE_KIND_COUNT; kind++) {
        REQUIRE(save_file_name((SaveFileKind)kind, 42, name, sizeof(name)));
        int number = 0;
        CHECK_EQ_INT(save_file_kind(name, &number), kind);
        CHECK_EQ_INT(number, kind == SAVE_FILE_MANUAL ? 42 : 0);
    }
    REQUIRE(save_file_name(SAVE_FILE_MANUAL, 7, name, sizeof(name)));
    CHECK_STR_EQ(name, "save-007.sav");
    CHECK(!save_file_name(SAVE_FILE_MANUAL, 0, name, sizeof(name)));
    CHECK(!save_file_name(SAVE_FILE_MANUAL, SAVE_FILE_NUMBER_MAX + 1, name, sizeof(name)));
    CHECK(!save_file_name(SAVE_FILE_NONE, 1, name, sizeof(name)));
    CHECK(!save_file_name(SAVE_FILE_QUICK, 0, name, 4)); /* Does not fit. */
}

static void test_the_next_number_follows_the_highest(void)
{
    CHECK_EQ_INT(save_file_next_number(NULL, 0), 1);
    const char *names[] = {"quicksave.sav", "save-003.sav", "save-012.sav", "notes.txt",
                           "save-005.sav"};
    CHECK_EQ_INT(save_file_next_number(names, 5), 13);
    const char *full[] = {"save-999.sav"};
    CHECK_EQ_INT(save_file_next_number(full, 1), 0);
}

int main(void)
{
    RUN_TEST(test_names_tell_their_kind);
    RUN_TEST(test_names_are_made_back);
    RUN_TEST(test_the_next_number_follows_the_highest);
    return test_summary();
}
