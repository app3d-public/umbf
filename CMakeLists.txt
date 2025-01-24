cmake_minimum_required(VERSION 3.17)
project(app3d-assets
    VERSION 0.0.1
    LANGUAGES CXX C
    DESCRIPTION "App3d Assets Lib")

add_files(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PROJECT_NAME} PRIVATE -DAPPLIB_BUILD)
target_include_directories(${PROJECT_NAME} PRIVATE $<TARGET_PROPERTY:app3d-core,INCLUDE_DIRECTORIES>)
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/modules/3rd-party/rectpack2D/src")
include_directories("${CMAKE_CURRENT_SOURCE_DIR}/include/")

target_link_libraries(${PROJECT_NAME} PRIVATE app3d-core)

if(USE_ASAN)
    target_link_options(${PROJECT_NAME} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
endif(USE_ASAN)

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)