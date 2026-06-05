#pragma once

#include "nmi_observer.h"

#include <asm6502/asm6502.h>
#include <cpu6502_bridge/cpu.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nmi_observer {

class TestDebugger {
public:
    void load_testcase(cpu6502_bridge::ICpu& cpu, const testcase& test);
    void load_program(cpu6502_bridge::ICpu& cpu, const std::vector<asm6502::mem_value>& program);

    std::string execute_command(std::string_view command);
    std::string execute_script(std::string_view script);

    std::string help() const;

    std::string restart();
    std::string restart_to_start_fetch();
    std::string restart_to_test();

    std::string step();
    std::string step(unsigned count);
    std::string run_to(std::uint16_t address, unsigned max_steps = default_run_to_cycle_limit);

    std::string irq_assert();
    std::string irq_deassert();
    std::string nmi_assert();
    std::string nmi_deassert();

    std::string log_registers() const;
    std::string log_bus_state() const;
    std::string log_stack() const;
    std::string log_mem(std::uint16_t first, std::uint16_t last) const;
    std::string log_mem_0xaa_to_0xac() const;
    std::string log_vectors() const;
    std::string cycle_details() const;

    std::string log_registers_on();
    std::string log_registers_off();
    std::string log_bus_state_on();
    std::string log_bus_state_off();
    std::string log_stack_on();
    std::string log_stack_off();
    std::string log_vectors_on();
    std::string log_vectors_off();
    std::string cycle_details_on();
    std::string cycle_details_off();

    static constexpr unsigned default_run_to_cycle_limit = 100000u;

private:
    enum class loaded_kind {
        none,
        testcase,
        program,
    };

    cpu6502_bridge::ICpu& cpu();
    cpu6502_bridge::ICpu& cpu() const;

    void reset_loaded_memory();
    std::string after_step_logs() const;

    cpu6502_bridge::ICpu* cpu_ = nullptr;
    loaded_kind loaded_ = loaded_kind::none;
    std::optional<testcase> test_{};
    std::vector<asm6502::mem_value> program_{};
    unsigned cycle_ = 0;
    bool after_step_log_registers_ = false;
    bool after_step_log_bus_state_ = false;
    bool after_step_log_stack_ = false;
    bool after_step_log_vectors_ = false;
    bool after_step_cycle_details_ = false;
};

std::string run_scripted_testcase(
    cpu6502_bridge::ICpu& cpu,
    const testcase& test,
    std::string_view script);

std::string run_scripted_program(
    cpu6502_bridge::ICpu& cpu,
    const std::vector<asm6502::mem_value>& program,
    std::string_view script);

} // namespace nmi_observer
