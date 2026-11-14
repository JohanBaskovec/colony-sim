add_library(colony_core STATIC
    appearance.c
    camera.c
    cli.c
    colonist.c
    construction.c
    designation.c
    dirt.c
    game.c
    game_clock.c
    gather_zone.c
    hauling.c
    i18n.c
    item.c
    knowledge.c
    log.c
    map.c
    noise.c
    pathfinding.c
    pool.c
    recipe.c
    region.c
    reservation.c
    rng.c
    stockpile.c
    structure.c
    util.c
    work.c
    workstation.c
    worldgen.c)

target_include_directories(colony_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(colony_core PUBLIC COLONY_VERSION="${PROJECT_VERSION}")

if(UNIX)
    target_link_libraries(colony_core PUBLIC m)
endif()

colony_configure_target(colony_core)
