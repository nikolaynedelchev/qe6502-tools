#include <tools6502/lockstep.hpp>

#include <algorithm>
#include <cstring>
#include <random>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace tools6502 {
namespace {

constexpr unsigned setup_restart_max_steps = 256u;
constexpr unsigned setup_bootstrap_max_steps = 4096u;

void apply_mem_values(memory_image& memory, const memory_setup& setup)
{
    for (const auto& [address, value] : setup) {
        memory[address] = value;
    }
}

void copy_memory_to_cpu(cpu6502_bridge::ICpu& cpu, const memory_image& image)
{
    std::memcpy(cpu.memory(), image.data(), image.size());
}

void initialize_memory(memory_image& image, const MemoryInit& init)
{
    std::visit([&image](const auto& policy) {
        using Policy = std::decay_t<decltype(policy)>;
        if constexpr (std::is_same_v<Policy, MemoryFill>) {
            image.fill(policy.value);
        } else if constexpr (std::is_same_v<Policy, MemoryRandom>) {
            std::mt19937 rng(policy.seed);
            std::uniform_int_distribution<int> dist(0, 255);
            for (auto& byte : image) {
                byte = static_cast<std::uint8_t>(dist(rng));
            }
        } else if constexpr (std::is_same_v<Policy, MemoryCallback>) {
            if (policy.apply) {
                policy.apply(image);
            }
        }
    }, init);
}

bool cpu_at_fetch_at(const cpu6502_bridge::ICpu& cpu,
                     std::uint16_t address) noexcept
{
    return cpu.is_opcode_fetch()
        && !cpu.is_write()
        && cpu.bus_address() == address;
}

std::uint16_t reset_vector(const cpu6502_bridge::ICpu& cpu) noexcept
{
    const auto* mem = const_cast<cpu6502_bridge::ICpu&>(cpu).memory();
    return static_cast<std::uint16_t>(mem[0xfffcu])
        | static_cast<std::uint16_t>(static_cast<std::uint16_t>(mem[0xfffdu]) << 8u);
}

bool restart_to_reset_fetch(cpu6502_bridge::ICpu& cpu)
{
    const std::uint16_t target = reset_vector(cpu);
    cpu.restart_to_start_fetch(setup_restart_max_steps);
    return cpu_at_fetch_at(cpu, target);
}

bool step_to_fetch_at_without_compare(cpu6502_bridge::ICpu& cpu,
                                      std::uint16_t address)
{
    for (unsigned steps = 0u; steps < setup_bootstrap_max_steps; ++steps) {
        if (cpu_at_fetch_at(cpu, address)) {
            return true;
        }
        cpu.step();
    }

    return cpu_at_fetch_at(cpu, address);
}

CpuTraceEntry capture_trace(const cpu6502_bridge::ICpu& cpu,
                            std::size_t cycle)
{
    return CpuTraceEntry{
        cycle,
        cpu.bus_address(),
        cpu.bus_data(),
        cpu.is_write(),
        cpu.is_opcode_fetch(),
        cpu.is_irq_asserted(),
        cpu.is_nmi_asserted(),
        cpu.pc(),
        cpu.a(),
        cpu.x(),
        cpu.y(),
        cpu.s(),
        cpu.p()
    };
}

bool traces_match(const CpuTraceEntry& left,
                  const CpuTraceEntry& right,
                  const CompareOptions& options)
{
    if (options.address && left.address != right.address) {
        return false;
    }
    if (options.data && left.data != right.data) {
        return false;
    }
    if (options.read_write && left.write != right.write) {
        return false;
    }
    if (options.opcode_fetch && left.opcode_fetch != right.opcode_fetch) {
        return false;
    }

    if (options.registers_on_fetch && left.opcode_fetch && right.opcode_fetch) {
        return left.pc == right.pc
            && left.a == right.a
            && left.x == right.x
            && left.y == right.y
            && left.s == right.s
            && left.p == right.p;
    }

    return true;
}

void append_result(LockstepRunResult& destination,
                   const LockstepRunResult& source)
{
    const std::size_t old_size = destination.left_trace.size();
    destination.left_trace.insert(destination.left_trace.end(),
                                  source.left_trace.begin(),
                                  source.left_trace.end());
    destination.right_trace.insert(destination.right_trace.end(),
                                   source.right_trace.begin(),
                                   source.right_trace.end());
    if (!destination.first_mismatch && source.first_mismatch) {
        destination.first_mismatch = old_size + *source.first_mismatch;
    }
}

} // namespace

LockstepRunner::LockstepRunner(std::unique_ptr<cpu6502_bridge::ICpu> left,
                               std::unique_ptr<cpu6502_bridge::ICpu> right)
    : left_(std::move(left)),
      right_(std::move(right))
{
    if (!left_ || !right_) {
        throw std::invalid_argument("LockstepRunner requires two CPU backends");
    }
}

cpu6502_bridge::ICpu& LockstepRunner::left() noexcept { return *left_; }
const cpu6502_bridge::ICpu& LockstepRunner::left() const noexcept { return *left_; }
cpu6502_bridge::ICpu& LockstepRunner::right() noexcept { return *right_; }
const cpu6502_bridge::ICpu& LockstepRunner::right() const noexcept { return *right_; }

bool LockstepRunner::setup_and_run(const LockstepConfig& config)
{
    memory_image image{};
    initialize_memory(image, config.memory);
    copy_memory_to_cpu(*left_, image);
    copy_memory_to_cpu(*right_, image);

    compare_ = config.compare;
    cycle_ = 0u;

    if (!restart_to_reset_fetch(*left_)) {
        return false;
    }
    if (!restart_to_reset_fetch(*right_)) {
        return false;
    }

    return true;
}

bool LockstepRunner::setup_and_run(const testcase& test,
                                   const LockstepConfig& config)
{
    memory_image image{};
    initialize_memory(image, config.memory);
    apply_mem_values(image, test.mem_setup);
    apply_mem_values(image, test.program);
    apply_mem_values(image, make_bootstrap(test));

    copy_memory_to_cpu(*left_, image);
    copy_memory_to_cpu(*right_, image);

    compare_ = config.compare;
    cycle_ = 0u;

    if (!restart_to_reset_fetch(*left_)) {
        return false;
    }
    if (!restart_to_reset_fetch(*right_)) {
        return false;
    }

    if (!step_to_fetch_at_without_compare(*left_, test.start_at)) {
        return false;
    }
    if (!step_to_fetch_at_without_compare(*right_, test.start_at)) {
        return false;
    }

    return true;
}

LockstepRunResult LockstepRunner::step()
{
    LockstepRunResult result{};
    result.left_trace.push_back(capture_trace(*left_, cycle_));
    result.right_trace.push_back(capture_trace(*right_, cycle_));

    if (!traces_match(result.left_trace.back(), result.right_trace.back(), compare_)) {
        result.first_mismatch = 0u;
        return result;
    }

    left_->step();
    right_->step();
    ++cycle_;
    return result;
}

LockstepRunResult LockstepRunner::step_cycles(std::size_t count)
{
    LockstepRunResult result{};
    for (std::size_t index = 0u; index < count; ++index) {
        const auto one = step();
        append_result(result, one);
        if (one.first_mismatch) {
            break;
        }
    }
    return result;
}

LockstepRunResult LockstepRunner::step_to_fetch(std::size_t max_steps)
{
    LockstepRunResult result{};
    for (std::size_t index = 0u; index < max_steps; ++index) {
        const auto one = step();
        append_result(result, one);
        if (one.first_mismatch) {
            break;
        }
        if (left_->is_opcode_fetch() && right_->is_opcode_fetch()) {
            break;
        }
    }
    return result;
}

LockstepRunResult LockstepRunner::step_to_fetch_at(std::uint16_t address,
                                                   std::size_t max_steps)
{
    LockstepRunResult result{};
    for (std::size_t index = 0u; index < max_steps; ++index) {
        const auto one = step();
        append_result(result, one);
        if (one.first_mismatch) {
            break;
        }
        if (cpu_at_fetch_at(*left_, address) && cpu_at_fetch_at(*right_, address)) {
            break;
        }
    }
    return result;
}

void LockstepRunner::irq(bool asserted) noexcept
{
    left_->irq(asserted);
    right_->irq(asserted);
}

void LockstepRunner::nmi(bool asserted) noexcept
{
    left_->nmi(asserted);
    right_->nmi(asserted);
}

} // namespace tools6502
