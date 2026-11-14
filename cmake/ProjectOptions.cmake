# Compiler warnings and sanitizers, applied only to our own targets
# (never to third-party dependencies such as SDL).

function(colony_configure_target target)
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
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()
