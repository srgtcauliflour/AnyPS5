#include "GuestCpu.hpp"
#include <array>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>

using anyps5::experimental::GuestCpu;

int main() {
    const std::array<std::uint8_t, 12> addition{
        0xB8, 0x28, 0, 0, 0,
        0x05, 0x02, 0, 0, 0,
        0xC3, 0x90
    };
    if (GuestCpu(addition).run() != 42) return EXIT_FAILURE;
    const std::array<std::uint8_t, 1> invalid{0x90};
    bool rejected = false;
    try {
        GuestCpu(invalid).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 5> truncated{0xB8, 1, 2, 3, 4};
    rejected = false;
    try {
        GuestCpu(truncated).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    rejected = false;
    try {
        GuestCpu(addition).run(1);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 18> branch{
        0xB8, 0x2A, 0, 0, 0,
        0x3D, 0x2A, 0, 0, 0,
        0x75, 0x05,
        0xC3,
        0xB8, 0, 0, 0, 0
    };
    if (GuestCpu(branch).run() != 42) return EXIT_FAILURE;
    const std::array<std::uint8_t, 7> extended{
        0x41, 0xB8, 0x07, 0, 0, 0, 0xC3
    };
    GuestCpu extendedCpu(extended);
    if (extendedCpu.run() != 0 || extendedCpu.registerValue(8) != 7) return EXIT_FAILURE;
    const std::array<std::uint8_t, 2> loop{0xEB, 0xFE};
    rejected = false;
    try {
        GuestCpu(loop).run(8);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    const std::array<std::uint8_t, 2> invalidJump{0xEB, 0x7F};
    rejected = false;
    try {
        GuestCpu(invalidJump).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    if (!rejected) return EXIT_FAILURE;
    return EXIT_SUCCESS;
}
