#include <cpu6502_bridge/cpu.hpp>

#include <cstdio>

namespace cpu6502_bridge {

std::string ICpu::to_string() const
{
    char buffer[160];
    const char rw = is_write() ? 'W' : 'R';
    const int count = std::snprintf(buffer,
                                    sizeof(buffer),
                                    "PC=%04X A=%02X X=%02X Y=%02X S=%02X P=%02X BUS=%c %04X DATA=%02X%s",
                                    static_cast<unsigned>(pc()),
                                    static_cast<unsigned>(a()),
                                    static_cast<unsigned>(x()),
                                    static_cast<unsigned>(y()),
                                    static_cast<unsigned>(s()),
                                    static_cast<unsigned>(p()),
                                    rw,
                                    static_cast<unsigned>(bus_address()),
                                    static_cast<unsigned>(bus_data()),
                                    is_opcode_fetch() ? " FETCH" : "");

    if (count <= 0) {
        return {};
    }

    const auto length = static_cast<std::size_t>(count);
    if (length < sizeof(buffer)) {
        return std::string(buffer, length);
    }

    return std::string(buffer);
}

} // namespace cpu6502_bridge
