#pragma once

#include <cinttypes>
#include <map>
#include <string>
#include <vector>

#include <asm6502/asm6502.h>

namespace nmi_observer{

struct testcase
{
    uint8_t opcode;
    uint8_t bytes;
    uint8_t expected_cycles;
    uint16_t start_at;
    uint16_t nmi_vector;
    uint8_t  A;
    uint8_t  X;
    uint8_t  Y;
    uint8_t  P;
    uint8_t  S;
    std::vector<asm6502::mem_value> mem_setup;
    std::string description;
};

std::map<uint8_t, std::vector<testcase>> get_example_testcases();

// Returns the full NMOS 6502 opcode scenario matrix: one map entry for every
// opcode byte 0x00-0xFF, including qe6502-supported undocumented/illegal
// opcodes. Each testcase contains a short description, register/stack setup,
// expected opcode-cycle count, and a mini-program made from exactly the opcode
// under test plus a terminal self-JMP trap at the expected continuation target.
std::map<uint8_t, std::vector<testcase>> get_nmos6502_opcode_testcases();


// Returns the same NMOS 6502 opcode scenario matrix, but assembles every
// mini-program with asm6502 mnemonic helpers and exact duplicate-opcode
// selectors instead of hand-written opcode bytes. This human-readable variant
// is intended to be compared against get_nmos6502_opcode_testcases() as the
// byte-level reference.
std::map<uint8_t, std::vector<testcase>> get_nmos6502_opcode_testcases_asm6502();

// Compares two opcode testcase matrices for byte-for-byte equivalence. All
// testcase fields are compared, but each testcase::mem_setup vector is first
// normalized into a map of {address, value} pairs so the comparison does not
// depend on the order in which the assembler returned those memory writes.
// When mismatch is non-null, the first detected difference is described there.
bool opcode_testcase_maps_equal(
    const std::map<uint8_t, std::vector<testcase>>& lhs,
    const std::map<uint8_t, std::vector<testcase>>& rhs,
    std::string* mismatch = nullptr);

// Builds the byte-level reference matrix and the asm6502-readable matrix, then
// compares them with opcode_testcase_maps_equal(). This is the public harness
// entry point used by the CTest equivalence test.
bool nmos6502_opcode_testcase_outputs_match(std::string* mismatch = nullptr);

} // namespace nmi_observer
