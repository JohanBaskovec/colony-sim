cmake_minimum_required(VERSION 3.25)

project(ColonySim
    VERSION 0.1.0
    DESCRIPTION "A colony simulation game written in C with SDL3"
    LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(COLONY_BUILD_GAME "Build the SDL3 game executable (fetches SDL3 if needed)" ON)
option(COLONY_BUILD_TESTS "Build the unit tests" ON)
option(COLONY_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(COLONY_ENABLE_SANITIZERS "Enable AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

include(cmake/ProjectOptions.cmake)

# Simulation core: pure C, no SDL dependency, fully unit-testable.
add_subdirectory(src/core)

if(COLONY_BUILD_GAME)
    include(cmake/Dependencies.cmake)
    add_subdirectory(src/game)
endif()

if(COLONY_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
