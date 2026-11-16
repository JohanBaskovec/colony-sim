add_library(colony_core STATIC
    appearance.c
    bed.c
    camera.c
    cli.c
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
    room.c
    temperature.c
    reservation.c
    rng.c
    stockpile.c
    tool.c
    structure.c
    util.c
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
