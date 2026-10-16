#include "GuestCpu.hpp"
#include <limits>

namespace anyps5::experimental {
GuestCpu::GuestCpu(std::span<const std::uint8_t> program, std::size_t stackBytes)
    : program_(program.begin(), program.end()), memory_(stackBytes, 0) {
    if (stackBytes < 8 || stackBytes > std::numeric_limits<std::uint32_t>::max()) {
        throw std::invalid_argument("Invalid guest stack size");
    }
    registers_[4] = stackBytes;
}

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

std::uint64_t GuestCpu::readMemory64(std::uint64_t address) const {
    if (address > memory_.size() || memory_.size() - static_cast<std::size_t>(address) < 8) {
        throw std::runtime_error("Guest memory read out of bounds");
    }
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{memory_[static_cast<std::size_t>(address) + i]} << (i * 8);
    return value;
}

void GuestCpu::writeMemory64(std::uint64_t address, std::uint64_t value) {
    if (address > memory_.size() || memory_.size() - static_cast<std::size_t>(address) < 8) {
        throw std::runtime_error("Guest memory write out of bounds");
    }
    for (unsigned i = 0; i < 8; ++i) memory_[static_cast<std::size_t>(address) + i] = static_cast<std::uint8_t>(value >> (i * 8));
}


std::uint32_t GuestCpu::readMemory32(std::uint64_t address) const {
    if (address > memory_.size() || memory_.size() - static_cast<std::size_t>(address) < 4)
        throw std::runtime_error("Guest memory read out of bounds");
    std::uint32_t value = 0;
    for (unsigned i = 0; i < 4; ++i)
        value |= std::uint32_t{memory_[static_cast<std::size_t>(address) + i]} << (i * 8);
    return value;
}

void GuestCpu::writeMemory32(std::uint64_t address, std::uint32_t value) {
    if (address > memory_.size() || memory_.size() - static_cast<std::size_t>(address) < 4)
        throw std::runtime_error("Guest memory write out of bounds");
    for (unsigned i = 0; i < 4; ++i)
        memory_[static_cast<std::size_t>(address) + i] = static_cast<std::uint8_t>(value >> (i * 8));
}

GuestCpu::Operand GuestCpu::decodeOperand(std::uint8_t modrm, bool rexB, bool rexX) {
    const auto mod = static_cast<unsigned>(modrm >> 6);
    const auto rm = static_cast<unsigned>(modrm & 7);
    if (mod == 3) return {true, rm + (rexB ? 8u : 0u), 0};
    std::uint64_t base = 0;
    std::uint64_t index = 0;
    unsigned scale = 1;
    bool hasBase = true;
    bool ripRelative = false;
    if (rm == 4) {
        const auto sib = read8();
        scale = 1u << (sib >> 6);
        const auto indexBits = static_cast<unsigned>((sib >> 3) & 7);
        const auto baseBits = static_cast<unsigned>(sib & 7);
        if (indexBits != 4 || rexX)
            index = registers_[indexBits + (rexX ? 8u : 0u)];
        if (mod == 0 && baseBits == 5) hasBase = false;
        else base = registers_[baseBits + (rexB ? 8u : 0u)];
    } else if (mod == 0 && rm == 5) {
        hasBase = false;
        ripRelative = true;
    } else {
        base = registers_[rm + (rexB ? 8u : 0u)];
    }
    std::int64_t displacement = 0;
    if (mod == 1) displacement = static_cast<std::int8_t>(read8());
    if (mod == 2 || (mod == 0 && !hasBase)) displacement = readSigned32();
    if (ripRelative) base = ip_;
    return {false, 0, base + index * scale + static_cast<std::uint64_t>(displacement)};
}

std::uint64_t GuestCpu::readOperand(const Operand& operand, bool wide) const {
    if (operand.isRegister) {
        const auto value = registers_[operand.index];
        return wide ? value : static_cast<std::uint32_t>(value);
    }
    return wide ? readMemory64(operand.address) : readMemory32(operand.address);
}

void GuestCpu::writeOperand(const Operand& operand, std::uint64_t value, bool wide) {
    if (operand.isRegister) {
        registers_[operand.index] = wide ? value : static_cast<std::uint32_t>(value);
    } else if (wide) {
        writeMemory64(operand.address, value);
    } else {
        writeMemory32(operand.address, static_cast<std::uint32_t>(value));
    }
}

void GuestCpu::push64(std::uint64_t value) {
    if (registers_[4] < 8 || registers_[4] > memory_.size()) throw std::runtime_error("Guest stack overflow");
    const auto next = registers_[4] - 8;
    writeMemory64(next, value);
    registers_[4] = next;
}

std::uint64_t GuestCpu::pop64() {
    const auto address = registers_[4];
    const auto value = readMemory64(address);
    registers_[4] += 8;
    return value;
}

std::uint64_t GuestCpu::run(std::size_t maxInstructions) {
    for (std::size_t count = 0; count < maxInstructions; ++count) {
        bool wide = false;
        bool extended = false;
        bool rexR = false;
        bool rexX = false;
        auto opcode = read8();
        if (opcode >= 0x40 && opcode <= 0x4F) {
            wide = (opcode & 8) != 0;
            extended = (opcode & 1) != 0;
            rexR = (opcode & 4) != 0;
            rexX = (opcode & 2) != 0;
            opcode = read8();
        }
        if (opcode == 0x89 || opcode == 0x8B) {
            const auto modrm = read8();
            const auto reg = static_cast<std::size_t>(((modrm >> 3) & 7) + (rexR ? 8 : 0));
            const auto operand = decodeOperand(modrm, extended, rexX);
            if (opcode == 0x89) writeOperand(operand, registers_[reg], wide);
            else registers_[reg] = readOperand(operand, wide);
            continue;
        }
        if (opcode == 0xC3) {
            if (wide || extended || rexR || rexX) throw std::runtime_error("Unsupported RET prefix");
            if (callDepth_ == 0) return registers_[0];
            const auto destination = pop64();
            if (destination >= program_.size()) throw std::runtime_error("Guest return outside code bounds");
            ip_ = static_cast<std::size_t>(destination);
            --callDepth_;
            continue;
        }
        if (opcode >= 0x50 && opcode <= 0x57) {
            if (rexR || rexX) throw std::runtime_error("Unsupported PUSH prefix");
            const auto index = static_cast<std::size_t>(opcode - 0x50 + (extended ? 8 : 0));
            push64(registers_[index]);
            continue;
        }
        if (opcode >= 0x58 && opcode <= 0x5F) {
            if (rexR || rexX) throw std::runtime_error("Unsupported POP prefix");
            const auto index = static_cast<std::size_t>(opcode - 0x58 + (extended ? 8 : 0));
            registers_[index] = pop64();
            continue;
        }
        if (opcode >= 0xB8 && opcode <= 0xBF) {
            if (rexR || rexX) throw std::runtime_error("Unsupported MOV immediate prefix");
            const auto index = static_cast<std::size_t>((opcode - 0xB8) + (extended ? 8 : 0));
            if (wide) throw std::runtime_error("64-bit immediate MOV not yet supported");
            registers_[index] = read32();
            continue;
        }
        if (opcode == 0x05 && !wide && !extended && !rexR && !rexX) {
            const auto left = static_cast<std::uint32_t>(registers_[0]);
            const auto right = read32();
            const auto result = static_cast<std::uint32_t>(left + right);
            registers_[0] = result;
            zeroFlag_ = result == 0;
            carryFlag_ = result < left;
            continue;
        }
        if (opcode == 0x3D && !wide && !extended) {
            const auto left = static_cast<std::uint32_t>(registers_[0]);
            const auto right = read32();
            zeroFlag_ = left == right;
            carryFlag_ = left < right;
            continue;
        }
        if (opcode == 0xE8 && !wide && !extended) {
            const auto displacement = readSigned32();
            const auto returnAddress = ip_;
            jumpRelative(displacement);
            push64(returnAddress);
            ++callDepth_;
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
