#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/ast.hpp"
#include "strella/codegen.hpp"
#include "strella/isa.hpp"
#include "strella/token.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace strella
{
auto CodeGen::Impl::emitBinary(Expr& expression) -> void
{
  if (expression.op == TokenKind::LogicalAnd
      || expression.op == TokenKind::LogicalOr)
  {
    emitLogical(expression);
    return;
  }

  switch (expression.op)
  {
  case TokenKind::Equal:
  case TokenKind::NotEqual:
  case TokenKind::Less:
  case TokenKind::LessEqual:
  case TokenKind::Greater:
  case TokenKind::GreaterEqual:
    emitComparison(expression);
    return;

  case TokenKind::ShiftLeft:
  case TokenKind::ShiftRight:
  {
    auto const wide = expression.type.isWide();
    auto const signedRight = expression.lhs->type.isSigned();

    auto const leftTemp = allocTemp();
    emitExpr(*expression.lhs);
    emitExtendOperand(*expression.lhs, wide);
    emitStoreTemp(leftTemp, wide);

    auto const rightTemp = allocTemp();
    emitExpr(*expression.rhs);
    emitStoreTemp(rightTemp, false);

    emitLoadTemp(leftTemp, wide, Reg::A, Reg::B);
    emitLoadTemp(rightTemp, false, Reg::C, Reg::D);

    std::vector<std::uint8_t> bytes;
    bytes.push_back(0xBA); // test c
    bytes.push_back(0x08); // jz done
    bytes.push_back(0x00);

    if (expression.op == TokenKind::ShiftLeft)
    {
      bytes.push_back(0xE0); // shl a
      if (wide)
      {
        bytes.push_back(0xC5); // rcl b
      }
    }
    else
    {
      if (wide)
      {
        bytes.push_back(signedRight ? 0xE9 : 0xE5); // sar b / shr b
        bytes.push_back(0xD0);                      // rcr a
      }
      else
      {
        bytes.push_back(signedRight ? 0xE8 : 0xE4); // sar a / shr a
      }
    }

    bytes.push_back(0x7A); // dec c
    bytes.push_back(0x03); // jmp loop
    bytes.push_back(0x00);

    auto const end = static_cast<int>(bytes.size());
    emitRawBlock(std::move(bytes), {{2, end}, {end - 1, 0}});

    releaseTemp(rightTemp);
    releaseTemp(leftTemp);
    return;
  }

  default:
    break;
  }

  auto const wide = expression.type.isWide();
  auto const leftTemp = allocTemp();
  emitExpr(*expression.lhs);
  emitExtendOperand(*expression.lhs, wide);
  emitStoreTemp(leftTemp, wide);

  auto const rightTemp = allocTemp();
  emitExpr(*expression.rhs);
  emitExtendOperand(*expression.rhs, wide);
  emitStoreTemp(rightTemp, wide);

  emitLoadTemp(leftTemp, wide, Reg::A, Reg::B);
  emitLoadTemp(rightTemp, wide, Reg::C, Reg::D);

  switch (expression.op)
  {
  case TokenKind::Plus:
    emitOp(isa::add(Reg::A, Reg::C));
    if (wide)
    {
      emitOp(isa::adc(Reg::B, Reg::D));
    }
    break;
  case TokenKind::Minus:
    emitOp(isa::sub(Reg::A, Reg::C));
    if (wide)
    {
      emitOp(isa::sbb(Reg::B, Reg::D));
    }
    break;
  case TokenKind::Amp:
    emitOp(isa::bitAnd(Reg::A, Reg::C));
    if (wide)
    {
      emitOp(isa::bitAnd(Reg::B, Reg::D));
    }
    break;
  case TokenKind::Pipe:
    emitOp(isa::bitOr(Reg::A, Reg::C));
    if (wide)
    {
      emitOp(isa::bitOr(Reg::B, Reg::D));
    }
    break;
  case TokenKind::Caret:
    emitOp(isa::bitXor(Reg::A, Reg::C));
    if (wide)
    {
      emitOp(isa::bitXor(Reg::B, Reg::D));
    }
    break;
  default:
    break;
  }

  releaseTemp(rightTemp);
  releaseTemp(leftTemp);
}

auto CodeGen::Impl::emitComparison(Expr& expression) -> void
{
  auto const wide
      = expression.lhs->type.isWide() || expression.rhs->type.isWide();
  auto const signedComparison
      = expression.lhs->type.isSigned() || expression.rhs->type.isSigned();

  auto const leftTemp = allocTemp();
  emitExpr(*expression.lhs);
  emitExtendOperand(*expression.lhs, wide);
  emitStoreTemp(leftTemp, wide);

  auto const rightTemp = allocTemp();
  emitExpr(*expression.rhs);
  emitExtendOperand(*expression.rhs, wide);
  emitStoreTemp(rightTemp, wide);

  emitLoadTemp(leftTemp, wide, Reg::A, Reg::B);
  emitLoadTemp(rightTemp, wide, Reg::C, Reg::D);

  // `left > right` is evaluated as `right < left`, so normalise the operator
  // and swap the operands.
  auto op = expression.op;
  auto const swapped
      = op == TokenKind::Greater || op == TokenKind::GreaterEqual;
  if (op == TokenKind::Greater)
  {
    op = TokenKind::Less;
  }
  else if (op == TokenKind::GreaterEqual)
  {
    op = TokenKind::LessEqual;
  }

  if (!wide)
  {
    if (swapped)
    {
      emitOp(isa::cmp(Reg::C, Reg::A));
    }
    else
    {
      emitOp(isa::cmp(Reg::A, Reg::C));
    }

    emitSetBoolFromComparison(op, signedComparison);
    releaseTemp(rightTemp);
    releaseTemp(leftTemp);
    return;
  }

  // Wide comparison: compute the 16-bit difference and move it into A/B.
  // `mov` does not change flags, so the carry and overflow flags survive.
  if (swapped)
  {
    emitOp(isa::sub(Reg::C, Reg::A));
    emitOp(isa::sbb(Reg::D, Reg::B));
    emitOp(isa::mov(Reg::A, Reg::C));
    emitOp(isa::mov(Reg::B, Reg::D));
  }
  else
  {
    emitOp(isa::sub(Reg::A, Reg::C));
    emitOp(isa::sbb(Reg::B, Reg::D));
  }

  auto const doneLabel = newLabel();

  if (op == TokenKind::Equal || op == TokenKind::NotEqual)
  {
    // Word equality: Z = (low | high) == 0.
    emitOp(isa::mov(Reg::C, Reg::A));
    emitOp(isa::bitOr(Reg::C, Reg::B));
    emitOp(isa::test(Reg::C));

    emitBytes(isa::ldi(Reg::A, 1));
    emitCondJump(doneLabel, Cond::Z, op == TokenKind::NotEqual);
    emitOp(isa::clr(Reg::A));
    emitLabel(doneLabel);

    releaseTemp(rightTemp);
    releaseTemp(leftTemp);
    return;
  }

  // Relational comparison.  `test c` computes word equality in Z while
  // preserving the carry and overflow flags from `sbb`.
  emitOp(isa::mov(Reg::C, Reg::A));
  emitOp(isa::bitOr(Reg::C, Reg::B));
  emitOp(isa::test(Reg::C));

  auto const overflowLabel = newLabel();

  if (signedComparison)
  {
    // Save the sign of the high difference; `or`/`test` clobber S.
    emitOp(isa::mov(Reg::D, Reg::B));

    emitBytes(isa::ldi(Reg::A, 1));
    if (op == TokenKind::LessEqual)
    {
      emitCondJump(doneLabel, Cond::Z);
    }

    emitCondJump(overflowLabel, Cond::O);
    emitOp(isa::test(Reg::D));
    emitCondJump(doneLabel, Cond::S);
    emitOp(isa::clr(Reg::A));
    emitUncondJump(doneLabel);

    emitLabel(overflowLabel);
    emitOp(isa::test(Reg::D));
    emitCondJump(doneLabel, Cond::S, true); // jns
    emitOp(isa::clr(Reg::A));
  }
  else
  {
    emitBytes(isa::ldi(Reg::A, 1));
    emitCondJump(doneLabel, Cond::C);
    if (op == TokenKind::LessEqual)
    {
      emitCondJump(doneLabel, Cond::Z);
    }
    emitOp(isa::clr(Reg::A));
  }

  emitLabel(doneLabel);

  releaseTemp(rightTemp);
  releaseTemp(leftTemp);
}

auto CodeGen::Impl::emitLogical(Expr& expression) -> void
{
  auto const isAnd = expression.op == TokenKind::LogicalAnd;
  auto const resultTemp = allocTemp();
  auto const shortLabel = newLabel();
  auto const doneLabel = newLabel();

  emitExpr(*expression.lhs);
  if (expression.lhs->type.isWide())
  {
    emitOp(isa::mov(Reg::C, Reg::A));
    emitOp(isa::bitOr(Reg::C, Reg::B));
    emitOp(isa::test(Reg::C));
  }
  else
  {
    emitOp(isa::test(Reg::A));
  }

  emitCondJump(shortLabel, Cond::Z, !isAnd);

  emitExpr(*expression.rhs);
  if (expression.rhs->type.isWide())
  {
    emitOp(isa::mov(Reg::C, Reg::A));
    emitOp(isa::bitOr(Reg::C, Reg::B));
    emitOp(isa::test(Reg::C));
  }
  else
  {
    emitOp(isa::test(Reg::A));
  }

  emitCondJump(shortLabel, Cond::Z, !isAnd);

  emitBytes(isa::ldi(Reg::A, isAnd ? 1 : 0));
  emitStoreTemp(resultTemp, false);
  emitUncondJump(doneLabel);

  emitLabel(shortLabel);
  emitBytes(isa::ldi(Reg::A, isAnd ? 0 : 1));
  emitStoreTemp(resultTemp, false);

  emitLabel(doneLabel);
  emitLoadTemp(resultTemp, false, Reg::A, Reg::B);
  releaseTemp(resultTemp);
}

auto CodeGen::Impl::emitTernary(Expr& expression) -> void
{
  auto const wide = expression.type.isWide();
  auto const resultTemp = allocTemp();
  auto const elseLabel = newLabel();
  auto const doneLabel = newLabel();

  emitCondition(*expression.lhs);
  emitCondJump(elseLabel, Cond::Z);

  emitExpr(*expression.rhs);
  emitExtendOperand(*expression.rhs, wide);
  emitStoreTemp(resultTemp, wide);
  emitUncondJump(doneLabel);

  emitLabel(elseLabel);
  emitExpr(*expression.third);
  emitExtendOperand(*expression.third, wide);
  emitStoreTemp(resultTemp, wide);

  emitLabel(doneLabel);
  emitLoadTemp(resultTemp, wide, Reg::A, Reg::B);
  releaseTemp(resultTemp);
}

auto CodeGen::Impl::emitAssign(Expr& expression) -> void
{
  if (expression.type.isStruct())
  {
    auto const destTemp = allocTemp();
    emitAddressOf(*expression.lhs);
    emitStoreTemp(destTemp, true);
    emitCopyBytesFromExpr(destTemp, 0, *expression.rhs, expression.type.size());
    releaseTemp(destTemp);
    return;
  }

  auto const wide = expression.type.isWide();
  emitExpr(*expression.rhs);
  emitExtendOperand(*expression.rhs, wide);

  if (expression.lhs->kind == ExprKind::FieldAccess
      && expression.lhs->lhs != nullptr && expression.lhs->lhs->type.isStruct())
  {
    auto const info = expression.lhs->lhs->type.structure();
    if (info != nullptr && expression.lhs->fieldIndex >= 0
        && expression.lhs->fieldIndex < static_cast<int>(info->fields.size()))
    {
      auto const index = static_cast<std::size_t>(expression.lhs->fieldIndex);
      auto const offset = info->fields[index].offset;
      if (offset <= 255
          && emitDirectFieldAddress(*expression.lhs, offset, Reg::C, true))
      {
        emitOp(isa::stReg(Reg::C, Reg::A));
        if (wide)
        {
          emitOp(isa::inc(Reg::C));
          emitOp(isa::stReg(Reg::C, Reg::B));
        }
        return;
      }
    }
  }

  if (expression.lhs->kind == ExprKind::Identifier
      && expression.lhs->symbol != nullptr)
  {
    emitStoreSymbol(*expression.lhs->symbol, wide);
    return;
  }

  emitStoreToLValue(*expression.lhs, wide);
}
} // namespace strella
