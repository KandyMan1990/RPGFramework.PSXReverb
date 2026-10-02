cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../package.json" package_json)
string(JSON package_version GET "${package_json}" version)

project(PSXReverb VERSION ${package_version} LANGUAGES C)

option(PSX_REVERB_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(PSX_REVERB_TOOLS "Build the command-line tools" ${PROJECT_IS_TOP_LEVEL})
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

add_library(psxreverb_core STATIC src/psx_reverb.c src/presets.c src/reverb.c src/unit.c src/resampler.c src/host.c)
target_include_directories(psxreverb_core PUBLIC include)
# Linked into the shared library Unity loads, which Linux and Android refuse without position-independent code.
set_target_properties(psxreverb_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# The resampler designs its filter with the maths library, which Linux and Android link separately.
if(NOT WIN32)
    target_link_libraries(psxreverb_core PUBLIC m)
endif()
psx_reverb_configure(psxreverb_core)

if(PSX_REVERB_TOOLS)
    add_executable(psxreverb_render tools/render.c tools/wav.c)
    target_link_libraries(psxreverb_render PRIVATE psxreverb_core)
    # The tool, like the tests, reaches the core's internals.
    target_include_directories(psxreverb_render PRIVATE src tools)
    # MSVC calls fopen unsafe, which warnings as errors would turn into a failed build.
    if(MSVC)
        target_compile_definitions(psxreverb_render PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    psx_reverb_configure(psxreverb_render)
endif()

if(PSX_REVERB_TESTS)
    enable_testing()
    add_executable(psxreverb_tests tests/main.c tests/reverb_tests.c tests/unit_tests.c tests/model_tests.c
        tests/wav_tests.c tests/resampler_tests.c tests/host_tests.c tests/signal.c tools/wav.c)
    target_link_libraries(psxreverb_tests PRIVATE psxreverb_core)
    # The tests reach the core's internals, which the Unity wrapper will not.
    target_include_directories(psxreverb_tests PRIVATE src tools)
    if(MSVC)
        target_compile_definitions(psxreverb_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    psx_reverb_configure(psxreverb_tests)
    add_test(NAME psxreverb_tests COMMAND psxreverb_tests)

    # The tool on a file the tests write, through a second of tail, so a crash in it fails the build everywhere.
    if(PSX_REVERB_TOOLS)
        add_test(NAME psxreverb_render COMMAND psxreverb_render --tail 1 wav_round_trip.wav render_output.wav)
        set_tests_properties(psxreverb_tests PROPERTIES FIXTURES_SETUP wav_files)
        set_tests_properties(psxreverb_render PROPERTIES FIXTURES_REQUIRED wav_files)
    endif()
endif()
