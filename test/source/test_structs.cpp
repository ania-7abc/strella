#include <catch2/catch_test_macros.hpp>

#include <string>

#include "machine.hpp"
#include "strella/diagnostics.hpp"

using namespace testutil;

TEST_CASE("packed structs", "[language][structs]")
{
  SECTION("fields share bytes without padding and assignments copy values")
  {
    auto const result = build(R"(
struct Point {
  byte x;
  word y;
};
static_assert(sizeof(Point) == 3, "packed size");
func main() {
  var p: Point = {65, 0x1234};
  put(p.x);
  put(lo(p.y));
  put(hi(p.y));
  p.x = 66;
  var q: Point = p;
  q.x = 67;
  put(p.x);
  put(q.x);
  halt;
}
)");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == "A" + std::string(1, '\x34') + std::string(1, '\x12') + "BC");
  }

  SECTION("pointers accept both . and -> for struct fields")
  {
    auto const result = build(R"(
struct Pair {
  byte a;
  byte b;
};
var p: Pair = {1, 2};
func main() {
  var q: ptr<Pair> = &p;
  put(q->a);
  q.b = 9;
  put(p.b);
  put(q.a);
  halt;
}
)");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x01') + std::string(1, '\x09')
                   + std::string(1, '\x01'));
  }

  SECTION("nested structs and arrays of structs use packed offsets")
  {
    auto const result = build(R"(
struct Inner {
  byte a;
  byte b;
};
struct Outer {
  byte tag;
  Inner inner;
};
static_assert(sizeof(Outer) == 3, "nested size");
const items: Outer[2] = [{1, {2, 3}}, {4, {5, 6}}];
func main() {
  put(items[0].tag);
  put(items[0].inner.a);
  put(items[1].inner.b);
  halt;
}
)");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x01') + std::string(1, '\x02')
                   + std::string(1, '\x06'));
  }

  SECTION("runtime struct literals and struct parameters are copied")
  {
    auto const result = build(R"(
struct Pair {
  byte a;
  byte b;
};
@inline func bump(Pair p) Pair {
  p.a = p.a + 1;
  return p;
}
@stack func swap(Pair p) Pair {
  var t: byte = p.a;
  p.a = p.b;
  p.b = t;
  return p;
}
func main() {
  var seed: byte = 10;
  var p: Pair = {seed, 20};
  var q: Pair = bump(p);
  put(p.a);
  put(q.a);
  var r: Pair = swap(q);
  put(r.a);
  put(r.b);
  halt;
}
)");
    Machine machine {result.image};
    machine.run();
    REQUIRE(machine.terminal
            == std::string(1, '\x0A') + std::string(1, '\x0B')
                   + std::string(1, '\x14') + std::string(1, '\x0B'));
  }
}

TEST_CASE("struct diagnostics", "[diagnostics][structs]")
{
  SECTION("unknown fields are rejected")
  {
    REQUIRE_THROWS_AS(
        build("struct S { byte a; }; func main() { var s: S = {1}; put(s.b); "
              "halt; }"),
        strella::CompileError);
  }

  SECTION("duplicate fields are rejected")
  {
    REQUIRE_THROWS_AS(build("struct S { byte a; word a; }; func main() { halt; "
                            "}"),
                      strella::CompileError);
  }

  SECTION("incomplete structs cannot be embedded by value")
  {
    REQUIRE_THROWS_AS(build("struct S { S next; }; func main() { halt; }"),
                      strella::CompileError);
  }

  SECTION("initializer field counts are checked")
  {
    REQUIRE_THROWS_AS(
        build("struct S { byte a; byte b; }; func main() { var s: S = {1}; "
              "halt; }"),
        strella::CompileError);
  }
}
