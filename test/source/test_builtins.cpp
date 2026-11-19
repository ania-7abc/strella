#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <string>
#include <vector>

#include "machine.hpp"
#include "strella/compiler.hpp"
#include "strella/diagnostics.hpp"
#include "strella/disasm.hpp"
#include "strella/target.hpp"

using namespace testutil;
using strella::Target;

TEST_CASE("terminal I/O builtins", "[builtins]")
{
  SECTION("put writes bytes to the terminal")
  {
    auto const result = build("func main() { put(65); put(66); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == "AB");
  }
  SECTION("puts writes a NUL-terminated string")
  {
    auto const result = build(R"(func main() { puts("Hi"); halt; })");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == "Hi");
  }
  SECTION("getchar reads the keyboard port")
  {
    auto const result = build("func main() { put(getchar()); halt; }");
    Machine machine {result.image};
    machine.input.push_back(0x41);
    machine.run();
    REQUIRE(machine.terminal == "A");
  }
  SECTION("puts accepts ptr<byte> and byte arrays")
  {
    auto const result
        = build("func main() { var text: byte[2]; text[0] = 'H'; text[1] = 0; "
                "var p: ptr<byte> = text; puts(p); puts(text); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == "HH");
  }
}

TEST_CASE("device and indicator output", "[builtins]")
{
  SECTION("set_devices, indicator and graphics")
  {
    auto const result
        = build("func main() { set_devices(0x30); indicator(0x0107); put(1, 2, "
                "3, 4, 5, 6); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.devices == 0x30);
    REQUIRE(machine.indicator == std::vector<std::uint8_t> {0x07});
    REQUIRE(machine.indicatorHigh == std::vector<std::uint8_t> {0x01});
    REQUIRE(machine.graphics == std::vector<std::uint8_t> {1, 2, 3, 4, 5, 6});
  }
  SECTION("indicator accepts word and sword values")
  {
    auto const result
        = build("func main() { var unsigned_value: word = 0x1234; var "
                "signed_value: sword = -2; indicator(unsigned_value); "
                "indicator(signed_value); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(
        machine.indicator
        == std::vector<std::uint8_t> {0x34, static_cast<std::uint8_t>(0xFE)});
    REQUIRE(
        machine.indicatorHigh
        == std::vector<std::uint8_t> {0x12, static_cast<std::uint8_t>(0xFF)});
  }
  SECTION("indicator widens smaller integers implicitly")
  {
    auto const result = build(
        "func main() { indicator(7); indicator(-5); indicator(true); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.indicator
            == std::vector<std::uint8_t> {0x07, static_cast<std::uint8_t>(0xFB),
                                          0x01});
    REQUIRE(machine.indicatorHigh
            == std::vector<std::uint8_t> {0x00, static_cast<std::uint8_t>(0xFF),
                                          0x00});
  }
  SECTION("builtin device constants are available")
  {
    auto const result
        = build("func main() { set_devices(display | terminal); "
                "put(display_mono + indicator + indicator_signed); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.devices == (48U | 1U));
    REQUIRE(machine.terminal.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 16 + 4 + 12);
  }
}

TEST_CASE("graphics output", "[builtins]")
{
  SECTION("draw writes a byte to display memory")
  {
    auto const result = build(
        "func main() { draw(0, 0x12); draw(1, 0x34); draw(63, 0x56); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.displayByte(0) == 0x12);
    REQUIRE(machine.displayByte(1) == 0x34);
    REQUIRE(machine.displayByte(63) == 0x56);
  }
  SECTION("draw works inside @stack functions")
  {
    auto const result = build(
        "@stack func paint(byte index, byte value) { draw(index, value); } "
        "func main() { paint(0, 0xAB); paint(4, 0xCD); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.displayByte(0) == 0xAB);
    REQUIRE(machine.displayByte(4) == 0xCD);
  }
}

TEST_CASE("preimage is embedded in the image", "[builtins]")
{
  auto const path
      = std::filesystem::temp_directory_path() / "strella_preimage_test.bin";
  {
    std::ofstream stream {path, std::ios::binary};
    for (int i = 0; i < 32; ++i)
    {
      stream.put(static_cast<char>(i + 1));
    }
  }

  auto pathString = path.string();
  std::replace(pathString.begin(), pathString.end(), '\\', '/');

  auto const result = build("preimage: \"" + pathString
                            + "\";\n"
                              "func main() { put(1); halt; }");
  std::filesystem::remove(path);

  REQUIRE(result.image.at(0x3E) == 48);
  for (int i = 0; i < 32; ++i)
  {
    REQUIRE(result.image.at(static_cast<std::size_t>(0x40 + i))
            == static_cast<std::uint8_t>(i + 1));
  }
  auto const text
      = strella::disassemble(result.image, false, result.dataRanges);
  REQUIRE(text.find("st a, 0x3E") == std::string::npos);

  Machine machine {result.image};
  machine.run();
  REQUIRE(machine.devices == 48);
  for (int i = 0; i < 32; ++i)
  {
    REQUIRE(machine.displayByte(i) == static_cast<std::uint8_t>(i + 1));
  }
  auto const plain = build("func main() { put(1); halt; }");
  REQUIRE(result.layout.codeBytes == plain.layout.codeBytes);
}

TEST_CASE("preimage is rejected on the 32k target", "[builtins][target]")
{
  REQUIRE_THROWS_AS(
      strella::compile("preimage: \"unused.bin\"; func main() { halt; }",
                       "<test>", true, Target::K32),
      strella::CompileError);
}

TEST_CASE("builtin argument types are checked strictly",
          "[builtins][diagnostics]")
{
  REQUIRE_THROWS_AS(build("func main() { puts(0x1234 as ptr); halt; }"),
                    strella::CompileError);
  REQUIRE_THROWS_AS(build("func main() { puts(0x1234 as ptr<word>); halt; }"),
                    strella::CompileError);
  REQUIRE_THROWS_AS(build("func main() { puts(1); halt; }"),
                    strella::CompileError);
  REQUIRE_THROWS_AS(build("func main() { indicator(0x1234 as ptr); halt; }"),
                    strella::CompileError);
}
