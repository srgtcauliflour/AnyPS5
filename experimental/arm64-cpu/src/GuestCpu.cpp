#include "GuestCpu.hpp"

namespace anyps5::experimental {
GuestCpu::GuestCpu(std::span<const std::uint8_t> program)
    : program_(program.begin(), program.end()) {}

std::uint8_t GuestCpu::read8() {
    if (ip_ >= program_.size()) {
        throw std::runtime_error("Guest instruction exceeds code bounds");
    }
    return program_[ip_++];
}

std::uint32_t GuestCpu::read32() {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) {
        value |= static_cast<std::uint32_t>(read8()) << shift;
    }
    return value;
}

std::uint64_t GuestCpu::run(std::size_t maxInstructions) {
    for (std::size_t count = 0; count < maxInstructions; ++count) {
        const auto opcode = read8();
        if (opcode == 0xC3) {
            return rax_;
        }
        if (opcode == 0xB8) {
            rax_ = read32();
            continue;
        }
        if (opcode == 0x05) {
            const std::uint32_t result = static_cast<std::uint32_t>(rax_) + read32();
            rax_ = result;
            continue;
        }
        throw std::runtime_error("Unsupported guest instruction");
    }
    throw std::runtime_error("Guest instruction limit exceeded");
}

std::uint64_t GuestCpu::rax() const noexcept {
    return rax_;
}
}
