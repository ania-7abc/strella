#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "machine.hpp"
#include "strella/disasm.hpp"

using namespace testutil;

TEST_CASE("disassembler decodes generated code and the ISA", "[disasm]")
{
  SECTION("generated code")
  {
    auto const result = build("func main() { put(65); halt; }");
    auto const text = strella::disassemble(result.image);
    REQUIRE(text.find("0000:  jmp 0x80") != std::string::npos);
    REQUIRE(text.find("ldi a, 0x41") != std::string::npos);
    REQUIRE(text.find("st a, 0x3C") != std::string::npos);
    REQUIRE(text.find("hlt") != std::string::npos);
    auto const plain = strella::disassemble(result.image, false);
    REQUIRE(plain.find("0000:") == std::string::npos);
    REQUIRE(plain.find("jmp 0x80") != std::string::npos);
    REQUIRE(plain.find("_data0 db 0x00, 0x00") != std::string::npos);
    REQUIRE(plain.find("_data1 db 0x00, 0x00") != std::string::npos);
  }
  SECTION("ISA table")
  {
    auto const text = strella::disassemble(
        {0x60, 0x61, 0x31, 0x41, 0x54, 0x07, 0xBA, 0xE1, 0xF0, 0x02});
    REQUIRE(text.find("inc a") != std::string::npos);
    REQUIRE(text.find("add b, a") != std::string::npos);
    REQUIRE(text.find("st b, a") != std::string::npos);
    REQUIRE(text.find("ld b, a") != std::string::npos);
    REQUIRE(text.find("ldi a, 0x07") != std::string::npos);
    REQUIRE(text.find("test c") != std::string::npos);
    REQUIRE(text.find("shl b") != std::string::npos);
    REQUIRE(text.find("_data0 db 0xF0, 0x02") != std::string::npos);
  }
  SECTION("register conditional jumps")
  {
    REQUIRE(strella::disassemble({0x11}, false) == "js a\n");
    REQUIRE(strella::disassemble({0x14}, false) == "jz b\n");
    REQUIRE(strella::disassemble({0x1F}, false) == "jo d\n");
    REQUIRE(strella::disassemble({0x20}, false) == "jnz a\n");
    REQUIRE(strella::disassemble({0x25}, false) == "jns b\n");
    REQUIRE(strella::disassemble({0x2F}, false) == "jno d\n");
  }
  SECTION("every opcode is handled")
  {
    for (unsigned opcode = 0; opcode <= 0xFF; ++opcode)
    {
      std::vector<std::uint8_t> const bytes {static_cast<std::uint8_t>(opcode),
                                             0x42};
      REQUIRE_NOTHROW(strella::disassemble(bytes, false));
      REQUIRE(!strella::disassemble(bytes, false).empty());
    }
  }
}
