#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/ast.hpp"
#include "strella/codegen.hpp"
#include "strella/diagnostics.hpp"
#include "strella/isa.hpp"
#include <cstddef>
#include <cstdint>

namespace strella
{
auto CodeGen::Impl::emitSymbolAddress(Symbol const& symbol) -> void
{
  auto const inlineTemp = inlineSlotFor(symbol);
  if (inlineTemp >= 0)
  {
    emitTempAddress(inlineTemp);
    return;
  }

  switch (symbol.storage)
  {
  case Storage::Global:
  case Storage::Local:
  case Storage::Parameter:
    emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(symbol.address)));
    emitBytes(isa::ldi(Reg::B, 0));
    break;

  case Storage::StackLocal:
    emitFrameAddress(symbol.frameOffset, Reg::C, false);
    emitOp(isa::mov(Reg::A, Reg::C));
    emitBytes(isa::ldi(Reg::B, 0));
    break;

  case Storage::StackParameter:
    emitFrameAddress(stackFrameOffset(symbol), Reg::C, false);
    emitOp(isa::mov(Reg::A, Reg::C));
    emitBytes(isa::ldi(Reg::B, 0));
    break;

  case Storage::Constant:
    fail(file_, currentFunction_->location,
         "cannot take the address of a constant");
  }
}

auto CodeGen::Impl::emitCopyBytesAtOffset(int destTemp, int offset, int srcTemp,
                                          int size) -> void
{
  if (size <= 0)
  {
    return;
  }
  if (size > 255 || offset < 0 || offset + size > 256)
  {
    fail(file_, currentFunction_->location, "struct is too large to copy");
  }

  for (int i = 0; i < size; ++i)
  {
    auto const destinationOffset = offset + i;
    emitLoadTemp(srcTemp, false, Reg::A, Reg::D);
    if (i != 0)
    {
      emitBytes(isa::ldi(Reg::C, static_cast<std::uint8_t>(i)));
      emitOp(isa::add(Reg::A, Reg::C));
    }
    emitOp(isa::mov(Reg::C, Reg::A));
    emitOp(isa::ldReg(Reg::C, Reg::B));

    emitLoadTemp(destTemp, false, Reg::C, Reg::D);
    if (destinationOffset != 0)
    {
      emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(destinationOffset)));
      emitOp(isa::add(Reg::C, Reg::A));
    }
    emitOp(isa::stReg(Reg::C, Reg::B));
  }
}

auto CodeGen::Impl::emitCopyBytes(int destTemp, int srcTemp, int size) -> void
{
  emitCopyBytesAtOffset(destTemp, 0, srcTemp, size);
}

auto CodeGen::Impl::emitCopyBytesFromAddress(int destTemp, int size) -> void
{
  if (size <= 0)
  {
    return;
  }
  if (size > 255)
  {
    fail(file_, currentFunction_->location, "struct is too large to copy");
  }

  // A holds the source address on entry.  Load the destination pointer into B
  // without touching A, then use C for the byte address and D for the value.
  if (!current_->stackBased)
  {
    emitBytes(
        isa::ldImm(Reg::B, static_cast<std::uint8_t>(tempAddress(destTemp))));
  }
  else
  {
    // The temporary holds the destination pointer, not the data itself.  Load
    // that pointer into B without disturbing the source address in A.
    emitFrameAddress(currentFunction_->stackFrameSize + (destTemp * 2), Reg::C,
                     true);
    emitOp(isa::ldReg(Reg::C, Reg::B));
  }

  for (int i = 0; i < size; ++i)
  {
    if (i != 0)
    {
      emitBytes(isa::ldi(Reg::C, static_cast<std::uint8_t>(i)));
      emitOp(isa::add(Reg::C, Reg::A));
    }
    else
    {
      emitOp(isa::mov(Reg::C, Reg::A));
    }
    emitOp(isa::ldReg(Reg::C, Reg::D));
    emitOp(isa::stReg(Reg::B, Reg::D));
    if (i + 1 < size)
    {
      emitOp(isa::inc(Reg::B));
    }
  }
}

auto CodeGen::Impl::emitCopyBytesFromExpr(int destTemp, int offset,
                                          Expr& source, int size) -> void
{
  emitExpr(source);
  if (offset == 0)
  {
    emitCopyBytesFromAddress(destTemp, size);
  }
  else
  {
    auto const srcTemp = allocTemp();
    emitStoreTemp(srcTemp, true);
    emitCopyBytesAtOffset(destTemp, offset, srcTemp, size);
    releaseTemp(srcTemp);
  }
  emitLoadTemp(destTemp, true, Reg::A, Reg::B);
}

auto CodeGen::Impl::emitCopyToSymbol(Symbol const& symbol, Expr& source,
                                     int size) -> void
{
  auto const destTemp = allocTemp();
  emitSymbolAddress(symbol);
  emitStoreTemp(destTemp, true);
  emitCopyBytesFromExpr(destTemp, 0, source, size);
  releaseTemp(destTemp);
}

auto CodeGen::Impl::emitCopyToStackPointer(Expr& source, int size) -> void
{
  auto const srcTemp = allocTemp();
  emitExpr(source);
  emitStoreTemp(srcTemp, true);

  auto const destTemp = allocTemp();
  // 0x0F holds the stack pointer itself.  Loading through C would read the
  // first stack byte instead of the pointer value.
  emitBytes(isa::ldImm(Reg::A, 0x0F));
  emitStoreTemp(destTemp, false);

  for (int i = 0; i < size; ++i)
  {
    emitLoadTemp(srcTemp, false, Reg::A, Reg::D);
    if (i != 0)
    {
      emitBytes(isa::ldi(Reg::C, static_cast<std::uint8_t>(i)));
      emitOp(isa::add(Reg::A, Reg::C));
    }
    emitOp(isa::mov(Reg::C, Reg::A));
    emitOp(isa::ldReg(Reg::C, Reg::B));
    emitLoadTemp(destTemp, false, Reg::C, Reg::D);
    if (i != 0)
    {
      emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(i)));
      emitOp(isa::add(Reg::C, Reg::A));
    }
    emitOp(isa::stReg(Reg::C, Reg::B));
  }

  emitLoadTemp(destTemp, false, Reg::A, Reg::D);
  emitBytes(isa::ldi(Reg::C, static_cast<std::uint8_t>(size)));
  emitOp(isa::add(Reg::A, Reg::C));
  emitBytes(isa::stImm(Reg::A, 0x0F));

  releaseTemp(destTemp);
  releaseTemp(srcTemp);
}

auto CodeGen::Impl::emitWriteValueAtTemp(int destTemp, int offset, Expr& value)
    -> void
{
  auto const wide = value.type.isWide();
  auto const valueTemp = allocTemp();
  emitExpr(value);
  emitExtendOperand(value, wide);
  emitStoreTemp(valueTemp, wide);

  emitLoadTemp(destTemp, false, Reg::C, Reg::D);
  if (offset != 0)
  {
    emitBytes(isa::ldi(Reg::A, static_cast<std::uint8_t>(offset)));
    emitOp(isa::add(Reg::C, Reg::A));
  }
  emitLoadTemp(valueTemp, wide, Reg::A, Reg::B);
  emitOp(isa::stReg(Reg::C, Reg::A));
  if (wide)
  {
    emitOp(isa::inc(Reg::C));
    emitOp(isa::stReg(Reg::C, Reg::B));
  }

  releaseTemp(valueTemp);
}

auto CodeGen::Impl::emitBuildStructLiteral(Expr& literal, int destTemp) -> void
{
  auto const info = literal.type.structure();
  if (info == nullptr)
  {
    return;
  }

  if (literal.isAggregateConstant)
  {
    auto const srcTemp = allocTemp();
    emitBytes(isa::ldi(Reg::A,
                       static_cast<std::uint8_t>(literal.dataAddress & 0xFF)));
    emitBytes(isa::ldi(
        Reg::B, static_cast<std::uint8_t>((literal.dataAddress >> 8) & 0xFF)));
    emitStoreTemp(srcTemp, true);
    emitCopyBytes(destTemp, srcTemp, info->size);
    emitLoadTemp(destTemp, true, Reg::A, Reg::B);
    releaseTemp(srcTemp);
    return;
  }

  for (std::size_t i = 0; i < info->fields.size(); ++i)
  {
    auto const& field = info->fields[i];
    auto& value = *literal.arguments[i];
    if (field.type.isStruct())
    {
      auto const nestedSize = field.type.isArray() ? field.type.pointee().size()
                                                   : field.type.size();
      auto const slots = (nestedSize + 1) / 2;
      auto const nestedStorage = allocTemp();
      for (int slot = 1; slot < slots; ++slot)
      {
        allocTemp();
      }
      auto const nestedPointer = allocTemp();
      emitTempAddress(nestedStorage);
      emitStoreTemp(nestedPointer, true);
      if (value.kind == ExprKind::StructLiteral)
      {
        emitBuildStructLiteral(value, nestedPointer);
      }
      else
      {
        emitExpr(value);
        emitStoreTemp(nestedPointer, true);
      }
      emitCopyBytesAtOffset(destTemp, field.offset, nestedPointer,
                            field.type.size());
      releaseTemp(nestedPointer);
      for (int slot = slots - 1; slot >= 0; --slot)
      {
        releaseTemp(nestedStorage + slot);
      }
      continue;
    }

    if (field.type.isArray() || field.type.isStruct())
    {
      emitCopyBytesFromExpr(destTemp, field.offset, value, field.type.size());
      continue;
    }

    emitWriteValueAtTemp(destTemp, field.offset, value);
  }

  emitLoadTemp(destTemp, true, Reg::A, Reg::B);
}
} // namespace strella
