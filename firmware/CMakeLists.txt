cmake_minimum_required(VERSION 3.16)

# Same pinned LVGL and same lv_conf.h as the simulator, compiled into the main component.
include(FetchContent)
FetchContent_Declare(lvgl
  URL https://github.com/lvgl/lvgl/archive/refs/tags/v9.5.0.tar.gz
  URL_HASH SHA256=34a955cdf3a2d005507b704e87357af669a114523b6d3f77b5344fdc68717bc6
  SOURCE_SUBDIR do-not-add)
FetchContent_MakeAvailable(lvgl)
set(DANGER_LVGL_DIR "${lvgl_SOURCE_DIR}")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(danger)
