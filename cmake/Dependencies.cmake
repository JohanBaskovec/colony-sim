# Third-party dependencies for the game executable.
#
# SDL3 and SDL3_ttf are taken from the system when available, otherwise they are
# downloaded and built from source with FetchContent (static libraries).

include(FetchContent)

set(COLONY_SDL3_VERSION "3.4.20" CACHE STRING "SDL3 release fetched when not found on the system")
set(COLONY_SDL3_TTF_VERSION "3.2.2" CACHE STRING "SDL3_ttf release fetched when not found on the system")

# --- SDL3 --------------------------------------------------------------------
find_package(SDL3 3.2 CONFIG QUIET)
if(SDL3_FOUND)
    message(STATUS "Using system SDL3 ${SDL3_VERSION}")
else()
    message(STATUS "SDL3 not found on the system: fetching release-${COLONY_SDL3_VERSION}")
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
    set(SDL_TESTS OFF CACHE BOOL "" FORCE)
    set(SDL_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(SDL_INSTALL OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-${COLONY_SDL3_VERSION}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        SYSTEM)
    FetchContent_MakeAvailable(SDL3)
endif()

# --- SDL3_ttf ----------------------------------------------------------------
find_package(SDL3_ttf 3.2 CONFIG QUIET)
if(SDL3_ttf_FOUND)
    message(STATUS "Using system SDL3_ttf ${SDL3_ttf_VERSION}")
else()
    message(STATUS "SDL3_ttf not found on the system: fetching release-${COLONY_SDL3_TTF_VERSION}")
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    set(SDLTTF_SAMPLES OFF CACHE BOOL "" FORCE)
    set(SDLTTF_INSTALL OFF CACHE BOOL "" FORCE)
    # HarfBuzz (complex text shaping: Arabic, Devanagari, ...) is not needed for
    # Latin/Cyrillic/Greek scripts. Enable it when such a translation is added.
    set(SDLTTF_HARFBUZZ OFF CACHE BOOL "" FORCE)
    set(SDLTTF_PLUTOSVG OFF CACHE BOOL "" FORCE)

    find_package(Freetype QUIET)
    if(FREETYPE_FOUND)
        set(SDLTTF_VENDORED OFF CACHE BOOL "" FORCE)
        set(_colony_ttf_submodules "")
    else()
        message(STATUS "FreeType not found: building the vendored copy")
        set(SDLTTF_VENDORED ON CACHE BOOL "" FORCE)
        set(_colony_ttf_submodules "external/freetype")
    endif()

    FetchContent_Declare(SDL3_ttf
        GIT_REPOSITORY https://github.com/libsdl-org/SDL_ttf.git
        GIT_TAG release-${COLONY_SDL3_TTF_VERSION}
        GIT_SHALLOW TRUE
        GIT_PROGRESS TRUE
        GIT_SUBMODULES "${_colony_ttf_submodules}"
        SYSTEM)
    FetchContent_MakeAvailable(SDL3_ttf)
endif()
