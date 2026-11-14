add_executable(colony_sim
    app.c
    hud.c
    localization.c
    main.c
    render.c
    text.c)

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
