#include <catch2/catch_test_macros.hpp>

#include <string>

#include "machine.hpp"
#include "strella/diagnostics.hpp"

using namespace testutil;

TEST_CASE("compile-time queries", "[language]")
{
  SECTION("sizeof, len, hi and lo fold to constants")
  {
    auto const result = build(R"(
static_assert(sizeof(byte) == 1, "byte size");
static_assert(sizeof(word) == 2, "word size");
static_assert(sizeof(byte[4]) == 4, "array size");
const data: byte[3] = [10, 20, 30];
static_assert(len(data) == 3, "array length");
static_assert(len("Hi") == 2, "string length");
func main() {
  put(sizeof(word));
  put(len(data));
  put(hi(0x1234));
  put(lo(0x1234));
  halt;
}
)");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x02') + std::string(1, '\x03')
                   + std::string(1, '\x12') + std::string(1, '\x34'));
  }
}

TEST_CASE("constant arrays and type aliases", "[language]")
{
  SECTION("array literals are stored in the image")
  {
    auto const result = build(R"(
type Addr = ptr<byte>;
const palette: byte[3] = [65, 66, 67];
const words: word[2] = [0x1234, 0x5678];
func main() {
  var p: Addr = palette;
  put(p[0]); put(p[1]); put(p[2]);
  put(hi(words[0])); put(lo(words[0]));
  put(hi(words[1])); put(lo(words[1]));
  halt;
}
)");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == "ABC" + std::string(1, '\x12') + std::string(1, '\x34')
                   + std::string(1, '\x56') + std::string(1, '\x78'));
  }

  SECTION("array literals work as expressions")
  {
    auto const result = build(
        "func main() { var p: ptr<byte> = [65, 66, 0]; puts(p); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == "AB");
  }
}

TEST_CASE("rotate builtins", "[builtins]")
{
  SECTION("rol8 and ror8 rotate a runtime byte")
  {
    auto const result
        = build("func main() { var x: byte = 0x81; put(rol8(x)); put(ror8(x)); "
                "halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x03') + std::string(1, '\xC0'));
  }

  SECTION("rol8 and ror8 fold constant arguments")
  {
    auto const result = build("func main() { put(rol8(1)); put(ror8(1)); "
                              "halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x02') + std::string(1, '\x80'));
  }
}

TEST_CASE("static assertions", "[diagnostics]")
{
  SECTION("static_assert works inside a function")
  {
    auto const result = build(
        "func main() { static_assert(1 == 1, \"ok\"); put(65); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == "A");
  }

  SECTION("failed assertions are reported")
  {
    REQUIRE_THROWS_AS(
        build("static_assert(1 == 2, \"failed\"); func main() { halt; }"),
        strella::CompileError);
  }

  SECTION("assertions need constant conditions")
  {
    REQUIRE_THROWS_AS(
        build("var x: byte = 1; static_assert(x == 1, \"x\"); func main() { "
              "halt; }"),
        strella::CompileError);
  }

  SECTION("array literal lengths are checked")
  {
    REQUIRE_THROWS_AS(
        build("const a: byte[2] = [1, 2, 3]; func main() { halt; }"),
        strella::CompileError);
  }
}
