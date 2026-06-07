#include <tools6502/testcase_collections.hpp>

#include <asm6502/asm6502.h>

#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace tools6502 {


namespace {

testcase make_testcase(std::uint8_t opcode,
                       std::uint8_t bytes,
                       std::uint16_t start_at,
                       std::uint16_t brk_irq_vector,
                       std::uint16_t nmi_vector,
                       std::uint8_t A,
                       std::uint8_t X,
                       std::uint8_t Y,
                       std::uint8_t P,
                       std::uint8_t S,
                       memory_setup program,
                       std::string description)
{
    testcase test{};
    test.opcode = opcode;
    test.bytes = bytes;
    test.start_at = start_at;
    test.vectors.reset = 0x0200u;
    test.vectors.brk_irq = brk_irq_vector;
    test.vectors.nmi = nmi_vector;
    test.initial_state.A = A;
    test.initial_state.X = X;
    test.initial_state.Y = Y;
    test.initial_state.P = P;
    test.initial_state.S = S;
    test.program = std::move(program);
    test.description = std::move(description);
    return test;
}

} // namespace

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
 * extra page-cross cycle have both no-cross and cross cases; fixed-cycle indexed
 * store/read-modify-write modes also include no-cross and cross addressing cases
 * where the bus/addressing path differs even when the cycle count does not.  Control-flow opcodes place
 * the final trap at the address reached by the opcode (BRK vector, JMP/JSR target,
 * RTS/RTI return address).  The NMOS JMP ($xxFF) indirect high-byte wrap is included as
 * an explicit addressing corner case.
 *
 * Every generated mini-program contains the opcode under test, emitted at
 * testcase::start_at with the requested operands, and a terminal self-jump trap
 * (`JMP trap`) at the address where execution is expected to continue after that
 * opcode. Some not-taken branch cases also define a non-fallthrough branch target
 * label so the encoded relative operand is meaningful even though control falls
 * through to the trap.  Any other bytes in testcase::program are data only: vectors,
 * pointer tables, stack return bytes, or memory operands needed by the instruction.
 */
std::map<std::uint8_t, std::vector<testcase>> get_nmos6502_opcode_testcases()
{
    using namespace asm6502;
    return{
        // 0x00
        {
            0x00,
            {
                make_testcase(0x00, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .brk_irq_vector("trap")
                        .org(0x0400)
                            .brk()
                        .org(0x0800, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BRK vectors through BRK/IRQ vector"),
            }
        },
        // 0x01
        {
            0x01,
            {
                make_testcase(0x01, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .ora(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x02
        {
            0x02,
            {
                make_testcase(0x02, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x02)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x03
        {
            0x03,
            {
                make_testcase(0x03, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .slo(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x04
        {
            0x04,
            {
                make_testcase(0x04, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x04, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp zero page"),
            }
        },
        // 0x05
        {
            0x05,
            {
                make_testcase(0x05, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .ora(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA zp zero page"),
            }
        },
        // 0x06
        {
            0x06,
            {
                make_testcase(0x06, 2, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x41)
                        .org(0x0400)
                            .asl(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ASL zp zero page"),
            }
        },
        // 0x07
        {
            0x07,
            {
                make_testcase(0x07, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x41)
                        .org(0x0400)
                            .slo(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO zp zero page"),
            }
        },
        // 0x08
        {
            0x08,
            {
                make_testcase(0x08, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0xe5, 0xfe,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .php()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "PHP implied"),
            }
        },
        // 0x09
        {
            0x09,
            {
                make_testcase(0x09, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .ora(0x18)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA #imm immediate"),
            }
        },
        // 0x0A
        {
            0x0A,
            {
                make_testcase(0x0A, 1, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .asl()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ASL A accumulator"),
            }
        },
        // 0x0B
        {
            0x0B,
            {
                make_testcase(0x0B, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .anc(0x7f)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ANC #imm immediate"),
            }
        },
        // 0x0C
        {
            0x0C,
            {
                make_testcase(0x0C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0x0C, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                    .end().compile(),
                    "NOP abs absolute"),
            }
        },
        // 0x0D
        {
            0x0D,
            {
                make_testcase(0x0D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .ora(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                    .end().compile(),
                    "ORA abs absolute"),
            }
        },
        // 0x0E
        {
            0x0E,
            {
                make_testcase(0x0E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .asl(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x41)
                    .end().compile(),
                    "ASL abs absolute"),
            }
        },
        // 0x0F
        {
            0x0F,
            {
                make_testcase(0x0F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .slo(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x41)
                    .end().compile(),
                    "SLO abs absolute"),
            }
        },
        // 0x10
        {
            0x10,
            {
                make_testcase(0x10, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0xa4, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bpl("branch_target")
                        .label("trap")
                            .jmp("trap")
                        .org(0x0420, "branch_target")
                    .end().compile(),
                    "BPL rel not taken with a non-fallthrough encoded target"),
                make_testcase(0x10, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bpl("trap")
                        .org(0x0420, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BPL rel taken without page cross to a non-fallthrough target"),
                make_testcase(0x10, 2, 0x04f0, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x04f0)
                            .bpl("trap")
                        .org(0x0505, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BPL rel taken with page cross"),
            }
        },
        // 0x11
        {
            0x11,
            {
                make_testcase(0x11, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .ora(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA (zp),Y indirect indexed without page cross"),
                make_testcase(0x11, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .ora(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x12
        {
            0x12,
            {
                make_testcase(0x12, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x12)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x13
        {
            0x13,
            {
                make_testcase(0x13, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .slo(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO (zp),Y indirect indexed"),
                make_testcase(0x13, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .slo(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x14
        {
            0x14,
            {
                make_testcase(0x14, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x14, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp,X zero page,X with wraparound"),
            }
        },
        // 0x15
        {
            0x15,
            {
                make_testcase(0x15, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .ora(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA zp,X zero page,X with wraparound"),
            }
        },
        // 0x16
        {
            0x16,
            {
                make_testcase(0x16, 2, 0x0400, 0x0800, 0x0800, 0x81, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x41)
                        .org(0x0400)
                            .asl(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ASL zp,X zero page,X with wraparound"),
            }
        },
        // 0x17
        {
            0x17,
            {
                make_testcase(0x17, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x41)
                        .org(0x0400)
                            .slo(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO zp,X zero page,X with wraparound"),
            }
        },
        // 0x18
        {
            0x18,
            {
                make_testcase(0x18, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .clc()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CLC implied"),
            }
        },
        // 0x19
        {
            0x19,
            {
                make_testcase(0x19, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .ora(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA abs,Y absolute,Y without page cross"),
                make_testcase(0x19, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .ora(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA abs,Y absolute,Y with page cross"),
            }
        },
        // 0x1A
        {
            0x1A,
            {
                make_testcase(0x1A, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0x1A)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP implied"),
            }
        },
        // 0x1B
        {
            0x1B,
            {
                make_testcase(0x1B, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .slo(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO abs,Y absolute,Y"),
                make_testcase(0x1B, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .slo(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO abs,Y absolute,Y with page cross"),
            }
        },
        // 0x1C
        {
            0x1C,
            {
                make_testcase(0x1C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x1C, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X without page cross"),
                make_testcase(0x1C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x1C, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X with page cross"),
            }
        },
        // 0x1D
        {
            0x1D,
            {
                make_testcase(0x1D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .ora(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA abs,X absolute,X without page cross"),
                make_testcase(0x1D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .ora(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ORA abs,X absolute,X with page cross"),
            }
        },
        // 0x1E
        {
            0x1E,
            {
                make_testcase(0x1E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .asl(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ASL abs,X absolute,X"),
                make_testcase(0x1E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .asl(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ASL abs,X absolute,X with page cross"),
            }
        },
        // 0x1F
        {
            0x1F,
            {
                make_testcase(0x1F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .slo(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO abs,X absolute,X"),
                make_testcase(0x1F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .slo(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SLO abs,X absolute,X with page cross"),
            }
        },
        // 0x20
        {
            0x20,
            {
                make_testcase(0x20, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .jsr("trap")
                        .org(0x0800, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "JSR abs jumps to subroutine target"),
            }
        },
        // 0x21
        {
            0x21,
            {
                make_testcase(0x21, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .and_(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x22
        {
            0x22,
            {
                make_testcase(0x22, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x22)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x23
        {
            0x23,
            {
                make_testcase(0x23, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rla(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x24
        {
            0x24,
            {
                make_testcase(0x24, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0xc0)
                        .org(0x0400)
                            .bit(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "BIT zp zero page"),
            }
        },
        // 0x25
        {
            0x25,
            {
                make_testcase(0x25, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .and_(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND zp zero page"),
            }
        },
        // 0x26
        {
            0x26,
            {
                make_testcase(0x26, 2, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x81)
                        .org(0x0400)
                            .rol(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROL zp zero page"),
            }
        },
        // 0x27
        {
            0x27,
            {
                make_testcase(0x27, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x81)
                        .org(0x0400)
                            .rla(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA zp zero page"),
            }
        },
        // 0x28
        {
            0x28,
            {
                make_testcase(0x28, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x01fe, "stack_value")
                            .db(0xa5)
                        .org(0x0400)
                            .plp()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "PLP pulls value from stack"),
            }
        },
        // 0x29
        {
            0x29,
            {
                make_testcase(0x29, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .and_(0x3c)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND #imm immediate"),
            }
        },
        // 0x2A
        {
            0x2A,
            {
                make_testcase(0x2A, 1, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .rol()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROL A accumulator"),
            }
        },
        // 0x2B
        {
            0x2B,
            {
                make_testcase(0x2B, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .anc_opcode(0x2B, 0x7f)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ANC #imm immediate"),
            }
        },
        // 0x2C
        {
            0x2C,
            {
                make_testcase(0x2C, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bit(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0xc0)
                    .end().compile(),
                    "BIT abs absolute"),
            }
        },
        // 0x2D
        {
            0x2D,
            {
                make_testcase(0x2D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .and_(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                    .end().compile(),
                    "AND abs absolute"),
            }
        },
        // 0x2E
        {
            0x2E,
            {
                make_testcase(0x2E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .rol(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x81)
                    .end().compile(),
                    "ROL abs absolute"),
            }
        },
        // 0x2F
        {
            0x2F,
            {
                make_testcase(0x2F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .rla(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x81)
                    .end().compile(),
                    "RLA abs absolute"),
            }
        },
        // 0x30
        {
            0x30,
            {
                make_testcase(0x30, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bmi("branch_target")
                        .label("trap")
                            .jmp("trap")
                        .org(0x0420, "branch_target")
                    .end().compile(),
                    "BMI rel not taken with a non-fallthrough encoded target"),
                make_testcase(0x30, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0xa4, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bmi("trap")
                        .org(0x0420, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BMI rel taken without page cross to a non-fallthrough target"),
                make_testcase(0x30, 2, 0x04f0, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0xa4, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x04f0)
                            .bmi("trap")
                        .org(0x0505, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BMI rel taken with page cross"),
            }
        },
        // 0x31
        {
            0x31,
            {
                make_testcase(0x31, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .and_(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND (zp),Y indirect indexed without page cross"),
                make_testcase(0x31, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .and_(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x32
        {
            0x32,
            {
                make_testcase(0x32, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x32)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x33
        {
            0x33,
            {
                make_testcase(0x33, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rla(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA (zp),Y indirect indexed"),
                make_testcase(0x33, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .rla(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x34
        {
            0x34,
            {
                make_testcase(0x34, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x34, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp,X zero page,X with wraparound"),
            }
        },
        // 0x35
        {
            0x35,
            {
                make_testcase(0x35, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .and_(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND zp,X zero page,X with wraparound"),
            }
        },
        // 0x36
        {
            0x36,
            {
                make_testcase(0x36, 2, 0x0400, 0x0800, 0x0800, 0x81, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x81)
                        .org(0x0400)
                            .rol(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROL zp,X zero page,X with wraparound"),
            }
        },
        // 0x37
        {
            0x37,
            {
                make_testcase(0x37, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x81)
                        .org(0x0400)
                            .rla(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA zp,X zero page,X with wraparound"),
            }
        },
        // 0x38
        {
            0x38,
            {
                make_testcase(0x38, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sec()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SEC implied"),
            }
        },
        // 0x39
        {
            0x39,
            {
                make_testcase(0x39, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .and_(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND abs,Y absolute,Y without page cross"),
                make_testcase(0x39, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .and_(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND abs,Y absolute,Y with page cross"),
            }
        },
        // 0x3A
        {
            0x3A,
            {
                make_testcase(0x3A, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0x3A)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP implied"),
            }
        },
        // 0x3B
        {
            0x3B,
            {
                make_testcase(0x3B, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rla(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA abs,Y absolute,Y"),
                make_testcase(0x3B, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .rla(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA abs,Y absolute,Y with page cross"),
            }
        },
        // 0x3C
        {
            0x3C,
            {
                make_testcase(0x3C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x3C, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X without page cross"),
                make_testcase(0x3C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x3C, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X with page cross"),
            }
        },
        // 0x3D
        {
            0x3D,
            {
                make_testcase(0x3D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .and_(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND abs,X absolute,X without page cross"),
                make_testcase(0x3D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .and_(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AND abs,X absolute,X with page cross"),
            }
        },
        // 0x3E
        {
            0x3E,
            {
                make_testcase(0x3E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rol(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROL abs,X absolute,X"),
                make_testcase(0x3E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .rol(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROL abs,X absolute,X with page cross"),
            }
        },
        // 0x3F
        {
            0x3F,
            {
                make_testcase(0x3F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rla(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA abs,X absolute,X"),
                make_testcase(0x3F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x41)
                        .org(0x0400)
                            .rla(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RLA abs,X absolute,X with page cross"),
            }
        },
        // 0x40
        {
            0x40,
            {
                make_testcase(0x40, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfc,
                    Asm6502::New()
                    .begin()
                        .org(0x01fd, "rti_stack_frame")
                            .db(0x24)
                            .dw("trap")
                        .org(0x0400)
                            .rti()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RTI pulls P and PC from stack"),
            }
        },
        // 0x41
        {
            0x41,
            {
                make_testcase(0x41, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .eor(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x42
        {
            0x42,
            {
                make_testcase(0x42, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x42)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x43
        {
            0x43,
            {
                make_testcase(0x43, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .sre(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x44
        {
            0x44,
            {
                make_testcase(0x44, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x44, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp zero page"),
            }
        },
        // 0x45
        {
            0x45,
            {
                make_testcase(0x45, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .eor(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR zp zero page"),
            }
        },
        // 0x46
        {
            0x46,
            {
                make_testcase(0x46, 2, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x82)
                        .org(0x0400)
                            .lsr(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LSR zp zero page"),
            }
        },
        // 0x47
        {
            0x47,
            {
                make_testcase(0x47, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x82)
                        .org(0x0400)
                            .sre(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE zp zero page"),
            }
        },
        // 0x48
        {
            0x48,
            {
                make_testcase(0x48, 1, 0x0400, 0x0800, 0x0800, 0x8e, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .pha()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "PHA implied"),
            }
        },
        // 0x49
        {
            0x49,
            {
                make_testcase(0x49, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .eor(0xff)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR #imm immediate"),
            }
        },
        // 0x4A
        {
            0x4A,
            {
                make_testcase(0x4A, 1, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .lsr()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LSR A accumulator"),
            }
        },
        // 0x4B
        {
            0x4B,
            {
                make_testcase(0x4B, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .alr(0xf0)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ALR #imm immediate"),
            }
        },
        // 0x4C
        {
            0x4C,
            {
                make_testcase(0x4C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .jmp("trap")
                        .org(0x0800, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "JMP abs absolute jump"),
            }
        },
        // 0x4D
        {
            0x4D,
            {
                make_testcase(0x4D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .eor(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                    .end().compile(),
                    "EOR abs absolute"),
            }
        },
        // 0x4E
        {
            0x4E,
            {
                make_testcase(0x4E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .lsr(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x82)
                    .end().compile(),
                    "LSR abs absolute"),
            }
        },
        // 0x4F
        {
            0x4F,
            {
                make_testcase(0x4F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sre(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x82)
                    .end().compile(),
                    "SRE abs absolute"),
            }
        },
        // 0x50
        {
            0x50,
            {
                make_testcase(0x50, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x64, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bvc("branch_target")
                        .label("trap")
                            .jmp("trap")
                        .org(0x0420, "branch_target")
                    .end().compile(),
                    "BVC rel not taken with a non-fallthrough encoded target"),
                make_testcase(0x50, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bvc("trap")
                        .org(0x0420, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BVC rel taken without page cross to a non-fallthrough target"),
                make_testcase(0x50, 2, 0x04f0, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x04f0)
                            .bvc("trap")
                        .org(0x0505, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BVC rel taken with page cross"),
            }
        },
        // 0x51
        {
            0x51,
            {
                make_testcase(0x51, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .eor(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR (zp),Y indirect indexed without page cross"),
                make_testcase(0x51, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .eor(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x52
        {
            0x52,
            {
                make_testcase(0x52, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x52)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x53
        {
            0x53,
            {
                make_testcase(0x53, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .sre(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE (zp),Y indirect indexed"),
                make_testcase(0x53, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .sre(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x54
        {
            0x54,
            {
                make_testcase(0x54, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x54, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp,X zero page,X with wraparound"),
            }
        },
        // 0x55
        {
            0x55,
            {
                make_testcase(0x55, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .eor(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR zp,X zero page,X with wraparound"),
            }
        },
        // 0x56
        {
            0x56,
            {
                make_testcase(0x56, 2, 0x0400, 0x0800, 0x0800, 0x81, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x82)
                        .org(0x0400)
                            .lsr(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LSR zp,X zero page,X with wraparound"),
            }
        },
        // 0x57
        {
            0x57,
            {
                make_testcase(0x57, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x82)
                        .org(0x0400)
                            .sre(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE zp,X zero page,X with wraparound"),
            }
        },
        // 0x58
        {
            0x58,
            {
                make_testcase(0x58, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .cli()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CLI implied"),
            }
        },
        // 0x59
        {
            0x59,
            {
                make_testcase(0x59, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .eor(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR abs,Y absolute,Y without page cross"),
                make_testcase(0x59, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .eor(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR abs,Y absolute,Y with page cross"),
            }
        },
        // 0x5A
        {
            0x5A,
            {
                make_testcase(0x5A, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0x5A)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP implied"),
            }
        },
        // 0x5B
        {
            0x5B,
            {
                make_testcase(0x5B, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .sre(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE abs,Y absolute,Y"),
                make_testcase(0x5B, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .sre(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE abs,Y absolute,Y with page cross"),
            }
        },
        // 0x5C
        {
            0x5C,
            {
                make_testcase(0x5C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x5C, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X without page cross"),
                make_testcase(0x5C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x5C, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X with page cross"),
            }
        },
        // 0x5D
        {
            0x5D,
            {
                make_testcase(0x5D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .eor(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR abs,X absolute,X without page cross"),
                make_testcase(0x5D, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .eor(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "EOR abs,X absolute,X with page cross"),
            }
        },
        // 0x5E
        {
            0x5E,
            {
                make_testcase(0x5E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .lsr(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LSR abs,X absolute,X"),
                make_testcase(0x5E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .lsr(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LSR abs,X absolute,X with page cross"),
            }
        },
        // 0x5F
        {
            0x5F,
            {
                make_testcase(0x5F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .sre(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE abs,X absolute,X"),
                make_testcase(0x5F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x82)
                        .org(0x0400)
                            .sre(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SRE abs,X absolute,X with page cross"),
            }
        },
        // 0x60
        {
            0x60,
            {
                make_testcase(0x60, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x01fe, "rts_return_address")
                            .dw(sym("trap", -1))
                        .org(0x0400)
                            .rts()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RTS pulls return address from stack"),
            }
        },
        // 0x61
        {
            0x61,
            {
                make_testcase(0x61, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .adc(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x62
        {
            0x62,
            {
                make_testcase(0x62, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x62)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x63
        {
            0x63,
            {
                make_testcase(0x63, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rra(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x64
        {
            0x64,
            {
                make_testcase(0x64, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x64, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp zero page"),
            }
        },
        // 0x65
        {
            0x65,
            {
                make_testcase(0x65, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x15)
                        .org(0x0400)
                            .adc(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC zp zero page"),
            }
        },
        // 0x66
        {
            0x66,
            {
                make_testcase(0x66, 2, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x81)
                        .org(0x0400)
                            .ror(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROR zp zero page"),
            }
        },
        // 0x67
        {
            0x67,
            {
                make_testcase(0x67, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x81)
                        .org(0x0400)
                            .rra(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA zp zero page"),
            }
        },
        // 0x68
        {
            0x68,
            {
                make_testcase(0x68, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x01fe, "stack_value")
                            .db(0x5a)
                        .org(0x0400)
                            .pla()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "PLA pulls value from stack"),
            }
        },
        // 0x69
        {
            0x69,
            {
                make_testcase(0x69, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .adc(0x15)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC #imm immediate"),
                make_testcase(0x69, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x2d, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .adc(0x38)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC #imm decimal mode immediate"),
            }
        },
        // 0x6A
        {
            0x6A,
            {
                make_testcase(0x6A, 1, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .ror()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROR A accumulator"),
            }
        },
        // 0x6B
        {
            0x6B,
            {
                make_testcase(0x6B, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .arr(0x6e)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ARR #imm immediate"),
            }
        },
        // 0x6C
        {
            0x6C,
            {
                make_testcase(0x6C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0200, "jump_ptr")
                            .dw("trap")
                        .org(0x0400)
                            .jmp(ind, "jump_ptr")
                        .org(0x0800, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "JMP (abs) indirect jump"),
                make_testcase(0x6C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0200, "jump_ptr_high_byte")
                            .db(0x08)
                        .org(0x02ff, "jump_ptr")
                            .db(0x34)
                        .org(0x0400)
                            .jmp(ind, "jump_ptr")
                        .org(0x0834, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "JMP (abs) NMOS indirect pointer high-byte wraps at $xxFF"),
            }
        },
        // 0x6D
        {
            0x6D,
            {
                make_testcase(0x6D, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .adc(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x15)
                    .end().compile(),
                    "ADC abs absolute"),
            }
        },
        // 0x6E
        {
            0x6E,
            {
                make_testcase(0x6E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .ror(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x81)
                    .end().compile(),
                    "ROR abs absolute"),
            }
        },
        // 0x6F
        {
            0x6F,
            {
                make_testcase(0x6F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .rra(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x81)
                    .end().compile(),
                    "RRA abs absolute"),
            }
        },
        // 0x70
        {
            0x70,
            {
                make_testcase(0x70, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bvs("branch_target")
                        .label("trap")
                            .jmp("trap")
                        .org(0x0420, "branch_target")
                    .end().compile(),
                    "BVS rel not taken with a non-fallthrough encoded target"),
                make_testcase(0x70, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x64, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bvs("trap")
                        .org(0x0420, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BVS rel taken without page cross to a non-fallthrough target"),
                make_testcase(0x70, 2, 0x04f0, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x64, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x04f0)
                            .bvs("trap")
                        .org(0x0505, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BVS rel taken with page cross"),
            }
        },
        // 0x71
        {
            0x71,
            {
                make_testcase(0x71, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .adc(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC (zp),Y indirect indexed without page cross"),
                make_testcase(0x71, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x20, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .adc(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x72
        {
            0x72,
            {
                make_testcase(0x72, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x72)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x73
        {
            0x73,
            {
                make_testcase(0x73, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rra(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA (zp),Y indirect indexed"),
                make_testcase(0x73, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .rra(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x74
        {
            0x74,
            {
                make_testcase(0x74, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x74, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp,X zero page,X with wraparound"),
            }
        },
        // 0x75
        {
            0x75,
            {
                make_testcase(0x75, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x15)
                        .org(0x0400)
                            .adc(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC zp,X zero page,X with wraparound"),
            }
        },
        // 0x76
        {
            0x76,
            {
                make_testcase(0x76, 2, 0x0400, 0x0800, 0x0800, 0x81, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x81)
                        .org(0x0400)
                            .ror(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROR zp,X zero page,X with wraparound"),
            }
        },
        // 0x77
        {
            0x77,
            {
                make_testcase(0x77, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x81)
                        .org(0x0400)
                            .rra(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA zp,X zero page,X with wraparound"),
            }
        },
        // 0x78
        {
            0x78,
            {
                make_testcase(0x78, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sei()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SEI implied"),
            }
        },
        // 0x79
        {
            0x79,
            {
                make_testcase(0x79, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .adc(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC abs,Y absolute,Y without page cross"),
                make_testcase(0x79, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x20, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .adc(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC abs,Y absolute,Y with page cross"),
            }
        },
        // 0x7A
        {
            0x7A,
            {
                make_testcase(0x7A, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0x7A)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP implied"),
            }
        },
        // 0x7B
        {
            0x7B,
            {
                make_testcase(0x7B, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rra(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA abs,Y absolute,Y"),
                make_testcase(0x7B, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .rra(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA abs,Y absolute,Y with page cross"),
            }
        },
        // 0x7C
        {
            0x7C,
            {
                make_testcase(0x7C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x7C, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X without page cross"),
                make_testcase(0x7C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0x7C, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X with page cross"),
            }
        },
        // 0x7D
        {
            0x7D,
            {
                make_testcase(0x7D, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .adc(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC abs,X absolute,X without page cross"),
                make_testcase(0x7D, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x20, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .adc(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ADC abs,X absolute,X with page cross"),
            }
        },
        // 0x7E
        {
            0x7E,
            {
                make_testcase(0x7E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .ror(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROR abs,X absolute,X"),
                make_testcase(0x7E, 3, 0x0400, 0x0800, 0x0800, 0x81, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .ror(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ROR abs,X absolute,X with page cross"),
            }
        },
        // 0x7F
        {
            0x7F,
            {
                make_testcase(0x7F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x81)
                        .org(0x0400)
                            .rra(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA abs,X absolute,X"),
                make_testcase(0x7F, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .rra(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "RRA abs,X absolute,X with page cross"),
            }
        },
        // 0x80
        {
            0x80,
            {
                make_testcase(0x80, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0x80, 0x7f)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP #imm immediate"),
            }
        },
        // 0x81
        {
            0x81,
            {
                make_testcase(0x81, 2, 0x0400, 0x0800, 0x0800, 0x5a, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("store_addr")
                        .org(0x2134, "store_addr")
                        .org(0x0400)
                            .sta(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x82
        {
            0x82,
            {
                make_testcase(0x82, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0x82, 0x7f)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP #imm immediate"),
            }
        },
        // 0x83
        {
            0x83,
            {
                make_testcase(0x83, 2, 0x0400, 0x0800, 0x0800, 0xf3, 0x10, 0x00, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("store_addr")
                        .org(0x2134, "store_addr")
                        .org(0x0400)
                            .sax(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SAX (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0x84
        {
            0x84,
            {
                make_testcase(0x84, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x7e, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "store_zp")
                        .org(0x0400)
                            .sty(zp, "store_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STY zp zero page"),
            }
        },
        // 0x85
        {
            0x85,
            {
                make_testcase(0x85, 2, 0x0400, 0x0800, 0x0800, 0x5a, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "store_zp")
                        .org(0x0400)
                            .sta(zp, "store_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA zp zero page"),
            }
        },
        // 0x86
        {
            0x86,
            {
                make_testcase(0x86, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x3c, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "store_zp")
                        .org(0x0400)
                            .stx(zp, "store_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STX zp zero page"),
            }
        },
        // 0x87
        {
            0x87,
            {
                make_testcase(0x87, 2, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x00, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "store_zp")
                        .org(0x0400)
                            .sax(zp, "store_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SAX zp zero page"),
            }
        },
        // 0x88
        {
            0x88,
            {
                make_testcase(0x88, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .dey()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DEY implied"),
            }
        },
        // 0x89
        {
            0x89,
            {
                make_testcase(0x89, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0x89, 0x7f)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP #imm immediate"),
            }
        },
        // 0x8A
        {
            0x8A,
            {
                make_testcase(0x8A, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x7c, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .txa()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "TXA implied"),
            }
        },
        // 0x8B
        {
            0x8B,
            {
                make_testcase(0x8B, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .xaa(0xee)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "XAA #imm immediate"),
            }
        },
        // 0x8C
        {
            0x8C,
            {
                make_testcase(0x8C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x7e, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sty(absolute, "store_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "store_addr")
                    .end().compile(),
                    "STY abs absolute"),
            }
        },
        // 0x8D
        {
            0x8D,
            {
                make_testcase(0x8D, 3, 0x0400, 0x0800, 0x0800, 0x5a, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sta(absolute, "store_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "store_addr")
                    .end().compile(),
                    "STA abs absolute"),
            }
        },
        // 0x8E
        {
            0x8E,
            {
                make_testcase(0x8E, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x3c, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .stx(absolute, "store_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "store_addr")
                    .end().compile(),
                    "STX abs absolute"),
            }
        },
        // 0x8F
        {
            0x8F,
            {
                make_testcase(0x8F, 3, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x00, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sax(absolute, "store_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "store_addr")
                    .end().compile(),
                    "SAX abs absolute"),
            }
        },
        // 0x90
        {
            0x90,
            {
                make_testcase(0x90, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bcc("branch_target")
                        .label("trap")
                            .jmp("trap")
                        .org(0x0420, "branch_target")
                    .end().compile(),
                    "BCC rel not taken with a non-fallthrough encoded target"),
                make_testcase(0x90, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bcc("trap")
                        .org(0x0420, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BCC rel taken without page cross to a non-fallthrough target"),
                make_testcase(0x90, 2, 0x04f0, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x04f0)
                            .bcc("trap")
                        .org(0x0505, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BCC rel taken with page cross"),
            }
        },
        // 0x91
        {
            0x91,
            {
                make_testcase(0x91, 2, 0x0400, 0x0800, 0x0800, 0x5a, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "store_addr")
                        .org(0x0400)
                            .sta(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA (zp),Y indirect indexed"),
                make_testcase(0x91, 2, 0x0400, 0x0800, 0x0800, 0x5a, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "store_addr")
                        .org(0x0400)
                            .sta(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA (zp),Y indirect indexed with page cross"),
            }
        },
        // 0x92
        {
            0x92,
            {
                make_testcase(0x92, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0x92)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0x93
        {
            0x93,
            {
                make_testcase(0x93, 2, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x10, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "store_addr")
                        .org(0x0400)
                            .ahx(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AHX (zp),Y unstable indirect indexed store with page cross"),
                make_testcase(0x93, 2, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x10, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "store_addr")
                        .org(0x0400)
                            .ahx(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AHX (zp),Y unstable indirect indexed without page cross"),
            }
        },
        // 0x94
        {
            0x94,
            {
                make_testcase(0x94, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x7e, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "store_zp")
                        .org(0x0400)
                            .sty(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STY zp,X zero page,X with wraparound"),
            }
        },
        // 0x95
        {
            0x95,
            {
                make_testcase(0x95, 2, 0x0400, 0x0800, 0x0800, 0x5a, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "store_zp")
                        .org(0x0400)
                            .sta(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA zp,X zero page,X with wraparound"),
            }
        },
        // 0x96
        {
            0x96,
            {
                make_testcase(0x96, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x3c, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "store_zp")
                        .org(0x0400)
                            .stx(zpy, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STX zp,Y zero page,Y with wraparound"),
            }
        },
        // 0x97
        {
            0x97,
            {
                make_testcase(0x97, 2, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x10, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "store_zp")
                        .org(0x0400)
                            .sax(zpy, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SAX zp,Y zero page,Y with wraparound"),
            }
        },
        // 0x98
        {
            0x98,
            {
                make_testcase(0x98, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x6d, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .tya()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "TYA implied"),
            }
        },
        // 0x99
        {
            0x99,
            {
                make_testcase(0x99, 3, 0x0400, 0x0800, 0x0800, 0x5a, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "store_addr")
                        .org(0x0400)
                            .sta(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA abs,Y absolute,Y"),
                make_testcase(0x99, 3, 0x0400, 0x0800, 0x0800, 0x5a, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "store_addr")
                        .org(0x0400)
                            .sta(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA abs,Y absolute,Y with page cross"),
            }
        },
        // 0x9A
        {
            0x9A,
            {
                make_testcase(0x9A, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x7c, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .txs()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "TXS implied"),
            }
        },
        // 0x9B
        {
            0x9B,
            {
                make_testcase(0x9B, 3, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x10, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "store_addr")
                        .org(0x0400)
                            .tas(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "TAS abs,Y unstable absolute,Y store with page cross"),
                make_testcase(0x9B, 3, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x10, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "store_addr")
                        .org(0x0400)
                            .tas(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "TAS abs,Y unstable absolute,Y store without page cross"),
            }
        },
        // 0x9C
        {
            0x9C,
            {
                make_testcase(0x9C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0xcf, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "store_addr")
                        .org(0x0400)
                            .shy(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SHY abs,X unstable absolute,X store with page cross"),
                make_testcase(0x9C, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0xcf, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "store_addr")
                        .org(0x0400)
                            .shy(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SHY abs,X unstable absolute,X store without page cross"),
            }
        },
        // 0x9D
        {
            0x9D,
            {
                make_testcase(0x9D, 3, 0x0400, 0x0800, 0x0800, 0x5a, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "store_addr")
                        .org(0x0400)
                            .sta(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA abs,X absolute,X"),
                make_testcase(0x9D, 3, 0x0400, 0x0800, 0x0800, 0x5a, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "store_addr")
                        .org(0x0400)
                            .sta(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "STA abs,X absolute,X with page cross"),
            }
        },
        // 0x9E
        {
            0x9E,
            {
                make_testcase(0x9E, 3, 0x0400, 0x0800, 0x0800, 0x42, 0xcf, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "store_addr")
                        .org(0x0400)
                            .shx(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SHX abs,Y unstable absolute,Y store with page cross"),
                make_testcase(0x9E, 3, 0x0400, 0x0800, 0x0800, 0x42, 0xcf, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "store_addr")
                        .org(0x0400)
                            .shx(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SHX abs,Y unstable absolute,Y store without page cross"),
            }
        },
        // 0x9F
        {
            0x9F,
            {
                make_testcase(0x9F, 3, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x10, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "store_addr")
                        .org(0x0400)
                            .ahx(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AHX abs,Y unstable absolute,Y store with page cross"),
                make_testcase(0x9F, 3, 0x0400, 0x0800, 0x0800, 0xf3, 0xcc, 0x10, 0x24, 0x9a,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "store_addr")
                        .org(0x0400)
                            .ahx(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AHX abs,Y unstable absolute,Y store without page cross"),
            }
        },
        // 0xA0
        {
            0xA0,
            {
                make_testcase(0xA0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .ldy(0x6d)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDY #imm immediate"),
            }
        },
        // 0xA1
        {
            0xA1,
            {
                make_testcase(0xA1, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0xA2
        {
            0xA2,
            {
                make_testcase(0xA2, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .ldx(0x7c)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDX #imm immediate"),
            }
        },
        // 0xA3
        {
            0xA3,
            {
                make_testcase(0xA3, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lax(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAX (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0xA4
        {
            0xA4,
            {
                make_testcase(0xA4, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x8e)
                        .org(0x0400)
                            .ldy(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDY zp zero page"),
            }
        },
        // 0xA5
        {
            0xA5,
            {
                make_testcase(0xA5, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA zp zero page"),
            }
        },
        // 0xA6
        {
            0xA6,
            {
                make_testcase(0xA6, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x8e)
                        .org(0x0400)
                            .ldx(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDX zp zero page"),
            }
        },
        // 0xA7
        {
            0xA7,
            {
                make_testcase(0xA7, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x8e)
                        .org(0x0400)
                            .lax(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAX zp zero page"),
            }
        },
        // 0xA8
        {
            0xA8,
            {
                make_testcase(0xA8, 1, 0x0400, 0x0800, 0x0800, 0x8e, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .tay()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "TAY implied"),
            }
        },
        // 0xA9
        {
            0xA9,
            {
                make_testcase(0xA9, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .lda(0x8e)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA #imm immediate"),
            }
        },
        // 0xAA
        {
            0xAA,
            {
                make_testcase(0xAA, 1, 0x0400, 0x0800, 0x0800, 0x8e, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .tax()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "TAX implied"),
            }
        },
        // 0xAB
        {
            0xAB,
            {
                make_testcase(0xAB, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .lxa(0xf3)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LXA #imm immediate"),
            }
        },
        // 0xAC
        {
            0xAC,
            {
                make_testcase(0xAC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .ldy(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x8e)
                    .end().compile(),
                    "LDY abs absolute"),
            }
        },
        // 0xAD
        {
            0xAD,
            {
                make_testcase(0xAD, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .lda(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x8e)
                    .end().compile(),
                    "LDA abs absolute"),
            }
        },
        // 0xAE
        {
            0xAE,
            {
                make_testcase(0xAE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .ldx(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x8e)
                    .end().compile(),
                    "LDX abs absolute"),
            }
        },
        // 0xAF
        {
            0xAF,
            {
                make_testcase(0xAF, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .lax(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x8e)
                    .end().compile(),
                    "LAX abs absolute"),
            }
        },
        // 0xB0
        {
            0xB0,
            {
                make_testcase(0xB0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bcs("branch_target")
                        .label("trap")
                            .jmp("trap")
                        .org(0x0420, "branch_target")
                    .end().compile(),
                    "BCS rel not taken with a non-fallthrough encoded target"),
                make_testcase(0xB0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bcs("trap")
                        .org(0x0420, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BCS rel taken without page cross to a non-fallthrough target"),
                make_testcase(0xB0, 2, 0x04f0, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x04f0)
                            .bcs("trap")
                        .org(0x0505, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BCS rel taken with page cross"),
            }
        },
        // 0xB1
        {
            0xB1,
            {
                make_testcase(0xB1, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA (zp),Y indirect indexed without page cross"),
                make_testcase(0xB1, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA (zp),Y indirect indexed with page cross"),
            }
        },
        // 0xB2
        {
            0xB2,
            {
                make_testcase(0xB2, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0xB2)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0xB3
        {
            0xB3,
            {
                make_testcase(0xB3, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lax(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAX (zp),Y indirect indexed without page cross"),
                make_testcase(0xB3, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lax(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAX (zp),Y indirect indexed with page cross"),
            }
        },
        // 0xB4
        {
            0xB4,
            {
                make_testcase(0xB4, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x8e)
                        .org(0x0400)
                            .ldy(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDY zp,X zero page,X with wraparound"),
            }
        },
        // 0xB5
        {
            0xB5,
            {
                make_testcase(0xB5, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA zp,X zero page,X with wraparound"),
            }
        },
        // 0xB6
        {
            0xB6,
            {
                make_testcase(0xB6, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x8e)
                        .org(0x0400)
                            .ldx(zpy, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDX zp,Y zero page,Y with wraparound"),
            }
        },
        // 0xB7
        {
            0xB7,
            {
                make_testcase(0xB7, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x8e)
                        .org(0x0400)
                            .lax(zpy, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAX zp,Y zero page,Y with wraparound"),
            }
        },
        // 0xB8
        {
            0xB8,
            {
                make_testcase(0xB8, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .clv()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CLV implied"),
            }
        },
        // 0xB9
        {
            0xB9,
            {
                make_testcase(0xB9, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA abs,Y absolute,Y without page cross"),
                make_testcase(0xB9, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA abs,Y absolute,Y with page cross"),
            }
        },
        // 0xBA
        {
            0xBA,
            {
                make_testcase(0xBA, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0x7b,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .tsx()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "TSX implied"),
            }
        },
        // 0xBB
        {
            0xBB,
            {
                make_testcase(0xBB, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x10, 0x24, 0xf3,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .las(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAS abs,Y absolute,Y without page cross"),
                make_testcase(0xBB, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x20, 0x24, 0xf3,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .las(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAS abs,Y absolute,Y with page cross"),
            }
        },
        // 0xBC
        {
            0xBC,
            {
                make_testcase(0xBC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .ldy(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDY abs,X absolute,X without page cross"),
                make_testcase(0xBC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .ldy(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDY abs,X absolute,X with page cross"),
            }
        },
        // 0xBD
        {
            0xBD,
            {
                make_testcase(0xBD, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA abs,X absolute,X without page cross"),
                make_testcase(0xBD, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lda(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDA abs,X absolute,X with page cross"),
            }
        },
        // 0xBE
        {
            0xBE,
            {
                make_testcase(0xBE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .ldx(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDX abs,Y absolute,Y without page cross"),
                make_testcase(0xBE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .ldx(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LDX abs,Y absolute,Y with page cross"),
            }
        },
        // 0xBF
        {
            0xBF,
            {
                make_testcase(0xBF, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lax(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAX abs,Y absolute,Y without page cross"),
                make_testcase(0xBF, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x8e)
                        .org(0x0400)
                            .lax(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "LAX abs,Y absolute,Y with page cross"),
            }
        },
        // 0xC0
        {
            0xC0,
            {
                make_testcase(0xC0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .cpy(0x30)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CPY #imm immediate"),
            }
        },
        // 0xC1
        {
            0xC1,
            {
                make_testcase(0xC1, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0xC2
        {
            0xC2,
            {
                make_testcase(0xC2, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0xC2, 0x7f)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP #imm immediate"),
            }
        },
        // 0xC3
        {
            0xC3,
            {
                make_testcase(0xC3, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0xC4
        {
            0xC4,
            {
                make_testcase(0xC4, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .cpy(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CPY zp zero page"),
            }
        },
        // 0xC5
        {
            0xC5,
            {
                make_testcase(0xC5, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP zp zero page"),
            }
        },
        // 0xC6
        {
            0xC6,
            {
                make_testcase(0xC6, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x80)
                        .org(0x0400)
                            .dec(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DEC zp zero page"),
            }
        },
        // 0xC7
        {
            0xC7,
            {
                make_testcase(0xC7, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP zp zero page"),
            }
        },
        // 0xC8
        {
            0xC8,
            {
                make_testcase(0xC8, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .iny()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "INY implied"),
            }
        },
        // 0xC9
        {
            0xC9,
            {
                make_testcase(0xC9, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .cmp(0x40)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP #imm immediate"),
            }
        },
        // 0xCA
        {
            0xCA,
            {
                make_testcase(0xCA, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .dex()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DEX implied"),
            }
        },
        // 0xCB
        {
            0xCB,
            {
                make_testcase(0xCB, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .axs(0x33)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "AXS #imm immediate"),
            }
        },
        // 0xCC
        {
            0xCC,
            {
                make_testcase(0xCC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .cpy(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                    .end().compile(),
                    "CPY abs absolute"),
            }
        },
        // 0xCD
        {
            0xCD,
            {
                make_testcase(0xCD, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .cmp(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                    .end().compile(),
                    "CMP abs absolute"),
            }
        },
        // 0xCE
        {
            0xCE,
            {
                make_testcase(0xCE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .dec(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x80)
                    .end().compile(),
                    "DEC abs absolute"),
            }
        },
        // 0xCF
        {
            0xCF,
            {
                make_testcase(0xCF, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .dcp(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x80)
                    .end().compile(),
                    "DCP abs absolute"),
            }
        },
        // 0xD0
        {
            0xD0,
            {
                make_testcase(0xD0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x26, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bne("branch_target")
                        .label("trap")
                            .jmp("trap")
                        .org(0x0420, "branch_target")
                    .end().compile(),
                    "BNE rel not taken with a non-fallthrough encoded target"),
                make_testcase(0xD0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .bne("trap")
                        .org(0x0420, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BNE rel taken without page cross to a non-fallthrough target"),
                make_testcase(0xD0, 2, 0x04f0, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x04f0)
                            .bne("trap")
                        .org(0x0505, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BNE rel taken with page cross"),
            }
        },
        // 0xD1
        {
            0xD1,
            {
                make_testcase(0xD1, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP (zp),Y indirect indexed without page cross"),
                make_testcase(0xD1, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP (zp),Y indirect indexed with page cross"),
            }
        },
        // 0xD2
        {
            0xD2,
            {
                make_testcase(0xD2, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0xD2)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0xD3
        {
            0xD3,
            {
                make_testcase(0xD3, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP (zp),Y indirect indexed"),
                make_testcase(0xD3, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP (zp),Y indirect indexed with page cross"),
            }
        },
        // 0xD4
        {
            0xD4,
            {
                make_testcase(0xD4, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0xD4, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp,X zero page,X with wraparound"),
            }
        },
        // 0xD5
        {
            0xD5,
            {
                make_testcase(0xD5, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP zp,X zero page,X with wraparound"),
            }
        },
        // 0xD6
        {
            0xD6,
            {
                make_testcase(0xD6, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x80)
                        .org(0x0400)
                            .dec(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DEC zp,X zero page,X with wraparound"),
            }
        },
        // 0xD7
        {
            0xD7,
            {
                make_testcase(0xD7, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP zp,X zero page,X with wraparound"),
            }
        },
        // 0xD8
        {
            0xD8,
            {
                make_testcase(0xD8, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .cld()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CLD implied"),
            }
        },
        // 0xD9
        {
            0xD9,
            {
                make_testcase(0xD9, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP abs,Y absolute,Y without page cross"),
                make_testcase(0xD9, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP abs,Y absolute,Y with page cross"),
            }
        },
        // 0xDA
        {
            0xDA,
            {
                make_testcase(0xDA, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0xDA)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP implied"),
            }
        },
        // 0xDB
        {
            0xDB,
            {
                make_testcase(0xDB, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP abs,Y absolute,Y"),
                make_testcase(0xDB, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP abs,Y absolute,Y with page cross"),
            }
        },
        // 0xDC
        {
            0xDC,
            {
                make_testcase(0xDC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0xDC, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X without page cross"),
                make_testcase(0xDC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0xDC, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X with page cross"),
            }
        },
        // 0xDD
        {
            0xDD,
            {
                make_testcase(0xDD, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP abs,X absolute,X without page cross"),
                make_testcase(0xDD, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .cmp(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CMP abs,X absolute,X with page cross"),
            }
        },
        // 0xDE
        {
            0xDE,
            {
                make_testcase(0xDE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dec(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DEC abs,X absolute,X"),
                make_testcase(0xDE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dec(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DEC abs,X absolute,X with page cross"),
            }
        },
        // 0xDF
        {
            0xDF,
            {
                make_testcase(0xDF, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP abs,X absolute,X"),
                make_testcase(0xDF, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x80)
                        .org(0x0400)
                            .dcp(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "DCP abs,X absolute,X with page cross"),
            }
        },
        // 0xE0
        {
            0xE0,
            {
                make_testcase(0xE0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .cpx(0x30)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CPX #imm immediate"),
            }
        },
        // 0xE1
        {
            0xE1,
            {
                make_testcase(0xE1, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0xE2
        {
            0xE2,
            {
                make_testcase(0xE2, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0xE2, 0x7f)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP #imm immediate"),
            }
        },
        // 0xE3
        {
            0xE3,
            {
                make_testcase(0xE3, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_ptr_base")
                        .org(0x0008, "zp_ptr")
                            .dw("value_addr")
                        .org(0x2134, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(izx, "zp_ptr_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC (zp,X) indexed indirect with zero-page pointer-base wraparound"),
            }
        },
        // 0xE4
        {
            0xE4,
            {
                make_testcase(0xE4, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .cpx(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "CPX zp zero page"),
            }
        },
        // 0xE5
        {
            0xE5,
            {
                make_testcase(0xE5, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC zp zero page"),
            }
        },
        // 0xE6
        {
            0xE6,
            {
                make_testcase(0xE6, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x7f)
                        .org(0x0400)
                            .inc(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "INC zp zero page"),
            }
        },
        // 0xE7
        {
            0xE7,
            {
                make_testcase(0xE7, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0080, "value_zp")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(zp, "value_zp")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC zp zero page"),
            }
        },
        // 0xE8
        {
            0xE8,
            {
                make_testcase(0xE8, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .inx()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "INX implied"),
            }
        },
        // 0xE9
        {
            0xE9,
            {
                make_testcase(0xE9, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sbc(0x12)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC #imm immediate"),
                make_testcase(0xE9, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x2d, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sbc(0x12)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC #imm decimal mode immediate"),
            }
        },
        // 0xEA
        {
            0xEA,
            {
                make_testcase(0xEA, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP implied"),
            }
        },
        // 0xEB
        {
            0xEB,
            {
                make_testcase(0xEB, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sbc_opcode(0xEB, 0x12)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC #imm immediate"),
                make_testcase(0xEB, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x2d, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sbc_opcode(0xEB, 0x12)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC #imm decimal mode immediate"),
            }
        },
        // 0xEC
        {
            0xEC,
            {
                make_testcase(0xEC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .cpx(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x87)
                    .end().compile(),
                    "CPX abs absolute"),
            }
        },
        // 0xED
        {
            0xED,
            {
                make_testcase(0xED, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sbc(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x15)
                    .end().compile(),
                    "SBC abs absolute"),
            }
        },
        // 0xEE
        {
            0xEE,
            {
                make_testcase(0xEE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .inc(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x7f)
                    .end().compile(),
                    "INC abs absolute"),
            }
        },
        // 0xEF
        {
            0xEF,
            {
                make_testcase(0xEF, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .isc(absolute, "value_addr")
                        .label("trap")
                            .jmp("trap")
                        .org(0x2134, "value_addr")
                            .db(0x7f)
                    .end().compile(),
                    "ISC abs absolute"),
            }
        },
        // 0xF0
        {
            0xF0,
            {
                make_testcase(0xF0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .beq("branch_target")
                        .label("trap")
                            .jmp("trap")
                        .org(0x0420, "branch_target")
                    .end().compile(),
                    "BEQ rel not taken with a non-fallthrough encoded target"),
                make_testcase(0xF0, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x26, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .beq("trap")
                        .org(0x0420, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BEQ rel taken without page cross to a non-fallthrough target"),
                make_testcase(0xF0, 2, 0x04f0, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x26, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x04f0)
                            .beq("trap")
                        .org(0x0505, "trap")
                            .jmp("trap")
                    .end().compile(),
                    "BEQ rel taken with page cross"),
            }
        },
        // 0xF1
        {
            0xF1,
            {
                make_testcase(0xF1, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC (zp),Y indirect indexed without page cross"),
                make_testcase(0xF1, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x20, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC (zp),Y indirect indexed with page cross"),
            }
        },
        // 0xF2
        {
            0xF2,
            {
                make_testcase(0xF2, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .kil_opcode(0xF2)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "KIL/JAM enters JAM/KIL bus loop"),
            }
        },
        // 0xF3
        {
            0xF3,
            {
                make_testcase(0xF3, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC (zp),Y indirect indexed"),
                make_testcase(0xF3, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0020, "zp_ptr")
                            .dw("value_base")
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(izy, "zp_ptr")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC (zp),Y indirect indexed with page cross"),
            }
        },
        // 0xF4
        {
            0xF4,
            {
                make_testcase(0xF4, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0xF4, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP zp,X zero page,X with wraparound"),
            }
        },
        // 0xF5
        {
            0xF5,
            {
                make_testcase(0xF5, 2, 0x0400, 0x0800, 0x0800, 0x45, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC zp,X zero page,X with wraparound"),
            }
        },
        // 0xF6
        {
            0xF6,
            {
                make_testcase(0xF6, 2, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x7f)
                        .org(0x0400)
                            .inc(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "INC zp,X zero page,X with wraparound"),
            }
        },
        // 0xF7
        {
            0xF7,
            {
                make_testcase(0xF7, 2, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x00f8, "zp_base")
                        .org(0x0008, "value_zp")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(zpx, "zp_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC zp,X zero page,X with wraparound"),
            }
        },
        // 0xF8
        {
            0xF8,
            {
                make_testcase(0xF8, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .sed()
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SED implied"),
            }
        },
        // 0xF9
        {
            0xF9,
            {
                make_testcase(0xF9, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC abs,Y absolute,Y without page cross"),
                make_testcase(0xF9, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x00, 0x20, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC abs,Y absolute,Y with page cross"),
            }
        },
        // 0xFA
        {
            0xFA,
            {
                make_testcase(0xFA, 1, 0x0400, 0x0800, 0x0800, 0x42, 0x00, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x0400)
                            .nop_opcode(0xFA)
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP implied"),
            }
        },
        // 0xFB
        {
            0xFB,
            {
                make_testcase(0xFB, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x10, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC abs,Y absolute,Y"),
                make_testcase(0xFB, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x00, 0x20, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(aby, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC abs,Y absolute,Y with page cross"),
            }
        },
        // 0xFC
        {
            0xFC,
            {
                make_testcase(0xFC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0xFC, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X without page cross"),
                make_testcase(0xFC, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x87)
                        .org(0x0400)
                            .nop_opcode(0xFC, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "NOP abs,X absolute,X with page cross"),
            }
        },
        // 0xFD
        {
            0xFD,
            {
                make_testcase(0xFD, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC abs,X absolute,X without page cross"),
                make_testcase(0xFD, 3, 0x0400, 0x0800, 0x0800, 0x45, 0x20, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x15)
                        .org(0x0400)
                            .sbc(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "SBC abs,X absolute,X with page cross"),
            }
        },
        // 0xFE
        {
            0xFE,
            {
                make_testcase(0xFE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x10, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .inc(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "INC abs,X absolute,X"),
                make_testcase(0xFE, 3, 0x0400, 0x0800, 0x0800, 0x42, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .inc(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "INC abs,X absolute,X with page cross"),
            }
        },
        // 0xFF
        {
            0xFF,
            {
                make_testcase(0xFF, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x10, 0x00, 0x25, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x2100, "value_base")
                        .org(0x2110, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC abs,X absolute,X"),
                make_testcase(0xFF, 3, 0x0400, 0x0800, 0x0800, 0xa5, 0x20, 0x00, 0x24, 0xfd,
                    Asm6502::New()
                    .begin()
                        .org(0x21f0, "value_base")
                        .org(0x2210, "value_addr")
                            .db(0x7f)
                        .org(0x0400)
                            .isc(abx, "value_base")
                        .label("trap")
                            .jmp("trap")
                    .end().compile(),
                    "ISC abs,X absolute,X with page cross"),
            }
        },
    };
}

} // namespace tools6502
