#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

#include "machine.hpp"

using namespace testutil;

TEST_CASE("inline functions expand into callers", "[functions]")
{
  SECTION("inline functions return values")
  {
    auto const result = build("@inline func add(byte a, byte b) byte { return "
                              "a + b; } func main() { put(add(1, 2)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x03'));
  }
  SECTION("expansion adds no map entry")
  {
    auto const result = build(
        "@inline func add(byte a, byte b) byte { var sum: byte = a + b; return "
        "sum; } func main() { put(add(1, 2)); put(add(3, 4)); halt; }");
    REQUIRE(result.map.size() == 1);
    REQUIRE(result.map[0].first == "main");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x03') + std::string(1, '\x07'));
  }
  SECTION("nested calls keep separate parameters and locals")
  {
    auto const result
        = build("@inline func inc(byte v) byte { var t: byte = v; t = t + 1; "
                "return t; } func main() { put(inc(inc(5))); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x07'));
  }
  SECTION("inline functions call other inline functions")
  {
    auto const result
        = build("@inline func inc(byte v) byte { return v + 1; } @inline func "
                "twice(byte v) byte { return inc(inc(v)); } func main() { "
                "put(twice(5)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x07'));
  }
  SECTION("return leaves the expansion early")
  {
    auto const result = build(
        "@inline func clamp(byte v) byte { if (v > 10) { return 10; } return "
        "v; } func main() { put(clamp(20)); put(clamp(3)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x0A') + std::string(1, '\x03'));
  }
  SECTION("loops stay inside the expansion")
  {
    auto const result
        = build("@inline func sum_to(byte n) byte { var s: byte = 0; var i: "
                "byte = 1; while (i <= n) { s = s + i; i = i + 1; } return s; "
                "} func main() { var i: byte = 0; while (i < 3) { put(sum_to(i "
                "+ 1)); i = i + 1; } halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x01') + std::string(1, '\x03')
                   + std::string(1, '\x06'));
  }
  SECTION("switch returns leave the expansion")
  {
    auto const result = build(
        "@inline func classify(byte v) byte { switch (v) { case 0: return 10; "
        "case 1: return 20; default: return 30; } } func main() { "
        "put(classify(0)); put(classify(1)); put(classify(2)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x0A') + std::string(1, '\x14')
                   + std::string(1, '\x1E'));
  }
  SECTION("inline functions return words")
  {
    auto const result = build(
        "@inline func double(word v) word { return v + v; } func main() { var "
        "v: word = double(500); put(v & 0xFF); put(v >> 8); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\xE8') + std::string(1, '\x03'));
  }
  SECTION("non-stack functions accept more than three parameter bytes")
  {
    auto const result
        = build("@inline func addw(word a, word b) word { return a + b; } func "
                "main() { put(addw(1000, 2000) as byte); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 0xB8);
  }
  SECTION("inline functions have arrays")
  {
    auto const result
        = build("@inline func inlined() byte { var values: byte[2]; values[0] "
                "= 4; values[1] = 5; return values[0] + values[1]; } func "
                "main() { put(inlined()); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 9);
  }
  SECTION("globals with runtime initializers run at startup")
  {
    auto const result
        = build("@inline func twice(byte v) byte { return v + v; } var total: "
                "byte = twice(3); func main() { put(total); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x06'));
  }
}

TEST_CASE("inline functions interoperate with @stack functions", "[functions]")
{
  SECTION("inline expansion inside @stack functions")
  {
    auto const result
        = build("@inline func twice(byte v) byte { return v + v; } @stack func "
                "compute(byte x) byte { return twice(x) + 1; } func main() { "
                "put(compute(4)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x09'));
  }
  SECTION("inline locals are safe in recursive @stack functions")
  {
    auto const result = build(
        "@inline func twice(byte v) byte { var t: byte = v; return t + t; } "
        "@stack func rec(byte n) byte { if (n == 0) { return 0; } return "
        "twice(n) + rec(n - 1); } func main() { put(rec(1)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x02'));
  }
  SECTION("inline functions call @stack functions")
  {
    auto const result
        = build("@stack func add_one(byte v) byte { return v + 1; } @inline "
                "func twice(byte v) byte { return add_one(add_one(v)); } func "
                "main() { put(twice(5)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x07'));
  }
}

TEST_CASE("@stack functions use the software stack", "[functions]")
{
  SECTION("parameters use the software stack")
  {
    auto const result = build("@stack func add(byte a, byte b) byte { return a "
                              "+ b; } func main() { put(add(4, 5)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x09'));
  }
  SECTION("recursive @stack functions work")
  {
    auto const result
        = build("@stack func sum(byte n) byte { if (n <= 1) { return 1; } "
                "return n + sum(n - 1); } func main() { put(sum(2)); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal == std::string(1, '\x03'));
  }
  SECTION("arrays work in @stack functions")
  {
    auto const result
        = build("@stack func stacked() byte { var values: byte[3]; values[0] = "
                "1; values[1] = 2; values[2] = 3; return values[0] + values[1] "
                "+ values[2]; } func main() { put(stacked()); halt; }");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal.size() == 1);
    REQUIRE(static_cast<std::uint8_t>(machine.terminal.at(0)) == 6);
  }
}
