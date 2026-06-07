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
        tools6502::tools6502
)

target_include_directories(${target}
    PRIVATE
        ${PROJECT_SOURCE_DIR}/qe6502/binds/cpp/include
        ${PROJECT_SOURCE_DIR}/qe6502/cpu/include
        ${PROJECT_SOURCE_DIR}/qe6502/harness/third_party/perfect6502
)

qe6502_configure_warnings(${target})

target_compile_features(${target} PUBLIC cxx_std_20)


add_executable(cpu6502_interrupt_observer
    cpu6502_interrupt_observer.cpp
    testaces.cpp
)

target_link_libraries(cpu6502_interrupt_observer
    PRIVATE
        tools6502::tools6502
)

qe6502_configure_warnings(cpu6502_interrupt_observer)
target_compile_features(cpu6502_interrupt_observer PUBLIC cxx_std_20)

add_library(nmi_observer_scripted_testcase_runner STATIC
    test_debugger.cpp
    test_debugger.h
    scripted_testcase_runner.cpp
    scripted_testcase_runner.h
    debug_terminal.cpp
    debug_terminal.h
)

target_link_libraries(nmi_observer_scripted_testcase_runner
    PUBLIC
        tools6502::tools6502
)

target_include_directories(nmi_observer_scripted_testcase_runner
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

qe6502_configure_warnings(nmi_observer_scripted_testcase_runner)
target_compile_features(nmi_observer_scripted_testcase_runner PUBLIC cxx_std_20)

add_executable(nmi_observer_scripted_testcase_runner_smoke
    scripted_testcase_runner_smoke.cpp
)

target_link_libraries(nmi_observer_scripted_testcase_runner_smoke
    PRIVATE
        nmi_observer_scripted_testcase_runner
)

qe6502_configure_warnings(nmi_observer_scripted_testcase_runner_smoke)
target_compile_features(nmi_observer_scripted_testcase_runner_smoke PUBLIC cxx_std_20)

add_test(NAME nmi_observer_scripted_testcase_runner_smoke COMMAND nmi_observer_scripted_testcase_runner_smoke)


add_executable(nmi_observer_debug_terminal_smoke
    debug_terminal_smoke.cpp
)

target_link_libraries(nmi_observer_debug_terminal_smoke
    PRIVATE
        nmi_observer_scripted_testcase_runner
)

qe6502_configure_warnings(nmi_observer_debug_terminal_smoke)
target_compile_features(nmi_observer_debug_terminal_smoke PUBLIC cxx_std_20)

add_test(NAME nmi_observer_debug_terminal_smoke COMMAND nmi_observer_debug_terminal_smoke)

add_executable(nmi_observer_debug_console
    debug_console.cpp
)

target_link_libraries(nmi_observer_debug_console
    PRIVATE
        nmi_observer_scripted_testcase_runner
)

qe6502_configure_warnings(nmi_observer_debug_console)
target_compile_features(nmi_observer_debug_console PUBLIC cxx_std_20)

add_test(NAME nmi_observer_debug_console_smoke
    COMMAND nmi_observer_debug_console
        --no-banner
        --backend qe6502
        --run-script-file ${CMAKE_CURRENT_SOURCE_DIR}/debug_console_smoke_script.txt
        --command terminal_help
        --command detailed_help
        --exit-after-script
)

