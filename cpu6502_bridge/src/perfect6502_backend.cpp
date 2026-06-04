#include <cpu6502_bridge/cpu.hpp>

#include <cstdint>
#include <cstring>
#include <memory>

extern "C" {
#include <types.h>
#include <perfect6502.h>
#include <netlist_sim.h>
}

namespace cpu6502_bridge {
namespace {

constexpr nodenum_t perfect_clk0 = 1171;
constexpr nodenum_t perfect_irq = 103;
constexpr nodenum_t perfect_nmi = 1297;
constexpr nodenum_t perfect_sync = 539;

bool next_step_is_memory_half(state_t* state) noexcept
{
    return isNodeHigh(state, perfect_clk0) == 0;
}

class Perfect6502Cpu final : public ICpu {
public:
    Perfect6502Cpu() noexcept
    {
        std::memset(::memory, 0, 65536u);
        restart();
    }

    ~Perfect6502Cpu() override
    {
        if (state_ != nullptr) {
            destroyChip(state_);
        }
    }

    Perfect6502Cpu(const Perfect6502Cpu&) = delete;
    Perfect6502Cpu& operator=(const Perfect6502Cpu&) = delete;

    void restart() noexcept override
    {
        if (state_ != nullptr) {
            destroyChip(state_);
            state_ = nullptr;
        }

        state_ = initAndResetChip();
        irq_asserted_ = false;
        nmi_asserted_ = false;
        normalize_to_memory_half();
    }


    void irq(bool assert_irq) noexcept override
    {
        irq_asserted_ = assert_irq;
        if (state_ != nullptr) {
            setNode(state_, perfect_irq, assert_irq ? 0 : 1);
            recalcNodeList(state_);
        }
    }

    bool is_irq_asserted() const noexcept override
    {
        return irq_asserted_;
    }

    void nmi(bool assert_nmi) noexcept override
    {
        nmi_asserted_ = assert_nmi;
        if (state_ != nullptr) {
            setNode(state_, perfect_nmi, assert_nmi ? 0 : 1);
            recalcNodeList(state_);
        }
    }

    bool is_nmi_asserted() const noexcept override
    {
        return nmi_asserted_;
    }

    void set_bus_data(std::uint8_t data) noexcept override
    {
        if (!is_write()) {
            ::memory[bus_address()] = data;
        }
    }

    void step() noexcept override
    {
        if (state_ == nullptr) {
            return;
        }

        normalize_to_memory_half();

        step_one_half(); /* memory/bus half-step */
        step_one_half(); /* CPU half-step; leaves the next request visible */
        normalize_to_memory_half();
    }

    std::uint16_t bus_address() const noexcept override
    {
        return state_ != nullptr ? readAddressBus(state_) : 0;
    }

    std::uint8_t bus_data() const noexcept override
    {
        if (state_ == nullptr) {
            return 0;
        }

        return is_write() ? readDataBus(state_) : ::memory[bus_address()];
    }

    std::uint8_t* memory() noexcept override
    {
        return ::memory;
    }

    bool is_write() const noexcept override
    {
        return state_ != nullptr && readRW(state_) == 0u;
    }

    bool is_opcode_fetch() const noexcept override
    {
        return state_ != nullptr
            && next_step_is_memory_half(state_)
            && !is_write()
            && isNodeHigh(state_, perfect_sync) != 0;
    }

    std::uint16_t pc() const noexcept override { return state_ != nullptr ? readPC(state_) : 0; }
    std::uint8_t s() const noexcept override { return state_ != nullptr ? readSP(state_) : 0; }
    std::uint8_t a() const noexcept override { return state_ != nullptr ? readA(state_) : 0; }
    std::uint8_t x() const noexcept override { return state_ != nullptr ? readX(state_) : 0; }
    std::uint8_t y() const noexcept override { return state_ != nullptr ? readY(state_) : 0; }
    std::uint8_t p() const noexcept override { return state_ != nullptr ? readP(state_) : 0; }

private:
    void step_one_half() noexcept
    {
        ::step(state_);
    }

    void normalize_to_memory_half() noexcept
    {
        if (state_ != nullptr && !next_step_is_memory_half(state_)) {
            step_one_half();
        }
    }

    state_t* state_ = nullptr;
    bool irq_asserted_ = false;
    bool nmi_asserted_ = false;
};

} // namespace

std::unique_ptr<ICpu> make_perfect6502_cpu()
{
    return std::make_unique<Perfect6502Cpu>();
}

} // namespace cpu6502_bridge
