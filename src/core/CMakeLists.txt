add_library(colony_core STATIC
    alert.c
    animal.c
    appearance.c
    attributes.c
    bed.c
    blood.c
    camera.c
    combat.c
    cli.c
    entry_strip.c
    experience.c
    exploration.c
    colonist.c
    construction.c
    conversation.c
    corpse.c
    journal.c
    designation.c
    dirt.c
    game.c
    game_clock.c
    gather_zone.c
    health.c
    hearth.c
    hauling.c
    i18n.c
    item.c
    item_move.c
    known.c
    knowledge.c
    log.c
    map.c
    mood.c
    noise.c
    notice.c
    occupancy.c
    pathfinding.c
    pool.c
    recipe.c
    region.c
    roof.c
    scenario.c
    settings.c
    social.c
    strike_anim.c
    soil.c
    temperature.c
    test_scenario.c
    tree.c
    reservation.c
    rng.c
    stockpile.c
    tool.c
    structure.c
    util.c
    vision.c
    weather.c
    work.c
    workstation.c
    worldgen.c)

target_include_directories(colony_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(colony_core PUBLIC COLONY_VERSION="${PROJECT_VERSION}")

if(UNIX)
    target_link_libraries(colony_core PUBLIC m)
endif()

colony_configure_target(colony_core)
