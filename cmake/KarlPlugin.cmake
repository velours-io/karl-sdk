# karl_plugin(<name> <sources...>)
#
# Builds <name>.so for KARL. Run in the build image (README, Build), which
# sets the compiler and libraries KARL itself uses.

set(KARL_SDK_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/../include)

find_package(OpenSceneGraph REQUIRED COMPONENTS osg osgText osgDB)

function(karl_plugin name)
    add_library(${name} MODULE ${ARGN})
    set_target_properties(${name} PROPERTIES
        PREFIX ""
        CXX_STANDARD 17
        CXX_STANDARD_REQUIRED ON
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON)
    if(APPLE)
        set_target_properties(${name} PROPERTIES SUFFIX ".dylib")
    endif()
    target_include_directories(${name} PRIVATE ${KARL_SDK_INCLUDE_DIR} ${OPENSCENEGRAPH_INCLUDE_DIRS})
    target_link_libraries(${name} PRIVATE ${OPENSCENEGRAPH_LIBRARIES})
endfunction()
