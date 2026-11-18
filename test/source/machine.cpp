#include "machine.hpp"
#include "strella/compiler.hpp"
#include "strella/isa.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace testutil
{

auto build(std::string const& source) -> CompilationResult
{
  return strella::compile(source, "<test>");
}

Machine::Machine(std::vector<std::uint8_t> image) : image_ {std::move(image)}
{
  // The image byte at the device port acts as a compile-time preimage.
  if (image_.size() > 0x3E)
  {
    devices = image_.at(0x3E);
  }
}

void Machine::run(int maxSteps)
{
  while (!halted_)
  {
    if (steps_ >= maxSteps)
    {
      throw std::runtime_error {"machine did not halt"};
    }

    ++steps_;
    execute(fetch());
  }
}

auto Machine::displayByte(int index) const -> std::uint8_t
{
  return image_.at(static_cast<std::size_t>(
      memIndex(static_cast<std::uint8_t>(0x40 + index))));
}

auto Machine::fetch() -> std::uint8_t { return read(ip_++); }

auto Machine::memIndex(std::uint8_t address) const -> int
{
  if (address < 0x80)
  {
    return address;
  }
  return (bank_ * 128) + (address - 0x80);
}

auto Machine::read(std::uint8_t address) -> std::uint8_t
{
  if (address == 0x3E)
  {
    if (!input.empty())
    {
      auto const value = input.front();
      input.pop_front();
      return value;
    }
    return 0;
  }
  return image_.at(static_cast<std::size_t>(memIndex(address)));
}

void Machine::write(std::uint8_t address, std::uint8_t value)
{
  if (address >= 0x3A && address <= 0x3F)
  {
    switch (address)
    {
    case 0x3F:
      bank_ = value == 0 ? 1 : value;
      return;
    case 0x3C:
      terminal.push_back(static_cast<char>(value));
      return;
    case 0x3D:
      graphics.push_back(value);
      return;
    case 0x3E:
      devices = value;
      return;
    case 0x3A:
      indicator.push_back(value);
      return;
    case 0x3B:
      indicatorHigh.push_back(value);
      return;
    default:
      return;
    }
  }

  image_.at(static_cast<std::size_t>(memIndex(address))) = value;
}

auto Machine::reg(int index) const -> std::uint8_t
{
  switch (index)
  {
  case 0:
    return a_;
  case 1:
    return b_;
  case 2:
    return c_;
  default:
    return d_;
  }
}

auto Machine::regRef(int index) -> std::uint8_t&
{
  switch (index)
  {
  case 0:
    return a_;
  case 1:
    return b_;
  case 2:
    return c_;
  default:
    return d_;
  }
}

auto Machine::flag(strella::isa::Cond condition) const -> bool
{
  switch (condition)
  {
  case strella::isa::Cond::Z:
    return z_;
  case strella::isa::Cond::S:
    return s_;
  case strella::isa::Cond::C:
    return cf_;
  case strella::isa::Cond::O:
    return o_;
  }
  return false;
}

void Machine::setZS(std::uint8_t value)
{
  z_ = value == 0;
  s_ = (value & 0x80) != 0;
}

auto Machine::add(std::uint8_t left, std::uint8_t right,
                  bool carry) -> std::uint8_t
{
  auto const carryIn = carry && cf_ ? 1 : 0;
  auto const sum = static_cast<int>(left) + static_cast<int>(right) + carryIn;
  auto const result = static_cast<std::uint8_t>(sum);
  z_ = result == 0;
  s_ = (result & 0x80) != 0;
  cf_ = sum > 0xFF;
  o_ = ((left ^ result) & (right ^ result) & 0x80) != 0;
  return result;
}

auto Machine::subtract(std::uint8_t left, std::uint8_t right,
                       bool borrow) -> std::uint8_t
{
  auto const borrowIn = borrow && cf_ ? 1 : 0;
  auto const difference
      = static_cast<int>(left) - static_cast<int>(right) - borrowIn;
  auto const result = static_cast<std::uint8_t>(difference);
  z_ = result == 0;
  s_ = (result & 0x80) != 0;
  cf_ = difference < 0;
  o_ = ((left ^ right) & (left ^ result) & 0x80) != 0;
  return result;
}

} // namespace testutil
