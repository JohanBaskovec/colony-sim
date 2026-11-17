add_library(colony_core STATIC
    alert.c
    animal.c
    appearance.c
    attributes.c
    bed.c
    camera.c
    cli.c
    entry_strip.c
    exploration.c
    colonist.c
    construction.c
    conversation.c
    designation.c
    dirt.c
    game.c
    game_clock.c
    gather_zone.c
    hearth.c
    hauling.c
    i18n.c
    item.c
    known.c
    knowledge.c
    log.c
    map.c
    mood.c
    noise.c
    notice.c
    pathfinding.c
    pool.c
    recipe.c
    region.c
    roof.c
    scenario.c
    settings.c
    temperature.c
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
