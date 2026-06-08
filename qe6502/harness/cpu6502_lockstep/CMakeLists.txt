enable_language(CXX)

function(add_cpu6502_lockstep_test target test_name source)
    add_executable(${target}
        ${source}
        common.cpp
    )
    target_link_libraries(${target}
        PRIVATE
            tools6502::tools6502
    )
    qe6502_configure_warnings(${target})
    target_compile_features(${target} PRIVATE cxx_std_17)
    add_test(NAME qe6502.lockstep.${test_name}
             COMMAND ${target})
    set_tests_properties(qe6502.lockstep.${test_name}
        PROPERTIES
            LABELS long
            TIMEOUT 1800
    )
endfunction()

add_cpu6502_lockstep_test(
    cpu6502_lockstep_nmi_irq_pulse_sweep
    nmi_irq_pulse_sweep
    nmi_irq_pulse_sweep.cpp
)

add_cpu6502_lockstep_test(
    cpu6502_lockstep_i_flag_windows
    i_flag_windows
    i_flag_windows.cpp
)

add_cpu6502_lockstep_test(
    cpu6502_lockstep_interrupt_arbitration
    interrupt_arbitration
    interrupt_arbitration.cpp
)
