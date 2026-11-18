#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "machine.hpp"
#include "strella/diagnostics.hpp"

using namespace testutil;

TEST_CASE("control flow statements", "[language]")
{
  SECTION("for loops and comparisons work")
  {
    auto const result
        = build("func main() { var sum: byte = 0; for (var i: byte = 1; i <= "
                "5; i = i + 1) { sum = sum + i; } put(sum); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x0F'));
  }
  SECTION("do-while, continue, break and ternary work")
  {
    auto const result = build(
        "func main() { var i: byte = 0; var sum: byte = 0; do { i = i + 1; if "
        "(i == 2) { continue; } if (i > 4) { break; } sum = sum + i; } while "
        "(i < 10); put(sum); put(i > 3 ? 1 : 0); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x08') + std::string(1, '\x01'));
  }
  SECTION("switch matches cases and falls through labels")
  {
    auto const result = build(
        "func main() { var x: byte = 2; switch (x) { case 1: put(65); break; "
        "case 2: case 3: put(66); break; default: put(67); break; } switch (x) "
        "{ case 1: put(68); break; default: put(69); break; } halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == "BE");
  }
  SECTION("wide switch compares both bytes")
  {
    auto const result
        = build("func main() { var x: word = 0x0123; switch (x) { case 0x0122: "
                "put(65); break; case 0x0123: put(66); break; default: "
                "put(67); break; } halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == "B");
  }
}

TEST_CASE("wide arithmetic and comparisons", "[language]")
{
  SECTION("wide arithmetic and comparisons work")
  {
    auto const result
        = build("@stack func addw(word a, word b) word { return a + b; } func "
                "main() { var x: word = addw(300, 500); var y: word = "
                "addw(300, 501); put(x & 0xFF); put(x >> 8); put(x < y); put(x "
                "== y); put(x <= x); put(x > y); halt; }");
    Machine machine {result.image};
    machine.run();
    std::string expected;
    expected += static_cast<char>(0x20);
    expected += static_cast<char>(0x03);
    expected += static_cast<char>(1);
    expected += static_cast<char>(0);
    expected += static_cast<char>(1);
    expected += static_cast<char>(0);
    REQUIRE(machine.terminal == expected);
  }
  SECTION("signed wide comparisons work")
  {
    auto const result
        = build("func main() { var a: sword = 0; var b: sword = 1; a = a - 1; "
                "put(a < b); put(a > b); put(a <= b); put(a >= b); halt; }");
    Machine machine {result.image};
    machine.run();
    std::vector<int> actual;
    for (auto value : machine.terminal)
    {
      actual.push_back(static_cast<unsigned char>(value));
    }
    REQUIRE(actual == std::vector<int> {1, 0, 1, 0});
  }
  SECTION("sword supports the same operations as word")
  {
    auto const result = build(
        "func main() { var a: sword = -10; var b: sword = 3; put((a + b) as "
        "byte); put((a - b) as byte); put((a & b) as byte); put((a | b) as "
        "byte); put((a ^ b) as byte); put((b << 2) as byte); put((a >> 1) as "
        "byte); if (a < b) { put('L'); } if (a == -10) { put('E'); } var c: "
        "sword = 5; c++; --c; put(c as byte); var values: sword[2]; values[0] "
        "= -1; values[0]++; put(values[0] as byte); var p: ptr<sword> = "
        "values; p[1] = -2; put(p[1] as byte); var address: sword = p as "
        "sword; var back: ptr<sword> = address as ptr<sword>; put(back[1] as "
        "byte); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(
                "\xF9\xF3\x02\xF7\xF5\x0C\xFB\x4C\x45\x05\x00\xFE\xFE", 13));
  }
  SECTION("prefix and postfix increment and decrement work")
  {
    auto const result
        = build("func main() { var i: byte = 5; var a: byte = i++; var b: byte "
                "= ++i; var c: byte = i--; var d: byte = --i; put(a); put(b); "
                "put(c); put(d); put(i); var values: byte[2]; values[0] = 10; "
                "values[0]++; ++values[0]; put(values[0]); var wide: word = "
                "1000; wide++; put(wide & 0xFF); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 7);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 5);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(1)) == 7);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(2)) == 7);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(3)) == 5);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(4)) == 5);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(5)) == 12);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(6)) == 0xE9);
  }
}

TEST_CASE("arrays and pointers", "[language]")
{
  SECTION("local arrays store and load elements")
  {
    auto const result
        = build("func main() { var data: byte[4]; data[0] = 10; data[1] = 20; "
                "data[2] = 30; data[3] = 40; put(data[0] + data[1] + data[2] + "
                "data[3]); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 100);
  }
  SECTION("global arrays and pointer interchange work")
  {
    auto const result
        = build("var global: byte[3]; func main() { var local: byte[3]; var p: "
                "ptr<byte> = local; global[0] = 5; global[1] = 6; p[0] = "
                "global[0] + 45; p[1] = global[1] + 1; put(local[0] + "
                "local[1]); put(global[0] + global[1]); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 2);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 57);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(1)) == 11);
  }
  SECTION("word array indexing scales by the element size")
  {
    auto const result
        = build("func main() { var values: word[3]; values[0] = 1000; "
                "values[1] = 2000; values[2] = 3000; var p: ptr<word> = "
                "values; p[1] = p[1] + 1; put(values[2] & 0xFF); put(values[2] "
                ">> 8); put(values[1] & 0xFF); put(values[1] >> 8); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 4);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 0xB8);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(1)) == 0x0B);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(2)) == 0xD1);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(3)) == 0x07);
  }
  SECTION("address-of and dereference read and write memory")
  {
    auto const result
        = build("var global: byte = 7; func main() { var local: byte = 3; var "
                "p: ptr<byte> = &local; *p = 9; var q: ptr<byte> = &global; "
                "q[0] = q[0] + 1; put(local); put(global); put(*p); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 3);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 9);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(1)) == 8);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(2)) == 9);
  }
  SECTION("array parameters behave like pointers")
  {
    auto const result
        = build("@inline func second(byte[4] values) byte { return values[1]; "
                "} func main() { var values: byte[4]; values[0] = 1; values[1] "
                "= 42; put(second(values)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 42);
  }
  SECTION("pointers convert explicitly to and from word and ptr")
  {
    auto const result = build(
        "func main() { var values: byte[2]; var p: ptr<byte> = values; var "
        "generic: ptr = p as ptr; var typed: ptr<byte> = generic as ptr<byte>; "
        "var raw: word = typed as word; var back: ptr<byte> = raw as "
        "ptr<byte>; back[0] = 21; var untyped: ptr = values as ptr; untyped[1] "
        "= 33; put(values[0]); put(values[1]); put(*untyped); if (back == p) { "
        "put(1); } halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 4);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 21);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(1)) == 33);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(2)) == 21);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(3)) == 1);
  }
  SECTION("pointers are valid conditions and normalize to bool")
  {
    auto const result
        = build("func main() { var p: ptr<byte> = 0 as word as ptr<byte>; var "
                "value: byte = 5; if (p) { put(1); } else { put(0); } if "
                "(value && p) { put(1); } else { put(2); } var present: bool = "
                "p; if (!present) { put(3); } halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string("\x00\x02\x03", 3));
  }
}

TEST_CASE("conversions and type deduction", "[language]")
{
  SECTION("implicit and explicit conversions follow the documented rules")
  {
    auto const result
        = build("func main() { var b: byte = 200; var w: word = b; var s: "
                "sword = -1; var truncated: byte = w as byte; var signedByte: "
                "sbyte = 200 as sbyte; put(w & 0xFF); put(truncated); "
                "put(signedByte as byte); put(s as word & 0xFF); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 4);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 200);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(1)) == 200);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(2)) == 200);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(3)) == 0xFF);
  }
  SECTION("fitting compile-time bytes convert implicitly to sbyte")
  {
    auto const result = build(R"(
struct Signed {
  sbyte value;
};
const folded: byte = 40 + 2;
var globalSigned: sbyte = 6;
@inline func accept(sbyte value) byte { return value as byte; }
@inline func make() sbyte { return 7; }
func main() {
  var name: sbyte = 1;
  var fromExpr: sbyte = 40 + 2;
  var fromConst: sbyte = folded;
  var assigned: sbyte;
  assigned = 3;
  var viaReturn: sbyte = make();
  var wrapped: Signed = {5};
  put(name as byte);
  put(fromExpr as byte);
  put(fromConst as byte);
  put(assigned as byte);
  put(accept(4));
  put(viaReturn as byte);
  put(wrapped.value as byte);
  put(globalSigned as byte);
  halt;
}
)");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x01') + std::string(1, '\x2A')
                   + std::string(1, '\x2A') + std::string(1, '\x03')
                   + std::string(1, '\x04') + std::string(1, '\x07')
                   + std::string(1, '\x05') + std::string(1, '\x06'));
  }
  SECTION("out-of-range and runtime bytes do not convert to sbyte")
  {
    REQUIRE_THROWS_AS(build("func main() { var bad: sbyte = 128; halt; }"),
                      strella::CompileError);
    REQUIRE_THROWS_AS(build("func main() { var value: byte = 1; var bad: sbyte "
                            "= value; halt; }"),
                      strella::CompileError);
  }
  SECTION("implicit conversions to bool produce 0 or 1")
  {
    auto const result
        = build("@inline func truth() bool { return 5; } @inline func "
                "read_bool(bool value) byte { return value; } const folded: "
                "bool = 5; func main() { var five: bool = 5; var zero: bool = "
                "0; var pointer: ptr<byte> = \"x\"; var present: bool = "
                "pointer; put(five); put(zero); put(present); put(folded); "
                "put(5 as bool); put(truth()); put(read_bool(7)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string("\x01\x00\x01\x01\x01\x01\x01", 7));
  }
  SECTION("auto deduces local and global types")
  {
    auto const result
        = build("var global: auto = 300; func main() { var small: auto = 5; "
                "var wide: auto = 1000; var pointer: auto = &small; *pointer = "
                "7; put(small); put(wide & 0xFF); put(global & 0xFF); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 3);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 7);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(1)) == 0xE8);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(2)) == 0x2C);
  }
  SECTION("auto deduces array pointers and constant strings")
  {
    auto const result = build(
        "const label: auto = \"A\"; func main() { var values: byte[2]; "
        "values[0] = 9; values[1] = 4; var p: auto = values; p[0] = 7; p[1]++; "
        "put(values[0]); put(values[1]); put(label[0]); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string("\x07\x05\x41", 3));
  }
}
