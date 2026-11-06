get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

set(SOURCES main.cpp mirror_task.cpp)
set(INCLUDES . "${REPO}" "${REPO}/core/ui")

# Component scripts also run once in script mode, where the project's variables do not exist.
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
  file(GLOB CORE_SOURCES
    "${REPO}/core/*.cpp" "${REPO}/core/synth/*.cpp" "${REPO}/core/ui/*.cpp" "${REPO}/core/mirror/*.cpp"
    "${REPO}/hal/esp32/*.cpp")
  file(GLOB_RECURSE LVGL_SOURCES "${DANGER_LVGL_DIR}/src/*.c")
  # PlatformIO flattens object names for external sources, and these trees repeat file names.
  list(FILTER LVGL_SOURCES EXCLUDE REGEX "/src/(debugging|drivers|libs/vg_lite_driver)/")
  list(APPEND SOURCES ${CORE_SOURCES} ${LVGL_SOURCES})
  list(APPEND INCLUDES "${DANGER_LVGL_DIR}")
endif()

idf_component_register(
  SRCS ${SOURCES}
  INCLUDE_DIRS ${INCLUDES}
  REQUIRES driver esp_driver_gpio esp_driver_i2c esp_driver_i2s esp_driver_spi esp_driver_usb_serial_jtag esp_lcd
           esp_timer freertos log heap)

target_compile_definitions(${COMPONENT_LIB} PRIVATE LV_CONF_INCLUDE_SIMPLE)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error -Wno-missing-field-initializers -O2)
target_compile_options(${COMPONENT_LIB} PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-std=gnu++17>)
