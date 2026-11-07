cmake_minimum_required(VERSION 3.21)
project(FXAnnounce VERSION 0.3.1 LANGUAGES C CXX)

# -----------------------------------------------------------------------------
# FXAnnounce: Fenix-style Virtual Purser cabin announcements for the ToLiss Airbus family.
# Output layout (X-Plane "fat plugin"):
#   <build>/FXAnnounce/{lin_x64,win_x64,mac_x64}/FXAnnounce.xpl
# -----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_C_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()
if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "x86_64;arm64" CACHE STRING "" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "" FORCE)
endif()

option(FXA_STATIC_RUNTIME "Linux: link libstdc++/libgcc statically (release builds)" OFF)
option(FXA_PANEL_GRAPHICS "Render via Panel Graphics on XP 12.4.4+ (dynamic, OpenGL fallback)" ON)

# ---- third-party (pinned) -----------------------------------------------------
include(FetchContent)
set(FETCHCONTENT_QUIET ON)
FetchContent_Declare(xpsdk
    URL https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG v1.92.9b
    GIT_SHALLOW TRUE)
FetchContent_Declare(imgwindow
    GIT_REPOSITORY https://github.com/slgoldberg/ImgWindow.git
    GIT_TAG ef979d57cb86e50cb7e98c1036cd9fd4805eccc7)   # v2.2.2: XPLM 4.4b3+ Panel Graphics (global coords)
# libogg/libvorbis are compiled from source below (decoder only); SOURCE_SUBDIR points at a folder that
# doesn't exist so FetchContent only downloads them and never runs their own CMake.
FetchContent_Declare(ogg
    GIT_REPOSITORY https://github.com/xiph/ogg.git
    GIT_TAG v1.3.6
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _fxa_no_cmake)
FetchContent_Declare(vorbis
    GIT_REPOSITORY https://github.com/xiph/vorbis.git
    GIT_TAG v1.3.7
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _fxa_no_cmake)
FetchContent_Declare(doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG v2.4.11
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR _fxa_no_cmake)
FetchContent_MakeAvailable(xpsdk imgui imgwindow ogg vorbis doctest)

set(XPSDK_DIR ${xpsdk_SOURCE_DIR})
if(EXISTS ${XPSDK_DIR}/SDK)
    set(XPSDK_DIR ${XPSDK_DIR}/SDK)
endif()
set(FXA_XPLM_DEFS XPLM200=1 XPLM210=1 XPLM300=1 XPLM301=1 XPLM303=1 XPLM400=1 XPLM410=1 XPLM420=1 XPLM430=1)

# ---- Ogg Vorbis decoder (static, PIC, hidden symbols) ---------------------------
add_library(fxa_vorbis STATIC
    ${ogg_SOURCE_DIR}/src/bitwise.c
    ${ogg_SOURCE_DIR}/src/framing.c
    ${vorbis_SOURCE_DIR}/lib/analysis.c
    ${vorbis_SOURCE_DIR}/lib/bitrate.c
    ${vorbis_SOURCE_DIR}/lib/block.c
    ${vorbis_SOURCE_DIR}/lib/codebook.c
    ${vorbis_SOURCE_DIR}/lib/envelope.c
    ${vorbis_SOURCE_DIR}/lib/floor0.c
    ${vorbis_SOURCE_DIR}/lib/floor1.c
    ${vorbis_SOURCE_DIR}/lib/info.c
    ${vorbis_SOURCE_DIR}/lib/lookup.c
    ${vorbis_SOURCE_DIR}/lib/lpc.c
    ${vorbis_SOURCE_DIR}/lib/lsp.c
    ${vorbis_SOURCE_DIR}/lib/mapping0.c
    ${vorbis_SOURCE_DIR}/lib/mdct.c
    ${vorbis_SOURCE_DIR}/lib/psy.c
    ${vorbis_SOURCE_DIR}/lib/registry.c
    ${vorbis_SOURCE_DIR}/lib/res0.c
    ${vorbis_SOURCE_DIR}/lib/sharedbook.c
    ${vorbis_SOURCE_DIR}/lib/smallft.c
    ${vorbis_SOURCE_DIR}/lib/synthesis.c
    ${vorbis_SOURCE_DIR}/lib/vorbisfile.c
    ${vorbis_SOURCE_DIR}/lib/window.c)
target_include_directories(fxa_vorbis PUBLIC
    ${ogg_SOURCE_DIR}/include
    ${vorbis_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/compat/ogg)
target_include_directories(fxa_vorbis PRIVATE ${vorbis_SOURCE_DIR}/lib)
if(MSVC)
    target_compile_definitions(fxa_vorbis PRIVATE _CRT_SECURE_NO_WARNINGS _USE_MATH_DEFINES)
    target_compile_options(fxa_vorbis PRIVATE /W0)
else()
    target_compile_options(fxa_vorbis PRIVATE -w)
    if(NOT APPLE)
        target_link_libraries(fxa_vorbis PUBLIC m)
    endif()
endif()

# ---- sources ------------------------------------------------------------------
# Pure C++ (no XPLM, no ImGui): linked into the plugin and the unit tests.
# native/data/airlines.tsv is embedded as a string literal (one literal: keep it under MSVC's 16 KB limit).
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/data/airlines.tsv FXA_AIRLINES_TSV)
string(LENGTH "${FXA_AIRLINES_TSV}" FXA_AIRLINES_LEN)
if(FXA_AIRLINES_LEN GREATER 16000)
    message(FATAL_ERROR "data/airlines.tsv is ${FXA_AIRLINES_LEN} bytes; split the embedding in airlines_data.cpp.in")
endif()
configure_file(src/core/airlines_data.cpp.in ${CMAKE_BINARY_DIR}/generated/airlines_data.cpp @ONLY)

set(FXA_CORE_SOURCES
    src/core/aircraft.cpp
    src/core/airlines.cpp
    src/core/announcement.cpp
    src/core/config.cpp
    src/core/guard.cpp
    src/core/log.cpp
    src/core/pack_library.cpp
    src/core/pack_resolver.cpp
    src/core/purser.cpp
    src/core/recorder.cpp
    src/core/refuel.cpp
    src/core/selector.cpp
    src/core/simbrief.cpp
    src/core/sound_file.cpp
    src/core/strings.cpp
    ${CMAKE_BINARY_DIR}/generated/airlines_data.cpp
)
# Audio without XPLM (decoder, decode cache, chime, hearing model): plugin and unit tests.
set(FXA_AUDIO_SOURCES
    src/audio/chime.cpp
    src/audio/decode_cache.cpp
    src/audio/decoder.cpp
    src/audio/hearing.cpp
    src/audio/muffle.cpp
)
# Panel content (ImGui only, no XPLM, no ImgWindow): linked into the plugin and the headless UI test.
set(FXA_UI_SOURCES
    src/ui/theme.cpp
    src/ui/fonts_b612.cpp
    src/ui/widgets.cpp
    src/ui/panel.cpp
    src/ui/tab_status.cpp
    src/ui/tab_timeline.cpp
    src/ui/tab_packs.cpp
    src/ui/tab_settings.cpp
    src/ui/tab_log.cpp
)
# X-Plane glue.
set(FXA_XP_SOURCES
    src/plugin.cpp
    src/app.cpp
    src/sim/detect.cpp
    src/sim/simbrief_source.cpp
    src/sim/toliss_adapter.cpp
    src/audio/player.cpp
    src/ui/fonts.cpp
    src/ui/window.cpp
)
set(THIRD_PARTY_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgwindow_SOURCE_DIR}/ImgWindow.cpp
    ${imgwindow_SOURCE_DIR}/ImgFontAtlas.cpp
)

add_library(FXAnnounce SHARED ${FXA_CORE_SOURCES} ${FXA_AUDIO_SOURCES} ${FXA_UI_SOURCES} ${FXA_XP_SOURCES} ${THIRD_PARTY_SOURCES})

target_include_directories(FXAnnounce PRIVATE
    src
    ${imgui_SOURCE_DIR}
    ${imgwindow_SOURCE_DIR}
    fonts
    ${XPSDK_DIR}/CHeaders/XPLM
    ${XPSDK_DIR}/CHeaders/Widgets)
target_link_libraries(FXAnnounce PRIVATE fxa_vorbis)

target_compile_definitions(FXAnnounce PRIVATE ${FXA_XPLM_DEFS} FXA_VERSION="${PROJECT_VERSION}")
if(FXA_PANEL_GRAPHICS)
    target_compile_definitions(FXAnnounce PRIVATE IMGWINDOW_USE_PANEL_GRAPHICS)
endif()

if(WIN32)
    target_compile_definitions(FXAnnounce PRIVATE IBM=1 WIN32_LEAN_AND_MEAN NOMINMAX _USE_MATH_DEFINES)
    target_link_libraries(FXAnnounce PRIVATE ${XPSDK_DIR}/Libraries/Win/XPLM_64.lib opengl32)
    # MSVC has no <GL/glext.h>; ImgWindow needs only GL 1.1, so an empty shim satisfies its include.
    target_include_directories(FXAnnounce PRIVATE compat/win)
    set(FXA_PLATFORM_DIR win_x64)
elseif(APPLE)
    target_compile_definitions(FXAnnounce PRIVATE APL=1 GL_SILENCE_DEPRECATION)
    find_library(XPLM_FRAMEWORK XPLM PATHS ${XPSDK_DIR}/Libraries/Mac NO_DEFAULT_PATH)
    find_library(OPENGL_FRAMEWORK OpenGL)
    target_link_libraries(FXAnnounce PRIVATE ${XPLM_FRAMEWORK} ${OPENGL_FRAMEWORK})
    set(FXA_PLATFORM_DIR mac_x64)
else()
    target_compile_definitions(FXAnnounce PRIVATE LIN=1)
    find_package(OpenGL REQUIRED)
    find_package(Threads REQUIRED)
    target_link_libraries(FXAnnounce PRIVATE OpenGL::GL Threads::Threads)
    # X-Plane resolves XPLM symbols at load time; only XPlugin* are exported.
    target_link_options(FXAnnounce PRIVATE -Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/exports.map
        $<$<CONFIG:Release>:-s>)
    # Release builds: static C++ runtime, since users' distros often ship an older libstdc++ than the
    # build machine (needs the libstdc++ static archive; CI turns this on).
    if(FXA_STATIC_RUNTIME)
        target_link_options(FXAnnounce PRIVATE -static-libstdc++ -static-libgcc)
    endif()
    set(FXA_PLATFORM_DIR lin_x64)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(FXAnnounce PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra>)
    set_source_files_properties(${THIRD_PARTY_SOURCES} PROPERTIES COMPILE_OPTIONS "-w")
endif()

set_target_properties(FXAnnounce PROPERTIES
    PREFIX ""
    OUTPUT_NAME FXAnnounce
    SUFFIX ".xpl"
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/FXAnnounce/${FXA_PLATFORM_DIR}
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/FXAnnounce/${FXA_PLATFORM_DIR})

# ---- unit tests (pure C++, no X-Plane) -------------------------------------------------------------
add_executable(fx_tests EXCLUDE_FROM_ALL tests/test_main.cpp tests/test_core.cpp tests/test_packs.cpp tests/test_purser.cpp tests/test_audio.cpp
    tests/test_toliss.cpp
    ${FXA_CORE_SOURCES} ${FXA_AUDIO_SOURCES})
target_include_directories(fx_tests PRIVATE src ${doctest_SOURCE_DIR})
find_package(Threads REQUIRED)
target_link_libraries(fx_tests PRIVATE Threads::Threads fxa_vorbis)
# DOCTEST_CONFIG_USE_STD_HEADERS: doctest otherwise forward-declares std::ostream, and MSVC then can't
# stringify std::string_view in a failed CHECK (C2027 "undefined type basic_ostream").
target_compile_definitions(fx_tests PRIVATE FXA_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}" DOCTEST_CONFIG_USE_STD_HEADERS)

# ---- headless UI test: every tab, many frames, ImGui asserts on (null backend) ----------------------
add_executable(fx_ui_tests EXCLUDE_FROM_ALL tests/test_ui.cpp ${FXA_CORE_SOURCES} ${FXA_UI_SOURCES}
    ${imgui_SOURCE_DIR}/backends/imgui_impl_null.cpp
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(fx_ui_tests PRIVATE src fonts ${imgui_SOURCE_DIR} ${imgui_SOURCE_DIR}/backends)
# ImGui asserts stay on in Release on every compiler (MSVC takes -U too and warns D9025 about overriding /DNDEBUG).
target_compile_options(fx_ui_tests PRIVATE -UNDEBUG)
