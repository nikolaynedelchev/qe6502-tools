#pragma once

#include "test_debugger.h"

#include <asm6502/asm6502.h>
#include <cpu6502_bridge/cpu.hpp>

#include <cstdint>
#include <memory>
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

    DebugTerminal();

    std::string execute_command(std::string_view command);
    std::string execute_script(std::string_view script);

    std::string help() const;
    std::string status() const;

    std::string use_qe6502_backend();
    std::string use_perfect6502_backend();

    std::string memory_clear(std::uint8_t value = 0x00u);
    std::string memory_fill(std::uint16_t first, std::uint16_t last, std::uint8_t value);
    std::string memory_random(std::uint16_t first, std::uint16_t last, std::uint32_t seed);
    std::string set_memory_byte(std::uint16_t address, std::uint8_t value);
    std::string load_program(std::string name, const std::vector<asm6502::mem_value>& program);
    std::string bootstrap(std::string_view options);

    TestDebugger& debugger() noexcept;
    const TestDebugger& debugger() const noexcept;

private:
    void create_cpu();
    bool has_backend() const noexcept;
    std::string require_backend_message() const;

    backend_kind backend_ = backend_kind::qe6502;
    bool backend_selected_ = false;
    std::unique_ptr<cpu6502_bridge::ICpu> cpu_{};
    TestDebugger debugger_{};
};

} // namespace nmi_observer
