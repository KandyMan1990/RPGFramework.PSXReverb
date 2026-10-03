cmake_minimum_required(VERSION 3.21)

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../package.json" package_json)
string(JSON package_version GET "${package_json}" version)

project(PSXReverb VERSION ${package_version} LANGUAGES C CXX)

option(PSX_REVERB_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(PSX_REVERB_TOOLS "Build the command-line tools" ${PROJECT_IS_TOP_LEVEL})
option(PSX_REVERB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(PSX_REVERB_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

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
            -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
            $<$<COMPILE_LANGUAGE:C>:-Wstrict-prototypes -Wmissing-prototypes>
            $<$<COMPILE_LANGUAGE:CXX>:-Wmissing-declarations>)
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

set(PSX_REVERB_CORE_SOURCES src/psx_reverb.c src/presets.c src/reverb.c src/unit.c src/resampler.c src/host.c)
add_library(psxreverb_core STATIC ${PSX_REVERB_CORE_SOURCES})
target_include_directories(psxreverb_core PUBLIC include)
# Linked into the shared library Unity loads, which Linux and Android refuse without position-independent code, and
# hidden there, so the library exports Unity's entry point alone.
set_target_properties(psxreverb_core PROPERTIES POSITION_INDEPENDENT_CODE ON C_VISIBILITY_PRESET hidden)
# The resampler designs its filter with the maths library, which Linux and Android link separately.
if(NOT WIN32)
    target_link_libraries(psxreverb_core PUBLIC m)
endif()
psx_reverb_configure(psxreverb_core)

# The Unity plugin. Unity loads a library whose name begins audioplugin, or libaudioplugin where a platform's libraries
# take that prefix: a bundle on macOS, and a static library on iOS, which cannot load one.
if(IOS)
    # A static library carries no other library with it, so the core is built into it; the archive is made below.
    add_library(audioplugin_psxreverb OBJECT unity/psx_reverb_unity.cpp ${PSX_REVERB_CORE_SOURCES})
    target_include_directories(audioplugin_psxreverb PRIVATE include)
    target_compile_definitions(audioplugin_psxreverb PRIVATE PSX_REVERB_STATIC_PLUGIN)
else()
    add_library(audioplugin_psxreverb MODULE unity/psx_reverb_unity.cpp)
    target_link_libraries(audioplugin_psxreverb PRIVATE psxreverb_core)
endif()
# Unity's header as Unity publishes it; its warnings are not ours to fix.
target_include_directories(audioplugin_psxreverb SYSTEM PRIVATE unity/sdk)
set_target_properties(audioplugin_psxreverb PROPERTIES
    C_VISIBILITY_PRESET hidden CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON)
# Android packs a native library only if its name begins lib, and Linux names them so too.
if(WIN32 OR (APPLE AND NOT IOS))
    set_target_properties(audioplugin_psxreverb PROPERTIES PREFIX "")
endif()
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # The C++ runtime is linked in, so a player needs no particular one, and kept out of what the library exports.
    target_link_options(audioplugin_psxreverb PRIVATE -Wl,--exclude-libs,ALL)
endif()
# The sanitizers bring a C++ runtime of their own, so a debug build keeps the system's.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND NOT PSX_REVERB_SANITIZE)
    target_link_options(audioplugin_psxreverb PRIVATE -static-libstdc++ -static-libgcc)
endif()
if(MSVC)
    # The linker stamps the time into a library, so every commit run would replace the Windows plugins unchanged;
    # /Brepro stamps a hash of the contents instead.
    target_link_options(audioplugin_psxreverb PRIVATE /Brepro)
endif()
if(ANDROID)
    # Google Play requires native libraries aligned to 16 KB pages from Android 15.
    target_link_options(audioplugin_psxreverb PRIVATE -Wl,-z,max-page-size=16384)
endif()
if(APPLE AND NOT IOS)
    # A loadable bundle's Info.plist, as Unity's own demo plugin has; CMake's default is an application's.
    set_target_properties(audioplugin_psxreverb PROPERTIES
        BUNDLE ON
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/unity/Info.plist.in"
        MACOSX_BUNDLE_GUI_IDENTIFIER com.rpgframework.psxreverb
        MACOSX_BUNDLE_BUNDLE_NAME audioplugin_psxreverb
        MACOSX_BUNDLE_BUNDLE_VERSION ${PROJECT_VERSION}
        MACOSX_BUNDLE_SHORT_VERSION_STRING ${PROJECT_VERSION}
        # A new Info.plist relinks, so the signing after the link runs again and covers it.
        LINK_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/unity/Info.plist.in"
        # Installing would otherwise rewrite the library's paths, which breaks its signature.
        BUILD_WITH_INSTALL_RPATH ON)
    # The linker signs the code alone; signing the bundle too seals its Info.plist, as macOS expects of a bundle.
    add_custom_command(TARGET audioplugin_psxreverb POST_BUILD
        COMMAND codesign --force --sign - "$<TARGET_BUNDLE_DIR:audioplugin_psxreverb>")
endif()
psx_reverb_configure(audioplugin_psxreverb)

if(IOS)
    # An app links every static library into itself, where two global names alike collide, and an archive leaves every
    # name in it global, hidden or not. So each CPU's objects are first linked into one object that keeps the entry
    # point alone global, and those are archived.
    set(ios_entry_point _psx_reverb_get_audio_effect_definitions)
    set(ios_archive "${CMAKE_CURRENT_BINARY_DIR}/libaudioplugin_psxreverb.a")
    if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        set(ios_simulator -simulator)
    endif()
    set(ios_prelink)
    set(ios_slices)
    foreach(arch IN LISTS CMAKE_OSX_ARCHITECTURES)
        set(slice "${CMAKE_CURRENT_BINARY_DIR}/audioplugin_psxreverb_${arch}.o")
        # Through the compiler, which marks the object as for a device or for the Simulator; the linker alone drops
        # the mark from objects built for more than one CPU, and Xcode then has to guess.
        list(APPEND ios_prelink COMMAND xcrun --sdk ${CMAKE_OSX_SYSROOT} clang++
            -target ${arch}-apple-ios${CMAKE_OSX_DEPLOYMENT_TARGET}${ios_simulator} -r -nostdlib
            -Wl,-exported_symbol,${ios_entry_point} -o "${slice}" "$<TARGET_OBJECTS:audioplugin_psxreverb>")
        list(APPEND ios_slices "${slice}")
    endforeach()
    # -D leaves out the times and owners an archive records, so an unchanged library builds to the same bytes and the
    # workflow's commit run finds nothing to commit.
    add_custom_command(OUTPUT "${ios_archive}"
        ${ios_prelink}
        COMMAND "${CMAKE_COMMAND}" -E rm -f "${ios_archive}"
        COMMAND xcrun libtool -static -D -o "${ios_archive}" ${ios_slices}
        DEPENDS "$<TARGET_OBJECTS:audioplugin_psxreverb>"
        COMMAND_EXPAND_LISTS VERBATIM)
    add_custom_target(audioplugin_psxreverb_archive ALL DEPENDS "${ios_archive}")

    # The registration Unity copies into a game's Xcode project, linked here against the archive, so a name the two
    # disagree on fails this build rather than a game's. Unity's own function is left to the app.
    enable_language(OBJCXX)
    add_library(psxreverb_ios_registration SHARED unity/ios/PSXReverbRegistration.mm)
    target_compile_options(psxreverb_ios_registration PRIVATE -fobjc-arc)
    target_link_libraries(psxreverb_ios_registration PRIVATE "${ios_archive}" "-framework Foundation" objc)
    target_link_options(psxreverb_ios_registration PRIVATE "LINKER:-U,_UnityRegisterAudioPlugin")
    add_dependencies(psxreverb_ios_registration audioplugin_psxreverb_archive)
    psx_reverb_configure(psxreverb_ios_registration)
endif()

# cmake --install with the package root as its prefix puts the plugin where the package carries it, with the .meta files
# that tell Unity which platform and CPU it is for, kept under unity/package.
if(IOS)
    # A game's Xcode project builds for devices or for the Simulator, and each needs a library of its own.
    if(CMAKE_OSX_SYSROOT MATCHES "[Ss]imulator")
        set(plugin_dirs iOS iOS/Simulator)
    else()
        set(plugin_dirs iOS iOS/Device)
    endif()
elseif(APPLE)
    set(plugin_dirs macOS)
elseif(ANDROID)
    set(plugin_dirs Android Android/${ANDROID_ABI})
elseif(WIN32)
    set(windows_x64 x86_64)
    set(windows_ARM64 ARM64)
    set(windows_X86 x86)
    set(plugin_dirs Windows Windows/${windows_${CMAKE_C_COMPILER_ARCHITECTURE_ID}})
else()
    set(plugin_dirs Linux Linux/x86_64)
endif()
list(GET plugin_dirs -1 plugin_dir)
set(package_meta "${CMAKE_CURRENT_SOURCE_DIR}/unity/package")
if(IOS)
    install(FILES "${ios_archive}" DESTINATION Runtime/Plugins/${plugin_dir})
    install(FILES unity/ios/PSXReverbRegistration.mm "${package_meta}/Runtime/Plugins/iOS/PSXReverbRegistration.mm.meta"
        DESTINATION Runtime/Plugins/iOS)
else()
    install(TARGETS audioplugin_psxreverb
        LIBRARY DESTINATION Runtime/Plugins/${plugin_dir}
        ARCHIVE DESTINATION Runtime/Plugins/${plugin_dir})
endif()
install(FILES "${package_meta}/Runtime.meta" DESTINATION .)
install(FILES "${package_meta}/Runtime/Plugins.meta" DESTINATION Runtime)
foreach(dir IN LISTS plugin_dirs)
    get_filename_component(parent "Runtime/Plugins/${dir}" DIRECTORY)
    install(FILES "${package_meta}/Runtime/Plugins/${dir}.meta" DESTINATION ${parent})
endforeach()
if(IOS)
    set(plugin_file libaudioplugin_psxreverb.a)
elseif(APPLE)
    set(plugin_file audioplugin_psxreverb.bundle)
elseif(WIN32)
    set(plugin_file audioplugin_psxreverb.dll)
else()
    set(plugin_file libaudioplugin_psxreverb.so)
endif()
install(FILES "${package_meta}/Runtime/Plugins/${plugin_dir}/${plugin_file}.meta"
    DESTINATION Runtime/Plugins/${plugin_dir})

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
    add_executable(psxreverb_tests tests/main.c tests/check.c tests/reverb_tests.c tests/unit_tests.c
        tests/model_tests.c tests/wav_tests.c tests/resampler_tests.c tests/host_tests.c tests/api_tests.c
        tests/signal.c tools/wav.c)
    target_link_libraries(psxreverb_tests PRIVATE psxreverb_core)
    # The tests reach the core's internals, which the Unity wrapper will not.
    target_include_directories(psxreverb_tests PRIVATE src tools)
    if(MSVC)
        target_compile_definitions(psxreverb_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    psx_reverb_configure(psxreverb_tests)
    add_test(NAME psxreverb_tests COMMAND psxreverb_tests)

    # Unity's part played against the built plugin, loaded as Unity loads it.
    add_executable(psxreverb_unity_tests tests/unity_tests.cpp tests/check.c tests/signal.c)
    target_link_libraries(psxreverb_unity_tests PRIVATE psxreverb_core ${CMAKE_DL_LIBS})
    target_include_directories(psxreverb_unity_tests SYSTEM PRIVATE unity/sdk)
    add_dependencies(psxreverb_unity_tests audioplugin_psxreverb)
    psx_reverb_configure(psxreverb_unity_tests)
    add_test(NAME psxreverb_unity_tests COMMAND psxreverb_unity_tests $<TARGET_FILE:audioplugin_psxreverb>)

    # The tool on a file the tests write, through a second of tail, so a crash in it fails the build everywhere.
    if(PSX_REVERB_TOOLS)
        add_test(NAME psxreverb_render COMMAND psxreverb_render --tail 1 wav_round_trip.wav render_output.wav)
        set_tests_properties(psxreverb_tests PROPERTIES FIXTURES_SETUP wav_files)
        set_tests_properties(psxreverb_render PROPERTIES FIXTURES_REQUIRED wav_files)
    endif()
endif()
