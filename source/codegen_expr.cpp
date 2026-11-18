#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/ast.hpp"
#include "strella/codegen.hpp"
#include "strella/isa.hpp"
#include "strella/token.hpp"
#include <cstdint>

namespace strella
{
auto CodeGen::Impl::emitExpr(Expr& expression) -> void
{
  if (expression.isConstant)
  {
    emitLoadImm(expression.constantValue, expression.type.isWide());
    return;
  }

  switch (expression.kind)
  {
  case ExprKind::IntLiteral:
    emitLoadImm(expression.intValue, expression.type.isWide());
    break;

  case ExprKind::ArrayLiteral:
    break;

  case ExprKind::StructLiteral:
  {
    auto const size = expression.type.size();
    auto const slots = (size + 1) / 2;
    auto const storage = allocTemp();
    for (int i = 1; i < slots; ++i)
    {
      allocTemp();
    }
    auto const pointer = allocTemp();
    emitTempAddress(storage);
    emitStoreTemp(pointer, true);
    emitBuildStructLiteral(expression, pointer);
    releaseTemp(pointer);
    for (int i = slots - 1; i >= 0; --i)
    {
      releaseTemp(storage + i);
    }
    break;
  }

  case ExprKind::FieldAccess:
    emitAddressOf(expression);
    if (!expression.type.isStruct())
    {
      emitLoadIndirect(expression.type.isWide());
    }
    break;

  case ExprKind::StringLiteral:
  {
    auto const address = sema_.stringAddress(expression.stringValue);
    emitLoadImm(static_cast<std::uint32_t>(address), true);
    break;
  }

  case ExprKind::Identifier:
    if (expression.type.isStruct())
    {
      emitAddressOf(expression);
    }
    else if (expression.symbol != nullptr)
    {
      emitLoadSymbol(*expression.symbol, expression.type.isWide());
    }
    else
    {
      emitLoadImm(expression.intValue, expression.type.isWide());
    }
    break;

  case ExprKind::Unary:
    emitUnary(expression);
    break;

  case ExprKind::Index:
    emitAddressOf(expression);
    if (!expression.type.isStruct())
    {
      emitLoadIndirect(expression.type.isWide());
    }
    break;

  case ExprKind::Cast:
    emitCast(expression);
    break;

  case ExprKind::IncDec:
    emitIncDec(expression);
    break;

  case ExprKind::Binary:
    emitBinary(expression);
    break;

  case ExprKind::Ternary:
    emitTernary(expression);
    break;

  case ExprKind::Assign:
    emitAssign(expression);
    break;

  case ExprKind::Call:
    if (expression.builtin)
    {
      emitBuiltinCall(expression);
    }
    else
    {
      emitUserCall(expression);
    }
    break;
  }
}

auto CodeGen::Impl::emitUnary(Expr& expression) -> void
{
  auto const wide = expression.lhs->type.isWide();

  switch (expression.op)
  {
  case TokenKind::Bang:
    emitExpr(*expression.lhs);
    if (wide)
    {
      emitOp(isa::mov(Reg::C, Reg::A));
      emitOp(isa::bitOr(Reg::C, Reg::B));
      emitOp(isa::test(Reg::C));
    }
    else
    {
      emitOp(isa::test(Reg::A));
    }
    emitSetBoolFromZero();
    break;

  case TokenKind::Amp:
    if (expression.lhs->type.isArray())
    {
      // An array already evaluates to the address of its first element.
      emitExpr(*expression.lhs);
    }
    else
    {
      emitAddressOf(*expression.lhs);
    }
    break;

  case TokenKind::Star:
    emitExpr(*expression.lhs);
    if (!expression.type.isStruct())
    {
      emitLoadIndirect(expression.type.isWide());
    }
    break;

  case TokenKind::Tilde:
    emitExpr(*expression.lhs);
    emitOp(isa::bitNot(Reg::A));
    if (wide)
    {
      emitOp(isa::bitNot(Reg::B));
    }
    break;

  case TokenKind::Minus:
    emitExpr(*expression.lhs);
    if (wide)
    {
      emitOp(isa::bitNot(Reg::A));
      emitOp(isa::bitNot(Reg::B));
      emitBytes(isa::ldi(Reg::C, 1));
      emitBytes(isa::ldi(Reg::D, 0));
      emitOp(isa::add(Reg::A, Reg::C));
      emitOp(isa::adc(Reg::B, Reg::D));
    }
    else
    {
      emitOp(isa::neg(Reg::A));
    }
    break;

  case TokenKind::Plus:
  default:
    emitExpr(*expression.lhs);
    break;
  }
}

auto CodeGen::Impl::emitCast(Expr& expression) -> void
{
  emitExpr(*expression.lhs);

  if (expression.type.isBool())
  {
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
    emitSetBoolFromNonzero();
    return;
  }

  if (expression.type.isWide())
  {
    emitExtendOperand(*expression.lhs, true);
  }
}

auto CodeGen::Impl::emitIncDec(Expr& expression) -> void
{
  auto const wide = expression.type.isWide();

  auto const addressTemp = allocTemp();
  emitAddressOf(*expression.lhs);
  emitStoreTemp(addressTemp, true);

  emitLoadTemp(addressTemp, true, Reg::A, Reg::B);
  emitLoadIndirect(wide);

  int oldTemp = -1;
  if (!expression.prefix)
  {
    oldTemp = allocTemp();
    emitStoreTemp(oldTemp, wide);
  }

  if (wide)
  {
    emitBytes(isa::ldi(Reg::C, 1));
    emitBytes(isa::ldi(Reg::D, 0));
    if (expression.op == TokenKind::Plus)
    {
      emitOp(isa::add(Reg::A, Reg::C));
      emitOp(isa::adc(Reg::B, Reg::D));
    }
    else
    {
      emitOp(isa::sub(Reg::A, Reg::C));
      emitOp(isa::sbb(Reg::B, Reg::D));
    }
  }
  else if (expression.op == TokenKind::Plus)
  {
    emitOp(isa::inc(Reg::A));
  }
  else
  {
    emitOp(isa::dec(Reg::A));
  }

  auto const newTemp = allocTemp();
  emitStoreTemp(newTemp, wide);

  emitLoadTemp(newTemp, wide, Reg::A, Reg::B);
  emitLoadTemp(addressTemp, true, Reg::C, Reg::D);
  emitOp(isa::stReg(Reg::C, Reg::A));
  if (wide)
  {
    emitOp(isa::inc(Reg::C));
    emitOp(isa::stReg(Reg::C, Reg::B));
  }

  emitLoadTemp(expression.prefix ? newTemp : oldTemp, wide, Reg::A, Reg::B);

  releaseTemp(newTemp);
  if (oldTemp >= 0)
  {
    releaseTemp(oldTemp);
  }
  releaseTemp(addressTemp);
}
} // namespace strella
