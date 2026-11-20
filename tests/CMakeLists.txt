# Unit tests: one executable per core module, registered with CTest.
# They link only colony_core, so they run headless (no SDL, no display).

set(COLONY_UNIT_TESTS
    test_alert
    test_animal
    test_appearance
    test_attributes
    test_bed
    test_blood
    test_carry
    test_weapon
    test_journal
    test_camera
    test_cli
    test_crafting
    test_debug
    test_entry_strip
    test_colonist
    test_combat
    test_construction
    test_corpse
    test_designation
    test_dirt
    test_fire_light
    test_forbid
    test_game
    test_game_clock
    test_gather_zone
    test_hauling
    test_i18n
    test_item
    test_item_move
    test_item_simulation
    test_knowledge
    test_learning
    test_levels
    test_health
    test_hearth
    test_map
    test_mood
    test_new_game
    test_noise
    test_occupancy
    test_pathfinding
    test_pool
    test_priority
    test_recipe
    test_region
    test_roof
    test_social
    test_strike_anim
    test_test_scenario
    test_vision
    test_fog
    test_scenario
    test_settings
    test_shadow
    test_sky
    test_reservation
    test_rng
    test_soil
    test_stockpile
    test_temperature
    test_tool
    test_tree
    test_util
    test_weather
    test_structures
    test_sun
    test_tile_light
    test_work
    test_workstation
    test_worldgen)

# The slowest files (several seconds under the sanitizers): each of their
# test functions is a CTest entry of its own (`test_game.test_plants_grow`),
# run with COLONY_TEST_ONLY so that they spread over the cores. Their names
# are read from the RUN_TEST lines; the file is a configure dependency, so a
# new test is picked up at the next build.
set(COLONY_SPLIT_TESTS
    test_animal
    test_colonist
    test_crafting
    test_game
    test_health
    test_item_simulation
    test_tree
    test_vision
    test_worldgen)

foreach(test_name IN LISTS COLONY_UNIT_TESTS)
    add_executable(${test_name} ${test_name}.c)
    target_link_libraries(${test_name} PRIVATE colony_core)
    target_compile_definitions(${test_name} PRIVATE
        COLONY_SOURCE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")
    colony_configure_target(${test_name})
    if(test_name IN_LIST COLONY_SPLIT_TESTS)
        set(source "${CMAKE_CURRENT_SOURCE_DIR}/${test_name}.c")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${source}")
        file(STRINGS "${source}" run_lines REGEX "^[ \t]*RUN_TEST\\([A-Za-z0-9_]+\\);")
        set(case_count 0)
        foreach(line IN LISTS run_lines)
            string(REGEX REPLACE "^[ \t]*RUN_TEST\\(([A-Za-z0-9_]+)\\);.*$" "\\1" case "${line}")
            add_test(NAME ${test_name}.${case} COMMAND ${test_name})
            set_tests_properties(${test_name}.${case} PROPERTIES
                LABELS "unit" ENVIRONMENT "COLONY_TEST_ONLY=${case}")
            math(EXPR case_count "${case_count} + 1")
        endforeach()
        if(case_count EQUAL 0)
            message(FATAL_ERROR "${test_name}.c: no RUN_TEST line found")
        endif()
    else()
        add_test(NAME ${test_name} COMMAND ${test_name})
        set_tests_properties(${test_name} PROPERTIES LABELS "unit")
    endif()
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
