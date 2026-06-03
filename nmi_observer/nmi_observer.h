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

} // namespace nmi_observer
