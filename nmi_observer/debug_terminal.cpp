#include "debug_terminal.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iomanip>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

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

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0u, prefix.size()) == prefix;
}

std::vector<std::string> split_words(std::string_view text)
{
    std::istringstream input{std::string(text)};
    std::vector<std::string> words;
    std::string word;
    while (input >> word) {
        words.push_back(word);
    }
    return words;
}

std::uint16_t parse_number(std::string_view text, unsigned max_value, const char* what)
{
    int base = 10;
    if (text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2u);
        base = 16;
    }
    if (text.empty()) {
        throw std::runtime_error(std::string(what) + " is empty");
    }

    unsigned value = 0;
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto result = std::from_chars(first, last, value, base);
    if (result.ec != std::errc{} || result.ptr != last || value > max_value) {
        std::ostringstream out;
        out << what << " is invalid or out of range: " << std::string(text);
        throw std::runtime_error(out.str());
    }
    return static_cast<std::uint16_t>(value);
}

std::optional<std::pair<std::uint16_t, std::uint8_t>> parse_memory_write_command(std::string_view command)
{
    if (command.size() <= 2u || command[0] != '0' || (command[1] != 'x' && command[1] != 'X')) {
        return std::nullopt;
    }

    std::size_t sep = command.find('=');
    if (sep == std::string_view::npos) {
        sep = command.find(':');
    }
    if (sep == std::string_view::npos || sep == 0u || sep + 1u >= command.size()) {
        return std::nullopt;
    }

    const std::uint16_t address = parse_number(command.substr(0u, sep), 0xFFFFu, "memory address");
    const std::uint16_t value = parse_number(command.substr(sep + 1u), 0xFFu, "memory value");
    return std::make_pair(address, static_cast<std::uint8_t>(value));
}

struct memory_fill_args {
    std::uint16_t first = 0;
    std::uint16_t last = 0;
    std::uint8_t value = 0;
};

memory_fill_args parse_memory_fill_args(std::string_view text, const char* command_name)
{
    const std::vector<std::string> words = split_words(text);
    if (words.size() != 3u) {
        throw std::runtime_error(std::string(command_name) + " usage: " + command_name + " 0xFIRST 0xLAST 0xBYTE");
    }

    memory_fill_args args;
    args.first = parse_number(words[0], 0xFFFFu, "memory_fill first address");
    args.last = parse_number(words[1], 0xFFFFu, "memory_fill last address");
    args.value = static_cast<std::uint8_t>(parse_number(words[2], 0xFFu, "memory_fill value"));
    if (args.first > args.last) {
        throw std::runtime_error("memory_fill first address must be <= last address");
    }
    return args;
}

struct bootstrap_options {
    std::uint8_t a = 0x00u;
    std::uint8_t x = 0x00u;
    std::uint8_t y = 0x00u;
    std::uint8_t p = 0x24u;
    std::uint8_t s = 0xFDu;
    std::uint16_t start_at = 0x0000u;
    bool has_start_at = false;
    std::uint16_t reset_vector = 0x0200u;
    std::uint16_t brk_irq_vector = 0x0000u;
    std::uint16_t nmi_vector = 0x0000u;
};

bootstrap_options parse_bootstrap_options(std::string_view text)
{
    bootstrap_options options;
    for (const std::string& word : split_words(text)) {
        const std::size_t sep = word.find('=');
        if (sep == std::string::npos || sep == 0u || sep + 1u >= word.size()) {
            throw std::runtime_error("bootstrap options must use key=value: " + word);
        }

        const std::string key = word.substr(0u, sep);
        const std::string_view value_text{word.data() + sep + 1u, word.size() - sep - 1u};
        if (key == "start_at") {
            options.start_at = parse_number(value_text, 0xFFFFu, "bootstrap start_at");
            options.has_start_at = true;
        } else if (key == "a") {
            options.a = static_cast<std::uint8_t>(parse_number(value_text, 0xFFu, "bootstrap a"));
        } else if (key == "x") {
            options.x = static_cast<std::uint8_t>(parse_number(value_text, 0xFFu, "bootstrap x"));
        } else if (key == "y") {
            options.y = static_cast<std::uint8_t>(parse_number(value_text, 0xFFu, "bootstrap y"));
        } else if (key == "p") {
            options.p = static_cast<std::uint8_t>(parse_number(value_text, 0xFFu, "bootstrap p"));
        } else if (key == "s") {
            options.s = static_cast<std::uint8_t>(parse_number(value_text, 0xFFu, "bootstrap s"));
        } else if (key == "reset_vector") {
            options.reset_vector = parse_number(value_text, 0xFFFFu, "bootstrap reset_vector");
        } else if (key == "brk_irq_vector") {
            options.brk_irq_vector = parse_number(value_text, 0xFFFFu, "bootstrap brk_irq_vector");
        } else if (key == "nmi_vector") {
            options.nmi_vector = parse_number(value_text, 0xFFFFu, "bootstrap nmi_vector");
        } else {
            throw std::runtime_error("unknown bootstrap option '" + key + "'");
        }
    }

    if (!options.has_start_at) {
        throw std::runtime_error("bootstrap requires start_at=0xADDR");
    }
    return options;
}

void apply_program(cpu6502_bridge::ICpu& cpu, const std::vector<asm6502::mem_value>& program)
{
    asm6502::Asm6502::apply(program, cpu.memory());
}

} // namespace

DebugTerminal::DebugTerminal() = default;

std::string DebugTerminal::execute_script(std::string_view script)
{
    std::string output;
    std::istringstream input{std::string(script)};
    std::string line;
    while (std::getline(input, line)) {
        const std::string command = trim_copy(line);
        if (command.empty() || command[0] == '#') {
            continue;
        }
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
    if (command == "memory_clear" || command == "clear_memory") {
        return memory_clear();
    }
    if (starts_with(command, "memory_clear ")) {
        const std::uint16_t value = parse_number(std::string_view(command).substr(13u), 0xFFu, "memory_clear value");
        return memory_clear(static_cast<std::uint8_t>(value));
    }
    if (starts_with(command, "clear_memory ")) {
        const std::uint16_t value = parse_number(std::string_view(command).substr(13u), 0xFFu, "clear_memory value");
        return memory_clear(static_cast<std::uint8_t>(value));
    }
    if (starts_with(command, "memory_fill ")) {
        const memory_fill_args args = parse_memory_fill_args(std::string_view(command).substr(12u), "memory_fill");
        return memory_fill(args.first, args.last, args.value);
    }
    if (starts_with(command, "fill_memory ")) {
        const memory_fill_args args = parse_memory_fill_args(std::string_view(command).substr(12u), "fill_memory");
        return memory_fill(args.first, args.last, args.value);
    }
    if (starts_with(command, "bootstrap")) {
        if (command.size() == std::string_view("bootstrap").size()) {
            return bootstrap({});
        }
        if (command[std::string_view("bootstrap").size()] == ' ' || command[std::string_view("bootstrap").size()] == '\t') {
            return bootstrap(std::string_view(command).substr(std::string_view("bootstrap").size() + 1u));
        }
    }
    if (const auto mem = parse_memory_write_command(command)) {
        return set_memory_byte(mem->first, mem->second);
    }

    if (!has_backend()) {
        return require_backend_message();
    }
    return debugger_.execute_command(command);
}

std::string DebugTerminal::help() const
{
    std::ostringstream out;
    out << "DebugTerminal commands:\n"
        << "help                         Show terminal and debugger commands.\n"
        << "status                       Show selected backend. No program/test is remembered.\n"
        << "backend_qe6502               Select qe6502; creates fresh CPU and clears memory.\n"
        << "backend_perfect6502          Select perfect6502; creates fresh CPU and clears memory.\n"
        << "memory_clear [BYTE]          Fill current backend memory with BYTE, default $00. Requires selected backend.\n"
        << "clear_memory [BYTE]          Alias for memory_clear [BYTE].\n"
        << "memory_fill 0xFIRST 0xLAST BYTE\n"
        << "                             Fill inclusive memory range with BYTE. Can be repeated for regions.\n"
        << "fill_memory 0xFIRST 0xLAST BYTE\n"
        << "                             Alias for memory_fill.\n"
        << "bootstrap start_at=0xADDR [a=BYTE x=BYTE y=BYTE p=BYTE s=BYTE reset_vector=0xADDR brk_irq_vector=0xADDR nmi_vector=0xADDR]\n"
        << "                             Write asm6502 bootstrap bytes once; does not remember/replay them.\n"
        << "0xADDR=0xBYTE                Write one byte to current backend memory.\n"
        << "0xADDR:0xBYTE                Same as 0xADDR=0xBYTE. Requires selected backend.\n"
        << "load_program_inline <name> A=V ...\n"
        << "                             Console app helper: write several bytes once. Not remembered.\n\n"
        << debugger_.help();
    return out.str();
}

std::string DebugTerminal::status() const
{
    std::ostringstream out;
    out << "status backend=" << (has_backend() ? backend_name(backend_) : std::string("none"))
        << " memory_state=explicit no_loaded_program_or_test\n";
    return out.str();
}

std::string DebugTerminal::use_qe6502_backend()
{
    backend_ = backend_kind::qe6502;
    create_cpu();
    return "backend=qe6502 memory=cleared\n";
}

std::string DebugTerminal::use_perfect6502_backend()
{
    backend_ = backend_kind::perfect6502;
    create_cpu();
    return "backend=perfect6502 memory=cleared\n";
}

std::string DebugTerminal::memory_clear(std::uint8_t value)
{
    if (!has_backend()) {
        return require_backend_message();
    }
    cpu_->clear_memory(value);

    std::ostringstream out;
    out << "memory cleared value=" << hex8(value) << "\n";
    return out.str();
}

std::string DebugTerminal::memory_fill(std::uint16_t first, std::uint16_t last, std::uint8_t value)
{
    if (!has_backend()) {
        return require_backend_message();
    }

    std::uint8_t* mem = cpu_->memory();
    for (unsigned address = first; address <= last; ++address) {
        mem[address] = value;
    }

    std::ostringstream out;
    out << "memory filled " << hex16(first) << ".." << hex16(last)
        << " value=" << hex8(value)
        << " bytes=" << (static_cast<unsigned>(last) - static_cast<unsigned>(first) + 1u)
        << "\n";
    return out.str();
}

std::string DebugTerminal::set_memory_byte(std::uint16_t address, std::uint8_t value)
{
    if (!has_backend()) {
        return require_backend_message();
    }
    cpu_->memory()[address] = value;
    std::ostringstream out;
    out << "mem " << hex16(address) << '=' << hex8(value) << "\n";
    return out.str();
}

std::string DebugTerminal::load_program(std::string name, const std::vector<asm6502::mem_value>& program)
{
    if (!has_backend()) {
        return require_backend_message();
    }
    apply_program(*cpu_, program);

    std::ostringstream out;
    out << "program bytes written name=\"" << name << "\""
        << " backend=" << backend_name(backend_)
        << " bytes=" << program.size()
        << " remembered=no\n";
    return out.str();
}

std::string DebugTerminal::bootstrap(std::string_view options_text)
{
    if (!has_backend()) {
        return require_backend_message();
    }

    const bootstrap_options options = parse_bootstrap_options(options_text);
    const auto program = asm6502::bootstrap_program(
        options.a,
        options.x,
        options.y,
        options.p,
        options.s,
        options.start_at,
        options.reset_vector,
        options.brk_irq_vector,
        options.nmi_vector);
    apply_program(*cpu_, program);

    std::ostringstream out;
    out << "bootstrap written"
        << " start_at=" << hex16(options.start_at)
        << " reset_vector=" << hex16(options.reset_vector)
        << " brk_irq_vector=" << hex16(options.brk_irq_vector)
        << " nmi_vector=" << hex16(options.nmi_vector)
        << " A=" << hex8(options.a)
        << " X=" << hex8(options.x)
        << " Y=" << hex8(options.y)
        << " P=" << hex8(options.p)
        << " S=" << hex8(options.s)
        << " bytes=" << program.size()
        << " remembered=no\n";
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
        break;
    case backend_kind::perfect6502:
        cpu_ = cpu6502_bridge::make_perfect6502_cpu();
        break;
    default:
        throw std::runtime_error("unknown backend");
    }
    backend_selected_ = true;
    std::fill(cpu_->memory(), cpu_->memory() + 65536u, 0x00u);
    debugger_ = TestDebugger{};
    debugger_.attach_cpu(*cpu_);
}

bool DebugTerminal::has_backend() const noexcept
{
    return backend_selected_ && cpu_ != nullptr;
}

std::string DebugTerminal::require_backend_message() const
{
    return "error: no backend selected; use backend_qe6502 or backend_perfect6502 first\n";
}

} // namespace nmi_observer
