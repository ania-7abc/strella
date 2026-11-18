#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "machine.hpp"
#include "strella/compiler.hpp"
#include "strella/diagnostics.hpp"
#include "strella/disasm.hpp"

using namespace testutil;

TEST_CASE("compiler image layout and bank packing", "[compiler]")
{
  SECTION("image layout and map")
  {
    auto const result = build("func main() { halt; }");
    REQUIRE(result.image.size() == 32768);
    REQUIRE(result.image[0] == 0x03);
    REQUIRE(result.image[1] == 0x80);
    REQUIRE(result.map.size() == 1);
    REQUIRE(result.map[0].first == "main");
    REQUIRE(result.map[0].second.first == 1);
    REQUIRE(result.map[0].second.second == 0x80);
  }
  SECTION("small @stack functions share banks")
  {
    auto const result = build(
        R"(@stack func a() { put(65); } @stack func b() { put(66); } @stack func c() { put(67); } @stack func d() { put(68); } func main() { a(); b(); c(); d(); halt; })");
    REQUIRE(result.layout.packedBanks > 0);
    REQUIRE(result.layout.banksUsed < 5);
    REQUIRE(result.layout.totalFunctions == 5);
    REQUIRE(result.layout.codeBytes + result.layout.freeBytes
            == result.layout.banksUsed * 128);
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == "ABCD");
  }
  SECTION("code continues across bank boundaries")
  {
    std::string source = "func main() { ";
    for (int i = 0; i < 80; ++i)
    {
      source += "put(65); ";
    }
    source += "halt; }";
    auto const result = build(source);
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(80, 'A'));
  }
}

TEST_CASE("display-protect bits 4 and 5", "[compiler][display-protect]")
{
  auto const find63rd = [](std::vector<std::uint8_t> const& image) -> int
  {
    int seen = 0;
    for (std::size_t i = 0; i < image.size(); ++i)
    {
      if (image[i] == 0)
      {
        continue;
      }

      ++seen;
      if (seen == 63)
      {
        return static_cast<int>(i);
      }
    }
    return -1;
  };

  SECTION("bit set: the byte becomes a nop")
  {
    std::string source = "func main() { ";
    for (int i = 0; i < 80; ++i)
    {
      source += "put(16); ";
    }
    source += "halt; }";

    auto const raw = strella::compile(source, "<test>", true, false);
    auto const guarded = strella::compile(source, "<test>", true, true);
    auto const offset = static_cast<std::size_t>(find63rd(raw.image));

    REQUIRE(offset < raw.image.size());
    REQUIRE((raw.image.at(offset) & 0x30) != 0);
    REQUIRE(guarded.image.at(offset) == 0);

    Machine machine {guarded.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(80, '\x10'));
  }

  SECTION("bits clear: the image is unchanged")
  {
    std::string source = "func main() { puts(\"x\"); ";
    for (int i = 0; i < 8; ++i)
    {
      source += "put(1); ";
    }
    source += "halt; }";

    auto const raw = strella::compile(source, "<test>", true, false);
    auto const guarded = strella::compile(source, "<test>", true, true);
    auto const offset = static_cast<std::size_t>(find63rd(raw.image));

    REQUIRE(offset < raw.image.size());
    REQUIRE((raw.image.at(offset) & 0x30) == 0);
    REQUIRE(guarded.image == raw.image);
  }
}

TEST_CASE("optimizer removes dead code and folds constants",
          "[compiler][optimizer]")
{
  SECTION("dead code elimination")
  {
    auto const* const source
        = R"(func main() { if (0) { puts("x"); } if (1) { puts("ok"); } else { puts("y"); } return; puts("z"); })";
    auto const unoptimized = strella::compile(source, "<test>", false);
    auto const optimized = strella::compile(source, "<test>", true);
    REQUIRE(unoptimized.optimizations == 0);
    REQUIRE(optimized.optimizations > 0);
    REQUIRE(optimized.listing.size() < unoptimized.listing.size());
    Machine machine {optimized.image};
    machine.run();
    REQUIRE(machine.terminal == "ok");
  }
  SECTION("constant folding")
  {
    auto const* const source = "func main() { put(1 + 2 + 3 + 4); halt; }";
    auto const unoptimized = strella::compile(source, "<test>", false);
    auto const optimized = strella::compile(source, "<test>", true);
    REQUIRE(unoptimized.optimizations == 0);
    REQUIRE(optimized.optimizations > 0);
    auto const text
        = strella::disassemble(optimized.image, false, optimized.dataRanges);
    REQUIRE(text.find("ldi a, 0x0A") != std::string::npos);
    REQUIRE(text.find("add a") == std::string::npos);
    Machine machine {optimized.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 10);
  }
}

TEST_CASE("compiler rejects invalid declarations", "[compiler][diagnostics]")
{
  SECTION("array, auto, pointer and lvalue restrictions")
  {
    REQUIRE_THROWS_AS(build("func main() { const a: byte[2]; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var a: byte[2] = 0; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var a: byte[0]; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var a: byte[2][2]; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var a: auto; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func f(auto a) {} func main() { halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(
        build("func f() auto { return 1; } func main() { halt; }"),
        strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var a: auto[2]; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var a: ptr<auto>; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var p: ptr<byte>; var q: ptr<byte> "
                            "= p + 1; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var p: ptr<byte>; p++; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var b: bool; b++; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { &5; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { *5; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(
        build("func main() { var p: ptr<byte> = 5 as ptr<byte>; halt; }"),
        strella::CompileError);
  }
  SECTION("unsized arrays are rejected")
  {
    REQUIRE_THROWS_AS(build("func main() { var values: byte[]; halt; }"),
                      strella::CompileError);
  }
  SECTION("cross_jmp is not a builtin")
  {
    REQUIRE_THROWS_AS(build("func main() { cross_jmp(1, 2); halt; }"),
                      strella::CompileError);
  }
}
