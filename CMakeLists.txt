cmake_minimum_required(VERSION 3.17)

if(NOT DEFINED ACBT_PROJECT_LOADED)
    include(cmake/project.cmake)
endif()

project(umbf
    VERSION 1.0.0
    LANGUAGES CXX C
    DESCRIPTION "UMBF Lib")

if(NOT DEFINED ACBT_LOADED)
    include(cmake/utils.cmake)
endif()

gen_version_file(${CMAKE_CURRENT_BINARY_DIR}/include/${PROJECT_NAME}/version.h)

add_files(${PROJECT_NAME} RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_library(${PROJECT_NAME} SHARED ${UMBF_SRC})

set_target_properties(${PROJECT_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)

add_subdirectory("modules/3rd-party/rectpack2D")

if(NOT TARGET acul)
    add_subdirectory(modules/acul)
endif()

if(NOT TARGET amal)
    add_subdirectory(modules/amal)
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE -DAPPLIB_BUILD)
target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/include/"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/"
)
target_link_libraries(${PROJECT_NAME} PUBLIC acul amal rectpack2D)

if(USE_ASAN)
    target_link_options(${PROJECT_NAME} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
endif(USE_ASAN)

if(WIN32)
    gen_manifest_lib(lib${PROJECT_NAME} ${PROJECT_VERSION})
endif()

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)