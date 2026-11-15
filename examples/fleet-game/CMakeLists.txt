cmake_minimum_required(VERSION 3.14)
project(fleet-game CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/KarlPlugin.cmake)
find_package(nlohmann_json 3.2.0 REQUIRED)

karl_plugin(fleet-game
    src/Plugin.cpp
    src/FleetGame.cpp
    src/Water.cpp
    src/Battle.cpp
    src/Models.cpp
    src/Particles.cpp
    src/Tactical.cpp
)
target_link_libraries(fleet-game PRIVATE nlohmann_json::nlohmann_json)
