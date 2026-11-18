#include "machine.hpp"
#include "strella/isa.hpp"
#include <cstdint>

namespace testutil
{

void Machine::execute(std::uint8_t opcode)
{
  using strella::isa::Cond;

  if (opcode == 0x00)
  {
    return;
  }

  if (opcode == 0x01)
  {
    halted_ = true;
    return;
  }

  if (opcode == 0x03)
  {
    ip_ = fetch();
    return;
  }

  if (opcode >= 0x04 && opcode <= 0x07)
  {
    ip_ = reg(opcode - 0x04);
    return;
  }

  if (opcode >= 0x08 && opcode <= 0x0B)
  {
    auto const address = fetch();
    if (flag(static_cast<Cond>(opcode - 0x08)))
    {
      ip_ = address;
    }
    return;
  }

  if (opcode >= 0x0C && opcode <= 0x0F)
  {
    auto const address = fetch();
    if (!flag(static_cast<Cond>(opcode - 0x0C)))
    {
      ip_ = address;
    }
    return;
  }

  if (opcode >= 0x10 && opcode <= 0x1F)
  {
    auto const index = (opcode - 0x10) / 4;
    auto const condition = (opcode - 0x10) % 4;
    if (flag(static_cast<Cond>(condition)))
    {
      ip_ = reg(index);
    }
    return;
  }

  if (opcode >= 0x20 && opcode <= 0x2F)
  {
    auto const index = (opcode - 0x20) / 4;
    auto const condition = (opcode - 0x20) % 4;
    if (!flag(static_cast<Cond>(condition)))
    {
      ip_ = reg(index);
    }
    return;
  }

  if (opcode >= 0x30 && opcode <= 0x3F)
  {
    if (opcode == 0x30 || opcode == 0x35 || opcode == 0x3A || opcode == 0x3F)
    {
      auto const valueReg = (opcode - 0x30) / 5;
      auto const address = fetch();
      write(address, reg(valueReg));
    }
    else
    {
      auto const addressReg = (opcode - 0x30) / 4;
      auto const valueReg = (opcode - 0x30) % 4;
      write(reg(addressReg), reg(valueReg));
    }
    return;
  }

  if (opcode >= 0x40 && opcode <= 0x4F)
  {
    auto const addressReg = (opcode - 0x40) / 4;
    auto const destReg = (opcode - 0x40) % 4;
    regRef(destReg) = read(reg(addressReg));
    return;
  }

  if (opcode >= 0x50 && opcode <= 0x53)
  {
    auto const destReg = opcode - 0x50;
    regRef(destReg) = read(fetch());
    return;
  }

  if (opcode >= 0x54 && opcode <= 0x57)
  {
    auto const destReg = opcode - 0x54;
    regRef(destReg) = fetch();
    return;
  }

  if (opcode >= 0x60 && opcode <= 0x6F)
  {
    if (opcode == 0x60 || opcode == 0x65 || opcode == 0x6A || opcode == 0x6F)
    {
      auto const index = (opcode - 0x60) / 5;
      auto const value = static_cast<std::uint8_t>(reg(index) + 1);
      regRef(index) = value;
      setZS(value);
    }
    else
    {
      auto const src = (opcode - 0x60) / 4;
      auto const dst = (opcode - 0x60) % 4;
      regRef(dst) = add(reg(dst), reg(src), false);
    }
    return;
  }

  if (opcode >= 0x70 && opcode <= 0x7F)
  {
    if (opcode == 0x70 || opcode == 0x75 || opcode == 0x7A || opcode == 0x7F)
    {
      auto const index = (opcode - 0x70) / 5;
      auto const value = static_cast<std::uint8_t>(reg(index) - 1);
      regRef(index) = value;
      setZS(value);
    }
    else
    {
      auto const src = (opcode - 0x70) / 4;
      auto const dst = (opcode - 0x70) % 4;
      regRef(dst) = subtract(reg(dst), reg(src), false);
    }
    return;
  }

  if (opcode >= 0x80 && opcode <= 0x8F)
  {
    if (opcode == 0x80 || opcode == 0x85 || opcode == 0x8A || opcode == 0x8F)
    {
      auto const index = (opcode - 0x80) / 5;
      auto const value = static_cast<std::uint8_t>(~reg(index));
      regRef(index) = value;
      setZS(value);
    }
    else
    {
      auto const src = (opcode - 0x80) / 4;
      auto const dst = (opcode - 0x80) % 4;
      regRef(dst) = add(reg(dst), reg(src), true);
    }
    return;
  }

  if (opcode >= 0x90 && opcode <= 0x9F)
  {
    if (opcode == 0x90 || opcode == 0x95 || opcode == 0x9A || opcode == 0x9F)
    {
      auto const index = (opcode - 0x90) / 5;
      auto const value = reg(index);
      auto const result = static_cast<std::uint8_t>(0U - value);
      regRef(index) = result;
      z_ = result == 0;
      s_ = (result & 0x80) != 0;
      cf_ = value != 0;
      o_ = value == 0x80;
    }
    else
    {
      auto const src = (opcode - 0x90) / 4;
      auto const dst = (opcode - 0x90) % 4;
      regRef(dst) = subtract(reg(dst), reg(src), true);
    }
    return;
  }

  if (opcode >= 0xA0 && opcode <= 0xAF)
  {
    if (opcode == 0xA0 || opcode == 0xA5 || opcode == 0xAA || opcode == 0xAF)
    {
      auto const index = (opcode - 0xA0) / 5;
      regRef(index) = 0;
    }
    else
    {
      auto const src = (opcode - 0xA0) / 4;
      auto const dst = (opcode - 0xA0) % 4;
      regRef(dst) = reg(src);
    }
    return;
  }

  if (opcode >= 0xB0 && opcode <= 0xBF)
  {
    if (opcode == 0xB0 || opcode == 0xB5 || opcode == 0xBA || opcode == 0xBF)
    {
      auto const index = (opcode - 0xB0) / 5;
      setZS(reg(index));
    }
    else
    {
      auto const src = (opcode - 0xB0) / 4;
      auto const dst = (opcode - 0xB0) % 4;
      auto const value = static_cast<std::uint8_t>(reg(dst) & reg(src));
      regRef(dst) = value;
      setZS(value);
    }
    return;
  }

  if (opcode >= 0xC0 && opcode <= 0xCF)
  {
    if (opcode == 0xC0 || opcode == 0xC5 || opcode == 0xCA || opcode == 0xCF)
    {
      auto const index = (opcode - 0xC0) / 5;
      auto const value = reg(index);
      auto const result
          = static_cast<std::uint8_t>((value << 1) | (cf_ ? 1U : 0U));
      cf_ = (value & 0x80) != 0;
      regRef(index) = result;
      setZS(result);
    }
    else
    {
      auto const src = (opcode - 0xC0) / 4;
      auto const dst = (opcode - 0xC0) % 4;
      auto const value = static_cast<std::uint8_t>(reg(dst) | reg(src));
      regRef(dst) = value;
      setZS(value);
    }
    return;
  }

  if (opcode >= 0xD0 && opcode <= 0xDF)
  {
    if (opcode == 0xD0 || opcode == 0xD5 || opcode == 0xDA || opcode == 0xDF)
    {
      auto const index = (opcode - 0xD0) / 5;
      auto const value = reg(index);
      auto const result
          = static_cast<std::uint8_t>((value >> 1) | (cf_ ? 0x80U : 0U));
      cf_ = (value & 1U) != 0;
      regRef(index) = result;
      setZS(result);
    }
    else
    {
      auto const src = (opcode - 0xD0) / 4;
      auto const dst = (opcode - 0xD0) % 4;
      auto const value = static_cast<std::uint8_t>(reg(dst) ^ reg(src));
      regRef(dst) = value;
      setZS(value);
    }
    return;
  }

  if (opcode >= 0xE0 && opcode <= 0xEF)
  {
    auto const index = opcode & 3;
    auto const kind = (opcode - 0xE0) / 4;
    auto const value = reg(index);

    switch (kind)
    {
    case 0: // shl
    {
      auto const result = static_cast<std::uint8_t>(value << 1);
      cf_ = (value & 0x80) != 0;
      regRef(index) = result;
      setZS(result);
      break;
    }
    case 1: // shr
    {
      auto const result = static_cast<std::uint8_t>(value >> 1);
      cf_ = (value & 1U) != 0;
      regRef(index) = result;
      setZS(result);
      break;
    }
    case 2: // sar
    {
      auto const result
          = static_cast<std::uint8_t>((value >> 1) | (value & 0x80));
      cf_ = (value & 1U) != 0;
      regRef(index) = result;
      setZS(result);
      break;
    }
    default: // rnd
    {
      randomState_
          = static_cast<std::uint16_t>((randomState_ * 25173U) + 13849U);
      auto const result = static_cast<std::uint8_t>(randomState_ >> 8);
      regRef(index) = result;
      setZS(result);
      break;
    }
    }
    return;
  }

  if (opcode >= 0xF0)
  {
    auto const src = (opcode - 0xF0) / 4;
    auto const dst = (opcode - 0xF0) % 4;
    auto const left = reg(dst);
    auto const right = reg(src);
    auto const difference = static_cast<std::uint8_t>(left - right);
    z_ = left == right;
    s_ = static_cast<std::int8_t>(left) < static_cast<std::int8_t>(right);
    cf_ = left < right;
    o_ = ((left ^ right) & (left ^ difference) & 0x80) != 0;
    return;
  }
}

} // namespace testutil
