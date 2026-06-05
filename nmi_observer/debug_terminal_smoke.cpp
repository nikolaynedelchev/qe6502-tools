#include "debug_terminal.h"

#include <asm6502/asm6502.h>

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

namespace {

bool contains(const std::string& text, const std::string& marker)
{
    return text.find(marker) != std::string::npos;
}

} // namespace

int main()
{
    nmi_observer::DebugTerminal terminal;

    const std::string help = terminal.execute_command("help");
    if (!contains(help, "DebugTerminal commands")
        || !contains(help, "TestDebugger commands")
        || !contains(help, "tests_details_0x50_1")
        || !contains(help, "log_mem_0x0400_to_0x04ff")
        || !contains(help, "cycle_details_on/off")
        || !contains(help, "log_registers_on/off")
        || !contains(help, "step_N")) {
        std::cerr << "DebugTerminal help is missing expected markers\n" << help;
        return EXIT_FAILURE;
    }

    const std::string opcodes = terminal.execute_command("list_opcodes");
    if (!contains(opcodes, "0x50:")) {
        std::cerr << "list_opcodes did not include opcode 0x50\n" << opcodes;
        return EXIT_FAILURE;
    }

    const std::string tests = terminal.execute_command("tests_0x50");
    if (!contains(tests, "tests opcode=$50") || !contains(tests, "desc=")) {
        std::cerr << "tests_0x50 did not include testcase descriptions\n" << tests;
        return EXIT_FAILURE;
    }

    const std::string details = terminal.execute_command("tests_details_0x50_0");
    if (!contains(details, "testcase opcode=$50 index=0")
        || !contains(details, "registers")
        || !contains(details, "program/memory bytes=")) {
        std::cerr << "tests_details_0x50_0 did not include full testcase details\n" << details;
        return EXIT_FAILURE;
    }

    std::string log;
    log += terminal.execute_command("backend_qe6502");
    log += terminal.execute_command("load_test_0x50_0");
    log += terminal.execute_command("status");
    log += terminal.execute_command("restart_to_test");
    log += terminal.execute_command("cycle_details");
    log += terminal.execute_command("cycle_details_on");
    log += terminal.execute_command("step_2");
    log += terminal.execute_command("cycle_details_off");
    log += terminal.execute_command("log_mem_0x0400_to_0x0403");
    log += terminal.execute_command("reload");
    log += terminal.execute_command("backend_perfect6502");
    log += terminal.execute_command("status");

    if (!contains(log, "backend=qe6502")
        || !contains(log, "loaded testcase opcode=$50 index=0")
        || !contains(log, "restart_to_test")
        || !contains(log, "cycle_details")
        || !contains(log, "cycle_details=on")
        || !contains(log, "cycle_details=off")
        || !contains(log, "mem $0400..$0403")
        || !contains(log, "backend=perfect6502")
        || !contains(log, "reloaded testcase opcode=$50 index=0")) {
        std::cerr << "terminal testcase flow did not include expected markers\n" << log;
        return EXIT_FAILURE;
    }

    const std::vector<asm6502::mem_value> program = asm6502::Asm6502::New()
        .begin()
            .org(0x8000u, "start")
                .nop()
                .nop()
                .jmp("done")
            .org(0x8005u, "done")
                .nop()
                .jmp("done")
            .org(0xFFFCu)
                .dw("start")
        .end()
        .compile();

    std::string program_log;
    program_log += terminal.use_qe6502_backend();
    program_log += terminal.load_program("smoke_raw", program);
    program_log += terminal.execute_script("restart_to_test run_to_0x8005 log_bus_state");
    program_log += terminal.reload();
    program_log += terminal.execute_command("status");

    if (!contains(program_log, "loaded program name=\"smoke_raw\"")
        || !contains(program_log, "restart_to_start_fetch")
        || !contains(program_log, "run_to address=$8005")
        || !contains(program_log, "reloaded program name=\"smoke_raw\"")
        || !contains(program_log, "loaded=program")) {
        std::cerr << "terminal raw program flow did not include expected markers\n" << program_log;
        return EXIT_FAILURE;
    }

    std::cout << help << opcodes << tests << details << log << program_log;
    return EXIT_SUCCESS;
}
