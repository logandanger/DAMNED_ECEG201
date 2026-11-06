cmake_minimum_required(VERSION 3.20)
project(danger_sim LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

get_filename_component(REPO "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# Pinned. LVGL: MIT. SDL2: zlib.
FetchContent_Declare(lvgl
  URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz
  URL_HASH SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6
  SOURCE_SUBDIR do-not-add)  # sources are compiled below with our lv_conf.h
FetchContent_Declare(sdl2
  URL https://github.com/libsdl-org/SDL/releases/download/release-2.32.10/SDL2-2.32.10.tar.gz
  URL_HASH SHA256=5f5993c530f084535c65a6879e9b26ad441169b3e25d789d83287040a9ca5165)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
set(SDL2_DISABLE_UNINSTALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(lvgl sdl2)

file(GLOB_RECURSE LVGL_SOURCES CONFIGURE_DEPENDS "${lvgl_SOURCE_DIR}/src/*.c")
add_library(lvgl STATIC ${LVGL_SOURCES})
target_include_directories(lvgl PUBLIC "${lvgl_SOURCE_DIR}" "${REPO}/core/ui")
target_compile_definitions(lvgl PUBLIC LV_CONF_INCLUDE_SIMPLE)

# core/ without the display: everything the unit tests need.
add_library(danger_engine STATIC
  ${REPO}/core/synth/dsp.cpp
  ${REPO}/core/synth/engine.cpp
  ${REPO}/core/synth/params.cpp
  ${REPO}/core/synth/presets.cpp
  ${REPO}/core/mirror/mirror_proto.cpp
  ${REPO}/core/mirror/mirror_sender.cpp
  ${REPO}/core/ui/mapper.cpp
  ${REPO}/core/ui/menu.cpp)
target_include_directories(danger_engine PUBLIC "${REPO}")
target_compile_options(danger_engine PRIVATE -Wall -Wextra)

add_library(danger_ui STATIC
  ${REPO}/core/app.cpp
  ${REPO}/core/ui/led_view.cpp
  ${REPO}/core/ui/view.cpp)
target_link_libraries(danger_ui PUBLIC danger_engine lvgl)

add_executable(danger_sim
  main.cpp
  replica.cpp
  serial_port.cpp
  mirror_client.cpp
  ${REPO}/hal/sim/hal_sim.cpp)
target_link_libraries(danger_sim PRIVATE danger_ui SDL2::SDL2-static)
target_compile_definitions(danger_sim PRIVATE SDL_MAIN_HANDLED)

enable_testing()
add_executable(danger_tests ${REPO}/tests/tests.cpp)
target_link_libraries(danger_tests PRIVATE danger_engine)
target_compile_definitions(danger_tests PRIVATE _USE_MATH_DEFINES)
add_test(NAME unit COMMAND danger_tests)

add_executable(danger_app_tests ${REPO}/tests/app_tests.cpp)
target_link_libraries(danger_app_tests PRIVATE danger_ui)
add_test(NAME app COMMAND danger_app_tests)

add_executable(danger_bench ${REPO}/tests/bench.cpp)
target_link_libraries(danger_bench PRIVATE danger_engine)
