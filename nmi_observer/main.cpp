#include "types.h"
#include <qe6502/cpu.hpp>

#include <algorithm>
#include <chrono>
#include <cinttypes>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <vector>


extern "C" {
#include <types.h>
#include <perfect6502.h>
#include <netlist_sim.h>
}

namespace nmi_observer{

int app_main(int argc, char** argv)
{
    (void)argc;
    (void)argv;

    return 0;
}
} // namespace

int main(int argc, char** argv)
{
    return ::nmi_observer::app_main(argc, argv);
}
