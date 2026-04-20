#pragma once

#include <array>
#include <cstdint>

class ApuIo {
public:
    void reset();

    uint8_t readPort(uint16_t addr) const;
    void writePort(uint16_t addr, uint8_t value);

    void step();

private:
    enum class Phase {
        BootAA_BB,
        BootCC,
        Ready
    };

    Phase m_phase = Phase::BootAA_BB;

    // What CPU writes to $2140-$2143
    std::array<uint8_t, 4> m_cpuToApu{};

    // What CPU reads from $2140-$2143
    std::array<uint8_t, 4> m_apuToCpu{};
};