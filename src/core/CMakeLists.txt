add_library(colony_core STATIC
    camera.c
    cli.c
    colonist.c
    designation.c
    game.c
    game_clock.c
    i18n.c
    log.c
    map.c
    noise.c
    pathfinding.c
    pool.c
    rng.c
    util.c
    worldgen.c)

target_include_directories(colony_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(colony_core PUBLIC COLONY_VERSION="${PROJECT_VERSION}")

if(UNIX)
    target_link_libraries(colony_core PUBLIC m)
endif()

colony_configure_target(colony_core)
