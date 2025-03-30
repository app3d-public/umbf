cmake_minimum_required(VERSION 3.17)
project(umbf
    VERSION 1.0.0
    LANGUAGES CXX C
    DESCRIPTION "UMBF Lib")

foreach(arch AVX512 AVX2 AVX SSE42)
    if(SIMD_${arch})
        set(APP_ARCH ${arch})
        break()
    endif()
endforeach()

set(APP_HNAME UMBF)

configure_file(${TEMPLATES_DIR}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/${PROJECT_NAME}/version.h)

add_files(${PROJECT_NAME} "${CMAKE_CURRENT_SOURCE_DIR}/src")

add_library(${PROJECT_NAME} SHARED ${SOURCE_FILES})
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PROJECT_NAME} PRIVATE -DAPPLIB_BUILD)
target_include_directories(${PROJECT_NAME} PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/include/"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/3rd-party/rectpack2D/src/")
target_link_libraries(${PROJECT_NAME} PRIVATE ${ACUL_LIB_NAME})

if(USE_ASAN)
    target_link_options(${PROJECT_NAME} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
endif(USE_ASAN)

if(WIN32)
    gen_manifest_lib(${PROJECT_NAME} ${PROJECT_VERSION})
endif()

set_target_properties(${PROJECT_NAME}
    PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)