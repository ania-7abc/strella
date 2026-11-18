#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/ast.hpp"
#include "strella/codegen.hpp"
#include "strella/diagnostics.hpp"
#include "strella/isa.hpp"
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace strella
{
auto CodeGen::Impl::emitUserCall(Expr& expression) -> void
{
  auto* callee = expression.function;
  if (callee == nullptr)
  {
    return;
  }

  if (callee->annotation == FunctionAnnotation::Inline)
  {
    emitInlineCall(expression);
  }
  else if (callee->annotation == FunctionAnnotation::Stack)
  {
    emitStackCallSequence(callee, expression.arguments);
  }
  else
  {
    for (std::size_t i = 0; i < expression.arguments.size(); ++i)
    {
      auto& argument = *expression.arguments[i];
      auto& parameter = callee->parameters[i];
      if (parameter.type.isStruct())
      {
        emitCopyToSymbol(*parameter.symbol, argument, parameter.type.size());
        continue;
      }
      auto const wide = parameter.type.isWide();

      emitExpr(argument);
      emitExtendOperand(argument, wide);
      emitStoreSymbol(*parameter.symbol, wide);
    }

    emitStaticCall(callee, 0);
  }
}

auto CodeGen::Impl::inlineSlotFor(Symbol const& symbol) const -> int
{
  for (auto it = inlineContexts_.rbegin(); it != inlineContexts_.rend(); ++it)
  {
    auto const found = it->slots.find(&symbol);
    if (found != it->slots.end())
    {
      return found->second.temp;
    }
  }
  return -1;
}

auto CodeGen::Impl::emitInlineCall(Expr& expression) -> void
{
  auto* callee = expression.function;
  if (callee == nullptr)
  {
    return;
  }

  auto const savedTempTop = tempTop_;
  auto const wide = callee->returnType.isWide();

  InlineContext context;
  context.function = callee;
  context.returnsValue = !callee->returnType.isVoid();
  context.endLabel = newLabel();

  for (auto& parameter : callee->parameters)
  {
    if (parameter.symbol != nullptr)
    {
      auto const slots = (parameter.type.size() + 1) / 2;
      auto const first = allocTemp();
      for (int slot = 1; slot < slots; ++slot)
      {
        allocTemp();
      }
      context.slots.emplace(parameter.symbol, InlineSlot {first, slots});
    }
  }

  std::vector<Symbol const*> locals;
  if (callee->body)
  {
    collectLocalSymbols(*callee->body, locals);
  }
  for (auto const* symbol : locals)
  {
    auto const count = inlineSlotCount(*symbol);
    auto const temp = allocTemp();
    for (int i = 1; i < count; ++i)
    {
      allocTemp();
    }
    context.slots.emplace(symbol, InlineSlot {temp, count});
  }

  if (context.returnsValue)
  {
    context.resultTemp = allocTemp();
  }

  for (std::size_t i = 0; i < expression.arguments.size(); ++i)
  {
    auto& argument = *expression.arguments[i];
    auto& parameter = callee->parameters[i];

    auto const found = context.slots.find(parameter.symbol);
    if (parameter.type.isStruct())
    {
      if (found != context.slots.end())
      {
        auto const destTemp = found->second.temp;
        emitExpr(argument);
        emitCopyBytesFromAddress(destTemp, parameter.type.size());
        emitLoadTemp(destTemp, true, Reg::A, Reg::B);
      }
      continue;
    }

    auto const parameterWide = parameter.type.isWide();
    emitExpr(argument);
    emitExtendOperand(argument, parameterWide);
    if (found != context.slots.end())
    {
      emitStoreTemp(found->second.temp, parameterWide);
    }
  }

  // A function that falls off the end returns zero, like its static epilogue.
  if (context.returnsValue)
  {
    emitBytes(isa::ldi(Reg::A, 0));
    if (wide)
    {
      emitBytes(isa::ldi(Reg::B, 0));
    }
    emitStoreTemp(context.resultTemp, wide);
  }

  inlineContexts_.push_back(std::move(context));
  if (callee->body)
  {
    emitStatement(*callee->body);
  }

  auto const endLabel = inlineContexts_.back().endLabel;
  auto const resultTemp = inlineContexts_.back().resultTemp;
  auto const returnsValue = inlineContexts_.back().returnsValue;
  emitLabel(endLabel);
  if (returnsValue)
  {
    emitLoadTemp(resultTemp, wide, Reg::A, Reg::B);
  }
  inlineContexts_.pop_back();

  // The parameters, locals and result of the expansion live in the caller's
  // temporaries; release them once the inlined body has been emitted.
  tempTop_ = savedTempTop;
}

auto CodeGen::Impl::emitStackCallSequence(
    FunctionDecl* callee, std::vector<ExprPtr>& arguments) -> void
{
  for (std::size_t i = 0; i < arguments.size(); ++i)
  {
    auto& argument = *arguments[i];
    auto& parameter = callee->parameters[i];
    if (parameter.type.isStruct())
    {
      emitCopyToStackPointer(argument, parameter.type.size());
      continue;
    }
    auto const wide = parameter.type.isWide();

    emitExpr(argument);
    emitExtendOperand(argument, wide);

    emitBytes(isa::ldImm(Reg::C, 0x0F));
    emitOp(isa::stReg(Reg::C, Reg::A));
    emitOp(isa::inc(Reg::C));
    if (wide)
    {
      emitOp(isa::stReg(Reg::C, Reg::B));
      emitOp(isa::inc(Reg::C));
    }
    emitBytes(isa::stImm(Reg::C, 0x0F));
  }

  emitStackCall(callee);
}

auto CodeGen::Impl::emitStackReturn(bool hasValue) -> void
{
  if (hasValue)
  {
    emitBytes(isa::stImm(Reg::A, kReturnLow));
    emitBytes(isa::stImm(Reg::B, kReturnHigh));
  }

  // Frame layout at this point: FP-3 = return bank, FP-2 = return address,
  // FP-1 = saved caller frame pointer, then the parameters below that.
  emitBytes(isa::ldImm(Reg::C, 0x10));
  emitOp(isa::dec(Reg::C));
  emitOp(isa::dec(Reg::C));
  emitOp(isa::dec(Reg::C));
  emitOp(isa::ldReg(Reg::C, Reg::C));

  emitBytes(isa::ldImm(Reg::D, 0x10));
  emitOp(isa::dec(Reg::D));
  emitOp(isa::dec(Reg::D));
  emitOp(isa::ldReg(Reg::D, Reg::D));

  // SP = FP - 3 - parameterBytes.  Compute this before restoring FP.
  auto const adjust = 3 + currentFunction_->stackParamBytes;
  if (adjust > 255)
  {
    fail(file_, currentFunction_->location, "too many @stack parameters");
  }

  emitBytes(isa::ldImm(Reg::A, 0x10));
  emitBytes(isa::ldi(Reg::B, static_cast<std::uint8_t>(adjust)));
  emitOp(isa::sub(Reg::A, Reg::B));
  emitBytes(isa::stImm(Reg::A, 0x0F));

  // Restore the caller's frame pointer from FP-1.
  emitBytes(isa::ldImm(Reg::A, 0x10));
  emitOp(isa::dec(Reg::A));
  emitOp(isa::ldReg(Reg::A, Reg::A));
  emitBytes(isa::stImm(Reg::A, 0x10));

  if (hasValue)
  {
    emitBytes(isa::ldImm(Reg::A, kReturnLow));
    emitBytes(isa::ldImm(Reg::B, kReturnHigh));
  }

  emitBytes(isa::jmpImm(0x05));
}

auto CodeGen::Impl::emitStoreReturnValue() -> void
{
  emitBytes(isa::stImm(Reg::A, kReturnLow));
  emitBytes(isa::stImm(Reg::B, kReturnHigh));
}

auto CodeGen::Impl::emitBuiltinCall(Expr& expression) -> void
{
  auto const name = expression.name;

  if (name == "puts")
  {
    // Keep the pointer in B and walk the NUL-terminated string.  A cross-bank
    // jump uses C and D for its trampoline, so C cannot hold the pointer.
    emitExpr(*expression.arguments[0]);
    emitOp(isa::mov(Reg::B, Reg::A));

    auto const loopLabel = newLabel();
    auto const doneLabel = newLabel();
    emitLabel(loopLabel);
    emitOp(isa::ldReg(Reg::B, Reg::A));
    emitOp(isa::test(Reg::A));
    emitCondJump(doneLabel, Cond::Z);
    emitBytes(isa::stImm(Reg::A, 0x3C));
    emitOp(isa::inc(Reg::B));
    emitUncondJump(loopLabel);
    emitLabel(doneLabel);
    return;
  }

  if (name == "put")
  {
    auto const port = expression.arguments.size() == 1 ? 0x3C : 0x3D;
    for (auto& argument : expression.arguments)
    {
      emitExpr(*argument);
      emitBytes(isa::stImm(Reg::A, static_cast<std::uint8_t>(port)));
    }
    return;
  }

  if (name == "draw")
  {
    // The first argument is an offset into the display memory; the second
    // argument is written there unchanged.
    emitExpr(*expression.arguments[0]);
    auto const indexTemp = allocTemp();
    emitStoreTemp(indexTemp, false);

    emitExpr(*expression.arguments[1]);
    emitLoadTemp(indexTemp, false, Reg::C, Reg::D);
    emitBytes(isa::ldi(Reg::D, 0x40));
    emitOp(isa::add(Reg::C, Reg::D));
    emitOp(isa::stReg(Reg::C, Reg::A));

    releaseTemp(indexTemp);
    return;
  }

  if (name == "indicator")
  {
    emitExpr(*expression.arguments[0]);
    emitExtendOperand(*expression.arguments[0], true);
    emitBytes(isa::stImm(Reg::B, 0x3B));
    emitBytes(isa::stImm(Reg::A, 0x3A));
    return;
  }

  if (name == "getchar")
  {
    emitBytes(isa::ldImm(Reg::A, 0x3E));
    return;
  }

  if (name == "set_devices")
  {
    emitExpr(*expression.arguments[0]);
    emitBytes(isa::stImm(Reg::A, 0x3E));
    return;
  }

  if (name == "hi" || name == "lo")
  {
    emitExpr(*expression.arguments[0]);
    if (name == "hi")
    {
      if (expression.arguments[0]->type.isWide())
      {
        emitOp(isa::mov(Reg::A, Reg::B));
      }
      else
      {
        emitBytes(isa::ldi(Reg::A, 0));
      }
    }
    return;
  }

  if (name == "rol8" || name == "ror8")
  {
    emitExpr(*expression.arguments[0]);
    emitOp(isa::mov(Reg::B, Reg::A));
    if (name == "rol8")
    {
      emitOp(isa::shl(Reg::A));
      emitOp(isa::rcl(Reg::B));
    }
    else
    {
      emitOp(isa::shr(Reg::A));
      emitOp(isa::rcr(Reg::B));
    }
    emitOp(isa::mov(Reg::A, Reg::B));
    return;
  }

  if (name == "random")
  {
    emitOp(isa::rnd(Reg::A));
    return;
  }
}
} // namespace strella
