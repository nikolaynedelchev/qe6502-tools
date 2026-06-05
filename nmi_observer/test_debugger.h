#pragma once

#include <cpu6502_bridge/cpu.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace nmi_observer {

class TestDebugger {
public:
    void attach_cpu(cpu6502_bridge::ICpu& cpu);

    std::string execute_command(std::string_view command);
    std::string execute_script(std::string_view script);

    std::string help() const;

    std::string restart();
    std::string restart_to_start_fetch();

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
    cpu6502_bridge::ICpu& cpu();
    cpu6502_bridge::ICpu& cpu() const;

    std::string after_step_logs() const;

    cpu6502_bridge::ICpu* cpu_ = nullptr;
    unsigned cycle_ = 0;
    bool after_step_log_registers_ = false;
    bool after_step_log_bus_state_ = false;
    bool after_step_log_stack_ = false;
    bool after_step_log_vectors_ = false;
    bool after_step_cycle_details_ = false;
};

} // namespace nmi_observer
