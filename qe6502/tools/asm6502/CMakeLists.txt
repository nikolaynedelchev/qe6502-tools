cmake_minimum_required(VERSION 3.16)

project(asm6502 LANGUAGES CXX)

add_library(asm6502 STATIC
    src/asm6502.cpp
)

add_library(asm6502::asm6502 ALIAS asm6502)

target_compile_features(asm6502 PUBLIC cxx_std_17)

target_include_directories(asm6502
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

if(QE6502_BUILD_TESTS)
    add_executable(asm6502_smoke
        tests/smoke.cpp
    )
    target_link_libraries(asm6502_smoke PRIVATE asm6502::asm6502)
    if(COMMAND qe6502_configure_warnings)
        qe6502_configure_warnings(asm6502_smoke)
    endif()
    add_test(NAME asm6502.smoke COMMAND asm6502_smoke)
endif()
