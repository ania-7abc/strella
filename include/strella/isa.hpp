#pragma once

#include <cstdint>
#include <initializer_list>
#include <vector>

namespace strella::isa
{

enum class Reg : std::uint8_t
{
  A = 0,
  B = 1,
  C = 2,
  D = 3
};

enum class Cond : std::uint8_t
{
  Z = 0,
  S = 1,
  C = 2,
  O = 3
};

using Bytes = std::vector<std::uint8_t>;

inline auto regIndex(Reg reg) -> std::uint8_t
{
  return static_cast<std::uint8_t>(reg);
}

inline auto condIndex(Cond cond) -> std::uint8_t
{
  return static_cast<std::uint8_t>(cond);
}

inline auto nop() -> std::uint8_t { return 0x00; }
inline auto hlt() -> std::uint8_t { return 0x01; }

inline auto jmpImm(std::uint8_t address) -> Bytes { return {0x03, address}; }

inline auto jmpReg(Reg reg) -> std::uint8_t
{
  return static_cast<std::uint8_t>(0x04 + regIndex(reg));
}

inline auto jCondImm(Cond cond, std::uint8_t address) -> Bytes
{
  return {static_cast<std::uint8_t>(0x08 + condIndex(cond)), address};
}

inline auto jnCondImm(Cond cond, std::uint8_t address) -> Bytes
{
  return {static_cast<std::uint8_t>(0x0C + condIndex(cond)), address};
}

inline auto stImm(Reg value, std::uint8_t address) -> Bytes
{
  return {static_cast<std::uint8_t>(0x30 + (5 * regIndex(value))), address};
}

inline auto stReg(Reg address, Reg value) -> std::uint8_t
{
  return static_cast<std::uint8_t>(0x30 + (regIndex(address) * 4)
                                   + regIndex(value));
}

inline auto ldReg(Reg address, Reg dest) -> std::uint8_t
{
  return static_cast<std::uint8_t>(0x40 + (regIndex(address) * 4)
                                   + regIndex(dest));
}

inline auto ldImm(Reg dest, std::uint8_t address) -> Bytes
{
  return {static_cast<std::uint8_t>(0x50 + regIndex(dest)), address};
}

inline auto ldi(Reg dest, std::uint8_t value) -> Bytes
{
  return {static_cast<std::uint8_t>(0x54 + regIndex(dest)), value};
}

// Binary operations use the layout `base + src * 4 + dst`.
inline auto alu(std::uint8_t base, Reg dst, Reg src) -> std::uint8_t
{
  return static_cast<std::uint8_t>(base + (regIndex(src) * 4) + regIndex(dst));
}

inline auto add(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0x60, dst, src);
}
inline auto sub(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0x70, dst, src);
}
inline auto adc(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0x80, dst, src);
}
inline auto sbb(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0x90, dst, src);
}
inline auto mov(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0xA0, dst, src);
}
inline auto bitAnd(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0xB0, dst, src);
}
inline auto bitOr(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0xC0, dst, src);
}
inline auto bitXor(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0xD0, dst, src);
}
inline auto cmp(Reg dst, Reg src) -> std::uint8_t
{
  return alu(0xF0, dst, src);
}

// Unary operations use the layout `base + reg * 5`.
inline auto unary(std::uint8_t base, Reg reg) -> std::uint8_t
{
  return static_cast<std::uint8_t>(base + (regIndex(reg) * 5));
}

inline auto inc(Reg reg) -> std::uint8_t { return unary(0x60, reg); }
inline auto dec(Reg reg) -> std::uint8_t { return unary(0x70, reg); }
inline auto bitNot(Reg reg) -> std::uint8_t { return unary(0x80, reg); }
inline auto neg(Reg reg) -> std::uint8_t { return unary(0x90, reg); }
inline auto clr(Reg reg) -> std::uint8_t { return unary(0xA0, reg); }
inline auto test(Reg reg) -> std::uint8_t { return unary(0xB0, reg); }

inline auto shl(Reg reg) -> std::uint8_t
{
  return static_cast<std::uint8_t>(0xE0 + regIndex(reg));
}
inline auto shr(Reg reg) -> std::uint8_t
{
  return static_cast<std::uint8_t>(0xE4 + regIndex(reg));
}
inline auto rcl(Reg reg) -> std::uint8_t { return unary(0xC0, reg); }
inline auto rcr(Reg reg) -> std::uint8_t { return unary(0xD0, reg); }
inline auto rnd(Reg reg) -> std::uint8_t
{
  return static_cast<std::uint8_t>(0xEC + regIndex(reg));
}

} // namespace strella::isa
