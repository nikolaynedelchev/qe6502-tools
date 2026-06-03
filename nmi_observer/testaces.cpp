#include "nmi_observer.h"

#include <initializer_list>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace nmi_observer{

std::map<std::uint8_t, std::vector<testcase>> get_example_testcases()
{
    using namespace asm6502;
    return{
        // 0x00
        {
            0x00,
            {
                testcase{
                    .opcode = 0x00,
                    .bytes = 1,
                    .expected_cycles = 7,
                    .start_at = 0x0400,
                    .nmi_vector = 0x0800,
                    .A = 2, .X = 12, .Y=8, .P = 12, .S = 0xfe,
                    .mem_setup = Asm6502::New()
                    .begin()
                        .brk_irq_vector("trap")
                        .org(0x0400)
                            .brk()
                        .org(0x0800, "trap")
                            .jmp("trap")
                    .end().compile(),
                    .description = "BRK example at $0400, vectoring to the trap at $0800",
                },
                testcase{
                    .opcode = 0x00, .bytes = 1, .expected_cycles = 7,
                    .start_at = 0x0600, .nmi_vector = 0x0800,
                    .A = 8, .X = 11, .Y=12, .P = 12, .S = 0xfa,
                    .mem_setup = Asm6502::New()
                    .begin()
                        .brk_irq_vector("trap")
                        .org(0x0600)
                            .brk()
                        .org(0x0800, "trap")
                            .jmp("trap")
                    .end().compile(),
                    .description = "BRK example at $0600, vectoring to the trap at $0800",
                }
            }
        }
    };
}

/*
 * Return NMOS 6502 opcode timing/scenario testcases for every byte value 0x00-0xFF,
 * including the undocumented/illegal opcodes implemented by the qe6502 NMOS control
 * store.  Each map key is an opcode and each value contains one or more scenarios for
 * that opcode.
 *
 * The scenarios are meant to drive a cycle/timing observer, not to be a full arithmetic
 * truth table for every possible input value.  For fixed-cycle opcodes there is one
 * representative straight-line case.  For opcodes whose execution length can vary, all
 * timing-distinct paths are represented: conditional branches have not-taken, taken on
 * the same page, and taken with page crossing; indexed read/NOP modes that take an
 * extra page-cross cycle have both no-cross and cross cases.  Control-flow opcodes place
 * the final trap at the address reached by the opcode (BRK vector, JMP/JSR target,
 * RTS/RTI return address).  The NMOS JMP ($xxFF) indirect high-byte wrap is included as
 * an explicit addressing corner case.
 *
 * Every generated mini-program contains exactly two executable instructions: the opcode
 * under test, emitted at testcase::start_at with the requested operands, and a terminal
 * self-jump trap (`JMP trap`) at the address where execution is expected to continue
 * after that opcode.  Any other bytes in testcase::mem_setup are data only: vectors,
 * pointer tables, stack return bytes, or memory operands needed by the instruction.
 */

} // namespace nmi_observer
