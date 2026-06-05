#pragma once

#include "nmi_observer.h"
#include "test_debugger.h"

#include <asm6502/asm6502.h>
#include <cpu6502_bridge/cpu.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nmi_observer {

class DebugTerminal {
public:
    enum class backend_kind {
        qe6502,
        perfect6502,
    };

    enum class loaded_kind {
        none,
        testcase,
        program,
    };

    DebugTerminal();

    std::string execute_command(std::string_view command);
    std::string execute_script(std::string_view script);

    std::string help() const;
    std::string status() const;

    std::string use_qe6502_backend();
    std::string use_perfect6502_backend();

    std::string list_opcodes() const;
    std::string list_tests(std::uint8_t opcode) const;
    std::string test_details(std::uint8_t opcode, std::size_t index) const;
    std::string load_testcase(std::uint8_t opcode, std::size_t index);

    std::string load_program(std::string name, const std::vector<asm6502::mem_value>& program);
    std::string reload();

    TestDebugger& debugger() noexcept;
    const TestDebugger& debugger() const noexcept;

private:
    void create_cpu();
    std::string reload_after_backend_change();
    const testcase& testcase_at(std::uint8_t opcode, std::size_t index) const;

    backend_kind backend_ = backend_kind::qe6502;
    std::unique_ptr<cpu6502_bridge::ICpu> cpu_{};
    TestDebugger debugger_{};

    loaded_kind loaded_ = loaded_kind::none;
    std::optional<std::uint8_t> loaded_opcode_{};
    std::optional<std::size_t> loaded_test_index_{};
    std::optional<testcase> loaded_test_{};
    std::vector<asm6502::mem_value> loaded_program_{};
    std::string loaded_program_name_{};
};

} // namespace nmi_observer
