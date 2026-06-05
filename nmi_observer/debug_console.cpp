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
    std::size_t equals = text.find('=');
    if (equals == std::string_view::npos) {
        equals = text.find(':');
    }
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


std::string detailed_help()
{
    std::ostringstream out;
    out << "Debug console guide:\n"
        << "1. Select a backend first. Backend selection creates a fresh CPU and clears memory.\n"
        << "     backend_qe6502\n"
        << "     backend_perfect6502\n\n"
        << "2. Script files are line-based. Use one command per line.\n"
        << "   Use run_script_file <path> interactively, or --run-script-file <path> from the command line.\n\n"
        << "3. Memory setup is explicit. There is no reload and no remembered program/testcase.\n"
        << "   If you want a clean layout, say memory_clear and then write exactly the bytes you need.\n\n"
        << "4. Raw memory probes can write bytes directly after a backend is selected:\n"
        << "     0xfffc=0x00\n"
        << "     0xfffd=0x80\n"
        << "     0x8000=0xea\n"
        << "   ':' is also accepted: 0x8000:0xea. Byte writes are immediate and are not replayed later.\n"
        << "   To fill an inclusive memory range, use memory_fill 0xFIRST 0xLAST BYTE.\n"
        << "   Example: memory_fill 0x0400 0x04ff 0xea. Repeat it for different regions.\n\n"
        << "5. bootstrap writes asm6502 bootstrap bytes once. Only these key names are accepted:\n"
        << "     start_at a x y p s reset_vector brk_irq_vector nmi_vector\n"
        << "   start_at is required; other values have defaults.\n"
        << "   Important: bootstrap code is loaded at reset_vector, then it sets registers and jumps to start_at.\n\n"
        << "6. Minimal raw program bootstrap example:\n"
        << "     backend_qe6502\n"
        << "     memory_clear\n"
        << "     bootstrap start_at=0x0400 a=0x12 x=0x45 y=0x10 p=0x24 s=0xfd\n"
        << "     0x0400=0xea\n"
        << "     0x0401=0xff\n"
        << "     run_to_0x0400\n"
        << "     cycle_details\n\n"
        << "7. Example usage: run one NMI probe against qe6502.\n"
        << "   Start command:\n"
        << "     nmi_observer_debug_console --no-banner --backend qe6502 --run-script-file nmi_nop_probe.txt --exit-after-script\n"
        << "   Script file nmi_nop_probe.txt:\n"
        << "     memory_clear\n"
        << "     bootstrap start_at=0x0400 a=0x00 x=0x00 y=0x00 p=0x24 s=0xfd reset_vector=0x0200 brk_irq_vector=0x9100 nmi_vector=0x9000\n"
        << "     0x0400=0xea\n"
        << "     0x0401=0xea\n"
        << "     0x0402=0xea\n"
        << "     0x9000=0xea\n"
        << "     0x9001=0xea\n"
        << "     run_to_0x0400\n"
        << "     cycle_details\n"
        << "     nmi_assert\n"
        << "     log_bus_state_on\n"
        << "     log_registers_on\n"
        << "     step_24\n\n"
        << "8. Example usage: run the same IRQ probe against both backends.\n"
        << "   Start commands:\n"
        << "     nmi_observer_debug_console --no-banner --backend qe6502 --run-script-file irq_lda_probe.txt --exit-after-script\n"
        << "     nmi_observer_debug_console --no-banner --backend perfect6502 --run-script-file irq_lda_probe.txt --exit-after-script\n"
        << "   Script file irq_lda_probe.txt:\n"
        << "     memory_clear\n"
        << "     bootstrap start_at=0x0400 a=0x00 x=0x00 y=0x00 p=0x24 s=0xfd reset_vector=0x0200 brk_irq_vector=0x9100 nmi_vector=0x9000\n"
        << "     0x0400=0x58\n"
        << "     0x0401=0xad\n"
        << "     0x0402=0x00\n"
        << "     0x0403=0x20\n"
        << "     0x0404=0xea\n"
        << "     0x2000=0x42\n"
        << "     0x9000=0xea\n"
        << "     0x9100=0xea\n"
        << "     run_to_0x0400\n"
        << "     cycle_details\n"
        << "     step_2\n"
        << "     irq_assert\n"
        << "     nmi_deassert\n"
        << "     log_bus_state_on\n"
        << "     log_registers_on\n"
        << "     step_28\n\n"
        << "9. You can patch memory during execution. Example:\n"
        << "     step_2\n"
        << "     0x8002=0x00\n"
        << "     step_10\n\n"
        << "10. Useful debugger logging commands:\n"
        << "     log_bus_state_on     show address/data/fetch/write after each step\n"
        << "     log_registers_on     show registers after each step\n"
        << "     cycle_details_on     show registers, bus state, and stack after each step\n"
        << "     step_N               execute N bus cycles, e.g. step_20\n"
        << "     run_to_next_fetch    run until the next opcode fetch after at least one step\n\n"
        << "11. load_program_inline is still available for short one-line layouts:\n"
        << "     load_program_inline probe 0xfffc=0x00 0xfffd=0x80 0x8000=0xea\n\n"
        << "12. repeat can run one console command several times:\n"
        << "     repeat 5 step\n\n"
        << "Use help for the compact command list and terminal_help for DebugTerminal/TestDebugger commands.\n";
    return out.str();
}

std::string console_help(const nmi_observer::DebugTerminal& terminal)
{
    std::ostringstream out;
    out << "Console app commands:\n"
        << "help                         Show this help plus terminal/debugger help.\n"
        << "app_help                     Show only console app commands.\n"
        << "terminal_help                Show DebugTerminal/TestDebugger commands.\n"
        << "detailed_help | guide        Show practical usage guide and scripting tips.\n"
        << "run_script_file <file>       Run one command per non-empty line from file.\n"
        << "source <file>                Alias for run_script_file.\n"
        << "repeat <N> <command>         Run one command N times.\n"
        << "memory_clear [BYTE]          Fill selected backend memory with BYTE, default $00.\n"
        << "clear_memory [BYTE]          Alias for memory_clear [BYTE].\n"
        << "memory_fill 0xFIRST 0xLAST BYTE\n"
        << "                             Fill inclusive memory range with BYTE.\n"
        << "fill_memory 0xFIRST 0xLAST BYTE\n"
        << "                             Alias for memory_fill.\n"
        << "bootstrap start_at=0xADDR ...\n"
        << "                             Write asm6502 bootstrap bytes once. See detailed_help.\n"
        << "load_program_inline <name> A=V ...\n"
        << "                             Write raw bytes once, e.g. 0xfffc=0x00 0x8000=0xea.\n"
        << "0xADDR=0xBYTE                Write byte to selected backend memory.\n"
        << "0xADDR:0xBYTE                Same as 0xADDR=0xBYTE.\n"
        << "exit | quit                  Exit interactive console.\n"
        << "\nCommand line options:\n"
        << "--help                       Print this help and exit.\n"
        << "--backend qe6502|perfect6502 Select backend before commands/source files.\n"
        << "--command <line>             Execute one console line before interactive mode.\n"
        << "--run-script-file <file>     Execute script file before interactive mode.\n"
        << "--source <file>              Alias for --run-script-file.\n"
        << "--exit-after-script          Exit after startup commands/scripts.\n"
        << "--exit-after-source          Alias for --exit-after-script.\n"
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
        << "detailed_help | guide        Show practical usage guide and scripting tips.\n"
        << "run_script_file <file>       Run one command per non-empty line from file.\n"
        << "source <file>                Alias for run_script_file.\n"
        << "repeat <N> <command>         Run one command N times.\n"
        << "memory_clear [BYTE]          Fill selected backend memory with BYTE, default $00.\n"
        << "clear_memory [BYTE]          Alias for memory_clear [BYTE].\n"
        << "memory_fill 0xFIRST 0xLAST BYTE\n"
        << "                             Fill inclusive memory range with BYTE.\n"
        << "fill_memory 0xFIRST 0xLAST BYTE\n"
        << "                             Alias for memory_fill.\n"
        << "bootstrap start_at=0xADDR ...\n"
        << "                             Write asm6502 bootstrap bytes once. See detailed_help.\n"
        << "load_program_inline <name> A=V ...\n"
        << "                             Write raw bytes once, e.g. 0xfffc=0x00 0x8000=0xea.\n"
        << "0xADDR=0xBYTE                Write byte to selected backend memory.\n"
        << "0xADDR:0xBYTE                Same as 0xADDR=0xBYTE.\n"
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
        if (line == "detailed_help" || line == "guide") {
            return detailed_help();
        }
        if (line == "exit" || line == "quit") {
            exit_requested_ = true;
            return "exit requested\n";
        }
        if (starts_with(line, "run_script_file ")) {
            return source_file(trim_copy(std::string_view(line).substr(16u)));
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
            return "run_script_file error: missing file path\n";
        }

        std::ifstream input(path);
        if (!input) {
            return "run_script_file error: cannot open " + path + "\n";
        }

        std::ostringstream out;
        out << "script begin " << path << "\n";
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
        out << "script end " << path << "\n";
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
        if (arg == "--exit-after-source" || arg == "--exit-after-script") {
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
        if (arg == "--source" || arg == "--run-script-file") {
            if (i + 1 >= argc) {
                std::cerr << arg << " requires a file path\n";
                return 2;
            }
            startup_commands.push_back("run_script_file " + std::string(argv[++i]));
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
