#include "debug_terminal.h"

#include <asm6502/asm6502.h>

#include <charconv>
#include <cctype>
#include <cstdint>
#include <exception>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

constexpr const char* prompt = "nmi-debug> ";

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

bool starts_with(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.substr(0u, prefix.size()) == prefix;
}

std::optional<unsigned> parse_unsigned(std::string_view text)
{
    if (text.empty()) {
        return std::nullopt;
    }
    unsigned value = 0;
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto result = std::from_chars(first, last, value, 10);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

std::optional<unsigned> parse_number(std::string_view text, unsigned max_value)
{
    if (text.empty()) {
        return std::nullopt;
    }

    int base = 10;
    if (text.size() > 2u && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        text.remove_prefix(2u);
        base = 16;
    }
    if (text.empty()) {
        return std::nullopt;
    }

    unsigned value = 0;
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto result = std::from_chars(first, last, value, base);
    if (result.ec != std::errc{} || result.ptr != last || value > max_value) {
        return std::nullopt;
    }
    return value;
}

std::optional<asm6502::mem_value> parse_mem_assignment(std::string_view text)
{
    const std::size_t equals = text.find('=');
    if (equals == std::string_view::npos || equals == 0u || equals + 1u >= text.size()) {
        return std::nullopt;
    }

    const auto address = parse_number(text.substr(0u, equals), 0xFFFFu);
    const auto value = parse_number(text.substr(equals + 1u), 0xFFu);
    if (!address || !value) {
        return std::nullopt;
    }

    return asm6502::mem_value{
        static_cast<std::uint16_t>(*address),
        static_cast<std::uint8_t>(*value),
    };
}

std::string console_help(const nmi_observer::DebugTerminal& terminal)
{
    std::ostringstream out;
    out << "Console app commands:\n"
        << "help                         Show this help plus terminal/debugger help.\n"
        << "app_help                     Show only console app commands.\n"
        << "terminal_help                Show DebugTerminal/TestDebugger commands.\n"
        << "source <file>                Run one command per non-empty line from file.\n"
        << "repeat <N> <command>         Run one command N times.\n"
        << "load_program_inline <name> A=V ...\n"
        << "                             Load raw bytes, e.g. 0xfffc=0x00 0x8000=0xea.\n"
        << "exit | quit                  Exit interactive console.\n"
        << "# comment                    Ignored in interactive/source input.\n"
        << "\nCommand line options:\n"
        << "--help                       Print this help and exit.\n"
        << "--backend qe6502|perfect6502 Select backend before commands/source files.\n"
        << "--command <line>             Execute one console line before interactive mode.\n"
        << "--source <file>              Execute source file before interactive mode.\n"
        << "--exit-after-source          Exit after --command/--source work.\n"
        << "--no-banner                  Suppress startup banner.\n\n"
        << terminal.help();
    return out.str();
}

std::string app_help_only()
{
    std::ostringstream out;
    out << "Console app commands:\n"
        << "help                         Show this help plus terminal/debugger help.\n"
        << "app_help                     Show only console app commands.\n"
        << "terminal_help                Show DebugTerminal/TestDebugger commands.\n"
        << "source <file>                Run one command per non-empty line from file.\n"
        << "repeat <N> <command>         Run one command N times.\n"
        << "load_program_inline <name> A=V ...\n"
        << "                             Load raw bytes, e.g. 0xfffc=0x00 0x8000=0xea.\n"
        << "exit | quit                  Exit interactive console.\n"
        << "# comment                    Ignored in interactive/source input.\n";
    return out.str();
}

class ConsoleApp {
public:
    std::string execute_line(std::string_view raw_line)
    {
        std::string line = trim_copy(raw_line);
        if (line.empty() || line[0] == '#') {
            return {};
        }

        if (line == "help") {
            return console_help(terminal_);
        }
        if (line == "app_help") {
            return app_help_only();
        }
        if (line == "terminal_help") {
            return terminal_.help();
        }
        if (line == "exit" || line == "quit") {
            exit_requested_ = true;
            return "exit requested\n";
        }
        if (starts_with(line, "source ")) {
            return source_file(trim_copy(std::string_view(line).substr(7u)));
        }
        if (starts_with(line, "repeat ")) {
            return repeat_command(trim_copy(std::string_view(line).substr(7u)));
        }
        if (starts_with(line, "load_program_inline ")) {
            return load_program_inline(trim_copy(std::string_view(line).substr(20u)));
        }

        return terminal_.execute_command(line);
    }

    bool exit_requested() const noexcept
    {
        return exit_requested_;
    }

private:
    std::string source_file(const std::string& path)
    {
        if (path.empty()) {
            return "source error: missing file path\n";
        }

        std::ifstream input(path);
        if (!input) {
            return "source error: cannot open " + path + "\n";
        }

        std::ostringstream out;
        out << "source begin " << path << "\n";
        std::string line;
        unsigned line_number = 0;
        while (std::getline(input, line)) {
            ++line_number;
            try {
                const std::string result = execute_line(line);
                if (!result.empty()) {
                    out << result;
                }
                if (exit_requested_) {
                    break;
                }
            } catch (const std::exception& error) {
                out << "source " << path << ':' << line_number << " error: " << error.what() << "\n";
            }
        }
        out << "source end " << path << "\n";
        return out.str();
    }

    std::string repeat_command(const std::string& rest)
    {
        const std::size_t first_space = rest.find_first_of(" \t");
        if (first_space == std::string::npos) {
            return "repeat error: usage repeat <N> <command>\n";
        }

        const std::string count_text = rest.substr(0u, first_space);
        const auto count = parse_unsigned(count_text);
        if (!count) {
            return "repeat error: invalid count " + count_text + "\n";
        }

        const std::string command = trim_copy(std::string_view(rest).substr(first_space + 1u));
        if (command.empty()) {
            return "repeat error: missing command\n";
        }

        std::ostringstream out;
        for (unsigned index = 0; index < *count; ++index) {
            out << execute_line(command);
            if (exit_requested_) {
                break;
            }
        }
        return out.str();
    }

    std::string load_program_inline(const std::string& rest)
    {
        const std::vector<std::string> words = split_words(rest);
        if (words.size() < 2u) {
            return "load_program_inline error: usage load_program_inline <name> 0xADDR=0xBYTE ...\n";
        }

        std::vector<asm6502::mem_value> program;
        for (std::size_t i = 1u; i < words.size(); ++i) {
            const auto parsed = parse_mem_assignment(words[i]);
            if (!parsed) {
                return "load_program_inline error: invalid assignment " + words[i] + "\n";
            }
            program.push_back(*parsed);
        }

        return terminal_.load_program(words[0], program);
    }

    nmi_observer::DebugTerminal terminal_{};
    bool exit_requested_ = false;
};

int run_console(int argc, char** argv)
{
    ConsoleApp app;
    bool show_banner = true;
    bool exit_after_source = false;
    std::vector<std::string> startup_commands;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            std::cout << app.execute_line("help");
            return 0;
        }
        if (arg == "--no-banner") {
            show_banner = false;
            continue;
        }
        if (arg == "--exit-after-source") {
            exit_after_source = true;
            continue;
        }
        if (arg == "--backend") {
            if (i + 1 >= argc) {
                std::cerr << "--backend requires qe6502 or perfect6502\n";
                return 2;
            }
            const std::string backend = argv[++i];
            if (backend == "qe6502") {
                startup_commands.push_back("backend_qe6502");
            } else if (backend == "perfect6502") {
                startup_commands.push_back("backend_perfect6502");
            } else {
                std::cerr << "unknown backend: " << backend << "\n";
                return 2;
            }
            continue;
        }
        if (arg == "--command") {
            if (i + 1 >= argc) {
                std::cerr << "--command requires a command line\n";
                return 2;
            }
            startup_commands.push_back(argv[++i]);
            continue;
        }
        if (arg == "--source") {
            if (i + 1 >= argc) {
                std::cerr << "--source requires a file path\n";
                return 2;
            }
            startup_commands.push_back("source " + std::string(argv[++i]));
            continue;
        }

        std::cerr << "unknown argument: " << arg << "\n";
        std::cerr << "use --help for usage\n";
        return 2;
    }

    if (show_banner) {
        std::cout << "nmi_observer_debug_console\n"
                  << "Type help for commands, exit to quit.\n";
    }

    for (const std::string& command : startup_commands) {
        try {
            const std::string result = app.execute_line(command);
            if (!result.empty()) {
                std::cout << result;
            }
        } catch (const std::exception& error) {
            std::cerr << "startup command error: " << error.what() << "\n";
            return 1;
        }
        if (app.exit_requested()) {
            return 0;
        }
    }

    if (exit_after_source) {
        return 0;
    }

    std::string line;
    while (!app.exit_requested()) {
        std::cout << prompt;
        if (!std::getline(std::cin, line)) {
            break;
        }
        try {
            const std::string result = app.execute_line(line);
            if (!result.empty()) {
                std::cout << result;
            }
        } catch (const std::exception& error) {
            std::cout << "error: " << error.what() << "\n";
        }
    }
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    try {
        return run_console(argc, argv);
    } catch (const std::exception& error) {
        std::cerr << "fatal: " << error.what() << "\n";
        return 1;
    }
}
