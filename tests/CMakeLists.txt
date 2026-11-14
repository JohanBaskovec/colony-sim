# Unit tests: one executable per core module, registered with CTest.
# They link only colony_core, so they run headless (no SDL, no display).

set(COLONY_UNIT_TESTS
    test_camera
    test_cli
    test_colonist
    test_designation
    test_game
    test_game_clock
    test_hauling
    test_i18n
    test_item
    test_map
    test_noise
    test_pathfinding
    test_pool
    test_region
    test_reservation
    test_rng
    test_skill
    test_stockpile
    test_work
    test_worldgen)

foreach(test_name IN LISTS COLONY_UNIT_TESTS)
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE colony_core)
    target_compile_definitions(${test_name} PRIVATE
        COLONY_SOURCE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
    colony_configure_target(${test_name})
    add_test(NAME ${test_name} COMMAND ${test_name})
    set_tests_properties(${test_name} PROPERTIES LABELS "unit")
endforeach()

# --- Translations --------------------------------------------------------------

add_executable(check_translations check_translations.c)
target_link_libraries(check_translations PRIVATE colony_core)
colony_configure_target(check_translations)

set(COLONY_REFERENCE_LANG "${PROJECT_SOURCE_DIR}/assets/lang/en.lang")
file(GLOB COLONY_LANG_FILES CONFIGURE_DEPENDS "${PROJECT_SOURCE_DIR}/assets/lang/*.lang")

# Every language file (English included) is checked against the reference.
foreach(lang_file IN LISTS COLONY_LANG_FILES)
    get_filename_component(lang_code "${lang_file}" NAME_WE)
    add_test(NAME translations_${lang_code}
        COMMAND check_translations "${COLONY_REFERENCE_LANG}" "${lang_file}")
    set_tests_properties(translations_${lang_code} PROPERTIES LABELS "i18n")
endforeach()

# Every key used in the source code (tr, tr_plural, N_) must exist in en.lang.
add_test(NAME translation_keys_used_in_code
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${PROJECT_SOURCE_DIR}/src
        -DLANG_FILE=${COLONY_REFERENCE_LANG}
        -P ${PROJECT_SOURCE_DIR}/cmake/CheckTranslationKeys.cmake)
set_tests_properties(translation_keys_used_in_code PROPERTIES LABELS "i18n")
