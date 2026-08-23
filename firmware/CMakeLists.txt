cmake_minimum_required(VERSION 3.20)
set(PICO_BOARD pico2_w)
# Declared before the SDK includes because PIP_AUDIO_SMOKE decides whether pico-extras is
# pulled in, and both imports have to run before project().
option(PIP_LITE "Bare Pico 2 W: no display, light sensor, or RGB LED" OFF)
option(PIP_AUDIO_SMOKE "Also build the I2S tone smoke (needs PICO_EXTRAS_PATH)" OFF)
include(pico_sdk_import.cmake)
if(PIP_AUDIO_SMOKE)
  include(pico_extras_import.cmake)
endif()
project(pip C CXX ASM)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/config.h)
  message(FATAL_ERROR "firmware/config.h missing: cp firmware/config.example.h firmware/config.h and fill in WiFi + brain")
endif()
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
pico_sdk_init()

set(PIP_CORE ${CMAKE_CURRENT_SOURCE_DIR}/../core)
file(GLOB PIP_CORE_SRC ${PIP_CORE}/src/*.cpp)

add_executable(pip src/main.cpp src/net/wifi.cpp src/net/http_server.cpp src/net/event_post.cpp src/net/link.cpp ${PIP_CORE_SRC})
target_include_directories(pip PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src ${PIP_CORE}/include)
target_link_libraries(pip pico_stdlib pico_cyw43_arch_lwip_threadsafe_background hardware_adc hardware_uart hardware_irq)
target_compile_options(pip PRIVATE -Wall -Wextra)
pico_set_printf_implementation(pip pico)
if(PIP_LITE)
  target_compile_definitions(pip PRIVATE PIP_LITE=1)
else()
  target_link_libraries(pip hardware_spi hardware_i2c hardware_pwm)
  target_sources(pip PRIVATE src/drivers/ili9341.cpp src/drivers/veml7700.cpp)
endif()
pico_enable_stdio_usb(pip 1)
pico_enable_stdio_uart(pip 0)
pico_add_extra_outputs(pip)

if(PIP_AUDIO_SMOKE)
  add_executable(pip_i2s_smoke smoke/i2s_tone.cpp)
  # CMake cannot read pins.hpp, so these two numbers are duplicated here and the smoke
  # static_asserts them against it. Change a pin in pins.hpp and this build stops.
  target_compile_definitions(pip_i2s_smoke PRIVATE
    PICO_AUDIO_I2S_DATA_PIN=28 PICO_AUDIO_I2S_CLOCK_PIN_BASE=26)
  target_include_directories(pip_i2s_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(pip_i2s_smoke PRIVATE -Wall -Wextra)
  # pico-extras' own audio.cpp leaves two audio_connection members default-initialized on
  # purpose. It compiles into this target, so it inherits our flags; exempt that one file.
  set_source_files_properties(${PICO_EXTRAS_PATH}/src/common/pico_audio/audio.cpp
    PROPERTIES COMPILE_OPTIONS "-Wno-missing-field-initializers")
  target_link_libraries(pip_i2s_smoke pico_stdlib pico_audio_i2s)
  pico_enable_stdio_usb(pip_i2s_smoke 1)
  pico_enable_stdio_uart(pip_i2s_smoke 0)
  pico_add_extra_outputs(pip_i2s_smoke)

  # Bench smoke: the tone plus a UART0 echo on GP0/GP1, to check the amp and the Zero link together.
  add_executable(pip_bench_smoke smoke/bench.cpp)
  target_compile_definitions(pip_bench_smoke PRIVATE
    PICO_AUDIO_I2S_DATA_PIN=28 PICO_AUDIO_I2S_CLOCK_PIN_BASE=26)
  target_include_directories(pip_bench_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_options(pip_bench_smoke PRIVATE -Wall -Wextra)
  target_link_libraries(pip_bench_smoke pico_stdlib pico_audio_i2s hardware_uart)
  pico_enable_stdio_usb(pip_bench_smoke 1)
  pico_enable_stdio_uart(pip_bench_smoke 0)
  pico_add_extra_outputs(pip_bench_smoke)
endif()
