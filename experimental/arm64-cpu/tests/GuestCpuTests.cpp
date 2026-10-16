#include "GuestCpu.hpp"
#include <array>
#include <cassert>
#include <cstdint>
#include <stdexcept>

using anyps5::experimental::GuestCpu;

int main() {
    const std::array<std::uint8_t, 12> addition{
        0xB8, 0x28, 0, 0, 0,
        0x05, 0x02, 0, 0, 0,
        0xC3, 0x90
    };
    assert(GuestCpu(addition).run() == 42);
    const std::array<std::uint8_t, 1> invalid{0x90};
    bool rejected = false;
    try {
        GuestCpu(invalid).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
    const std::array<std::uint8_t, 5> truncated{0xB8, 1, 2, 3, 4};
    rejected = false;
    try {
        GuestCpu(truncated).run();
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
    rejected = false;
    try {
        GuestCpu(addition).run(1);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);
}
