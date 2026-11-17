# Verifies that every translation key referenced in the source code exists in
# the reference language file.
#
# Usage: cmake -DSOURCE_DIR=<src dir> -DLANG_FILE=<en.lang> -P CheckTranslationKeys.cmake
#
# Detected references (the key must be a string literal):
#   tr("some.key")          -> "some.key" must exist
#   N_("some.key")          -> "some.key" must exist
#   tr_plural("some.key"    -> "some.key.other" must exist
#   NP_("some.key")         -> "some.key.other" must exist (a plural stored in a table)
#
# Keys built at runtime cannot be checked: store them in tables wrapped in N_().

cmake_minimum_required(VERSION 3.25)

if(NOT SOURCE_DIR OR NOT LANG_FILE)
    message(FATAL_ERROR "SOURCE_DIR and LANG_FILE must be defined")
endif()

# Collect the keys defined in the language file.
file(STRINGS "${LANG_FILE}" lang_lines ENCODING UTF-8)
set(defined_keys "")
foreach(line IN LISTS lang_lines)
    if(line MATCHES "^[ \t]*([a-z0-9_.-]+)[ \t]*=")
        list(APPEND defined_keys "${CMAKE_MATCH_1}")
    endif()
endforeach()

file(GLOB_RECURSE source_files "${SOURCE_DIR}/*.c" "${SOURCE_DIR}/*.h")

set(missing_count 0)
set(reference_count 0)
foreach(source_file IN LISTS source_files)
    file(READ "${source_file}" content)
    string(REGEX MATCHALL "[^A-Za-z0-9_](tr|tr_plural|N_|NP_)\\(\"[a-z0-9_.-]+\"" matches "${content}")
    foreach(match IN LISTS matches)
        string(REGEX REPLACE "^.(tr|tr_plural|N_|NP_)\\(\"([a-z0-9_.-]+)\"$" "\\1" function "${match}")
        string(REGEX REPLACE "^.(tr|tr_plural|N_|NP_)\\(\"([a-z0-9_.-]+)\"$" "\\2" key "${match}")
        if(function STREQUAL "tr_plural" OR function STREQUAL "NP_")
            set(key "${key}.other")
        endif()
        math(EXPR reference_count "${reference_count} + 1")
        if(NOT key IN_LIST defined_keys)
            file(RELATIVE_PATH relative_path "${SOURCE_DIR}" "${source_file}")
            message(SEND_ERROR "${relative_path}: key '${key}' is not defined in ${LANG_FILE}")
            math(EXPR missing_count "${missing_count} + 1")
        endif()
    endforeach()
endforeach()

if(missing_count GREATER 0)
    message(FATAL_ERROR "${missing_count} translation key(s) missing")
endif()
message(STATUS "${reference_count} translation key reference(s) checked, all defined")
