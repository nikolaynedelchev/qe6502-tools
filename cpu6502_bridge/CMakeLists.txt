enable_language(CXX)

if(NOT TARGET perfect6502)
    add_subdirectory(
        ${CMAKE_SOURCE_DIR}/qe6502/harness/third_party/perfect6502
        ${CMAKE_BINARY_DIR}/cpu6502_bridge/perfect6502
    )
endif()

add_library(cpu6502_bridge STATIC
    include/cpu6502_bridge/cpu.hpp
    src/cpu.cpp
    src/qe6502_backend.cpp
    src/perfect6502_backend.cpp
)

add_library(cpu6502_bridge::cpu6502_bridge ALIAS cpu6502_bridge)

target_include_directories(cpu6502_bridge
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_link_libraries(cpu6502_bridge
    PUBLIC
        qe6502::static
    PRIVATE
        perfect6502
)

qe6502_configure_warnings(cpu6502_bridge)
target_compile_features(cpu6502_bridge PUBLIC cxx_std_17)

if(QE6502_INSTALL)
    install(TARGETS cpu6502_bridge
        EXPORT qe6502ToolsTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/cpu6502_bridge
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()

add_executable(cpu6502_bridge_fetch_compare
    tools/compare_fetch_log.cpp
)
target_link_libraries(cpu6502_bridge_fetch_compare PRIVATE cpu6502_bridge::cpu6502_bridge)
qe6502_configure_warnings(cpu6502_bridge_fetch_compare)
target_compile_features(cpu6502_bridge_fetch_compare PRIVATE cxx_std_17)
