#include "common.hpp"

#include <asm6502/asm6502.h>
#include <cpu6502_bridge/cpu.hpp>
#include <lockstep.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

namespace {

constexpr std::uint16_t program_start = 0x0400u;
constexpr std::uint16_t nmi_handler = 0x9000u;
constexpr std::uint16_t brk_irq_handler = 0x9100u;

constexpr std::size_t min_pulse_length = 1u;
constexpr std::size_t max_pulse_start_cycle = 8u;
constexpr std::size_t pulse_end_cycle_limit = 10u;
constexpr std::size_t scenario_total_steps = 28u;

struct ArbitrationCase
{
    const char* name = "";
    tools6502::testcase test{};
};

struct GroupStats
{
    std::size_t scenarios = 0u;
    std::size_t passed = 0u;
    std::size_t failures = 0u;
    std::size_t nmi_first_handler_fetches = 0u;
    std::size_t brk_irq_first_handler_fetches = 0u;
};

struct SummaryStats
{
    std::size_t groups_total = 0u;
    std::size_t groups_passed = 0u;
    std::size_t groups_failed = 0u;
    std::size_t scenarios_total = 0u;
    std::size_t scenarios_passed = 0u;
    std::size_t scenarios_failed = 0u;
    std::size_t nmi_first_handler_fetches = 0u;
    std::size_t brk_irq_first_handler_fetches = 0u;
};

struct FirstFailure
{
    bool set = false;
    std::string group{};
    std::size_t pulse_start = 0u;
    std::size_t pulse_length = 0u;
    tools6502::LockstepScenarioResult scenario_result{};
};

enum class FirstHandlerFetch
{
    None,
    Nmi,
    BrkIrq,
};

tools6502::memory_setup make_handlers()
{
    return asm6502::Asm6502::New()
        .begin()
            .org(nmi_handler, "nmi_handler")
                .nop()
                .jmp("nmi_handler")
            .org(brk_irq_handler, "brk_irq_handler")
                .nop()
                .jmp("brk_irq_handler")
        .end().compile();
}

ArbitrationCase make_brk_case(const char* name)
{
    auto program = asm6502::Asm6502::New()
        .begin()
            .org(program_start)
                .brk()
                .nop()
                .nop()
                .nop()
        .end().compile();

    const auto handlers = make_handlers();
    program.insert(program.end(), handlers.begin(), handlers.end());

    tools6502::testcase test{};
    test.opcode = 0x00u;
    test.bytes = 1u;
    test.start_at = program_start;
    test.vectors.reset = 0x0200u;
    test.vectors.nmi = nmi_handler;
    test.vectors.brk_irq = brk_irq_handler;
    test.A = 0x00u;
    test.X = 0x00u;
    test.Y = 0x00u;
    test.P = 0x24u;
    test.S = 0xfdu;
    test.program = std::move(program);
    test.description = name;

    return ArbitrationCase{name, std::move(test)};
}

FirstHandlerFetch observe_first_handler_fetch(const tools6502::LockstepScenarioResult& result)
{
    for (const auto& command_result : result.results) {
        for (const auto& entry : command_result.left_trace) {
            if (!entry.opcode_fetch) {
                continue;
            }
            if (entry.address == nmi_handler) {
                return FirstHandlerFetch::Nmi;
            }
            if (entry.address == brk_irq_handler) {
                return FirstHandlerFetch::BrkIrq;
            }
        }
    }

    return FirstHandlerFetch::None;
}

bool run_one_pulse_scenario(tools6502::LockstepScenarioRunner& runner,
                            bool nmi,
                            std::size_t pulse_start,
                            std::size_t pulse_length,
                            const tools6502::LockstepScenarioConfig& scenario_config,
                            SummaryStats& summary,
                            GroupStats& group,
                            FirstFailure& first_failure,
                            const char* group_name)
{
    const auto script = cpu6502_lockstep::make_pulse_script(
        nmi,
        pulse_start,
        pulse_length,
        scenario_total_steps);

    const auto result = runner.restart_run(script, scenario_config);

    ++summary.scenarios_total;
    ++group.scenarios;

    const auto first_handler = observe_first_handler_fetch(result);
    if (first_handler == FirstHandlerFetch::Nmi) {
        ++summary.nmi_first_handler_fetches;
        ++group.nmi_first_handler_fetches;
    }
    if (first_handler == FirstHandlerFetch::BrkIrq) {
        ++summary.brk_irq_first_handler_fetches;
        ++group.brk_irq_first_handler_fetches;
    }

    if (result.passed) {
        ++summary.scenarios_passed;
        ++group.passed;
        return true;
    }

    ++summary.scenarios_failed;
    ++group.failures;

    if (!first_failure.set) {
        first_failure.set = true;
        first_failure.group = group_name;
        first_failure.pulse_start = pulse_start;
        first_failure.pulse_length = pulse_length;
        first_failure.scenario_result = result;
    }

    return false;
}

GroupStats run_pulse_group(const char* group_name,
                           const ArbitrationCase& test_case,
                           bool nmi,
                           SummaryStats& summary,
                           FirstFailure& first_failure)
{
    tools6502::LockstepConfig lockstep_config{};
    lockstep_config.memory = tools6502::MemoryFill{0xeau};
    lockstep_config.compare.address = true;
    lockstep_config.compare.data = true;
    lockstep_config.compare.read_write = true;
    lockstep_config.compare.opcode_fetch = true;
    lockstep_config.compare.registers_on_fetch = false;

    tools6502::LockstepScenarioConfig scenario_config{};
    scenario_config.stop_on_failure = true;

    tools6502::LockstepScenarioRunner runner(
        cpu6502_bridge::make_qe6502_cpu(),
        cpu6502_bridge::make_perfect6502_cpu());

    GroupStats group{};
    if (!runner.setup(test_case.test, lockstep_config)) {
        ++summary.scenarios_total;
        ++summary.scenarios_failed;
        ++group.scenarios;
        ++group.failures;
        if (!first_failure.set) {
            first_failure.set = true;
            first_failure.group = group_name;
        }
        return group;
    }

    for (std::size_t pulse_start = 0u;
         pulse_start <= max_pulse_start_cycle;
         ++pulse_start) {
        const std::size_t max_pulse_length = pulse_end_cycle_limit - pulse_start;
        for (std::size_t pulse_length = min_pulse_length;
             pulse_length <= max_pulse_length;
             ++pulse_length) {
            run_one_pulse_scenario(runner,
                                   nmi,
                                   pulse_start,
                                   pulse_length,
                                   scenario_config,
                                   summary,
                                   group,
                                   first_failure,
                                   group_name);
        }
    }

    return group;
}

} // namespace

int main()
{
    SummaryStats summary{};
    FirstFailure first_failure{};

    const auto brk_nmi_case = make_brk_case("BRK + NMI hijack windows");
    const auto brk_irq_case = make_brk_case("BRK + IRQ windows");

    struct GroupSpec
    {
        const char* name;
        const ArbitrationCase* test_case;
        bool nmi;
    };

    const std::vector<GroupSpec> groups{
        {"BRK + NMI hijack windows", &brk_nmi_case, true},
        {"BRK + IRQ windows", &brk_irq_case, false},
    };

    for (const auto& spec : groups) {
        ++summary.groups_total;
        const auto group = run_pulse_group(
            spec.name,
            *spec.test_case,
            spec.nmi,
            summary,
            first_failure);

        const bool group_passed = group.failures == 0u;
        if (group_passed) {
            ++summary.groups_passed;
        } else {
            ++summary.groups_failed;
        }

        std::cout << spec.name << ": " << (group_passed ? "PASS" : "FAIL")
                  << " scenarios=" << group.scenarios
                  << " failures=" << group.failures
                  << " hijacks=" << (spec.nmi ? group.nmi_first_handler_fetches : 0u)
                  << " nmi_first_handler_fetches=" << group.nmi_first_handler_fetches
                  << " brk_irq_first_handler_fetches=" << group.brk_irq_first_handler_fetches
                  << '\n' << std::flush;
    }

    std::cout << "\nsummary\n"
              << "  groups:    total=" << summary.groups_total
              << " pass=" << summary.groups_passed
              << " fail=" << summary.groups_failed << '\n'
              << "  scenarios: total=" << summary.scenarios_total
              << " pass=" << summary.scenarios_passed
              << " fail=" << summary.scenarios_failed << '\n'
              << "  first handler fetches: nmi=" << summary.nmi_first_handler_fetches
              << " brk_irq=" << summary.brk_irq_first_handler_fetches << '\n';

    if (first_failure.set) {
        std::cout << "\nfirst failure\n"
                  << "  group:        " << first_failure.group << '\n'
                  << "  pulse_start:  " << first_failure.pulse_start << '\n'
                  << "  pulse_length: " << first_failure.pulse_length << '\n';

        if (first_failure.scenario_result.first_failed_command) {
            const auto command_index = *first_failure.scenario_result.first_failed_command;
            std::cout << "  command:      " << command_index << '\n';
            if (command_index < first_failure.scenario_result.results.size()) {
                cpu6502_lockstep::print_failure_trace(
                    std::cout,
                    first_failure.scenario_result.results[command_index]);
            }
        }
    }

    return summary.scenarios_failed == 0u ? 0 : 1;
}
