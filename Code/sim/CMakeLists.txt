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
#  LVGL is fetched at configure time, pinned to the v9.4.0 commit, and is NOT
#  vendored into the repository. It uses the SAME lv_conf.h the firmware uses
#  (Code/libraries/lv_conf.h) — including the 64 KB LV_MEM_SIZE — so memory
#  pressure and font metrics match the Teensy build rather than approximating it.
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(cubesolver_sim CXX C)   # C too: the baked theme assets are .c

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(FW   ${CMAKE_CURRENT_SOURCE_DIR}/../libraries/CubeSolver)
set(LIBS ${CMAKE_CURRENT_SOURCE_DIR}/../libraries)

# Which sketch to compile in. The firmware by default; Test_Menu is the bench
# sketch for the wheel, the buttons and the panel, and being able to run it
# here is most of its value — it needs no cube and no machine.
#
#     cmake -S Code/sim -B Code/sim/build-test -DSIM_SKETCH=Test_Menu
set(SIM_SKETCH "CubeSolver" CACHE STRING "Sketch to build: CubeSolver or Test_Menu")

if(SIM_SKETCH STREQUAL "CubeSolver")
    set(SKETCH "${CMAKE_CURRENT_SOURCE_DIR}/../Main Code/CubeSolver")
else()
    set(SKETCH "${CMAKE_CURRENT_SOURCE_DIR}/../Test Code/${SIM_SKETCH}")
endif()
if(NOT EXISTS "${SKETCH}/${SIM_SKETCH}.ino")
    message(FATAL_ERROR "No sketch at ${SKETCH}/${SIM_SKETCH}.ino")
endif()
message(STATUS "Simulator sketch: ${SIM_SKETCH}")

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

# Tell LVGL where lv_conf.h actually is.
#
# From 9.4 its desktop cmake locates lv_conf.h itself and FATAL_ERRORs when it
# is not beside the top-level CMakeLists. This project keeps it at
# Code/libraries/lv_conf.h on purpose — LVGL requires it one level above the
# lvgl/ folder, and lvgl/ is installed in the Arduino sketchbook rather than in
# the repo (see the forwarder header there). Pointing LVGL at the real file is
# right; moving it to satisfy the simulator would break the firmware build.
set(LV_BUILD_CONF_DIR ${LIBS} CACHE PATH "" FORCE)

include(FetchContent)
FetchContent_Declare(
    lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    # v9.4.0 — the SAME release the Arduino sketchbook installs, so the
    # simulator and the firmware compile against one LVGL rather than two.
    # Two versions sharing one lv_conf.h is a silent fidelity gap until it is
    # not: 9.1's lv_api_map_v8.h types lv_btnmatrix_ctrl_t unconditionally and
    # stops compiling once LV_USE_BUTTONMATRIX is 0, while 9.4 guards that
    # typedef and builds fine on the Teensy.
    GIT_TAG        c016f72d4c125098287be5e83c0f1abed4706ee5  # v9.4.0
    GIT_SHALLOW    FALSE
)
FetchContent_MakeAvailable(lvgl)

# ---------------------------------------------------------------------------
#  SDL2
# ---------------------------------------------------------------------------
# pkg-config first: it is how SDL2 is installed on the Linux and macOS benches,
# and stays the fast path. Windows — the machine this firmware is actually
# developed on, and where the simulator is the only way to see the UI — has no
# pkg-config and no system SDL2, so the fallback builds SDL2 from source. That
# costs a few minutes of configure time once per build directory.
set(SIM_SDL_TARGETS "")

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(SDL2 QUIET IMPORTED_TARGET sdl2)
    if(TARGET PkgConfig::SDL2)
        set(SIM_SDL_TARGETS PkgConfig::SDL2)
    endif()
endif()

if(NOT SIM_SDL_TARGETS)
    message(STATUS "SDL2: not found via pkg-config, building from source")
    # Static, and no test/shared build: the point is one self-contained cubesim
    # binary that runs without an SDL2 DLL beside it.
    set(SDL_SHARED       OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC       ON  CACHE BOOL "" FORCE)
    set(SDL_TEST         OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
        SDL2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        release-2.30.9
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL2)
    set(SIM_SDL_TARGETS SDL2::SDL2-static)
    # SDL redefines main() to SDL_main on Windows and supplies the real entry
    # point in SDL2main. Without it the link fails on a missing WinMain, which
    # reads as a broken project rather than a missing library.
    if(WIN32)
        list(APPEND SIM_SDL_TARGETS SDL2::SDL2main)
    endif()
endif()

# ---------------------------------------------------------------------------
#  Sources
# ---------------------------------------------------------------------------
file(GLOB THEME_ASSETS CONFIGURE_DEPENDS ${FW}/utility/*.c)
if(NOT THEME_ASSETS)
    message(FATAL_ERROR
        "No theme assets in ${FW}/utility — run: python3 Code/tools/bake_theme.py")
endif()

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
    ${FW}/CubeOpShape.cpp
    ${FW}/CubeDisplay.cpp
    ${FW}/RotaryEncoder.cpp
    ${FW}/CubePump.cpp
    ${FW}/CubeTuning.cpp
    ${FW}/CubeTuneTable.cpp
    ${FW}/CubeFaultLog.cpp
    ${FW}/CubeStats.cpp
    ${FW}/CubeHardwareConfig.cpp
    ${FW}/CubeServo.cpp
    ${FW}/CubeMotors.cpp
    ${FW}/ColorSensor.cpp
    ${FW}/MotorEncoder.cpp
    ${FW}/VirtualCube.cpp

    # VirtualCube uses kociemba::cornerFacelet / edgeFacelet from here, rather
    # than transcribing the piece definitions a second time.
    ${LIBS}/kociemba/facecube.cpp

    # Baked theme assets. Generated by Code/tools/bake_theme.py; globbed rather
    # than listed because the set is the baker's output, not a hand-kept list.
    # They live in utility/ because that is the only subdirectory the Arduino
    # 1.0 library format compiles, and the Teensy build has to see them too.
    ${THEME_ASSETS}
)

# shim/ must precede nothing in particular, but ${SKETCH} is needed for
# ino_main.cpp's include of the .ino, and ${FW} for the library headers.
target_include_directories(cubesim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/shim
    ${FW}
    ${SKETCH}
    ${LIBS}/kociemba
)

target_compile_definitions(cubesim PRIVATE SIM_SKETCH_FILE="${SIM_SKETCH}.ino")

target_link_libraries(cubesim PRIVATE lvgl ${SIM_SDL_TARGETS})

# The firmware is not warning-clean under -Wextra (it predates this build), so
# warnings are enabled but not fatal. New code should still come in clean.
target_compile_options(cubesim PRIVATE -Wall)
