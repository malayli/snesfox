#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "apu_io.hpp"

class Bus {
public:
    explicit Bus(const std::vector<uint8_t>& rom);

    void reset();
    void stepPeripherals();

    uint8_t read(uint8_t bank, uint16_t addr) const;
    void write(uint8_t bank, uint16_t addr, uint8_t value);

private:
    const std::vector<uint8_t>& m_rom;

    // 128 KB WRAM
    std::array<uint8_t, 128 * 1024> m_wram{};

    ApuIo m_apu;

    bool isLoRomArea(uint8_t bank, uint16_t addr) const;
    uint32_t loRomToFileOffset(uint8_t bank, uint16_t addr) const;
};