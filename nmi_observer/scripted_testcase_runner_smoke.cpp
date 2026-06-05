#include "test_debugger.h"

#include <asm6502/asm6502.h>
#include <cpu6502_bridge/cpu.hpp>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {

bool contains(const std::string& text, const std::string& marker)
{
    return text.find(marker) != std::string::npos;
}

unsigned count_occurrences(const std::string& text, const std::string& marker)
{
    unsigned count = 0;
    std::size_t pos = 0;
    while ((pos = text.find(marker, pos)) != std::string::npos) {
        ++count;
        pos += marker.size();
    }
    return count;
}

} // namespace

int main()
{
    std::unique_ptr<cpu6502_bridge::ICpu> cpu = cpu6502_bridge::make_qe6502_cpu();
    std::fill(cpu->memory(), cpu->memory() + 65536u, 0x00u);

    const std::vector<asm6502::mem_value> boot = asm6502::bootstrap_program(
        0x11u, 0x22u, 0x33u, 0x24u, 0xF8u,
        0x8000u, 0x0200u, 0x9100u, 0x9000u);
    asm6502::Asm6502::apply(boot, cpu->memory());
    cpu->memory()[0x8000u] = 0xEAu;
    cpu->memory()[0x8001u] = 0xEAu;
    cpu->memory()[0x8002u] = 0x4Cu;
    cpu->memory()[0x8003u] = 0x02u;
    cpu->memory()[0x8004u] = 0x80u;

    nmi_observer::TestDebugger debugger;
    debugger.attach_cpu(*cpu);

    const std::string help = debugger.help();
    if (!contains(help, "restart_to_start_fetch")
        || !contains(help, "step_N")
        || !contains(help, "run_to_next_fetch")
        || contains(help, "restart_to_test")) {
        std::cerr << "TestDebugger help has unexpected/missing markers\n" << help;
        return EXIT_FAILURE;
    }

    std::string log;
    log += debugger.restart_to_start_fetch();
    log += debugger.execute_command("run_to_0x8000");
    log += debugger.execute_command("cycle_details");
    log += debugger.execute_command("run_to_next_fetch");
    log += debugger.execute_command("log_registers_on");
    log += debugger.execute_command("log_bus_state_on");
    log += debugger.execute_command("step_2");
    log += debugger.execute_command("log_registers_off");
    log += debugger.execute_command("log_bus_state_off");
    log += debugger.execute_command("nmi_assert");
    log += debugger.execute_command("step");
    log += debugger.execute_command("nmi_deassert");
    log += debugger.execute_command("log_mem_0x8000_to_0x8004");
    log += debugger.execute_command("log_vectors");

    const bool ok = contains(log, "restart_to_start_fetch")
        && contains(log, "run_to address=$8000")
        && contains(log, "reached=yes")
        && contains(log, "cycle_details")
        && contains(log, "run_to_next_fetch")
        && contains(log, "registers PC=$8000 A=$11 X=$22 Y=$33 S=$F8")
        && contains(log, "log_registers=on")
        && contains(log, "log_bus_state=on")
        && contains(log, "log_registers=off")
        && contains(log, "nmi=asserted")
        && contains(log, "mem $8000..$8004")
        && count_occurrences(log, "stepped cycle=") >= 3u
        && count_occurrences(log, "registers") >= 4u
        && count_occurrences(log, "bus") >= 4u;

    if (!ok) {
        std::cerr << "TestDebugger smoke output did not contain expected markers\n";
        std::cerr << log;
        return EXIT_FAILURE;
    }

    try {
        (void)debugger.execute_command("restart_to_test");
        std::cerr << "restart_to_test unexpectedly succeeded\n";
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        if (std::string(error.what()).find("unknown TestDebugger command") == std::string::npos) {
            std::cerr << "restart_to_test produced unexpected error: " << error.what() << "\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << help << log;
    return EXIT_SUCCESS;
}
