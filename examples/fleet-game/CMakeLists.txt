cmake_minimum_required(VERSION 3.14)
project(fleet-game CXX)

# The SDK: $KARL_SDK in the build image, else this repository.
if(DEFINED ENV{KARL_SDK})
    set(KARL_SDK $ENV{KARL_SDK})
else()
    set(KARL_SDK ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()
include(${KARL_SDK}/cmake/KarlPlugin.cmake)
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
