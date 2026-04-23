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
            // CPU writes a non-zero value to port 0 as the "start transfer" command
            // (typically $CC for pvsneslib / most SNES ROMs).  Echo it back immediately
            // so the CPU's spin-wait on $2140 can complete.
            if (index == 0 && value != 0) {
                m_apuToCpu[0] = value;
                m_phase = Phase::BootCC;
            }
            break;

        case Phase::BootCC:
            // Mirror port-0 writes back so any subsequent handshake steps complete.
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