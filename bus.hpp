#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "apu_io.hpp"
#include "header.hpp"

class Bus {
public:
    explicit Bus(const std::vector<uint8_t>& rom);

    void reset();
    void stepPeripherals(uint64_t totalCycles);

    uint8_t read(uint8_t bank, uint16_t addr) const;
    void write(uint8_t bank, uint16_t addr, uint8_t value);

    RomMapping mapMode() const;
    size_t sramBytes() const;

    // Called once per frame at VBlank start; sets NMI flag and returns true if NMI is enabled
    bool onVBlank();

private:
    const std::vector<uint8_t>& m_rom;

    // 128 KB WRAM
    std::array<uint8_t, 128 * 1024> m_wram{};

    ApuIo m_apu;

    RomMapping m_mapMode = RomMapping::LoROM;
    size_t m_sramBytes = 0;

    bool m_nmiEnabled = false;
    mutable bool m_nmiFlag = false;

    // Hardware multiply/divide unit
    uint8_t  m_wrmpya = 0xFF;
    uint16_t m_wrdiv  = 0xFFFF;
    uint16_t m_rddiv  = 0;
    uint16_t m_rdmpy  = 0;

    bool isLoRomArea(uint8_t bank, uint16_t addr) const;
    uint32_t loRomToFileOffset(uint8_t bank, uint16_t addr) const;
};