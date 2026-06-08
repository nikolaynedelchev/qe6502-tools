#include "common.hpp"

#include <cpu6502_bridge/cpu.hpp>
#include <lockstep.hpp>
#include <tools6502/testcase_collections.hpp>

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr std::size_t min_pulse_length = 1u;
constexpr std::size_t max_pulse_start_cycle = 8u;
constexpr std::size_t pulse_end_cycle_limit = 10u;
constexpr std::size_t scenario_total_steps = 20u;

struct SweepStats
{
    std::size_t opcodes_total = 0u;
    std::size_t opcodes_passed = 0u;
    std::size_t opcodes_failed = 0u;
    std::size_t opcodes_skipped = 0u;
    std::size_t testcases_total = 0u;
    std::size_t testcases_skipped = 0u;
    std::size_t scenarios_total = 0u;
    std::size_t scenarios_passed = 0u;
    std::size_t scenarios_failed = 0u;
    std::size_t nmi_scenarios_total = 0u;
    std::size_t nmi_scenarios_failed = 0u;
    std::size_t irq_scenarios_total = 0u;
    std::size_t irq_scenarios_failed = 0u;
};

struct OpcodeStats
{
    std::size_t testcases = 0u;
    std::size_t scenarios = 0u;
    std::size_t failures = 0u;
};


bool is_unstable_opcode(std::uint8_t opcode) noexcept
{
    switch (opcode) {
    case 0x0B:
    case 0x2B:
    case 0x4B:
    case 0x6B:
    case 0x8B:
    case 0xAB:
    case 0xBB:
        return true;
    default:
        return false;
    }
}

struct FirstFailure
{
    bool set = false;
    std::uint8_t opcode = 0u;
    std::string testcase_description{};
    const char* signal = "";
    std::size_t pulse_start = 0u;
    std::size_t pulse_length = 0u;
    tools6502::LockstepScenarioResult scenario_result{};
};

bool run_one_scenario(tools6502::LockstepScenarioRunner& runner,
                      const tools6502::testcase& test,
                      bool nmi,
                      std::size_t pulse_start,
                      std::size_t pulse_length,
                      const tools6502::LockstepScenarioConfig& scenario_config,
                      SweepStats& stats,
                      OpcodeStats& opcode_stats,
                      FirstFailure& first_failure)
{
    const auto script = cpu6502_lockstep::make_pulse_script(
        nmi,
        pulse_start,
        pulse_length,
        scenario_total_steps);

    const auto result = runner.restart_run(script, scenario_config);

    ++stats.scenarios_total;
    ++opcode_stats.scenarios;
    if (nmi) {
        ++stats.nmi_scenarios_total;
    } else {
        ++stats.irq_scenarios_total;
    }

    if (result.passed) {
        ++stats.scenarios_passed;
        return true;
    }

    ++stats.scenarios_failed;
    ++opcode_stats.failures;
    if (nmi) {
        ++stats.nmi_scenarios_failed;
    } else {
        ++stats.irq_scenarios_failed;
    }

    if (!first_failure.set) {
        first_failure.set = true;
        first_failure.opcode = test.opcode;
        first_failure.testcase_description = test.description;
        first_failure.signal = nmi ? "NMI" : "IRQ";
        first_failure.pulse_start = pulse_start;
        first_failure.pulse_length = pulse_length;
        first_failure.scenario_result = result;
    }

    return false;
}

} // namespace

int main()
{
    const auto testcases_by_opcode = tools6502::get_nmos6502_opcode_testcases();

    tools6502::LockstepConfig base_lockstep_config{};
    base_lockstep_config.memory = tools6502::MemoryUnchanged{};
    base_lockstep_config.compare.data = true;
    base_lockstep_config.compare.registers_on_fetch = false;

    tools6502::LockstepScenarioConfig scenario_config{};
    scenario_config.stop_on_failure = true;

    SweepStats stats{};
    FirstFailure first_failure{};

    for (const auto& [opcode, tests] : testcases_by_opcode) {
        ++stats.opcodes_total;
        stats.testcases_total += tests.size();

        OpcodeStats opcode_stats{};
        opcode_stats.testcases = tests.size();

        if (is_unstable_opcode(opcode)) {
            ++stats.opcodes_skipped;
            stats.testcases_skipped += tests.size();
            std::cout << "opcode " << cpu6502_lockstep::hex8(opcode)
                      << " done: SKIP unstable testcases=" << opcode_stats.testcases
                      << " scenarios=0 failures=0\n" << std::flush;
            continue;
        }

        bool opcode_passed = true;

        for (const auto& test : tests) {
            auto lockstep_config = base_lockstep_config;
            tools6502::LockstepScenarioRunner runner(
                cpu6502_bridge::make_qe6502_cpu(),
                cpu6502_bridge::make_perfect6502_cpu());

            if (!runner.setup(test, lockstep_config)) {
                opcode_passed = false;
                ++stats.scenarios_total;
                ++stats.scenarios_failed;
                ++opcode_stats.scenarios;
                ++opcode_stats.failures;
                if (!first_failure.set) {
                    first_failure.set = true;
                    first_failure.opcode = test.opcode;
                    first_failure.testcase_description = test.description;
                    first_failure.signal = "setup";
                }
                continue;
            }

            for (std::size_t pulse_start = 0u;
                 pulse_start <= max_pulse_start_cycle;
                 ++pulse_start) {
                const std::size_t max_pulse_length = pulse_end_cycle_limit - pulse_start;

                for (std::size_t pulse_length = min_pulse_length;
                     pulse_length <= max_pulse_length;
                     ++pulse_length) {
                    if (!run_one_scenario(runner,
                                          test,
                                          true,
                                          pulse_start,
                                          pulse_length,
                                          scenario_config,
                                          stats,
                                          opcode_stats,
                                          first_failure)) {
                        opcode_passed = false;
                    }
                    if (!run_one_scenario(runner,
                                          test,
                                          false,
                                          pulse_start,
                                          pulse_length,
                                          scenario_config,
                                          stats,
                                          opcode_stats,
                                          first_failure)) {
                        opcode_passed = false;
                    }
                }
            }
        }

        if (opcode_passed) {
            ++stats.opcodes_passed;
        } else {
            ++stats.opcodes_failed;
        }

        std::cout << "opcode " << cpu6502_lockstep::hex8(opcode)
                  << " done: " << (opcode_passed ? "PASS" : "FAIL")
                  << " testcases=" << opcode_stats.testcases
                  << " scenarios=" << opcode_stats.scenarios
                  << " failures=" << opcode_stats.failures
                  << '\n' << std::flush;
    }

    std::cout << "\nsummary\n"
              << "  opcodes:   total=" << stats.opcodes_total
              << " pass=" << stats.opcodes_passed
              << " fail=" << stats.opcodes_failed
              << " skip=" << stats.opcodes_skipped << '\n'
              << "  testcases: total=" << stats.testcases_total
              << " skipped=" << stats.testcases_skipped << '\n'
              << "  scenarios: total=" << stats.scenarios_total
              << " pass=" << stats.scenarios_passed
              << " fail=" << stats.scenarios_failed << '\n'
              << "  NMI:       total=" << stats.nmi_scenarios_total
              << " fail=" << stats.nmi_scenarios_failed << '\n'
              << "  IRQ:       total=" << stats.irq_scenarios_total
              << " fail=" << stats.irq_scenarios_failed << '\n';

    if (first_failure.set) {
        std::cout << "\nfirst failure\n"
                  << "  opcode:       " << cpu6502_lockstep::hex8(first_failure.opcode) << '\n'
                  << "  testcase:     " << first_failure.testcase_description << '\n'
                  << "  signal:       " << first_failure.signal << '\n'
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

    return stats.scenarios_failed == 0u ? 0 : 1;
}
