#include "core/save_files.h"

#include <stdio.h>
#include <string.h>

#define MANUAL_PREFIX "save-"
#define EXTENSION ".sav"
#define QUICK_NAME "quicksave" EXTENSION
#define AUTO_NAME "autosave" EXTENSION
#define AUTO_PREVIOUS_NAME "autosave-previous" EXTENSION
#define MANUAL_DIGITS 3

SaveFileKind save_file_kind(const char *name, int *number)
{
    if (number != NULL) {
        *number = 0;
    }
    if (name == NULL) {
        return SAVE_FILE_NONE;
    }
    if (strcmp(name, QUICK_NAME) == 0) {
        return SAVE_FILE_QUICK;
    }
    if (strcmp(name, AUTO_NAME) == 0) {
        return SAVE_FILE_AUTO;
    }
    if (strcmp(name, AUTO_PREVIOUS_NAME) == 0) {
        return SAVE_FILE_AUTO_PREVIOUS;
    }
    size_t prefix = strlen(MANUAL_PREFIX);
    if (strlen(name) != prefix + MANUAL_DIGITS + strlen(EXTENSION) ||
        strncmp(name, MANUAL_PREFIX, prefix) != 0 ||
        strcmp(name + prefix + MANUAL_DIGITS, EXTENSION) != 0) {
        return SAVE_FILE_NONE;
    }
    int value = 0;
    for (int i = 0; i < MANUAL_DIGITS; i++) {
        char c = name[prefix + (size_t)i];
        if (c < '0' || c > '9') {
            return SAVE_FILE_NONE;
        }
        value = value * 10 + (c - '0');
    }
    if (value < 1) {
        return SAVE_FILE_NONE;
    }
    if (number != NULL) {
        *number = value;
    }
    return SAVE_FILE_MANUAL;
}

bool save_file_name(SaveFileKind kind, int number, char *out, size_t size)
{
    int written = -1;
    switch (kind) {
    case SAVE_FILE_MANUAL:
        if (number < 1 || number > SAVE_FILE_NUMBER_MAX) {
            return false;
        }
        written = snprintf(out, size, MANUAL_PREFIX "%03d" EXTENSION, number);
        break;
    case SAVE_FILE_QUICK: written = snprintf(out, size, "%s", QUICK_NAME); break;
    case SAVE_FILE_AUTO: written = snprintf(out, size, "%s", AUTO_NAME); break;
    case SAVE_FILE_AUTO_PREVIOUS: written = snprintf(out, size, "%s", AUTO_PREVIOUS_NAME); break;
    default: return false;
    }
    return written > 0 && (size_t)written < size;
}

int save_file_next_number(const char *const *names, int count)
{
    int highest = 0;
    for (int i = 0; i < count; i++) {
        int number = 0;
        if (save_file_kind(names[i], &number) == SAVE_FILE_MANUAL && number > highest) {
            highest = number;
        }
    }
    return highest < SAVE_FILE_NUMBER_MAX ? highest + 1 : 0;
}
