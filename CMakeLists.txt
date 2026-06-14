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

create_version_file()
create_symbol_export_file()

if(NOT TARGET acul)
    add_subdirectory(modules/acul)
endif()

if(NOT TARGET amal)
    add_subdirectory(modules/amal)
endif()

add_files(${PROJECT_NAME} RECURSE "${CMAKE_CURRENT_SOURCE_DIR}/src")
add_library(${PROJECT_NAME} SHARED ${UMBF_SRC})
set_target_properties(${PROJECT_NAME} PROPERTIES
    DEFINE_SYMBOL UMBF_BUILD
    CXX_VISIBILITY_PRESET hidden
)

target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/include/"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/"
)
target_link_libraries(${PROJECT_NAME} PUBLIC acul amal)

if(WIN32)
    gen_manifest_lib(lib${PROJECT_NAME} ${PROJECT_VERSION})
endif()

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    CXX_EXTENSIONS YES
)
