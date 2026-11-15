cmake_minimum_required(VERSION 3.14)
project(pocket-seq CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/KarlPlugin.cmake)
find_package(nlohmann_json 3.2.0 REQUIRED)

karl_plugin(pocket-seq
    src/Plugin.cpp
    src/PocketSeq.cpp
)
target_link_libraries(pocket-seq PRIVATE nlohmann_json::nlohmann_json)
