enable_language(CXX)

set(target nmi_observer)

if(NOT TARGET perfect6502)
    add_subdirectory(${PROJECT_SOURCE_DIR}/qe6502/harness/third_party/perfect6502 ${CMAKE_CURRENT_BINARY_DIR}/perfect6502)
endif()

add_executable(${target}
    main.cpp
    testaces.cpp
    ${PROJECT_SOURCE_DIR}/qe6502/binds/cpp/src/cpu.cpp
)

target_link_libraries(${target}
    PRIVATE
        qe6502::static
        perfect6502
        asm6502::asm6502
)

target_include_directories(${target}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/qe6502/binds/cpp/include
        ${PROJECT_SOURCE_DIR}/qe6502/harness/third_party/perfect6502
)

qe6502_configure_warnings(${target})

target_compile_features(${target} PUBLIC cxx_std_20)
