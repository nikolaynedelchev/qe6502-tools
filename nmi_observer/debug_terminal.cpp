#include "debug_terminal.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>

namespace nmi_observer {
namespace {

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

std::string hex_opcode_name(std::uint8_t value)
{
    std::ostringstream out;
    out << "0x" << std::nouppercase << std::hex << std::setw(2) << std::setfill('0')
        << static_cast<unsigned>(value);
    return out.str();
}

std::string backend_name(DebugTerminal::backend_kind backend)
{
    switch (backend) {
    case DebugTerminal::backend_kind::qe6502:
        return "qe6502";
    case DebugTerminal::backend_kind::perfect6502:
        return "perfect6502";
    }
    return "unknown";
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

std::uint16_t parse_address(std::string_view text, const char* command_name)
{
    int base = 10;
    if (text.size() > 2u
        && text[0] == '0'
        && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2u);
        base = 16;
    }
    if (text.empty()) {
        throw std::runtime_error(std::string(command_name) + " address is empty");
    }

    unsigned value = 0;
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto result = std::from_chars(first, last, value, base);
    if (result.ec != std::errc{} || result.ptr != last || value > 0xFFFFu) {
        throw std::runtime_error(std::string(command_name) + " address is invalid: " + std::string(text));
    }
    return static_cast<std::uint16_t>(value);
}

std::optional<std::uint8_t> parse_opcode_suffix(std::string_view command, std::string_view prefix)
{
    if (command.size() <= prefix.size() || command.substr(0u, prefix.size()) != prefix) {
        return std::nullopt;
    }
    const std::uint16_t opcode = parse_address(command.substr(prefix.size()), "opcode");
    if (opcode > 0xFFu) {
        throw std::runtime_error("opcode is outside 0x00..0xff");
    }
    return static_cast<std::uint8_t>(opcode);
}

std::optional<std::pair<std::uint8_t, std::size_t>> parse_opcode_index_suffix(
    std::string_view command,
    std::string_view prefix)
{
    if (command.size() <= prefix.size() || command.substr(0u, prefix.size()) != prefix) {
        return std::nullopt;
    }

    std::string_view rest = command.substr(prefix.size());
    const std::size_t sep = rest.find('_');
    if (sep == std::string_view::npos) {
        return std::nullopt;
    }

    const std::uint16_t opcode16 = parse_address(rest.substr(0u, sep), "opcode");
    if (opcode16 > 0xFFu) {
        throw std::runtime_error("opcode is outside 0x00..0xff");
    }

    const std::string_view index_text = rest.substr(sep + 1u);
    if (index_text.empty()) {
        throw std::runtime_error("test index is empty");
    }

    std::size_t index = 0;
    const auto* first = index_text.data();
    const auto* last = first + index_text.size();
    const auto result = std::from_chars(first, last, index, 10);
    if (result.ec != std::errc{} || result.ptr != last) {
        throw std::runtime_error("test index is invalid: " + std::string(index_text));
    }

    return std::make_pair(static_cast<std::uint8_t>(opcode16), index);
}

const std::map<std::uint8_t, std::vector<testcase>>& all_tests()
{
    static const auto tests = get_nmos6502_opcode_testcases();
    return tests;
}

std::string loaded_kind_name(DebugTerminal::loaded_kind kind)
{
    switch (kind) {
    case DebugTerminal::loaded_kind::none:
        return "none";
    case DebugTerminal::loaded_kind::testcase:
        return "testcase";
    case DebugTerminal::loaded_kind::program:
        return "program";
    }
    return "unknown";
}

} // namespace

DebugTerminal::DebugTerminal()
{
    create_cpu();
}

std::string DebugTerminal::execute_script(std::string_view script)
{
    std::string output;
    for (const std::string& command : tokenize(script)) {
        output += execute_command(command);
    }
    return output;
}

std::string DebugTerminal::execute_command(std::string_view command_text)
{
    const std::string command = trim_copy(command_text);
    if (command.empty()) {
        return {};
    }

    if (command == "help") {
        return help();
    }
    if (command == "status") {
        return status();
    }
    if (command == "backend_qe6502") {
        return use_qe6502_backend();
    }
    if (command == "backend_perfect6502") {
        return use_perfect6502_backend();
    }
    if (command == "list_opcodes") {
        return list_opcodes();
    }
    if (command == "reload") {
        return reload();
    }

    if (const auto test = parse_opcode_index_suffix(command, "tests_details_")) {
        return test_details(test->first, test->second);
    }
    if (const auto test = parse_opcode_index_suffix(command, "load_test_")) {
        return load_testcase(test->first, test->second);
    }
    if (const auto opcode = parse_opcode_suffix(command, "tests_")) {
        return list_tests(*opcode);
    }

    return debugger_.execute_command(command);
}

std::string DebugTerminal::help() const
{
    std::ostringstream out;
    out << "DebugTerminal commands:\n"
        << "help                         Show terminal and debugger commands.\n"
        << "status                       Show backend and loaded item.\n"
        << "backend_qe6502               Use qe6502 backend and reload current item.\n"
        << "backend_perfect6502          Use perfect6502 backend and reload current item.\n"
        << "list_opcodes                 List opcodes with NMOS testcases.\n"
        << "tests_0x50                   List testcase descriptions for opcode 0x50.\n"
        << "tests_details_0x50_1         Show full testcase 1 for opcode 0x50.\n"
        << "load_test_0x50_1             Load testcase 1 for opcode 0x50.\n"
        << "reload                       Reload current testcase/program on current backend.\n\n"
        << debugger_.help();
    return out.str();
}

std::string DebugTerminal::status() const
{
    std::ostringstream out;
    out << "status backend=" << backend_name(backend_)
        << " loaded=" << loaded_kind_name(loaded_);
    if (loaded_ == loaded_kind::testcase) {
        out << " opcode=" << hex8(*loaded_opcode_)
            << " index=" << *loaded_test_index_
            << " description=\"" << loaded_test_->description << "\"";
    } else if (loaded_ == loaded_kind::program) {
        out << " program=\"" << loaded_program_name_ << "\""
            << " bytes=" << loaded_program_.size();
    }
    out << "\n";
    return out.str();
}

std::string DebugTerminal::use_qe6502_backend()
{
    backend_ = backend_kind::qe6502;
    create_cpu();
    std::ostringstream out;
    out << "backend=qe6502\n";
    out << reload_after_backend_change();
    return out.str();
}

std::string DebugTerminal::use_perfect6502_backend()
{
    backend_ = backend_kind::perfect6502;
    create_cpu();
    std::ostringstream out;
    out << "backend=perfect6502\n";
    out << reload_after_backend_change();
    return out.str();
}

std::string DebugTerminal::list_opcodes() const
{
    const auto& tests = all_tests();
    std::ostringstream out;
    out << "opcodes";
    for (const auto& [opcode, cases] : tests) {
        if (!cases.empty()) {
            out << ' ' << hex_opcode_name(opcode) << ':' << cases.size();
        }
    }
    out << "\n";
    return out.str();
}

std::string DebugTerminal::list_tests(std::uint8_t opcode) const
{
    const auto& tests = all_tests();
    const auto it = tests.find(opcode);
    std::ostringstream out;
    out << "tests opcode=" << hex8(opcode) << "\n";
    if (it == tests.end() || it->second.empty()) {
        out << "  none\n";
        return out.str();
    }

    for (std::size_t index = 0; index < it->second.size(); ++index) {
        const testcase& test = it->second[index];
        out << "  " << index
            << ": start_at=" << hex16(test.start_at)
            << " cycles=" << static_cast<unsigned>(test.expected_cycles)
            << " desc=\"" << test.description << "\"\n";
    }
    return out.str();
}

std::string DebugTerminal::test_details(std::uint8_t opcode, std::size_t index) const
{
    const testcase& test = testcase_at(opcode, index);
    std::vector<asm6502::mem_value> mem = test.mem_setup;
    std::sort(mem.begin(), mem.end(), [](const auto& left, const auto& right) {
        if (left.first != right.first) {
            return left.first < right.first;
        }
        return left.second < right.second;
    });

    std::ostringstream out;
    out << "testcase opcode=" << hex8(opcode)
        << " index=" << index
        << " description=\"" << test.description << "\"\n"
        << "  bytes=" << static_cast<unsigned>(test.bytes)
        << " expected_cycles=" << static_cast<unsigned>(test.expected_cycles)
        << " start_at=" << hex16(test.start_at)
        << " nmi_vector=" << hex16(test.nmi_vector) << "\n"
        << "  registers"
        << " A=" << hex8(test.A)
        << " X=" << hex8(test.X)
        << " Y=" << hex8(test.Y)
        << " P=" << hex8(test.P)
        << " S=" << hex8(test.S) << "\n"
        << "  program/memory bytes=" << mem.size() << "\n";

    for (const auto& [address, value] : mem) {
        out << "    " << hex16(address) << " = " << hex8(value) << "\n";
    }
    return out.str();
}

std::string DebugTerminal::load_testcase(std::uint8_t opcode, std::size_t index)
{
    const testcase& selected = testcase_at(opcode, index);
    loaded_ = loaded_kind::testcase;
    loaded_opcode_ = opcode;
    loaded_test_index_ = index;
    loaded_test_ = selected;
    loaded_program_.clear();
    loaded_program_name_.clear();

    debugger_.load_testcase(*cpu_, *loaded_test_);

    std::ostringstream out;
    out << "loaded testcase opcode=" << hex8(opcode)
        << " index=" << index
        << " backend=" << backend_name(backend_)
        << " description=\"" << selected.description << "\"\n";
    return out.str();
}

std::string DebugTerminal::load_program(std::string name, const std::vector<asm6502::mem_value>& program)
{
    loaded_ = loaded_kind::program;
    loaded_opcode_.reset();
    loaded_test_index_.reset();
    loaded_test_.reset();
    loaded_program_ = program;
    loaded_program_name_ = std::move(name);

    debugger_.load_program(*cpu_, loaded_program_);

    std::ostringstream out;
    out << "loaded program name=\"" << loaded_program_name_ << "\""
        << " backend=" << backend_name(backend_)
        << " bytes=" << loaded_program_.size() << "\n";
    return out.str();
}

std::string DebugTerminal::reload()
{
    if (loaded_ == loaded_kind::none) {
        return "reload skipped: nothing loaded\n";
    }
    if (loaded_ == loaded_kind::testcase) {
        debugger_.load_testcase(*cpu_, *loaded_test_);
        std::ostringstream out;
        out << "reloaded testcase opcode=" << hex8(*loaded_opcode_)
            << " index=" << *loaded_test_index_
            << " backend=" << backend_name(backend_) << "\n";
        return out.str();
    }

    debugger_.load_program(*cpu_, loaded_program_);
    std::ostringstream out;
    out << "reloaded program name=\"" << loaded_program_name_ << "\""
        << " backend=" << backend_name(backend_)
        << " bytes=" << loaded_program_.size() << "\n";
    return out.str();
}

TestDebugger& DebugTerminal::debugger() noexcept
{
    return debugger_;
}

const TestDebugger& DebugTerminal::debugger() const noexcept
{
    return debugger_;
}

void DebugTerminal::create_cpu()
{
    switch (backend_) {
    case backend_kind::qe6502:
        cpu_ = cpu6502_bridge::make_qe6502_cpu();
        return;
    case backend_kind::perfect6502:
        cpu_ = cpu6502_bridge::make_perfect6502_cpu();
        return;
    }
    throw std::runtime_error("unknown backend");
}

std::string DebugTerminal::reload_after_backend_change()
{
    if (loaded_ == loaded_kind::none) {
        return {};
    }
    return reload();
}

const testcase& DebugTerminal::testcase_at(std::uint8_t opcode, std::size_t index) const
{
    const auto& tests = all_tests();
    const auto it = tests.find(opcode);
    if (it == tests.end() || index >= it->second.size()) {
        std::ostringstream out;
        out << "no testcase for opcode=" << hex8(opcode) << " index=" << index;
        throw std::runtime_error(out.str());
    }
    return it->second[index];
}

} // namespace nmi_observer
