#include "nmi_observer.h"

#include <cpu6502_bridge/cpu.hpp>

#include <algorithm>
#include <array>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace nmi_observer {
namespace {

constexpr std::uint8_t fill_byte = 0xEAu; // legal NOP
constexpr std::uint16_t reset_entry = 0xE000u;
constexpr std::uint16_t irq_trap = 0xE100u;
constexpr std::uint16_t nmi_trap = 0xE200u;
constexpr std::uint32_t pretest_cycle_limit = 20000u;
constexpr int trace_cycles = 20;
constexpr int default_interrupt_hold_cycles = 1;

enum class backend_kind {
    perfect6502,
    qe6502,
};

enum class interrupt_mode {
    nmi,
    irq,
};

const char* backend_name(backend_kind backend)
{
    return backend == backend_kind::qe6502 ? "qe6502" : "perfect6502";
}

const char* interrupt_name(interrupt_mode mode)
{
    return mode == interrupt_mode::irq ? "IRQ" : "NMI";
}

const char* interrupt_name_lower(interrupt_mode mode)
{
    return mode == interrupt_mode::irq ? "irq" : "nmi";
}

struct options {
    std::optional<std::uint8_t> opcode_filter{};
    std::optional<std::uint32_t> testcase_filter{};
    bool traces = true;
    int nmi_hold_cycles = default_interrupt_hold_cycles;
    interrupt_mode mode = interrupt_mode::nmi;
    backend_kind backend = backend_kind::perfect6502;
};

struct interrupt_pulse {
    int assert_before_cycle = 0;
    int deassert_before_cycle = -1;
};

struct bus_sample {
    int cycle = 0;
    bool fetch = false;
    bool write = false;
    std::uint16_t address = 0;
    std::uint8_t data = 0;
};

struct trace_result {
    bool aligned = false;
    std::vector<bus_sample> samples{};
};

struct testcase_report {
    std::uint8_t opcode = 0;
    std::uint32_t testcase_index = 0;
    std::uint32_t metadata_cycles = 0;
    bool baseline_reproducible = false;
    bool boundary_reproducible = false;
    int boundary_cycle = -1;
    int boundary_cycle_confirm = -1;
    int primary_scan_last_cycle = -1;
    int primary_last_immediate = -1;
    int primary_first_deferred = -1;
};

bool parse_u32(const char* text, std::uint32_t& out)
{
    if (text == nullptr || *text == '\0') {
        return false;
    }

    char* end = nullptr;
    const unsigned long long value = std::strtoull(text, &end, 0);
    if (end == nullptr || *end != '\0' || value > 0xFFFFFFFFull) {
        return false;
    }

    out = static_cast<std::uint32_t>(value);
    return true;
}

void print_usage(const char* argv0)
{
    std::fprintf(stderr,
        "usage: %s [--backend perfect|qe6502] [--opcode XX] [--case N] [--no-traces] [--interrupt nmi|irq] [--nmi-hold-cycles N]\n"
        "  --backend NAME:      use perfect6502 or qe6502 backend; default perfect6502\n"
        "  --opcode XX:         run only one opcode, decimal or 0x-prefixed\n"
        "  --case N:            run only testcase index N within the selected/all opcodes\n"
        "  --no-traces:         print summaries without full 20-cycle timelines\n"
        "  --interrupt MODE:    test nmi or irq; default nmi\n"
        "  --nmi-hold-cycles N: keep the interrupt line asserted for N whole-cycle steps after assert; default 1\n"
        "  --nmi-hold-tests N:  deprecated alias for --nmi-hold-cycles\n",
        argv0);
}

bool parse_args(int argc, char** argv, options& out)
{
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--backend") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return false;
            }
            const char* backend = argv[++i];
            if (std::strcmp(backend, "perfect") == 0 || std::strcmp(backend, "perfect6502") == 0) {
                out.backend = backend_kind::perfect6502;
            } else if (std::strcmp(backend, "qe") == 0 || std::strcmp(backend, "qe6502") == 0) {
                out.backend = backend_kind::qe6502;
            } else {
                std::fprintf(stderr, "invalid backend: %s\n", backend);
                return false;
            }
        } else if (std::strcmp(argv[i], "--perfect") == 0 || std::strcmp(argv[i], "--perfect6502") == 0) {
            out.backend = backend_kind::perfect6502;
        } else if (std::strcmp(argv[i], "--qe") == 0 || std::strcmp(argv[i], "--qe6502") == 0) {
            out.backend = backend_kind::qe6502;
        } else if (std::strcmp(argv[i], "--opcode") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return false;
            }
            std::uint32_t value = 0;
            if (!parse_u32(argv[++i], value) || value > 0xFFu) {
                std::fprintf(stderr, "invalid opcode: %s\n", argv[i]);
                return false;
            }
            out.opcode_filter = static_cast<std::uint8_t>(value);
        } else if (std::strcmp(argv[i], "--case") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return false;
            }
            std::uint32_t value = 0;
            if (!parse_u32(argv[++i], value)) {
                std::fprintf(stderr, "invalid testcase index: %s\n", argv[i]);
                return false;
            }
            out.testcase_filter = value;
        } else if (std::strcmp(argv[i], "--no-traces") == 0) {
            out.traces = false;
        } else if (std::strcmp(argv[i], "--interrupt") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return false;
            }
            const char* mode = argv[++i];
            if (std::strcmp(mode, "nmi") == 0 || std::strcmp(mode, "NMI") == 0) {
                out.mode = interrupt_mode::nmi;
            } else if (std::strcmp(mode, "irq") == 0 || std::strcmp(mode, "IRQ") == 0) {
                out.mode = interrupt_mode::irq;
            } else {
                std::fprintf(stderr, "invalid interrupt mode: %s\n", mode);
                return false;
            }
        } else if (std::strcmp(argv[i], "--nmi") == 0) {
            out.mode = interrupt_mode::nmi;
        } else if (std::strcmp(argv[i], "--irq") == 0) {
            out.mode = interrupt_mode::irq;
        } else if (std::strcmp(argv[i], "--nmi-hold-cycles") == 0 || std::strcmp(argv[i], "--nmi-hold-tests") == 0) {
            if (i + 1 >= argc) {
                print_usage(argv[0]);
                return false;
            }
            std::uint32_t value = 0;
            if (!parse_u32(argv[++i], value) || value == 0u || value > 1000u) {
                std::fprintf(stderr, "invalid interrupt hold cycle count: %s\n", argv[i]);
                return false;
            }
            out.nmi_hold_cycles = static_cast<int>(value);
        } else if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            std::exit(EXIT_SUCCESS);
        } else {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage(argv[0]);
            return false;
        }
    }

    return true;
}

std::unique_ptr<cpu6502_bridge::ICpu> make_cpu(backend_kind backend)
{
    if (backend == backend_kind::qe6502) {
        return cpu6502_bridge::make_qe6502_cpu();
    }
    return cpu6502_bridge::make_perfect6502_cpu();
}

void set_interrupt(cpu6502_bridge::ICpu& cpu, interrupt_mode mode, bool asserted)
{
    if (mode == interrupt_mode::irq) {
        cpu.irq(asserted);
    } else {
        cpu.nmi(asserted);
    }
}

bus_sample current_bus_sample(const cpu6502_bridge::ICpu& cpu, int cycle_index)
{
    return bus_sample{
        cycle_index,
        cpu.is_opcode_fetch(),
        cpu.is_write(),
        cpu.bus_address(),
        cpu.bus_data(),
    };
}

bool same_bus_sample(const bus_sample& lhs, const bus_sample& rhs)
{
    // Compare externally visible bus requests only.  The bridge exposes FETCH as
    // useful diagnostic metadata, but qe6502 and perfect6502 can disagree on
    // whether the first interrupt-internal read still has FETCH/SYNC asserted.
    // The observer's timing classification is based on bus divergence.
    return lhs.write == rhs.write
        && lhs.address == rhs.address
        && lhs.data == rhs.data;
}

bool same_trace(const std::vector<bus_sample>& lhs, const std::vector<bus_sample>& rhs)
{
    if (lhs.size() != rhs.size()) {
        return false;
    }
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        if (!same_bus_sample(lhs[i], rhs[i])) {
            return false;
        }
    }
    return true;
}

int first_diff_cycle(const std::vector<bus_sample>& baseline, const std::vector<bus_sample>& observed)
{
    const std::size_t count = std::min(baseline.size(), observed.size());
    for (std::size_t i = 0; i < count; ++i) {
        if (!same_bus_sample(baseline[i], observed[i])) {
            return static_cast<int>(i);
        }
    }
    if (baseline.size() != observed.size()) {
        return static_cast<int>(count);
    }
    return -1;
}

void print_sample(const char* prefix, const bus_sample& sample)
{
    std::printf("%sC%02d %s%c $%04X data=$%02X\n",
        prefix,
        sample.cycle,
        sample.fetch ? "F" : " ",
        sample.write ? 'W' : 'R',
        static_cast<unsigned>(sample.address),
        static_cast<unsigned>(sample.data));
}

void print_trace(const char* title, const std::vector<bus_sample>& trace)
{
    std::printf("  %s\n", title);
    for (const bus_sample& sample : trace) {
        print_sample("    ", sample);
    }
}

void print_trace_diff_window(
    const std::vector<bus_sample>& baseline,
    const std::vector<bus_sample>& observed,
    int diff_cycle)
{
    if (diff_cycle < 0) {
        std::printf("      diff_window: none\n");
        return;
    }

    const int begin = std::max(0, diff_cycle - 2);
    const int end = std::min(trace_cycles - 1, diff_cycle + 3);
    std::printf("      diff_window:\n");
    for (int i = begin; i <= end; ++i) {
        std::printf("        C%02d baseline=", i);
        if (i < static_cast<int>(baseline.size())) {
            const bus_sample& b = baseline[static_cast<std::size_t>(i)];
            std::printf("%s%c:$%04X:$%02X", b.fetch ? "F" : " ", b.write ? 'W' : 'R', static_cast<unsigned>(b.address), static_cast<unsigned>(b.data));
        } else {
            std::printf("<missing>");
        }
        std::printf(" observed=");
        if (i < static_cast<int>(observed.size())) {
            const bus_sample& o = observed[static_cast<std::size_t>(i)];
            std::printf("%s%c:$%04X:$%02X", o.fetch ? "F" : " ", o.write ? 'W' : 'R', static_cast<unsigned>(o.address), static_cast<unsigned>(o.data));
        } else {
            std::printf("<missing>");
        }
        std::printf("%s\n", i == diff_cycle ? "  <-- first-diff" : "");
    }
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

std::uint8_t testcase_status_for_mode(const testcase& test, interrupt_mode mode)
{
    constexpr std::uint8_t irq_disable_flag = 0x04u;
    if (mode == interrupt_mode::irq) {
        return static_cast<std::uint8_t>(test.P & ~irq_disable_flag);
    }
    return test.P;
}

void load_clean_memory(cpu6502_bridge::ICpu& cpu, const testcase& test, interrupt_mode mode)
{
    std::uint8_t* mem = cpu.memory();
    std::fill(mem, mem + 65536u, fill_byte);

    apply_mem_values(cpu, test.mem_setup);

    const auto boot = asm6502::bootstrap_program(
        test.A,
        test.X,
        test.Y,
        testcase_status_for_mode(test, mode),
        test.S,
        test.start_at,
        reset_entry,
        irq_trap,
        nmi_trap);
    apply_mem_values(cpu, boot);
    apply_mem_values(cpu, make_irq_handler());
    apply_mem_values(cpu, make_nmi_handler());
}

bool align_to_test_start(cpu6502_bridge::ICpu& cpu, const testcase& test)
{
    for (std::uint32_t cycle = 0; cycle < pretest_cycle_limit; ++cycle) {
        if (cpu.is_opcode_fetch()
            && !cpu.is_write()
            && cpu.bus_address() == test.start_at
            && cpu.bus_data() == test.opcode) {
            return true;
        }
        cpu.step();
    }
    return false;
}

trace_result run_trace(const testcase& test, std::optional<interrupt_pulse> pulse, interrupt_mode mode, backend_kind backend)
{
    auto cpu = make_cpu(backend);
    load_clean_memory(*cpu, test, mode);
    cpu->restart();
    set_interrupt(*cpu, interrupt_mode::irq, false);
    set_interrupt(*cpu, interrupt_mode::nmi, false);

    trace_result result{};
    if (!align_to_test_start(*cpu, test)) {
        return result;
    }

    result.aligned = true;
    result.samples.reserve(static_cast<std::size_t>(trace_cycles));

    for (int cycle_index = 0; cycle_index < trace_cycles; ++cycle_index) {
        if (pulse.has_value() && pulse->assert_before_cycle == cycle_index) {
            set_interrupt(*cpu, mode, true);
        }
        if (pulse.has_value() && pulse->deassert_before_cycle == cycle_index) {
            set_interrupt(*cpu, mode, false);
        }

        result.samples.push_back(current_bus_sample(*cpu, cycle_index));
        cpu->step();
    }

    return result;
}

trace_result run_trace_assert_held(const testcase& test, int inject_before_cycle, interrupt_mode mode, backend_kind backend)
{
    return run_trace(test, interrupt_pulse{inject_before_cycle, -1}, mode, backend);
}

trace_result run_trace_pulse(const testcase& test, int inject_before_cycle, int hold_cycles, interrupt_mode mode, backend_kind backend)
{
    return run_trace(test, interrupt_pulse{inject_before_cycle, inject_before_cycle + hold_cycles}, mode, backend);
}

const char* classification_text(int diff_cycle, int boundary_cycle)
{
    if (diff_cycle < 0) {
        return "no-diff-within-window";
    }
    if (boundary_cycle < 0) {
        return "unclassified-no-boundary";
    }
    if (diff_cycle == boundary_cycle) {
        return "immediate";
    }
    if (diff_cycle > boundary_cycle) {
        return "deferred";
    }
    return "earlier-than-boundary";
}

bool should_run_opcode(const options& opts, std::uint8_t opcode)
{
    if (opts.opcode_filter.has_value()) {
        return *opts.opcode_filter == opcode;
    }
    return opcode != 0x00u;
}

bool should_run_case(const options& opts, std::uint32_t index)
{
    return !opts.testcase_filter.has_value() || *opts.testcase_filter == index;
}

void print_testcase_header(const testcase& test, std::uint32_t index, const options& opts)
{
    const std::uint8_t effective_p = testcase_status_for_mode(test, opts.mode);
    std::printf("\n=== opcode=$%02X testcase=%" PRIu32 " ===\n",
        static_cast<unsigned>(test.opcode),
        index);
    std::printf("  description: %s\n", test.description.c_str());
    std::printf("  backend: %s\n", backend_name(opts.backend));
    std::printf("  start_at: $%04X bytes=%u metadata_cycles=%u A=$%02X X=$%02X Y=$%02X P=$%02X S=$%02X",
        static_cast<unsigned>(test.start_at),
        static_cast<unsigned>(test.bytes),
        static_cast<unsigned>(test.expected_cycles),
        static_cast<unsigned>(test.A),
        static_cast<unsigned>(test.X),
        static_cast<unsigned>(test.Y),
        static_cast<unsigned>(effective_p),
        static_cast<unsigned>(test.S));
    if (effective_p != test.P) {
        std::printf(" original_P=$%02X", static_cast<unsigned>(test.P));
    }
    std::printf("\n");
}

testcase_report run_testcase(const testcase& test, std::uint32_t index, const options& opts)
{
    print_testcase_header(test, index, opts);

    testcase_report report{};
    report.opcode = test.opcode;
    report.testcase_index = index;
    report.metadata_cycles = test.expected_cycles;

    const trace_result baseline0 = run_trace(test, std::nullopt, opts.mode, opts.backend);
    const trace_result baseline1 = run_trace(test, std::nullopt, opts.mode, opts.backend);
    if (!baseline0.aligned || !baseline1.aligned) {
        std::printf("  ERROR: failed to align %s backend to opcode fetch at testcase.start_at\n", backend_name(opts.backend));
        return report;
    }

    report.baseline_reproducible = same_trace(baseline0.samples, baseline1.samples);
    std::printf("  baseline_reproducible: %s\n", report.baseline_reproducible ? "yes" : "no");
    if (opts.traces) {
        print_trace("baseline_trace_20:", baseline0.samples);
    }

    if (!report.baseline_reproducible) {
        const int diff = first_diff_cycle(baseline0.samples, baseline1.samples);
        std::printf("  baseline_repro_first_diff: %d\n", diff);
        print_trace_diff_window(baseline0.samples, baseline1.samples, diff);
        return report;
    }

    const trace_result early0 = run_trace_assert_held(test, 0, opts.mode, opts.backend);
    const trace_result early1 = run_trace_assert_held(test, 0, opts.mode, opts.backend);
    if (!early0.aligned || !early1.aligned) {
        std::printf("  ERROR: failed to align during early-%s boundary discovery\n", interrupt_name(opts.mode));
        return report;
    }

    report.boundary_cycle = first_diff_cycle(baseline0.samples, early0.samples);
    report.boundary_cycle_confirm = first_diff_cycle(baseline0.samples, early1.samples);
    report.boundary_reproducible = report.boundary_cycle == report.boundary_cycle_confirm;

    std::printf("  early_%s_boundary_cycle: %d\n", interrupt_name_lower(opts.mode), report.boundary_cycle);
    std::printf("  early_%s_boundary_confirm: %d\n", interrupt_name_lower(opts.mode), report.boundary_cycle_confirm);
    std::printf("  boundary_reproducible: %s\n", report.boundary_reproducible ? "yes" : "no");
    if (opts.traces) {
        char title[64];
        std::snprintf(title, sizeof(title), "early_%s_trace_20:", interrupt_name_lower(opts.mode));
        print_trace(title, early0.samples);
        print_trace_diff_window(baseline0.samples, early0.samples, report.boundary_cycle);
    }

    if (!report.boundary_reproducible || report.boundary_cycle < 0) {
        std::printf("  injection_scan: skipped; boundary is not stable or not visible inside trace window\n");
        return report;
    }

    const int primary_scan_last = std::min(trace_cycles - 1, static_cast<int>(test.expected_cycles));
    report.primary_scan_last_cycle = primary_scan_last;

    std::printf("  injection_scan_current_instruction_plus_reserve: inject_range=0..%02d metadata_cycles=%u %s_hold_cycles=%d\n",
        primary_scan_last,
        static_cast<unsigned>(test.expected_cycles),
        interrupt_name_lower(opts.mode),
        opts.nmi_hold_cycles);
    for (int inject_cycle = 0; inject_cycle <= primary_scan_last; ++inject_cycle) {
        const int deassert_cycle = inject_cycle + opts.nmi_hold_cycles;
        const trace_result observed = run_trace_pulse(test, inject_cycle, opts.nmi_hold_cycles, opts.mode, opts.backend);
        if (!observed.aligned) {
            std::printf("    inject_before_cycle=%02d hold_cycles=%02d deassert_before_cycle=%02d aligned=no\n",
                inject_cycle,
                opts.nmi_hold_cycles,
                deassert_cycle);
            continue;
        }
        const int diff = first_diff_cycle(baseline0.samples, observed.samples);
        const char* classification = classification_text(diff, report.boundary_cycle);
        std::printf("    inject_before_cycle=%02d hold_cycles=%02d deassert_before_cycle=%02d first_diff_cycle=%d classification=%s\n",
            inject_cycle,
            opts.nmi_hold_cycles,
            deassert_cycle,
            diff,
            classification);
        if (std::strcmp(classification, "immediate") == 0) {
            report.primary_last_immediate = inject_cycle;
        } else if (std::strcmp(classification, "deferred") == 0 && report.primary_first_deferred < 0) {
            report.primary_first_deferred = inject_cycle;
        }
        if (opts.traces) {
            print_trace_diff_window(baseline0.samples, observed.samples, diff);
        }
    }

    std::printf("  testcase_classification_summary: primary_scan=0..%02d hold_cycles=%02d last_immediate=",
        primary_scan_last,
        opts.nmi_hold_cycles);
    if (report.primary_last_immediate >= 0) {
        std::printf("%02d", report.primary_last_immediate);
    } else {
        std::printf("-");
    }
    std::printf(" first_deferred=");
    if (report.primary_first_deferred >= 0) {
        std::printf("%02d cycles_from_metadata_end=%d",
            report.primary_first_deferred,
            static_cast<int>(test.expected_cycles) - report.primary_first_deferred);
    } else {
        std::printf("-");
    }
    std::printf("\n");

    if (primary_scan_last + 1 < trace_cycles) {
        std::printf("  post_instruction_nop_stream_scan: inject_range=%02d..%02d %s_hold_cycles=%d\n",
            primary_scan_last + 1,
            trace_cycles - 1,
            interrupt_name_lower(opts.mode),
            opts.nmi_hold_cycles);
        for (int inject_cycle = primary_scan_last + 1; inject_cycle < trace_cycles; ++inject_cycle) {
            const int deassert_cycle = inject_cycle + opts.nmi_hold_cycles;
            const trace_result observed = run_trace_pulse(test, inject_cycle, opts.nmi_hold_cycles, opts.mode, opts.backend);
            if (!observed.aligned) {
                std::printf("    inject_before_cycle=%02d hold_cycles=%02d deassert_before_cycle=%02d aligned=no\n",
                    inject_cycle,
                    opts.nmi_hold_cycles,
                    deassert_cycle);
                continue;
            }
            const int diff = first_diff_cycle(baseline0.samples, observed.samples);
            std::printf("    inject_before_cycle=%02d hold_cycles=%02d deassert_before_cycle=%02d first_diff_cycle=%d classification=%s\n",
                inject_cycle,
                opts.nmi_hold_cycles,
                deassert_cycle,
                diff,
                classification_text(diff, report.boundary_cycle));
            if (opts.traces) {
                print_trace_diff_window(baseline0.samples, observed.samples, diff);
            }
        }
    }

    return report;
}

void print_final_summary(const std::vector<testcase_report>& reports)
{
    std::printf("\n=== summary ===\n");
    std::printf("  testcases_run: %zu\n", reports.size());

    std::array<std::vector<int>, 256> boundaries{};
    std::array<std::vector<int>, 256> first_deferred_by_opcode{};
    std::array<std::vector<int>, 256> deferred_from_end_by_opcode{};

    for (const testcase_report& report : reports) {
        if (report.baseline_reproducible && report.boundary_reproducible && report.boundary_cycle >= 0) {
            boundaries[report.opcode].push_back(report.boundary_cycle);
        }
        if (report.primary_first_deferred >= 0) {
            first_deferred_by_opcode[report.opcode].push_back(report.primary_first_deferred);
            deferred_from_end_by_opcode[report.opcode].push_back(
                static_cast<int>(report.metadata_cycles) - report.primary_first_deferred);
        }
    }

    for (std::size_t opcode = 0; opcode < boundaries.size(); ++opcode) {
        if (boundaries[opcode].empty()) {
            continue;
        }
        std::vector<int> boundary_values = boundaries[opcode];
        std::sort(boundary_values.begin(), boundary_values.end());
        boundary_values.erase(std::unique(boundary_values.begin(), boundary_values.end()), boundary_values.end());

        std::vector<int> deferred_values = first_deferred_by_opcode[opcode];
        std::sort(deferred_values.begin(), deferred_values.end());
        deferred_values.erase(std::unique(deferred_values.begin(), deferred_values.end()), deferred_values.end());

        std::vector<int> from_end_values = deferred_from_end_by_opcode[opcode];
        std::sort(from_end_values.begin(), from_end_values.end());
        from_end_values.erase(std::unique(from_end_values.begin(), from_end_values.end()), from_end_values.end());

        std::printf("  opcode=$%02X boundary_cycles=", static_cast<unsigned>(opcode));
        for (std::size_t i = 0; i < boundary_values.size(); ++i) {
            std::printf("%s%d", i == 0u ? "" : ",", boundary_values[i]);
        }
        std::printf("%s", boundary_values.size() == 1u ? " stable-across-reported-testcases" : " varies-across-testcases");

        std::printf(" first_deferred=");
        if (deferred_values.empty()) {
            std::printf("-");
        } else {
            for (std::size_t i = 0; i < deferred_values.size(); ++i) {
                std::printf("%s%d", i == 0u ? "" : ",", deferred_values[i]);
            }
        }
        std::printf(" cycles_from_metadata_end=");
        if (from_end_values.empty()) {
            std::printf("-");
        } else {
            for (std::size_t i = 0; i < from_end_values.size(); ++i) {
                std::printf("%s%d", i == 0u ? "" : ",", from_end_values[i]);
            }
        }
        std::printf("\n");
    }
}

} // namespace

int app_main(int argc, char** argv)
{
    options opts{};
    if (!parse_args(argc, argv, opts)) {
        return EXIT_FAILURE;
    }

    const auto testcases = get_nmos6502_opcode_testcases();
    std::vector<testcase_report> reports{};

    std::printf("cpu6502_interrupt_observer: bridge-based %s timing observer\n", interrupt_name(opts.mode));
    std::printf("cpu6502_interrupt_observer: backend is %s; use --backend perfect|qe6502 to change this\n", backend_name(opts.backend));
    std::printf("cpu6502_interrupt_observer: interrupt mode is %s; use --interrupt nmi|irq to change this\n", interrupt_name_lower(opts.mode));
    std::printf("cpu6502_interrupt_observer: memory fill byte is legal NOP $%02X; BRK opcode $00 is skipped unless selected explicitly\n",
        static_cast<unsigned>(fill_byte));
    std::printf("cpu6502_interrupt_observer: each trace records %d whole cycles after opcode fetch at testcase.start_at\n", trace_cycles);
    std::printf("cpu6502_interrupt_observer: both bridge backends can expose an initial fake fetch; sync starts only at fetch from testcase.start_at\n");
    std::printf("cpu6502_interrupt_observer: primary injection scan is 0..metadata_cycles inclusive; later cycles are reported separately as post-instruction NOP-stream checks\n");
    std::printf("cpu6502_interrupt_observer: for each inject cycle, %s is deasserted after %d whole-cycle step(s); use --nmi-hold-cycles N to change this\n",
        interrupt_name(opts.mode),
        opts.nmi_hold_cycles);

    for (const auto& [opcode, cases] : testcases) {
        if (!should_run_opcode(opts, opcode)) {
            continue;
        }
        for (std::size_t i = 0; i < cases.size(); ++i) {
            if (!should_run_case(opts, static_cast<std::uint32_t>(i))) {
                continue;
            }
            reports.push_back(run_testcase(cases[i], static_cast<std::uint32_t>(i), opts));
        }
    }

    print_final_summary(reports);
    return EXIT_SUCCESS;
}

} // namespace nmi_observer

int main(int argc, char** argv)
{
    return ::nmi_observer::app_main(argc, argv);
}
