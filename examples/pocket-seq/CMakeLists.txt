cmake_minimum_required(VERSION 3.14)
project(pocket-seq CXX)

# The SDK: $KARL_SDK in the build image, else this repository.
if(DEFINED ENV{KARL_SDK})
    set(KARL_SDK $ENV{KARL_SDK})
else()
    set(KARL_SDK ${CMAKE_CURRENT_LIST_DIR}/../..)
endif()
include(${KARL_SDK}/cmake/KarlPlugin.cmake)
find_package(nlohmann_json 3.2.0 REQUIRED)

karl_plugin(pocket-seq
    src/Plugin.cpp
    src/PocketSeq.cpp
)
target_link_libraries(pocket-seq PRIVATE nlohmann_json::nlohmann_json)
