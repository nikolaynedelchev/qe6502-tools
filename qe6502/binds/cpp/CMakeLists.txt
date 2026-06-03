enable_language(CXX)

set(QE6502_CPP_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qe6502/cpu.hpp
)

add_library(qe6502_cpp_static STATIC
    ${QE6502_CPP_PUBLIC_HEADERS}
    src/cpu.cpp
)

target_link_libraries(qe6502_cpp_static
    PUBLIC qe6502::static
)

target_include_directories(qe6502_cpp_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

qe6502_configure_warnings(qe6502_cpp_static)
target_compile_features(qe6502_cpp_static PUBLIC cxx_std_17)

set_target_properties(qe6502_cpp_static PROPERTIES
    EXPORT_NAME cpp
)

add_library(qe6502_cpp ALIAS qe6502_cpp_static)
add_library(qe6502::cpp ALIAS qe6502_cpp_static)

if(QE6502_INSTALL)
    install(TARGETS qe6502_cpp_static
        EXPORT qe6502Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )

    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/qe6502
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    )
endif()
