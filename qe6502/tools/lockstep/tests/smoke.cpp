#include <tools6502/lockstep.hpp>

#include <asm6502/asm6502.h>

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void test_cpu_names_are_available()
{
    const auto qe = cpu6502_bridge::make_qe6502_cpu();
    const auto pf = cpu6502_bridge::make_perfect6502_cpu();

    require(std::string(qe->get_name()) == "qe6502", "unexpected qe6502 name");
    require(std::string(pf->get_name()) == "perfect6502", "unexpected perfect6502 name");
}

void test_bootstrapped_testcase_runs_to_start_and_steps()
{
    tools6502::testcase test{};
    test.start_at = 0x0400u;
    test.program = asm6502::Asm6502::New()
        .begin()
        .org(test.start_at)
            .nop()
            .nop()
            .jmp(test.start_at)
        .end()
        .compile();

    tools6502::LockstepRunner runner(cpu6502_bridge::make_qe6502_cpu(),
                                     cpu6502_bridge::make_perfect6502_cpu());

    require(runner.setup_and_run(test), "setup_and_run(testcase) failed");
    require(runner.left().is_opcode_fetch(), "left not at fetch after testcase setup");
    require(runner.right().is_opcode_fetch(), "right not at fetch after testcase setup");
    require(runner.left().bus_address() == test.start_at, "left not at testcase start");
    require(runner.right().bus_address() == test.start_at, "right not at testcase start");

    const auto result = runner.step_cycles(4u);
    require(!result.first_mismatch, "unexpected mismatch in bootstrapped testcase");
    require(result.left_trace.size() == 4u, "left trace size mismatch");
    require(result.right_trace.size() == 4u, "right trace size mismatch");
    require(result.left_trace.front().address == test.start_at, "left trace did not start at program");
    require(result.right_trace.front().address == test.start_at, "right trace did not start at program");
}

void test_config_callback_can_install_full_memory_image()
{
    tools6502::LockstepConfig config{};
    config.memory = tools6502::MemoryCallback{
        [](tools6502::memory_image& memory) {
            memory.fill(0xeau);
            memory[0xfffcu] = 0x00u;
            memory[0xfffdu] = 0x04u;
            memory[0x0400u] = 0xeau;
            memory[0x0401u] = 0x4cu;
            memory[0x0402u] = 0x00u;
            memory[0x0403u] = 0x04u;
        }
    };

    tools6502::LockstepRunner runner(cpu6502_bridge::make_qe6502_cpu(),
                                     cpu6502_bridge::make_perfect6502_cpu());

    require(runner.setup_and_run(config), "setup_and_run(config) failed");
    require(runner.left().bus_address() == 0x0400u, "left not at raw reset vector");
    require(runner.right().bus_address() == 0x0400u, "right not at raw reset vector");

    const auto result = runner.step_cycles(2u);
    require(!result.first_mismatch, "unexpected mismatch in config callback setup");
}

} // namespace

int main()
{
    test_cpu_names_are_available();
    test_bootstrapped_testcase_runs_to_start_and_steps();
    test_config_callback_can_install_full_memory_image();
    return 0;
}
