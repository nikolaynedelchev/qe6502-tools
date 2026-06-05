#include "test_debugger.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace nmi_observer {
namespace {

constexpr std::uint8_t fill_byte = 0xEAu; // legal NOP
constexpr std::uint16_t reset_entry = 0xE000u;
constexpr std::uint16_t irq_trap = 0xE100u;
constexpr std::uint16_t nmi_trap = 0xE200u;
constexpr unsigned bootstrap_to_test_cycle_limit = 20000u;

std::string hex8(std::uint8_t value)
{
    std::ostringstream out;
    out << "$" << std::uppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(value);
    return out.str();
}

std::string hex16(std::uint16_t value)
{
    std::ostringstream out;
    out << "$" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
        << static_cast<unsigned>(value);
    return out.str();
}

void apply_mem_values(cpu6502_bridge::ICpu& cpu, const std::vector<asm6502::mem_value>& values)
{
    asm6502::Asm6502::apply(values, cpu.memory());
}

std::vector<asm6502::mem_value> make_irq_handler()
{
    return asm6502::Asm6502::New()
        .begin()
            .org(irq_trap, "irq_trap")
                .iny()
                .jmp("irq_trap")
        .end()
        .compile();
}

std::vector<asm6502::mem_value> make_nmi_handler()
{
    return asm6502::Asm6502::New()
        .begin()
            .org(nmi_trap, "nmi_trap")
                .inx()
                .jmp("nmi_trap")
        .end()
        .compile();
}

std::vector<std::string> tokenize(std::string_view script)
{
    std::istringstream input{std::string(script)};
    std::vector<std::string> tokens;
    std::string token;
    while (input >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

std::string trim_copy(std::string_view text)
{
    auto first = text.begin();
    auto last = text.end();

    while (first != last && std::isspace(static_cast<unsigned char>(*first)) != 0) {
        ++first;
    }
    while (first != last && std::isspace(static_cast<unsigned char>(*(last - 1))) != 0) {
        --last;
    }

    return std::string(first, last);
}

std::optional<std::uint16_t> parse_run_to_address(std::string_view command)
{
    constexpr std::string_view prefix = "run_to_";
    if (command.size() <= prefix.size() || command.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }

    std::string_view address_text = command.substr(prefix.size());
    int base = 10;
    if (address_text.size() > 2u
        && address_text[0] == '0'
        && (address_text[1] == 'x' || address_text[1] == 'X')) {
        address_text.remove_prefix(2u);
        base = 16;
    }

    if (address_text.empty()) {
        throw std::runtime_error("run_to_ command has no address");
    }

    unsigned value = 0;
    const auto* first = address_text.data();
    const auto* last = first + address_text.size();
    const auto result = std::from_chars(first, last, value, base);
    if (result.ec != std::errc{} || result.ptr != last || value > 0xFFFFu) {
        throw std::runtime_error("invalid run_to_ address: " + std::string(command));
    }

    return static_cast<std::uint16_t>(value);
}

std::optional<unsigned> parse_step_count(std::string_view command)
{
    constexpr std::string_view prefix = "step_";
    if (command.size() <= prefix.size() || command.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }

    const std::string_view count_text = command.substr(prefix.size());
    if (count_text.empty()) {
        throw std::runtime_error("step_ command has no count");
    }

    unsigned value = 0;
    const auto* first = count_text.data();
    const auto* last = first + count_text.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last) {
        throw std::runtime_error("invalid step_ count: " + std::string(command));
    }
    if (value == 0u) {
        throw std::runtime_error("step_ count must be greater than zero");
    }

    return value;
}

std::optional<std::pair<std::uint16_t, std::uint16_t>> parse_log_mem_range(std::string_view command)
{
    constexpr std::string_view prefix = "log_mem_";
    constexpr std::string_view separator = "_to_";
    if (command.size() <= prefix.size() || command.substr(0, prefix.size()) != prefix) {
        return std::nullopt;
    }

    const std::string_view range_text = command.substr(prefix.size());
    const std::size_t sep = range_text.find(separator);
    if (sep == std::string_view::npos) {
        return std::nullopt;
    }

    auto parse_address = [](std::string_view text) -> std::uint16_t {
        int base = 10;
        if (text.size() > 2u
            && text[0] == '0'
            && (text[1] == 'x' || text[1] == 'X')) {
            text.remove_prefix(2u);
            base = 16;
        }
        if (text.empty()) {
            throw std::runtime_error("log_mem address is empty");
        }

        unsigned value = 0;
        const auto* first = text.data();
        const auto* last = first + text.size();
        const auto result = std::from_chars(first, last, value, base);
        if (result.ec != std::errc{} || result.ptr != last || value > 0xFFFFu) {
            throw std::runtime_error("invalid log_mem address");
        }
        return static_cast<std::uint16_t>(value);
    };

    const std::uint16_t first = parse_address(range_text.substr(0u, sep));
    const std::uint16_t last = parse_address(range_text.substr(sep + separator.size()));
    return std::make_pair(first, last);
}

} // namespace

void TestDebugger::load_testcase(cpu6502_bridge::ICpu& cpu, const testcase& test)
{
    cpu_ = &cpu;
    loaded_ = loaded_kind::testcase;
    test_ = test;
    program_.clear();
    cycle_ = 0;
    reset_loaded_memory();
}

void TestDebugger::load_program(cpu6502_bridge::ICpu& cpu, const std::vector<asm6502::mem_value>& program)
{
    cpu_ = &cpu;
    loaded_ = loaded_kind::program;
    test_.reset();
    program_ = program;
    cycle_ = 0;
    reset_loaded_memory();
}

std::string TestDebugger::execute_script(std::string_view script)
{
    std::string output;
    for (const std::string& token : tokenize(script)) {
        output += execute_command(token);
    }
    return output;
}

std::string TestDebugger::execute_command(std::string_view command_text)
{
    const std::string command = trim_copy(command_text);
    if (command.empty()) {
        return {};
    }

    if (command == "help") {
        return help();
    }
    if (command == "restart") {
        return restart();
    }
    if (command == "restart_to_start_fetch") {
        return restart_to_start_fetch();
    }
    if (command == "restart_to_test") {
        return restart_to_test();
    }
    if (command == "step") {
        return step();
    }
    if (const std::optional<unsigned> count = parse_step_count(command)) {
        return step(*count);
    }
    if (command == "irq_assert") {
        return irq_assert();
    }
    if (command == "irq_deassert") {
        return irq_deassert();
    }
    if (command == "nmi_assert") {
        return nmi_assert();
    }
    if (command == "nmi_deassert") {
        return nmi_deassert();
    }
    if (command == "log_registers") {
        return log_registers();
    }
    if (command == "log_registers_on") {
        return log_registers_on();
    }
    if (command == "log_registers_off") {
        return log_registers_off();
    }
    if (command == "log_bus_state") {
        return log_bus_state();
    }
    if (command == "log_bus_state_on") {
        return log_bus_state_on();
    }
    if (command == "log_bus_state_off") {
        return log_bus_state_off();
    }
    if (command == "log_stack") {
        return log_stack();
    }
    if (command == "log_stack_on") {
        return log_stack_on();
    }
    if (command == "log_stack_off") {
        return log_stack_off();
    }
    if (const auto range = parse_log_mem_range(command)) {
        return log_mem(range->first, range->second);
    }
    if (command == "log_vectors") {
        return log_vectors();
    }
    if (command == "log_vectors_on") {
        return log_vectors_on();
    }
    if (command == "log_vectors_off") {
        return log_vectors_off();
    }
    if (command == "cycle_details") {
        return cycle_details();
    }
    if (command == "cycle_details_on") {
        return cycle_details_on();
    }
    if (command == "cycle_details_off") {
        return cycle_details_off();
    }

    if (const std::optional<std::uint16_t> address = parse_run_to_address(command)) {
        return run_to(*address);
    }

    throw std::runtime_error("unknown TestDebugger command: " + command);
}

std::string TestDebugger::help() const
{
    return
        "TestDebugger commands:\n"
        "help                         Show debugger commands.\n"
        "restart                      Reset CPU and keep reset bus state.\n"
        "restart_to_start_fetch       Reset and stop at reset-vector opcode fetch.\n"
        "restart_to_test              Testcase: run bootstrap to test opcode; raw program: same as restart_to_start_fetch.\n"
        "step                         Execute one bus cycle.\n"
        "step_N                       Execute N bus cycles, e.g. step_10. Per-step logs still run each cycle.\n"
        "run_to_0xabcd                Run until opcode fetch at address 0xabcd.\n"
        "irq_assert / irq_deassert    Set or clear IRQ line.\n"
        "nmi_assert / nmi_deassert    Set or clear NMI line.\n"
        "log_registers                Print CPU registers and IRQ/NMI line state.\n"
        "log_registers_on/off         Show registers after each step/run_to cycle.\n"
        "log_bus_state                Print address, data, fetch/write state.\n"
        "log_bus_state_on/off         Show bus state after each step/run_to cycle.\n"
        "log_stack                    Print stack bytes above S.\n"
        "log_stack_on/off             Show stack after each step/run_to cycle.\n"
        "log_mem_0x0400_to_0x04ff     Print memory from first to last address. No on/off yet.\n"
        "log_vectors                  Print $FFFA..$FFFF.\n"
        "log_vectors_on/off           Show vectors after each step/run_to cycle.\n"
        "cycle_details                Print cycle, registers, bus, and stack.\n"
        "cycle_details_on/off         Show cycle details after each step/run_to cycle.\n";
}

std::string TestDebugger::restart()
{
    reset_loaded_memory();
    cpu().irq(false);
    cpu().nmi(false);
    cpu().restart();
    cycle_ = 0;
    return "restart done\n";
}

std::string TestDebugger::restart_to_start_fetch()
{
    reset_loaded_memory();
    cpu().irq(false);
    cpu().nmi(false);
    const unsigned steps = cpu().restart_to_start_fetch();
    cycle_ = steps;

    std::ostringstream out;
    out << "restart_to_start_fetch steps=" << steps
        << " address=" << hex16(cpu().bus_address())
        << " data=" << hex8(cpu().bus_data())
        << "\n";
    return out.str();
}

std::string TestDebugger::restart_to_test()
{
    if (loaded_ != loaded_kind::testcase) {
        return restart_to_start_fetch();
    }

    reset_loaded_memory();
    cpu().irq(false);
    cpu().nmi(false);
    cpu().restart();

    const testcase& test = *test_;
    for (unsigned steps = 0; steps < bootstrap_to_test_cycle_limit; ++steps) {
        if (cpu().is_opcode_fetch()
            && !cpu().is_write()
            && cpu().bus_address() == test.start_at
            && cpu().bus_data() == test.opcode) {
            cycle_ = steps;
            std::ostringstream out;
            out << "restart_to_test steps=" << steps << " aligned=yes"
                << " start_at=" << hex16(test.start_at)
                << " opcode=" << hex8(test.opcode)
                << "\n";
            return out.str();
        }
        cpu().step();
    }

    cycle_ = bootstrap_to_test_cycle_limit;
    std::ostringstream out;
    out << "restart_to_test steps=" << bootstrap_to_test_cycle_limit << " aligned=no"
        << " wanted_start_at=" << hex16(test.start_at)
        << " wanted_opcode=" << hex8(test.opcode)
        << " current_address=" << hex16(cpu().bus_address())
        << " current_data=" << hex8(cpu().bus_data())
        << "\n";
    return out.str();
}

std::string TestDebugger::step()
{
    return step(1u);
}

std::string TestDebugger::step(unsigned count)
{
    if (count == 0u) {
        throw std::runtime_error("step count must be greater than zero");
    }

    std::ostringstream out;
    for (unsigned i = 0; i < count; ++i) {
        cpu().step();
        ++cycle_;
        out << "stepped cycle=" << cycle_ << "\n";
        out << after_step_logs();
    }
    return out.str();
}

std::string TestDebugger::run_to(std::uint16_t address, unsigned max_steps)
{
    std::ostringstream out_after_steps;
    for (unsigned steps = 0; steps <= max_steps; ++steps) {
        if (cpu().is_opcode_fetch() && !cpu().is_write() && cpu().bus_address() == address) {
            std::ostringstream out;
            out << "run_to address=" << hex16(address)
                << " steps=" << steps
                << " reached=yes"
                << " cycle=" << cycle_
                << " data=" << hex8(cpu().bus_data())
                << "\n";
            return out_after_steps.str() + out.str();
        }

        if (steps == max_steps) {
            break;
        }

        cpu().step();
        ++cycle_;
        out_after_steps << after_step_logs();
    }

    std::ostringstream out;
    out << "run_to address=" << hex16(address)
        << " steps=" << max_steps
        << " reached=no"
        << " cycle=" << cycle_
        << " current_address=" << hex16(cpu().bus_address())
        << " current_data=" << hex8(cpu().bus_data())
        << "\n";
    return out_after_steps.str() + out.str();
}

std::string TestDebugger::irq_assert()
{
    cpu().irq(true);
    return "irq=asserted\n";
}

std::string TestDebugger::irq_deassert()
{
    cpu().irq(false);
    return "irq=deasserted\n";
}

std::string TestDebugger::nmi_assert()
{
    cpu().nmi(true);
    return "nmi=asserted\n";
}

std::string TestDebugger::nmi_deassert()
{
    cpu().nmi(false);
    return "nmi=deasserted\n";
}

std::string TestDebugger::log_registers() const
{
    const auto& c = cpu();
    std::ostringstream out;
    out << "registers"
        << " PC=" << hex16(c.pc())
        << " A=" << hex8(c.a())
        << " X=" << hex8(c.x())
        << " Y=" << hex8(c.y())
        << " S=" << hex8(c.s())
        << " P=" << hex8(c.p())
        << " irq=" << (c.is_irq_asserted() ? "asserted" : "deasserted")
        << " nmi=" << (c.is_nmi_asserted() ? "asserted" : "deasserted")
        << "\n";
    return out.str();
}

std::string TestDebugger::log_bus_state() const
{
    const auto& c = cpu();
    std::ostringstream out;
    out << "bus"
        << " address=" << hex16(c.bus_address())
        << " data=" << hex8(c.bus_data())
        << " is_opcode_fetch=" << (c.is_opcode_fetch() ? "true" : "false")
        << " is_write=" << (c.is_write() ? "true" : "false")
        << " rw=" << (c.is_write() ? 'W' : 'R')
        << "\n";
    return out.str();
}

std::string TestDebugger::log_stack() const
{
    const std::uint8_t* mem = cpu().memory();
    const std::uint8_t s = cpu().s();
    const std::uint16_t next = static_cast<std::uint16_t>(0x0100u | static_cast<std::uint16_t>((s + 1u) & 0xFFu));

    std::ostringstream out;
    out << "stack S=" << hex8(s) << " next=" << hex16(next);
    for (unsigned offset = 0; offset < 8u; ++offset) {
        const auto zp = static_cast<std::uint8_t>(static_cast<unsigned>(s) + 1u + offset);
        const auto address = static_cast<std::uint16_t>(0x0100u | static_cast<std::uint16_t>(zp));
        out << ' ' << hex16(address) << '=' << hex8(mem[address]);
    }
    out << "\n";
    return out.str();
}

std::string TestDebugger::log_mem(std::uint16_t first, std::uint16_t last) const
{
    if (last < first) {
        std::swap(first, last);
    }

    const std::uint8_t* mem = cpu().memory();
    std::ostringstream out;
    out << "mem " << hex16(first) << ".." << hex16(last);
    for (std::uint16_t address = first; address <= last; ++address) {
        out << ' ' << hex16(address) << '=' << hex8(mem[address]);
        if (address == 0xFFFFu) {
            break;
        }
    }
    out << "\n";
    return out.str();
}

std::string TestDebugger::log_mem_0xaa_to_0xac() const
{
    return log_mem(0x00AAu, 0x00ACu);
}

std::string TestDebugger::log_vectors() const
{
    return log_mem(0xFFFAu, 0xFFFFu);
}

std::string TestDebugger::cycle_details() const
{
    std::ostringstream out;
    out << "cycle_details cycle=" << cycle_ << "\n";
    out << log_registers();
    out << log_bus_state();
    out << log_stack();
    return out.str();
}

std::string TestDebugger::log_registers_on()
{
    after_step_log_registers_ = true;
    return "log_registers=on\n";
}

std::string TestDebugger::log_registers_off()
{
    after_step_log_registers_ = false;
    return "log_registers=off\n";
}

std::string TestDebugger::log_bus_state_on()
{
    after_step_log_bus_state_ = true;
    return "log_bus_state=on\n";
}

std::string TestDebugger::log_bus_state_off()
{
    after_step_log_bus_state_ = false;
    return "log_bus_state=off\n";
}

std::string TestDebugger::log_stack_on()
{
    after_step_log_stack_ = true;
    return "log_stack=on\n";
}

std::string TestDebugger::log_stack_off()
{
    after_step_log_stack_ = false;
    return "log_stack=off\n";
}

std::string TestDebugger::log_vectors_on()
{
    after_step_log_vectors_ = true;
    return "log_vectors=on\n";
}

std::string TestDebugger::log_vectors_off()
{
    after_step_log_vectors_ = false;
    return "log_vectors=off\n";
}

std::string TestDebugger::cycle_details_on()
{
    after_step_cycle_details_ = true;
    return "cycle_details=on\n";
}

std::string TestDebugger::cycle_details_off()
{
    after_step_cycle_details_ = false;
    return "cycle_details=off\n";
}

std::string TestDebugger::after_step_logs() const
{
    std::ostringstream out;
    if (after_step_log_registers_) {
        out << log_registers();
    }
    if (after_step_log_bus_state_) {
        out << log_bus_state();
    }
    if (after_step_log_stack_) {
        out << log_stack();
    }
    if (after_step_log_vectors_) {
        out << log_vectors();
    }
    if (after_step_cycle_details_) {
        out << cycle_details();
    }
    return out.str();
}

cpu6502_bridge::ICpu& TestDebugger::cpu()
{
    if (cpu_ == nullptr) {
        throw std::runtime_error("TestDebugger has no loaded CPU");
    }
    return *cpu_;
}

cpu6502_bridge::ICpu& TestDebugger::cpu() const
{
    if (cpu_ == nullptr) {
        throw std::runtime_error("TestDebugger has no loaded CPU");
    }
    return *cpu_;
}

void TestDebugger::reset_loaded_memory()
{
    if (cpu_ == nullptr || loaded_ == loaded_kind::none) {
        throw std::runtime_error("TestDebugger has no loaded testcase/program");
    }

    std::uint8_t* mem = cpu_->memory();
    std::fill(mem, mem + 65536u, fill_byte);

    if (loaded_ == loaded_kind::testcase) {
        const testcase& test = *test_;
        apply_mem_values(*cpu_, test.mem_setup);

        const auto boot = asm6502::bootstrap_program(
            test.A,
            test.X,
            test.Y,
            test.P,
            test.S,
            test.start_at,
            reset_entry,
            irq_trap,
            nmi_trap);
        apply_mem_values(*cpu_, boot);
        apply_mem_values(*cpu_, make_irq_handler());
        apply_mem_values(*cpu_, make_nmi_handler());
        return;
    }

    apply_mem_values(*cpu_, program_);
}

std::string run_scripted_testcase(
    cpu6502_bridge::ICpu& cpu,
    const testcase& test,
    std::string_view script)
{
    TestDebugger debugger;
    debugger.load_testcase(cpu, test);

    std::ostringstream out;
    out << "scripted_testcase"
        << " opcode=" << hex8(test.opcode)
        << " start_at=" << hex16(test.start_at)
        << " expected_cycles=" << static_cast<unsigned>(test.expected_cycles)
        << " description=\"" << test.description << "\"\n";
    out << debugger.execute_script(script);
    return out.str();
}

std::string run_scripted_program(
    cpu6502_bridge::ICpu& cpu,
    const std::vector<asm6502::mem_value>& program,
    std::string_view script)
{
    TestDebugger debugger;
    debugger.load_program(cpu, program);

    std::ostringstream out;
    out << "scripted_program bytes=" << program.size() << "\n";
    out << debugger.execute_script(script);
    return out.str();
}

} // namespace nmi_observer
