#include "GuestCpu.hpp"
#include <limits>

namespace anyps5::experimental {
GuestCpu::GuestCpu(std::span<const std::uint8_t> program)
    : program_(program.begin(), program.end()) {}

std::uint8_t GuestCpu::read8() {
    if (ip_ >= program_.size()) throw std::runtime_error("Guest instruction exceeds code bounds");
    return program_[ip_++];
}

std::uint32_t GuestCpu::read32() {
    std::uint32_t value = 0;
    for (unsigned shift = 0; shift < 32; shift += 8) value |= static_cast<std::uint32_t>(read8()) << shift;
    return value;
}

std::int32_t GuestCpu::readSigned32() {
    const auto bits = read32();
    const std::int64_t signedValue = bits <= INT32_MAX ? static_cast<std::int64_t>(bits) : static_cast<std::int64_t>(bits) - (std::int64_t{1} << 32);
    return static_cast<std::int32_t>(signedValue);
}

void GuestCpu::jumpRelative(std::int32_t displacement) {
    const auto destination = static_cast<std::int64_t>(ip_) + displacement;
    if (destination < 0 || static_cast<std::uint64_t>(destination) >= program_.size()) {
        throw std::runtime_error("Guest branch outside code bounds");
    }
    ip_ = static_cast<std::size_t>(destination);
}

std::uint64_t GuestCpu::run(std::size_t maxInstructions) {
    for (std::size_t count = 0; count < maxInstructions; ++count) {
        bool wide = false;
        bool extended = false;
        auto opcode = read8();
        if (opcode >= 0x40 && opcode <= 0x4F) {
            wide = (opcode & 8) != 0;
            extended = (opcode & 1) != 0;
            opcode = read8();
        }
        if (opcode == 0xC3) return registers_[0];
        if (opcode >= 0xB8 && opcode <= 0xBF) {
            const auto index = static_cast<std::size_t>((opcode - 0xB8) + (extended ? 8 : 0));
            if (wide) throw std::runtime_error("64-bit immediate MOV not yet supported");
            registers_[index] = read32();
            continue;
        }
        if (opcode == 0x05 && !wide && !extended) {
            const auto result = static_cast<std::uint32_t>(registers_[0]) + read32();
            registers_[0] = result;
            zeroFlag_ = result == 0;
            continue;
        }
        if (opcode == 0x3D && !wide && !extended) {
            zeroFlag_ = static_cast<std::uint32_t>(registers_[0]) == read32();
            continue;
        }
        if (opcode == 0xE9 && !wide && !extended) {
            jumpRelative(readSigned32());
            continue;
        }
        if (opcode == 0xEB && !wide && !extended) {
            const auto offset = static_cast<std::int8_t>(read8());
            jumpRelative(offset);
            continue;
        }
        if (opcode == 0x74 && !wide && !extended) {
            const auto offset = static_cast<std::int8_t>(read8());
            if (zeroFlag_) jumpRelative(offset);
            continue;
        }
        if (opcode == 0x75 && !wide && !extended) {
            const auto offset = static_cast<std::int8_t>(read8());
            if (!zeroFlag_) jumpRelative(offset);
            continue;
        }
        throw std::runtime_error("Unsupported guest instruction");
    }
    throw std::runtime_error("Guest instruction limit exceeded");
}

std::uint64_t GuestCpu::rax() const noexcept { return registers_[0]; }
std::uint64_t GuestCpu::registerValue(std::size_t index) const {
    if (index >= registers_.size()) throw std::out_of_range("Invalid guest register index");
    return registers_[index];
}
std::size_t GuestCpu::instructionPointer() const noexcept { return ip_; }
}
