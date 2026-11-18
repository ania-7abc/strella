#include "codegen_impl.hpp"
#include "codegen_types.hpp"
#include "strella/codegen.hpp"
#include "strella/diagnostics.hpp"
#include "strella/isa.hpp"
#include "strella/location.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace strella
{
auto CodeGen::Impl::protectedCodeOffset(int absoluteBank) const -> int
{
  if (!displayProtect_ || protectedImageOffset_ < kBankSize)
  {
    return -1;
  }

  auto const bank = protectedImageOffset_ / kBankSize;
  if (absoluteBank != bank)
  {
    return -1;
  }

  // Leave the protected code byte unused so applyDisplayProtect() can place a
  // nop there.
  return 0x80 + (protectedImageOffset_ % kBankSize);
}

auto CodeGen::Impl::emitFunctionImage(FunctionCode& code) -> void
{
  auto const startBank = code.startBank;

  auto writeAt = [&](int bank, int address, std::uint8_t value) -> void
  {
    auto const index = (bank * kBankSize) + (address - 0x80);
    if (index < 0 || index >= kImageSize)
    {
      fail(file_, code.function->location,
           "program does not fit into the 32 KB memory image");
    }
    image_[static_cast<std::size_t>(index)] = value;
  };

  auto writeSequence = [&](int bank, int address,
                           std::vector<std::uint8_t> const& bytes) -> void
  {
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
      writeAt(bank, address + static_cast<int>(i), bytes[i]);
    }
  };

  // Continuation banks restore the registers from the common scratch bytes.
  for (auto const relativeBank : code.continuationBanks)
  {
    auto const bank = startBank + relativeBank;
    writeSequence(bank, 0x80, isa::ldImm(Reg::A, kScratchA));
    writeSequence(bank, 0x82, isa::ldImm(Reg::B, kScratchB));
    writeSequence(bank, 0x84, isa::ldImm(Reg::C, kScratchC));
    writeSequence(bank, 0x86, isa::ldImm(Reg::D, kScratchD));
  }

  // Boundary jumps preserve every register while switching to the next bank.
  for (auto const& boundary : code.boundaries)
  {
    auto const bank = startBank + boundary.bank;
    auto const nextBank = startBank + boundary.nextBank;

    std::vector<std::uint8_t> bytes;
    auto append = [&](std::vector<std::uint8_t> const& part) -> void
    { bytes.insert(bytes.end(), part.begin(), part.end()); };
    append(isa::stImm(Reg::A, kScratchA));
    append(isa::stImm(Reg::B, kScratchB));
    append(isa::stImm(Reg::C, kScratchC));
    append(isa::stImm(Reg::D, kScratchD));
    append(isa::ldi(Reg::A, static_cast<std::uint8_t>(nextBank)));
    append(isa::ldi(Reg::B, 0x80));
    append(isa::jmpImm(0x02));

    writeSequence(bank, boundary.address, bytes);
  }

  for (auto& item : code.items)
  {
    auto const bank = startBank + item.bank;
    auto const address = item.offset;

    auto writeBytes = [&](std::vector<std::uint8_t> const& bytes) -> void
    { writeSequence(bank, address, bytes); };

    switch (item.kind)
    {
    case Item::Kind::Bytes:
      writeBytes(item.bytes);
      break;

    case Item::Kind::RawBlock:
    {
      auto bytes = item.bytes;
      for (auto const& patch : item.shortJumps)
      {
        if (patch.first < 0 || patch.first >= static_cast<int>(bytes.size()))
        {
          continue;
        }
        bytes[static_cast<std::size_t>(patch.first)]
            = static_cast<std::uint8_t>(address + patch.second);
      }
      writeBytes(bytes);
      break;
    }

    case Item::Kind::Label:
      break;

    case Item::Kind::Jump:
    {
      auto const found = code.labels.find(item.target);
      if (found == code.labels.end())
      {
        fail(file_, code.function->location,
             "internal error: unresolved label in '" + code.function->name
                 + "'");
      }

      auto const targetBank = startBank + found->second.first;
      auto const targetOffset = found->second.second;
      auto const sameBank = found->second.first == item.bank;

      std::vector<std::uint8_t> bytes;
      if (sameBank)
      {
        if (item.condition < 0)
        {
          bytes = isa::jmpImm(static_cast<std::uint8_t>(targetOffset));
        }
        else if (item.condition < 4)
        {
          bytes = isa::jCondImm(static_cast<Cond>(item.condition),
                                static_cast<std::uint8_t>(targetOffset));
        }
        else
        {
          bytes = isa::jnCondImm(static_cast<Cond>(item.condition - 4),
                                 static_cast<std::uint8_t>(targetOffset));
        }
      }
      else
      {
        auto const skip = static_cast<std::uint8_t>(address + 8);
        if (item.condition >= 0 && item.condition < 4)
        {
          bytes = isa::jnCondImm(static_cast<Cond>(item.condition), skip);
        }
        else if (item.condition >= 4)
        {
          bytes = isa::jCondImm(static_cast<Cond>(item.condition - 4), skip);
        }

        auto append = [&](std::vector<std::uint8_t> const& part) -> void
        { bytes.insert(bytes.end(), part.begin(), part.end()); };
        // Jump through C/D so that A and B keep their values across banks.
        append(isa::ldi(Reg::C, static_cast<std::uint8_t>(targetBank)));
        append(isa::ldi(Reg::D, static_cast<std::uint8_t>(targetOffset)));
        append(isa::jmpImm(0x05));
      }

      while (bytes.size() < 8)
      {
        bytes.push_back(isa::nop());
      }

      writeBytes(bytes);
      break;
    }

    case Item::Kind::StaticCall:
    {
      auto* callee = item.callee;
      if (callee == nullptr)
      {
        fail(file_, code.function->location,
             "internal error: missing static-call target");
      }

      auto const returnAddress = static_cast<std::uint8_t>(address + 10);

      std::vector<std::uint8_t> bytes;
      auto append = [&](std::vector<std::uint8_t> const& part) -> void
      { bytes.insert(bytes.end(), part.begin(), part.end()); };
      append(isa::ldi(Reg::A, static_cast<std::uint8_t>(callee->bank)));
      append(isa::ldi(Reg::B, static_cast<std::uint8_t>(callee->entry)));
      append(isa::ldi(Reg::C, static_cast<std::uint8_t>(bank)));
      append(isa::ldi(Reg::D, returnAddress));
      append(isa::jmpImm(0x08));

      writeBytes(bytes);
      break;
    }

    case Item::Kind::StackCall:
    {
      auto* callee = item.callee;
      if (callee == nullptr)
      {
        fail(file_, code.function->location,
             "internal error: missing stack-call target");
      }

      auto const returnAddress = static_cast<std::uint8_t>(address + 18);

      std::vector<std::uint8_t> bytes;
      auto append = [&](std::vector<std::uint8_t> const& part) -> void
      { bytes.insert(bytes.end(), part.begin(), part.end()); };
      append(isa::ldi(Reg::A, static_cast<std::uint8_t>(bank)));
      append(isa::ldi(Reg::B, returnAddress));
      append(isa::ldImm(Reg::C, 0x0F));
      bytes.push_back(isa::stReg(Reg::C, Reg::A));
      bytes.push_back(isa::inc(Reg::C));
      bytes.push_back(isa::stReg(Reg::C, Reg::B));
      bytes.push_back(isa::inc(Reg::C));
      append(isa::stImm(Reg::C, 0x0F));
      append(isa::ldi(Reg::A, static_cast<std::uint8_t>(callee->bank)));
      append(isa::ldi(Reg::B, static_cast<std::uint8_t>(callee->entry)));
      append(isa::jmpImm(0x02));

      writeBytes(bytes);
      break;
    }
    }
  }
}

auto CodeGen::Impl::buildImage() -> void
{
  dataRanges_.clear();
  std::fill(image_.begin(), image_.end(), std::uint8_t {0});

  auto writeCommon
      = [&](int address, std::vector<std::uint8_t> const& bytes) -> void
  {
    for (std::size_t i = 0; i < bytes.size(); ++i)
    {
      auto const index = address + static_cast<int>(i);
      if (index < 0 || index >= kBankSize)
      {
        fail(file_, SourceLocation {}, "internal error: common code overflow");
      }
      image_[static_cast<std::size_t>(index)] = bytes[i];
    }
  };

  // 0x00: jump into bank 1.
  writeCommon(0x00, isa::jmpImm(0x80));

  // 0x02: jump to bank A, address B.
  writeCommon(0x02, {isa::stImm(Reg::A, 0x3F)[0], isa::stImm(Reg::A, 0x3F)[1],
                     isa::jmpReg(Reg::B)});

  // 0x05: jump to bank C, address D.
  writeCommon(0x05, {isa::stImm(Reg::C, 0x3F)[0], isa::stImm(Reg::C, 0x3F)[1],
                     isa::jmpReg(Reg::D)});

  // 0x08: static-call trampoline.
  {
    std::vector<std::uint8_t> bytes;
    auto append = [&](std::vector<std::uint8_t> const& part) -> void
    { bytes.insert(bytes.end(), part.begin(), part.end()); };
    append(isa::stImm(Reg::A, 0x3F));
    append(isa::stImm(Reg::C, 0x80));
    append(isa::stImm(Reg::D, 0x81));
    bytes.push_back(isa::jmpReg(Reg::B));
    writeCommon(0x08, bytes);
  }

  // Constant global data.
  for (auto const& initializer : sema_.dataInitializers())
  {
    dataRanges_.emplace_back(initializer.address, initializer.size);
    for (int i = 0; i < initializer.size; ++i)
    {
      auto const index = initializer.address + i;
      if (index < 0 || index >= kImageSize)
      {
        fail(file_, SourceLocation {},
             "internal error: data initializer out of range");
      }
      image_[static_cast<std::size_t>(index)]
          = static_cast<std::uint8_t>((initializer.value >> (8 * i)) & 0xFF);
    }
  }

  // String literals.
  for (auto const& entry : sema_.stringData())
  {
    auto const address = entry.first;
    auto const& text = entry.second;
    dataRanges_.emplace_back(address, static_cast<int>(text.size()) + 1);
    for (std::size_t i = 0; i < text.size(); ++i)
    {
      auto const index = address + static_cast<int>(i);
      if (index < 0 || index >= kImageSize)
      {
        fail(file_, SourceLocation {},
             "internal error: string data out of range");
      }
      image_[static_cast<std::size_t>(index)]
          = static_cast<std::uint8_t>(text[i]);
    }
    image_[static_cast<std::size_t>(address) + text.size()] = 0;
  }

  // Preimage: both the device value and the pixels are part of the final
  // memory image, so no startup instructions are generated.  The machine
  // picks the device value up while the image itself is loaded.
  if (program_.preimage.present)
  {
    image_[static_cast<std::size_t>(kDevicesPort)] = kPreimageDevices;
    dataRanges_.emplace_back(kDevicesPort, 1);

    auto const& bytes = program_.preimage.bytes;
    if (!bytes.empty())
    {
      dataRanges_.emplace_back(kDisplayStart, static_cast<int>(bytes.size()));
      for (std::size_t i = 0; i < bytes.size(); ++i)
      {
        auto const index = kDisplayStart + static_cast<int>(i);
        if (index > kDisplayEnd)
        {
          fail(file_, SourceLocation {},
               "preimage does not fit into the display memory");
        }
        image_[static_cast<std::size_t>(index)] = bytes[i];
      }
    }
  }

  for (auto* function : functionOrder_)
  {
    auto const found = functions_.find(function);
    if (found != functions_.end())
    {
      emitFunctionImage(found->second);
    }
  }
}

auto CodeGen::Impl::findProtectedImageOffset() -> void
{
  protectedImageOffset_ = -1;
  if (!displayProtect_)
  {
    return;
  }

  int seen = 0;
  for (std::size_t index = 0; index < image_.size(); ++index)
  {
    if (image_[index] == 0)
    {
      continue;
    }

    ++seen;
    if (seen == kProtectedNonzeroIndex)
    {
      if ((image_[index] & kProtectedBits) != 0)
      {
        protectedImageOffset_ = static_cast<int>(index);
      }
      return;
    }
  }
}

auto CodeGen::Impl::applyDisplayProtect() -> void
{
  if (!displayProtect_ || protectedImageOffset_ < 0
      || protectedImageOffset_ >= kImageSize)
  {
    return;
  }

  image_[static_cast<std::size_t>(protectedImageOffset_)] = isa::nop();
}
} // namespace strella
