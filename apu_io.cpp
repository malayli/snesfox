#include "apu_io.hpp"

void ApuIo::reset() {
    m_cpuToApu.fill(0);
    m_apuToCpu.fill(0);

    // Standard boot signature visible to CPU
    m_apuToCpu[0] = 0xAA;
    m_apuToCpu[1] = 0xBB;
    m_apuToCpu[2] = 0x00;
    m_apuToCpu[3] = 0x00;

    m_phase = Phase::BootAA_BB;
}

uint8_t ApuIo::readPort(uint16_t addr) const {
    return m_apuToCpu[addr - 0x2140];
}

void ApuIo::writePort(uint16_t addr, uint8_t value) {
    const size_t index = static_cast<size_t>(addr - 0x2140);
    m_cpuToApu[index] = value;

    switch (m_phase) {
        case Phase::BootAA_BB:
            // Super Mario World clears all four ports first.
            // After that, the APU handshake moves to CC on port 0.
            if (m_cpuToApu[0] == 0x00 &&
                m_cpuToApu[1] == 0x00 &&
                m_cpuToApu[2] == 0x00 &&
                m_cpuToApu[3] == 0x00) {
                m_apuToCpu[0] = 0xCC;
                m_apuToCpu[1] = 0x00;
                m_apuToCpu[2] = 0x00;
                m_apuToCpu[3] = 0x00;
                m_phase = Phase::BootCC;
            }
            break;

        case Phase::BootCC:
            // Minimal generic follow-up:
            // if CPU writes to port 1 after CC phase, mirror it back on port 0
            // so the boot handshake can continue evolving instead of staying fixed.
            if (index == 1) {
                m_apuToCpu[0] = value;
            }
            if (index == 0) {
                m_apuToCpu[0] = value;
            }
            break;

        case Phase::Ready:
            // Basic echo behavior once ready
            m_apuToCpu[index] = value;
            break;
    }
}

void ApuIo::step() {
    // Passive stub for now.
}