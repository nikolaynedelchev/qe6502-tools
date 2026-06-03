find_program(QE6502_NODE_EXECUTABLE node)

if(NOT QE6502_NODE_EXECUTABLE)
    message(FATAL_ERROR "qe6502_js_smoke requires Node.js when QE6502_BUILD_WASM and BUILD_TESTING are enabled")
endif()

if(QE6502_NODE_EXECUTABLE)
    add_test(
        NAME qe6502_js_smoke
        COMMAND
            "${QE6502_NODE_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/js_smoke.mjs"
            "$<TARGET_FILE:qe6502_js>"
    )

    set_tests_properties(qe6502_js_smoke PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    )
endif()
