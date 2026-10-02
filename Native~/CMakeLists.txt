cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../package.json" package_json)
string(JSON package_version GET "${package_json}" version)

project(PSXReverb VERSION ${package_version} LANGUAGES C)

option(PSX_REVERB_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(PSX_REVERB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PSX_REVERB_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(MSVC AND PSX_REVERB_SANITIZE)
    message(STATUS "Sanitizers are not set up for MSVC; building without them")
endif()

function(psx_reverb_configure target)
    target_compile_definitions(${target} PRIVATE
        PSX_REVERB_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
        PSX_REVERB_VERSION_MINOR=${PROJECT_VERSION_MINOR}
        PSX_REVERB_VERSION_PATCH=${PROJECT_VERSION_PATCH})

    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
        if(PSX_REVERB_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow -Wstrict-prototypes -Wmissing-prototypes)
        if(PSX_REVERB_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(PSX_REVERB_SANITIZE)
            target_compile_options(${target} PRIVATE
                -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer)
            target_link_options(${target} PRIVATE -fsanitize=address,undefined)
        endif()
    endif()
endfunction()

add_library(psxreverb_core STATIC src/psx_reverb.c src/presets.c src/reverb.c src/unit.c)
target_include_directories(psxreverb_core PUBLIC include)
# Linked into the shared library Unity loads, which Linux and Android refuse without position-independent code.
set_target_properties(psxreverb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
psx_reverb_configure(psxreverb_core)

if(PSX_REVERB_TESTS)
    enable_testing()
    add_executable(psxreverb_tests tests/main.c tests/reverb_tests.c tests/unit_tests.c tests/model_tests.c)
    target_link_libraries(psxreverb_tests PRIVATE psxreverb_core)
    # The tests reach the core's internals, which the Unity wrapper will not.
    target_include_directories(psxreverb_tests PRIVATE src)
    # MSVC calls fopen unsafe, which warnings as errors would turn into a failed build.
    if(MSVC)
        target_compile_definitions(psxreverb_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    psx_reverb_configure(psxreverb_tests)
    add_test(NAME psxreverb_tests COMMAND psxreverb_tests)
endif()
