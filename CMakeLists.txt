# /CMakeLists.txt
cmake_minimum_required(VERSION 3.15)
project(qe6502-tools VERSION 0.1.0 LANGUAGES C)

include(CTest)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(QE6502_DEFAULT_BUILD_TESTS ${BUILD_TESTING})
    set(QE6502_DEFAULT_INSTALL ON)
else()
    set(QE6502_DEFAULT_BUILD_TESTS OFF)
    set(QE6502_DEFAULT_INSTALL OFF)
endif()

option(QE6502_BUILD_WASM "Enable WebAssembly target mode" OFF)
option(QE6502_BUILD_STATIC "Build the fast static qe6502 C library" ON)
option(QE6502_BUILD_SHARED "Build the stable shared qe6502 ABI library" ON)
option(QE6502_BUILD_CPP "Build the qe6502 C++ wrapper" ON)
option(QE6502_BUILD_TESTS "Build qe6502 tests and harnesses" ${QE6502_DEFAULT_BUILD_TESTS})
option(QE6502_ENABLE_WERROR "Treat qe6502 warnings as errors" OFF)
option(QE6502_INSTALL "Install qe6502 headers, libraries and package metadata" ${QE6502_DEFAULT_INSTALL})

if(QE6502_BUILD_WASM AND NOT QE6502_BUILD_STATIC)
    message(FATAL_ERROR "QE6502_BUILD_WASM requires QE6502_BUILD_STATIC because the JS/WASM bindings link the static core")
endif()

if(QE6502_BUILD_CPP AND NOT QE6502_BUILD_STATIC)
    message(FATAL_ERROR "QE6502_BUILD_CPP requires QE6502_BUILD_STATIC")
endif()

if(NOT QE6502_BUILD_STATIC AND NOT QE6502_BUILD_SHARED)
    message(FATAL_ERROR "At least one of QE6502_BUILD_STATIC or QE6502_BUILD_SHARED must be enabled")
endif()

if(QE6502_BUILD_WASM AND QE6502_BUILD_CPP)
    message(STATUS "Disabling QE6502_BUILD_CPP because QE6502_BUILD_WASM is enabled")
    set(QE6502_BUILD_CPP OFF CACHE BOOL "Build the qe6502 C++ wrapper" FORCE)
endif()

if(QE6502_BUILD_TESTS AND NOT QE6502_BUILD_WASM)
    if(NOT QE6502_BUILD_STATIC OR NOT QE6502_BUILD_SHARED OR NOT QE6502_BUILD_CPP)
        message(FATAL_ERROR "Native QE6502_BUILD_TESTS requires QE6502_BUILD_STATIC, QE6502_BUILD_SHARED and QE6502_BUILD_CPP")
    endif()
endif()

if(QE6502_BUILD_TESTS AND QE6502_BUILD_WASM AND NOT QE6502_BUILD_STATIC)
    message(FATAL_ERROR "WASM QE6502_BUILD_TESTS requires QE6502_BUILD_STATIC")
endif()

function(qe6502_configure_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            $<$<BOOL:${QE6502_ENABLE_WERROR}>:/WX>
            $<$<BOOL:${QE6502_ENABLE_WERROR}>:/we4505>
        )
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic
            -Wconversion -Wsign-conversion -Wshadow
            -Wdouble-promotion -Wformat=2 -Wundef
            -Wunused-function
            $<$<BOOL:${QE6502_ENABLE_WERROR}>:-Werror>
            $<$<BOOL:${QE6502_ENABLE_WERROR}>:-Werror=unused-function>
        )
    endif()
endfunction()

message(STATUS "  QE6502_BUILD_WASM        = ${QE6502_BUILD_WASM}")
message(STATUS "  QE6502_BUILD_STATIC      = ${QE6502_BUILD_STATIC}")
message(STATUS "  QE6502_BUILD_SHARED      = ${QE6502_BUILD_SHARED}")
message(STATUS "  QE6502_BUILD_CPP         = ${QE6502_BUILD_CPP}")
message(STATUS "  QE6502_BUILD_TESTS       = ${QE6502_BUILD_TESTS}")
message(STATUS "  QE6502_ENABLE_WERROR     = ${QE6502_ENABLE_WERROR}")

add_subdirectory(qe6502)
add_subdirectory(nmi_observer)


