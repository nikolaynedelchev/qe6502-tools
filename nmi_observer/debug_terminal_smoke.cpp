#include "debug_terminal.h"

#include <cstdlib>
#include <iostream>
#include <string>

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

} // namespace

int main()
{
    nmi_observer::DebugTerminal terminal;

    const std::string help = terminal.execute_command("help");
    if (!contains(help, "DebugTerminal commands")
        || !contains(help, "TestDebugger commands")
        || !contains(help, "memory_clear [BYTE]")
        || !contains(help, "clear_memory [BYTE]")
        || !contains(help, "bootstrap start_at=0xADDR")
        || !contains(help, "memory_fill 0xFIRST 0xLAST BYTE")
        || !contains(help, "log_mem_0x0400_to_0x04ff")
        || !contains(help, "cycle_details_on/off")
        || !contains(help, "step_N")
        || !contains(help, "run_to_next_fetch")
        || contains(help, "restart_to_test")
        || contains(help, "reload")
        || contains(help, "list_opcodes")) {
        std::cerr << "DebugTerminal help has unexpected/missing markers\n" << help;
        return EXIT_FAILURE;
    }

    const std::string early_write = terminal.execute_command("0x8000=0xea");
    if (!contains(early_write, "error: no backend selected")) {
        std::cerr << "memory write before backend did not produce expected error\n" << early_write;
        return EXIT_FAILURE;
    }

    std::string log;
    log += terminal.execute_command("backend_qe6502");
    log += terminal.execute_command("status");
    log += terminal.execute_command("memory_clear 0xea");
    log += terminal.execute_command("clear_memory");
    log += terminal.execute_command("memory_fill 0x0400 0x0404 0xcc");
    log += terminal.execute_command("fill_memory 0x0500 0x0502 0xdd");
    log += terminal.execute_command("bootstrap start_at=0x0400 a=0x12 x=0x45 y=0x10 p=0x24 s=0xfd reset_vector=0x0200 brk_irq_vector=0x9100 nmi_vector=0x9000");
    log += terminal.execute_command("0x0400=0xea");
    log += terminal.execute_command("0x0401:0xea");
    log += terminal.execute_command("0x0402=0x4c");
    log += terminal.execute_command("0x0403=0x02");
    log += terminal.execute_command("0x0404=0x04");
    log += terminal.execute_command("restart_to_start_fetch");
    log += terminal.execute_command("run_to_0x0400");
    log += terminal.execute_command("cycle_details");
    log += terminal.execute_command("run_to_next_fetch");
    log += terminal.execute_command("cycle_details_on");
    log += terminal.execute_command("step_2");
    log += terminal.execute_command("cycle_details_off");
    log += terminal.execute_command("log_mem_0x0400_to_0x0404");

    if (!contains(log, "backend=qe6502 memory=cleared")
        || !contains(log, "status backend=qe6502")
        || !contains(log, "memory cleared value=$EA")
        || !contains(log, "memory cleared value=$00")
        || !contains(log, "memory filled $0400..$0404 value=$CC bytes=5")
        || !contains(log, "memory filled $0500..$0502 value=$DD bytes=3")
        || !contains(log, "bootstrap written start_at=$0400")
        || !contains(log, "mem $0400=$EA")
        || !contains(log, "mem $0401=$EA")
        || !contains(log, "restart_to_start_fetch")
        || !contains(log, "run_to address=$0400")
        || !contains(log, "run_to_next_fetch")
        || !contains(log, "reached=yes")
        || !contains(log, "registers PC=$0400 A=$12 X=$45 Y=$10 S=$FD")
        || !contains(log, "cycle_details=on")
        || !contains(log, "cycle_details=off")
        || !contains(log, "mem $0400..$0404")
        || count_occurrences(log, "stepped cycle=") < 2u
        || count_occurrences(log, "cycle_details") < 3u) {
        std::cerr << "terminal raw/bootstrap flow did not include expected markers\n" << log;
        return EXIT_FAILURE;
    }

    std::string program_log;
    program_log += terminal.execute_command("backend_perfect6502");
    program_log += terminal.load_program("inline_raw", {
        {0xFFFCu, 0x00u}, {0xFFFDu, 0x80u},
        {0x8000u, 0xEAu}, {0x8001u, 0xEAu},
    });
    program_log += terminal.execute_command("restart_to_start_fetch");
    program_log += terminal.execute_command("step_1");
    program_log += terminal.execute_command("status");

    if (!contains(program_log, "backend=perfect6502 memory=cleared")
        || !contains(program_log, "program bytes written name=\"inline_raw\"")
        || !contains(program_log, "remembered=no")
        || !contains(program_log, "restart_to_start_fetch")
        || !contains(program_log, "stepped cycle=")
        || !contains(program_log, "no_loaded_program_or_test")) {
        std::cerr << "terminal load_program one-shot flow did not include expected markers\n" << program_log;
        return EXIT_FAILURE;
    }

    try {
        (void)terminal.execute_command("restart_to_test");
        std::cerr << "restart_to_test unexpectedly succeeded\n";
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        if (std::string(error.what()).find("unknown TestDebugger command") == std::string::npos) {
            std::cerr << "restart_to_test produced unexpected error: " << error.what() << "\n";
            return EXIT_FAILURE;
        }
    }

    try {
        (void)terminal.execute_command("reload");
        std::cerr << "reload unexpectedly succeeded\n";
        return EXIT_FAILURE;
    } catch (const std::exception& error) {
        if (std::string(error.what()).find("unknown TestDebugger command") == std::string::npos) {
            std::cerr << "reload produced unexpected error: " << error.what() << "\n";
            return EXIT_FAILURE;
        }
    }

    std::cout << help << early_write << log << program_log;
    return EXIT_SUCCESS;
}
