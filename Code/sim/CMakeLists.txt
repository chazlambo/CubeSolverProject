# =============================================================================
#  CubeSolver desktop menu simulator
# =============================================================================
#
#  Builds the real firmware against SDL-backed hardware shims. See README.md.
#
#      cmake -S Code/sim -B Code/sim/build
#      cmake --build Code/sim/build -j
#      ./Code/sim/build/cubesim
#
#  LVGL is fetched at configure time, pinned to the v9.1 commit, and is NOT
#  vendored into the repository. It uses the SAME lv_conf.h the firmware uses
#  (Code/libraries/lv_conf.h) — including the 32 KB LV_MEM_SIZE — so memory
#  pressure and font metrics match the Teensy build rather than approximating it.
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(cubesolver_sim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(FW   ${CMAKE_CURRENT_SOURCE_DIR}/../libraries/CubeSolver)
set(LIBS ${CMAKE_CURRENT_SOURCE_DIR}/../libraries)
set(SKETCH ${CMAKE_CURRENT_SOURCE_DIR}/../Firmware/CubeSolver)

# ---------------------------------------------------------------------------
#  LVGL
# ---------------------------------------------------------------------------
# LV_CONF_INCLUDE_SIMPLE makes LVGL resolve lv_conf.h off the include path,
# which is how the firmware's own copy gets picked up. Set before the
# add_subdirectory that FetchContent performs, so LVGL's own sources inherit it.
add_compile_definitions(LV_CONF_INCLUDE_SIMPLE)
include_directories(${LIBS})

set(LV_CONF_BUILD_DISABLE_EXAMPLES 1 CACHE BOOL "" FORCE)
set(LV_CONF_BUILD_DISABLE_DEMOS    1 CACHE BOOL "" FORCE)

include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG        0b09f5866d4a9251e3509acc3c44e794d4225002  # release/v9.1
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(lvgl)

# ---------------------------------------------------------------------------
#  SDL2
# ---------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED IMPORTED_TARGET sdl2)

# ---------------------------------------------------------------------------
#  Sources
# ---------------------------------------------------------------------------
# Everything under ${FW} is the shipping firmware, compiled unmodified.
# CubeSystem.cpp is the one file NOT listed: CubeSystemSim.cpp replaces it.
add_executable(cubesim
    main.cpp
    ino_main.cpp
    CubeSystemSim.cpp

    shim/SimHost.cpp
    shim/Arduino.cpp
    shim/EEPROM.cpp
    shim/Wire.cpp
    shim/Adafruit_seesaw.cpp
    shim/kociemba.cpp

    ${FW}/CubeMenu.cpp
    ${FW}/CubeDisplay.cpp
    ${FW}/RotaryEncoder.cpp
    ${FW}/CubePump.cpp
    ${FW}/CubeHardwareConfig.cpp
    ${FW}/CubeServo.cpp
    ${FW}/CubeMotors.cpp
    ${FW}/ColorSensor.cpp
    ${FW}/MotorEncoder.cpp
    ${FW}/VirtualCube.cpp

    # VirtualCube uses kociemba::cornerFacelet / edgeFacelet from here, rather
    # than transcribing the piece definitions a second time.
    ${LIBS}/kociemba/facecube.cpp
)

# shim/ must precede nothing in particular, but ${SKETCH} is needed for
# ino_main.cpp's include of the .ino, and ${FW} for the library headers.
target_include_directories(cubesim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${FW}
    ${SKETCH}
    ${LIBS}/kociemba
)

target_link_libraries(cubesim PRIVATE lvgl PkgConfig::SDL2)

# The firmware is not warning-clean under -Wextra (it predates this build), so
# warnings are enabled but not fatal. New code should still come in clean.
target_compile_options(cubesim PRIVATE -Wall)
