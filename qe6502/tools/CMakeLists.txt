cmake_minimum_required(VERSION 3.16)

project(tools6502 LANGUAGES CXX)

if(NOT TARGET perfect6502)
    add_subdirectory(
        ${CMAKE_CURRENT_LIST_DIR}/../harness/third_party/perfect6502
        ${CMAKE_CURRENT_BINARY_DIR}/perfect6502
    )
endif()

add_library(tools6502 STATIC
    asm6502/src/asm6502.cpp
    common/include/tools6502/testcase_collections.hpp
    common/src/nmos6502_opcode_testcases.cpp
    lockstep/include/tools6502/lockstep.hpp
    lockstep/src/lockstep.cpp
    cpu6502_bridge/include/cpu6502_bridge/cpu.hpp
    cpu6502_bridge/src/cpu.cpp
    cpu6502_bridge/src/qe6502_backend.cpp
    cpu6502_bridge/src/perfect6502_backend.cpp
)
add_library(tools6502::tools6502 ALIAS tools6502)

target_include_directories(tools6502
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/asm6502/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/common/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/lockstep/include>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/cpu6502_bridge/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(tools6502
    PRIVATE
        qe6502::static
        perfect6502
)

qe6502_configure_warnings(tools6502)
target_compile_features(tools6502 PUBLIC cxx_std_17)

if(QE6502_INSTALL)
    install(TARGETS tools6502
        EXPORT qe6502ToolsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/asm6502/include/asm6502
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/common/include/tools6502
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/lockstep/include/tools6502
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/cpu6502_bridge/include/cpu6502_bridge
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

if(QE6502_BUILD_TESTS)
    add_executable(asm6502_smoke
        asm6502/tests/smoke.cpp
    )
    target_link_libraries(asm6502_smoke PRIVATE tools6502::tools6502)
    qe6502_configure_warnings(asm6502_smoke)
    target_compile_features(asm6502_smoke PRIVATE cxx_std_17)
    add_test(NAME asm6502.smoke COMMAND asm6502_smoke)

    add_executable(tools6502_common_smoke
        common/tests/smoke.cpp
    )
    target_link_libraries(tools6502_common_smoke PRIVATE tools6502::tools6502)
    qe6502_configure_warnings(tools6502_common_smoke)
    target_compile_features(tools6502_common_smoke PRIVATE cxx_std_17)
    add_test(NAME tools6502.common.smoke COMMAND tools6502_common_smoke)

    add_executable(tools6502_lockstep_smoke
        lockstep/tests/smoke.cpp
    )
    target_link_libraries(tools6502_lockstep_smoke PRIVATE tools6502::tools6502)
    qe6502_configure_warnings(tools6502_lockstep_smoke)
    target_compile_features(tools6502_lockstep_smoke PRIVATE cxx_std_17)
    add_test(NAME tools6502.lockstep.smoke COMMAND tools6502_lockstep_smoke)
endif()

add_executable(cpu6502_bridge_fetch_compare
    cpu6502_bridge/tools/compare_fetch_log.cpp
)
target_link_libraries(cpu6502_bridge_fetch_compare PRIVATE tools6502::tools6502)
qe6502_configure_warnings(cpu6502_bridge_fetch_compare)
target_compile_features(cpu6502_bridge_fetch_compare PRIVATE cxx_std_17)
