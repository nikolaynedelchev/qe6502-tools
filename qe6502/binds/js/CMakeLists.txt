add_executable(qe6502_js
    qe6502_js.c
)

target_compile_features(qe6502_js PRIVATE c_std_11)

target_compile_options(qe6502_js PRIVATE
    -ffreestanding
    -fno-builtin
)

qe6502_configure_warnings(qe6502_js)

target_link_libraries(qe6502_js
    PRIVATE
        -Wl,--whole-archive
        qe6502_static
        -Wl,--no-whole-archive
)

target_link_options(qe6502_js PRIVATE
    -nostdlib
    LINKER:--no-entry
    LINKER:--export-memory
    LINKER:--export=qe6502js_context_pool_reset
    LINKER:--export=qe6502js_context_alloc
    LINKER:--export=qe6502js_context_free
    LINKER:--export=qe6502abi_version
    LINKER:--export=qe6502abi_setup
    LINKER:--export=qe6502abi_restart
    LINKER:--export=qe6502abi_goto
    LINKER:--export=qe6502abi_irq_assert
    LINKER:--export=qe6502abi_is_irq_asserted
    LINKER:--export=qe6502abi_save
    LINKER:--export=qe6502abi_load
    LINKER:--export=qe6502abi_nmi_assert
    LINKER:--export=qe6502abi_is_nmi_asserted
    LINKER:--export=qe6502abi_tick
    LINKER:--export=qe6502abi_get_pc
    LINKER:--export=qe6502abi_set_pc
    LINKER:--export=qe6502abi_get_s
    LINKER:--export=qe6502abi_set_s
    LINKER:--export=qe6502abi_get_a
    LINKER:--export=qe6502abi_set_a
    LINKER:--export=qe6502abi_get_x
    LINKER:--export=qe6502abi_set_x
    LINKER:--export=qe6502abi_get_y
    LINKER:--export=qe6502abi_set_y
    LINKER:--export=qe6502abi_get_p
    LINKER:--export=qe6502abi_set_p
    LINKER:--export=qe6502abi_get_model
    LINKER:--export=qe6502abi_set_model
)

set_target_properties(qe6502_js PROPERTIES
    OUTPUT_NAME "qe6502_js"
    SUFFIX ".wasm"
)
