#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace anyps5::experimental {
class GuestCpu {
public:
    explicit GuestCpu(std::span<const std::uint8_t> program);
    std::uint64_t run(std::size_t maxInstructions = 10000);
    std::uint64_t rax() const noexcept;
    std::uint64_t registerValue(std::size_t index) const;
    std::size_t instructionPointer() const noexcept;
private:
    std::vector<std::uint8_t> program_;
    std::array<std::uint64_t, 16> registers_{};
    std::size_t ip_ = 0;
    bool zeroFlag_ = false;
    std::uint8_t read8();
    std::uint32_t read32();
    std::int32_t readSigned32();
    void jumpRelative(std::int32_t displacement);
};
}
