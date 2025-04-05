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
set(UMBF_LIB_NAME ${PROJECT_NAME}-${APP_SIMD_ARCH})

configure_file(${TEMPLATES_DIR}/version.h.in ${CMAKE_CURRENT_BINARY_DIR}/include/${PROJECT_NAME}/version.h)

set(SOURCE_FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/umbf.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/stream.cpp"    
)
if(NOT ACUL_BUILD_MIN)
    list(APPEND SOURCE_FILES "${CMAKE_CURRENT_SOURCE_DIR}/src/utils.cpp")
endif()

add_library(${UMBF_LIB_NAME} SHARED ${SOURCE_FILES})

if(ACUL_BUILD_MIN)
    target_compile_definitions(${UMBF_LIB_NAME} PUBLIC UMBF_BUILD_MIN)
endif()

set_target_properties(${UMBF_LIB_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${UMBF_LIB_NAME} PRIVATE -DAPPLIB_BUILD)
target_include_directories(${UMBF_LIB_NAME} PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}/include/"
    "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    "${CMAKE_CURRENT_SOURCE_DIR}/modules/3rd-party/rectpack2D/src/")
target_link_libraries(${UMBF_LIB_NAME} PRIVATE ${ACUL_LIB_NAME})

if(USE_ASAN)
    target_link_options(${UMBF_LIB_NAME} PUBLIC -fsanitize=address -fno-omit-frame-pointer)
endif(USE_ASAN)

if(WIN32)
    gen_manifest_lib(${UMBF_LIB_NAME} ${PROJECT_VERSION})
endif()

set_target_properties(${UMBF_LIB_NAME}
    PROPERTIES
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED YES
    CXX_EXTENSIONS YES
)

set(UMBF_LIB_NAME ${UMBF_LIB_NAME} PARENT_SCOPE)