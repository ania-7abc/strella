#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "strella/compiler.hpp"
#include "strella/isa.hpp"

namespace testutil
{

using strella::CompilationResult;

auto build(std::string const& source) -> CompilationResult;

class Machine
{
public:
  explicit Machine(std::vector<std::uint8_t> image);

  void run(int maxSteps = 2000000);

  std::string terminal;
  std::vector<std::uint8_t> graphics;
  std::vector<std::uint8_t> indicator;
  std::vector<std::uint8_t> indicatorHigh;
  std::deque<std::uint8_t> input;
  std::uint8_t devices = 0;

  [[nodiscard]] auto displayByte(int index) const -> std::uint8_t;

private:
  auto fetch() -> std::uint8_t;
  [[nodiscard]] auto memIndex(std::uint8_t address) const -> int;
  auto read(std::uint8_t address) -> std::uint8_t;
  void write(std::uint8_t address, std::uint8_t value);
  [[nodiscard]] auto reg(int index) const -> std::uint8_t;
  auto regRef(int index) -> std::uint8_t&;
  [[nodiscard]] auto flag(strella::isa::Cond condition) const -> bool;
  void setZS(std::uint8_t value);
  auto add(std::uint8_t left, std::uint8_t right, bool carry) -> std::uint8_t;
  auto subtract(std::uint8_t left, std::uint8_t right, bool borrow)
      -> std::uint8_t;
  void execute(std::uint8_t opcode);

  std::vector<std::uint8_t> image_;
  std::uint8_t ip_ = 0;
  std::uint8_t a_ = 0;
  std::uint8_t b_ = 0;
  std::uint8_t c_ = 0;
  std::uint8_t d_ = 0;
  bool z_ = false;
  bool s_ = false;
  bool cf_ = false;
  bool o_ = false;
  int bank_ = 1;
  int steps_ = 0;
  bool halted_ = false;
  std::uint16_t randomState_ = 0xACE1;
};

} // namespace testutil
