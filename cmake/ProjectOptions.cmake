# Compiler warnings and sanitizers, applied only to our own targets
# (never to third-party dependencies such as SDL).

function(colony_configure_target target)
    # SDL3_ttf caches CMAKE_POSITION_INDEPENDENT_CODE=ON: our code is never
    # part of a shared library, and -fPIC keeps the compiler from inlining
    # exported functions (a slower simulation). Executables stay PIE (the
    # toolchain's default).
    set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE OFF)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /utf-8)
        if(COLONY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall
            -Wextra
            -Wpedantic
            -Wshadow
            -Wstrict-prototypes
            -Wmissing-prototypes
            -Wvla
            -Wformat=2
            -Wundef)
        if(COLONY_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(COLONY_ENABLE_SANITIZERS)
            # -O1 in Debug, as AddressSanitizer's documentation advises: the
            # tests run twice as fast as unoptimized, the same errors caught.
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined
                $<$<CONFIG:Debug>:-O1>)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
