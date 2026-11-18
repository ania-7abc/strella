#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/ast.hpp"
#include "strella/codegen.hpp"
#include "strella/diagnostics.hpp"
#include "strella/isa.hpp"
#include "strella/token.hpp"
#include "strella/type.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace strella
{
auto CodeGen::Impl::emitFrameAddress(int offset, Reg addressReg,
                                     bool preserveAB) -> void
{
  emitBytes(isa::ldImm(addressReg, 0x10));
  if (offset == 0)
  {
    return;
  }

  if (offset > 255 || offset < -255)
  {
    fail(file_, currentFunction_->location, "stack frame is too large");
  }

  auto const magnitude
      = static_cast<std::uint8_t>(offset > 0 ? offset : -offset);
  if (preserveAB)
  {
    emitBytes(isa::ldi(Reg::D, magnitude));
    if (offset > 0)
    {
      emitOp(isa::add(addressReg, Reg::D));
    }
    else
    {
      emitOp(isa::sub(addressReg, Reg::D));
    }
  }
  else
  {
    emitBytes(isa::ldi(Reg::A, magnitude));
    if (offset > 0)
    {
      emitOp(isa::add(addressReg, Reg::A));
    }
    else
    {
      emitOp(isa::sub(addressReg, Reg::A));
    }
  }
}

auto CodeGen::Impl::emitLoadSymbol(Symbol const& symbol, bool word) -> void
{
  auto const inlineTemp = inlineSlotFor(symbol);
  if (inlineTemp >= 0)
  {
    emitLoadTemp(inlineTemp, word, Reg::A, Reg::B);
    return;
  }

  switch (symbol.storage)
  {
  case Storage::Constant:
    emitLoadImm(symbol.constantValue, word);
    break;

  case Storage::Global:
  case Storage::Local:
  case Storage::Parameter:
    emitBytes(isa::ldImm(Reg::A, static_cast<std::uint8_t>(symbol.address)));
    if (word)
    {
      emitBytes(
          isa::ldImm(Reg::B, static_cast<std::uint8_t>(symbol.address + 1)));
    }
    break;

  case Storage::StackParameter:
  case Storage::StackLocal:
  {
    emitFrameAddress(stackFrameOffset(symbol), Reg::C, false);
    emitOp(isa::ldReg(Reg::C, Reg::A));
    if (word)
    {
      emitOp(isa::inc(Reg::C));
      emitOp(isa::ldReg(Reg::C, Reg::B));
      emitOp(isa::dec(Reg::C));
    }
    break;
  }
  }
}

auto CodeGen::Impl::emitStoreSymbol(Symbol const& symbol, bool word) -> void
{
  auto const inlineTemp = inlineSlotFor(symbol);
  if (inlineTemp >= 0)
  {
    emitStoreTemp(inlineTemp, word);
    return;
  }

  switch (symbol.storage)
  {
  case Storage::Constant:
    // Assignments to constants are rejected by semantic analysis.
    break;

  case Storage::Global:
  case Storage::Local:
  case Storage::Parameter:
    emitBytes(isa::stImm(Reg::A, static_cast<std::uint8_t>(symbol.address)));
    if (word)
    {
      emitBytes(
          isa::stImm(Reg::B, static_cast<std::uint8_t>(symbol.address + 1)));
    }
    break;

  case Storage::StackParameter:
  case Storage::StackLocal:
    emitFrameAddress(stackFrameOffset(symbol), Reg::C, true);
    emitOp(isa::stReg(Reg::C, Reg::A));
    if (word)
    {
      emitOp(isa::inc(Reg::C));
      emitOp(isa::stReg(Reg::C, Reg::B));
      emitOp(isa::dec(Reg::C));
    }
    break;
  }
}

auto CodeGen::Impl::emitExtendOperand(Expr const& operand, bool wide) -> void
{
  if (!wide || operand.type.isWide())
  {
    return;
  }

  if (!operand.type.isSigned())
  {
    emitOp(isa::clr(Reg::B));
    return;
  }

  // Sign-extend the byte in A into the word in A/B.
  auto const doneLabel = newLabel();
  emitBytes(isa::ldi(Reg::B, 0x00));
  emitOp(isa::test(Reg::A));
  emitCondJump(doneLabel, Cond::S, true); // jns done
  emitBytes(isa::ldi(Reg::B, 0xFF));
  emitLabel(doneLabel);
}

auto CodeGen::Impl::emitLoadTemp(int index, bool word, Reg low,
                                 Reg high) -> void
{
  if (!current_->stackBased)
  {
    auto const address = tempAddress(index);
    emitBytes(isa::ldImm(low, static_cast<std::uint8_t>(address)));
    if (word)
    {
      emitBytes(isa::ldImm(high, static_cast<std::uint8_t>(address + 1)));
    }
    return;
  }

  auto const offset = currentFunction_->stackFrameSize + (index * 2);

  if (low == Reg::A && high == Reg::B)
  {
    // Use D for the frame address: callers may already hold a destination
    // address in C while loading a value back from its temporary.
    emitFrameAddress(offset, Reg::D, false);
    emitOp(isa::ldReg(Reg::D, Reg::A));
    if (word)
    {
      emitOp(isa::inc(Reg::D));
      emitOp(isa::ldReg(Reg::D, Reg::B));
      emitOp(isa::dec(Reg::D));
    }
    return;
  }

  // D holds the frame address and C the offset.  This preserves A/B for the
  // common (C, D) loads; struct copying asks for A explicitly, so load the low
  // byte directly into the requested register.
  emitBytes(isa::ldImm(Reg::D, 0x10));
  if (offset != 0)
  {
    if (offset > 255 || offset < -255)
    {
      fail(file_, currentFunction_->location, "stack frame is too large");
    }
    auto const magnitude
        = static_cast<std::uint8_t>(offset > 0 ? offset : -offset);
    emitBytes(isa::ldi(Reg::C, magnitude));
    if (offset > 0)
    {
      emitOp(isa::add(Reg::D, Reg::C));
    }
    else
    {
      emitOp(isa::sub(Reg::D, Reg::C));
    }
  }
  emitOp(isa::ldReg(Reg::D, low));
  if (word)
  {
    emitOp(isa::inc(Reg::D));
    emitOp(isa::ldReg(Reg::D, high));
  }
}

auto CodeGen::Impl::emitStoreTemp(int index, bool word) -> void
{
  if (!current_->stackBased)
  {
    auto const address = tempAddress(index);
    emitBytes(isa::stImm(Reg::A, static_cast<std::uint8_t>(address)));
    if (word)
    {
      emitBytes(isa::stImm(Reg::B, static_cast<std::uint8_t>(address + 1)));
    }
    return;
  }

  emitFrameAddress(currentFunction_->stackFrameSize + (index * 2), Reg::C,
                   true);
  emitOp(isa::stReg(Reg::C, Reg::A));
  if (word)
  {
    emitOp(isa::inc(Reg::C));
    emitOp(isa::stReg(Reg::C, Reg::B));
    emitOp(isa::dec(Reg::C));
  }
}

auto CodeGen::Impl::emitTempAddress(int index) -> void
{
  if (!current_->stackBased)
  {
    auto const address = tempAddress(index);
    emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(address)));
    emitBytes(isa::ldi(Reg::B, 0));
    return;
  }

  emitFrameAddress(currentFunction_->stackFrameSize + (index * 2), Reg::C,
                   false);
  emitOp(isa::mov(Reg::A, Reg::C));
  emitBytes(isa::ldi(Reg::B, 0));
}

auto CodeGen::Impl::emitDirectFieldAddress(Expr& expression, int offset,
                                           Reg addressReg,
                                           bool preserveAB) -> bool
{
  if (expression.kind != ExprKind::FieldAccess
      || expression.lhs->kind != ExprKind::Identifier
      || expression.lhs->symbol == nullptr || expression.op == TokenKind::Arrow
      || expression.lhs->type.isPointer()
      || inlineSlotFor(*expression.lhs->symbol) >= 0)
  {
    return false;
  }

  auto const& symbol = *expression.lhs->symbol;
  switch (symbol.storage)
  {
  case Storage::Global:
  case Storage::Local:
  case Storage::Parameter:
    if (symbol.address + offset > 255)
    {
      fail(file_, expression.location, "struct field address is too large");
    }
    emitBytes(isa::ldi(addressReg,
                       static_cast<std::uint8_t>(symbol.address + offset)));
    return true;

  case Storage::StackParameter:
  case Storage::StackLocal:
    emitFrameAddress(stackFrameOffset(symbol) + offset, addressReg, preserveAB);
    return true;

  case Storage::Constant:
    return false;
  }
  return false;
}

auto CodeGen::Impl::emitAddressOf(Expr& expression) -> void
{
  switch (expression.kind)
  {
  case ExprKind::Identifier:
  {
    auto const& symbol = *expression.symbol;
    auto const inlineTemp = inlineSlotFor(symbol);
    if (inlineTemp >= 0)
    {
      emitTempAddress(inlineTemp);
      return;
    }

    switch (symbol.storage)
    {
    case Storage::Constant:
      fail(file_, expression.location, "cannot take the address of a constant");
      break;

    case Storage::Global:
    case Storage::Local:
    case Storage::Parameter:
      emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(symbol.address)));
      emitBytes(isa::ldi(Reg::B, 0));
      break;

    case Storage::StackParameter:
    case Storage::StackLocal:
      emitFrameAddress(stackFrameOffset(symbol), Reg::C, false);
      emitOp(isa::mov(Reg::A, Reg::C));
      emitBytes(isa::ldi(Reg::B, 0));
      break;
    }
    return;
  }

  case ExprKind::Index:
  {
    auto const elemSize = expression.lhs->type.hasPointee()
                              ? expression.lhs->type.pointee().size()
                              : 1;
    auto const baseTemp = allocTemp();
    emitExpr(*expression.lhs);
    emitStoreTemp(baseTemp, true);

    emitExpr(*expression.rhs);
    emitExtendOperand(*expression.rhs, true);
    auto const indexTemp = allocTemp();
    emitStoreTemp(indexTemp, true);

    auto const scaledTemp = allocTemp();
    emitLoadImm(0, true);
    emitStoreTemp(scaledTemp, true);
    for (int bit = 0; bit < 8; ++bit)
    {
      if ((elemSize & (1 << bit)) == 0)
      {
        continue;
      }
      emitLoadTemp(indexTemp, true, Reg::A, Reg::B);
      for (int shift = 0; shift < bit; ++shift)
      {
        emitOp(isa::shl(Reg::A));
        emitOp(0xC5); // rcl b
      }
      emitLoadTemp(scaledTemp, true, Reg::C, Reg::D);
      emitOp(isa::add(Reg::A, Reg::C));
      emitOp(isa::adc(Reg::B, Reg::D));
      emitStoreTemp(scaledTemp, true);
    }

    emitLoadTemp(baseTemp, true, Reg::A, Reg::B);
    emitLoadTemp(scaledTemp, true, Reg::C, Reg::D);
    emitOp(isa::add(Reg::A, Reg::C));
    emitOp(isa::adc(Reg::B, Reg::D));
    releaseTemp(scaledTemp);
    releaseTemp(indexTemp);
    releaseTemp(baseTemp);
    return;
  }

  case ExprKind::FieldAccess:
  {
    StructInfo const* info = nullptr;
    if (expression.lhs->type.isStruct())
    {
      info = expression.lhs->type.structure().get();
    }
    else if (expression.lhs->type.hasPointee())
    {
      info = expression.lhs->type.pointee().structure().get();
    }
    if (info == nullptr || expression.fieldIndex < 0
        || expression.fieldIndex >= static_cast<int>(info->fields.size()))
    {
      fail(file_, expression.location,
           "internal error: unresolved struct field");
    }

    auto const index = static_cast<std::size_t>(expression.fieldIndex);
    auto const offset = info->fields[index].offset;
    if (offset > 255)
    {
      fail(file_, expression.location, "struct field offset is too large");
    }

    // A field of a named struct object can be addressed directly.  The generic
    // path below needs a pointer temporary, which would make simple field
    // assignments exhaust the small stack frames of @stack functions.
    if (emitDirectFieldAddress(expression, offset, Reg::C, false))
    {
      emitOp(isa::mov(Reg::A, Reg::C));
      emitBytes(isa::ldi(Reg::B, 0));
      return;
    }

    auto const baseTemp = allocTemp();
    if (expression.op == TokenKind::Arrow || expression.lhs->type.isPointer())
    {
      emitExpr(*expression.lhs);
    }
    else
    {
      emitAddressOf(*expression.lhs);
    }
    emitStoreTemp(baseTemp, true);

    emitLoadTemp(baseTemp, true, Reg::A, Reg::B);
    if (offset != 0)
    {
      emitBytes(isa::ldi(Reg::C, static_cast<std::uint8_t>(offset)));
      emitOp(isa::add(Reg::A, Reg::C));
      emitBytes(isa::ldi(Reg::C, 0));
      emitOp(isa::adc(Reg::B, Reg::C));
    }
    releaseTemp(baseTemp);
    return;
  }

  case ExprKind::Unary:
    if (expression.op == TokenKind::Star)
    {
      emitExpr(*expression.lhs);
      return;
    }
    break;

  default:
    break;
  }

  fail(file_, expression.location,
       "internal error: expression is not an lvalue");
}

auto CodeGen::Impl::emitLoadIndirect(bool word) -> void
{
  emitOp(isa::mov(Reg::C, Reg::A));
  emitOp(isa::ldReg(Reg::C, Reg::A));
  if (word)
  {
    emitOp(isa::inc(Reg::C));
    emitOp(isa::ldReg(Reg::C, Reg::B));
  }
}

auto CodeGen::Impl::emitStoreToLValue(Expr& expression, bool word) -> void
{
  // The value is kept in a temporary while the destination address is
  // computed.  The address itself needs no second temporary: emitAddressOf()
  // leaves it in A/B, and B (the high byte) is always zero for common-memory
  // addresses.  Moving A into C frees A/B for the value again.
  auto const valueTemp = allocTemp();
  emitStoreTemp(valueTemp, word);

  emitAddressOf(expression);
  emitOp(isa::mov(Reg::C, Reg::A));

  emitLoadTemp(valueTemp, word, Reg::A, Reg::B);
  emitOp(isa::stReg(Reg::C, Reg::A));
  if (word)
  {
    emitOp(isa::inc(Reg::C));
    emitOp(isa::stReg(Reg::C, Reg::B));
  }

  emitLoadTemp(valueTemp, word, Reg::A, Reg::B);
  releaseTemp(valueTemp);
}

auto CodeGen::Impl::emitCondition(Expr& expression) -> void
{
  emitExpr(expression);
  if (expression.type.isWide())
  {
    emitOp(isa::mov(Reg::C, Reg::A));
    emitOp(isa::bitOr(Reg::C, Reg::B));
    emitOp(isa::test(Reg::C));
  }
  else
  {
    emitOp(isa::test(Reg::A));
  }
}

auto CodeGen::Impl::emitSetBoolFromZero() -> void
{
  // Flags are set so that Z means "false".  The block sets A = 1 when Z is
  // set and A = 0 otherwise.
  emitRawBlock(
      {
          static_cast<std::uint8_t>(0x54),
          0x01, // ldi a, 1
          static_cast<std::uint8_t>(0x08),
          0x00,                            // jz end
          static_cast<std::uint8_t>(0xA0), // clr a
      },
      {{3, 5}});
}

auto CodeGen::Impl::emitSetBoolFromNonzero() -> void
{
  // Flags are set so that Z means "false".  Produce 1 when Z is clear.
  emitRawBlock(
      {
          static_cast<std::uint8_t>(0xA0), // clr a
          static_cast<std::uint8_t>(0x08),
          0x00, // jz end
          static_cast<std::uint8_t>(0x54),
          0x01, // ldi a, 1
      },
      {{2, 5}});
}

auto CodeGen::Impl::emitSetBoolFromComparison(TokenKind op,
                                              bool signedComparison) -> void
{
  std::vector<std::uint8_t> bytes {static_cast<std::uint8_t>(0x54),
                                   0x01}; // ldi a, 1
  std::vector<std::pair<int, int>> patches;

  auto addJump = [&](std::uint8_t opcode) -> void
  {
    auto const operand = static_cast<int>(bytes.size()) + 1;
    bytes.push_back(opcode);
    bytes.push_back(0);
    patches.emplace_back(operand, 0);
  };

  switch (op)
  {
  case TokenKind::Equal:
    addJump(0x08); // jz
    break;
  case TokenKind::NotEqual:
    addJump(0x0C); // jnz
    break;
  case TokenKind::Less:
    addJump(signedComparison ? 0x09 : 0x0A); // js / jc
    break;
  case TokenKind::LessEqual:
    addJump(0x08);                           // jz
    addJump(signedComparison ? 0x09 : 0x0A); // js / jc
    break;
  default:
    break;
  }

  auto const target = static_cast<int>(bytes.size()) + 1; // skip clr a
  bytes.push_back(0xA0);                                  // clr a
  for (auto& patch : patches)
  {
    patch.second = target;
  }

  emitRawBlock(std::move(bytes), std::move(patches));
}
} // namespace strella
