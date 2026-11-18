add_executable(colony_sim
    app.c
    alerts_list.c
    animal_panel.c
    attributes_panel.c
    animals_panel.c
    bottom_bar.c
    build_menu.c
    colonist_bar.c
    debug_panel.c
    game_menu.c
    gather_zone_panel.c
    health_panel.c
    hud.c
    inspector.c
    items_panel.c
    layout.c
    log_window.c
    localization.c
    main.c
    render.c
    resources_list.c
    settings_window.c
    sprites.c
    station_panel.c
    stockpile_panel.c
    text.c
    theme.c
    tile_panel.c
    ui.c
    ui_layout.c
    view_controls.c
    work_panel.c)

target_link_libraries(colony_sim PRIVATE colony_core SDL3_ttf::SDL3_ttf SDL3::SDL3)

# Fallback used in development when the copied assets are not found next to
# the executable.
target_compile_definitions(colony_sim PRIVATE
    COLONY_SOURCE_ASSETS_DIR="${PROJECT_SOURCE_DIR}/assets")

set_target_properties(colony_sim PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")

colony_configure_target(colony_sim)

# Ship the assets next to the executable (bin/assets).
add_custom_command(TARGET colony_sim POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${PROJECT_SOURCE_DIR}/assets" "$<TARGET_FILE_DIR:colony_sim>/assets"
    COMMENT "Copying assets")
