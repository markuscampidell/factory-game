# Optional CMake build (build.bat does the same thing without CMake).
#   cmake -B build -G "MinGW Makefiles"   (or Ninja)
#   cmake --build build
cmake_minimum_required(VERSION 3.16)
project(factory_game CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

find_package(raylib REQUIRED)

set(SIM_SOURCES
  src/core/Items.cpp src/core/Inventory.cpp
  src/sim/Terrain.cpp src/sim/World.cpp src/sim/Transport.cpp
  src/sim/Player.cpp src/sim/Build.cpp src/sim/Save.cpp src/sim/Blueprint.cpp)

add_library(factory_sim STATIC ${SIM_SOURCES})
target_include_directories(factory_sim PUBLIC third_party)

add_executable(factory_game WIN32
  src/game/main.cpp src/game/Assets.cpp src/game/Ui.cpp src/game/MainMenu.cpp
  src/game/GameScreen.cpp src/game/GameDraw.cpp src/game/GamePanels.cpp src/game/GameMenu.cpp src/game/GameBlueprints.cpp)
target_link_libraries(factory_game PRIVATE factory_sim raylib)
if(WIN32)
  target_link_libraries(factory_game PRIVATE opengl32 gdi32 winmm)
endif()
# put the exe next to assets/ and saves/
set_target_properties(factory_game PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR})

add_executable(sim_tests tests/sim_tests.cpp)
target_link_libraries(sim_tests PRIVATE factory_sim)
