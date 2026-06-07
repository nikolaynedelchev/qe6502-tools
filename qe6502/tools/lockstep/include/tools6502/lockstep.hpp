#pragma once

#include <cpu6502_bridge/cpu.hpp>
#include <tools6502/common.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <variant>
#include <vector>

namespace tools6502 {

using memory_image = std::array<std::uint8_t, 65536>;

struct MemoryFill
{
    std::uint8_t value = 0xeau;
};

struct MemoryRandom
{
    std::uint32_t seed = 0u;
};

struct MemoryCallback
{
    std::function<void(memory_image& memory)> apply;
};

using MemoryInit = std::variant<MemoryFill, MemoryRandom, MemoryCallback>;

struct CompareOptions
{
    bool address = true;
    bool data = true;
    bool read_write = true;
    bool opcode_fetch = true;
    bool registers_on_fetch = false;
};

struct LockstepConfig
{
    MemoryInit memory = MemoryFill{};
    CompareOptions compare{};
};

struct CpuTraceEntry
{
    std::size_t cycle = 0u;
    std::uint16_t address = 0u;
    std::uint8_t data = 0u;
    bool write = false;
    bool opcode_fetch = false;
    bool irq_asserted = false;
    bool nmi_asserted = false;
    std::uint16_t pc = 0u;
    std::uint8_t a = 0u;
    std::uint8_t x = 0u;
    std::uint8_t y = 0u;
    std::uint8_t s = 0u;
    std::uint8_t p = 0u;
};

struct LockstepRunResult
{
    std::vector<CpuTraceEntry> left_trace{};
    std::vector<CpuTraceEntry> right_trace{};
    std::optional<std::size_t> first_mismatch{};
};

class LockstepRunner
{
public:
    LockstepRunner(std::unique_ptr<cpu6502_bridge::ICpu> left,
                   std::unique_ptr<cpu6502_bridge::ICpu> right);

    LockstepRunner(const LockstepRunner&) = delete;
    LockstepRunner& operator=(const LockstepRunner&) = delete;

    cpu6502_bridge::ICpu& left() noexcept;
    const cpu6502_bridge::ICpu& left() const noexcept;
    cpu6502_bridge::ICpu& right() noexcept;
    const cpu6502_bridge::ICpu& right() const noexcept;

    bool setup_and_run(const LockstepConfig& config = {});
    bool setup_and_run(const testcase& test,
                       const LockstepConfig& config = {});

    LockstepRunResult step();
    LockstepRunResult step_cycles(std::size_t count);
    LockstepRunResult step_to_fetch(std::size_t max_steps);
    LockstepRunResult step_to_fetch_at(std::uint16_t address,
                                       std::size_t max_steps);

    void irq(bool asserted) noexcept;
    void nmi(bool asserted) noexcept;

private:
    std::unique_ptr<cpu6502_bridge::ICpu> left_;
    std::unique_ptr<cpu6502_bridge::ICpu> right_;
    CompareOptions compare_{};
    std::size_t cycle_ = 0u;
};

} // namespace tools6502
