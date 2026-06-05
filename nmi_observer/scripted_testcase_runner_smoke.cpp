#include "test_debugger.h"

#include <cpu6502_bridge/cpu.hpp>

#include <asm6502/asm6502.h>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

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

nmi_observer::testcase make_smoke_testcase()
{
    nmi_observer::testcase test{};
    test.opcode = 0xEAu;
    test.bytes = 1u;
    test.expected_cycles = 2u;
    test.start_at = 0x9000u;
    test.nmi_vector = 0xE200u;
    test.A = 0x11u;
    test.X = 0x22u;
    test.Y = 0x33u;
    test.P = 0x24u;
    test.S = 0xF8u;
    test.description = "scripted debugger smoke NOP";
    test.mem_setup = asm6502::Asm6502::New()
        .begin()
            .org(test.start_at, "test_start")
                .nop()
                .nop()
                .jmp("test_start")
        .end()
        .compile();
    return test;
}

} // namespace

int main()
{
    std::unique_ptr<cpu6502_bridge::ICpu> cpu = cpu6502_bridge::make_qe6502_cpu();
    const nmi_observer::testcase test = make_smoke_testcase();

    nmi_observer::TestDebugger testcase_debugger;
    testcase_debugger.load_testcase(*cpu, test);
    const std::string testcase_log = testcase_debugger.execute_script(
        "restart_to_test\n"
        "cycle_details\n"
        "log_registers_on log_bus_state_on step_2 log_registers_off log_bus_state_off\n"
        "nmi_assert step nmi_deassert\n"
        "log_registers log_bus_state log_stack log_mem_0x00aa_to_0x00ac log_vectors\n");

    const bool testcase_ok = contains(testcase_log, "restart_to_test")
        && contains(testcase_log, "aligned=yes")
        && contains(testcase_log, "cycle_details")
        && contains(testcase_log, "log_registers=on")
        && contains(testcase_log, "log_bus_state=on")
        && contains(testcase_log, "log_registers=off")
        && contains(testcase_log, "nmi=asserted")
        && contains(testcase_log, "mem $00AA..$00AC")
        && count_occurrences(testcase_log, "stepped cycle=") >= 3u
        && count_occurrences(testcase_log, "registers") >= 4u
        && count_occurrences(testcase_log, "bus") >= 4u;

    if (!testcase_ok) {
        std::cerr << "TestDebugger testcase smoke output did not contain expected markers\n";
        std::cerr << testcase_log;
        return EXIT_FAILURE;
    }

    const std::vector<asm6502::mem_value> program = asm6502::Asm6502::New()
        .begin()
            .org(0x8000u, "start")
                .nop()
                .nop()
                .lda(0x42u)
                .jmp("done")
            .org(0x8007u, "done")
                .nop()
                .jmp("done")
            .org(0xFFFCu)
                .dw("start")
        .end()
        .compile();

    nmi_observer::TestDebugger program_debugger;
    program_debugger.load_program(*cpu, program);

    std::string program_log;
    program_log += program_debugger.restart_to_test(); // raw program: same as restart_to_start_fetch
    program_log += program_debugger.execute_command("log_bus_state_on");
    program_log += program_debugger.execute_command("run_to_0x8007");
    program_log += program_debugger.execute_command("log_bus_state_off");
    program_log += program_debugger.log_registers();

    const bool program_ok = contains(program_log, "restart_to_start_fetch")
        && contains(program_log, "run_to address=$8007")
        && contains(program_log, "log_bus_state=on")
        && contains(program_log, "log_bus_state=off")
        && contains(program_log, "reached=yes")
        && contains(program_log, "registers");

    if (!program_ok) {
        std::cerr << "TestDebugger program smoke output did not contain expected markers\n";
        std::cerr << program_log;
        return EXIT_FAILURE;
    }

    const std::string wrapper_log = nmi_observer::run_scripted_program(
        *cpu,
        program,
        "restart_to_start_fetch step_2 run_to_0x8007 log_bus_state");

    if (!contains(wrapper_log, "scripted_program") || !contains(wrapper_log, "run_to address=$8007")) {
        std::cerr << "run_scripted_program wrapper smoke output did not contain expected markers\n";
        std::cerr << wrapper_log;
        return EXIT_FAILURE;
    }

    const std::string testcase_wrapper_log = nmi_observer::run_scripted_testcase(
        *cpu,
        test,
        "restart_to_test run_to_0x9001 log_bus_state");

    if (!contains(testcase_wrapper_log, "scripted_testcase") || !contains(testcase_wrapper_log, "run_to address=$9001")) {
        std::cerr << "run_scripted_testcase wrapper smoke output did not contain expected markers\n";
        std::cerr << testcase_wrapper_log;
        return EXIT_FAILURE;
    }

    std::cout << testcase_log << program_log << wrapper_log << testcase_wrapper_log;
    return EXIT_SUCCESS;
}
