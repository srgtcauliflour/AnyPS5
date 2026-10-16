#pragma once
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
private:
    std::vector<std::uint8_t> program_;
    std::size_t ip_ = 0;
    std::uint64_t rax_ = 0;
    std::uint8_t read8();
    std::uint32_t read32();
};
}
