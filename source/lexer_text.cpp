#include "lexer_internal.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "strella/diagnostics.hpp"
#include "strella/location.hpp"
#include "strella/token.hpp"

namespace strella
{
namespace lex_detail
{

auto isIdentifierStart(char c) -> bool
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

auto isIdentifierPart(char c) -> bool
{
  return isIdentifierStart(c) || (c >= '0' && c <= '9');
}

auto keywordKind(std::string const& text) -> TokenKind
{
  static std::unordered_map<std::string, TokenKind> const keywords {
      {"const", TokenKind::KwConst},   {"var", TokenKind::KwVar},
      {"func", TokenKind::KwFunc},     {"preimage", TokenKind::KwPreimage},
      {"if", TokenKind::KwIf},         {"else", TokenKind::KwElse},
      {"while", TokenKind::KwWhile},   {"do", TokenKind::KwDo},
      {"for", TokenKind::KwFor},       {"switch", TokenKind::KwSwitch},
      {"case", TokenKind::KwCase},     {"default", TokenKind::KwDefault},
      {"halt", TokenKind::KwHalt},     {"return", TokenKind::KwReturn},
      {"break", TokenKind::KwBreak},   {"continue", TokenKind::KwContinue},
      {"true", TokenKind::KwTrue},     {"false", TokenKind::KwFalse},
      {"as", TokenKind::KwAs},         {"auto", TokenKind::KwAuto},
      {"struct", TokenKind::KwStruct},
  };

  auto const it = keywords.find(text);
  return it == keywords.end() ? TokenKind::Identifier : it->second;
}

auto decodeUtf8(std::string const& text, std::size_t& index,
                std::string const& file, SourceLocation const& location)
    -> std::uint32_t
{
  auto const first = static_cast<unsigned char>(text[index]);
  if (first < 0x80)
  {
    ++index;
    return first;
  }

  auto continuation = [&](std::size_t at) -> std::uint32_t
  {
    if (at >= text.size())
    {
      fail(file, location, "truncated UTF-8 sequence");
    }
    auto const value = static_cast<unsigned char>(text[at]);
    if ((value & 0xC0) != 0x80)
    {
      fail(file, location, "invalid UTF-8 continuation byte");
    }
    return value & 0x3F;
  };

  if ((first & 0xE0) == 0xC0)
  {
    auto const value = ((first & 0x1F) << 6) | continuation(index + 1);
    index += 2;
    return value;
  }
  if ((first & 0xF0) == 0xE0)
  {
    auto const value = ((first & 0x0F) << 12) | (continuation(index + 1) << 6)
                       | continuation(index + 2);
    index += 3;
    return value;
  }
  if ((first & 0xF8) == 0xF0)
  {
    auto const value = ((first & 0x07) << 18) | (continuation(index + 1) << 12)
                       | (continuation(index + 2) << 6)
                       | continuation(index + 3);
    index += 4;
    return value;
  }

  fail(file, location, "invalid UTF-8 leading byte");
}

auto cp1251Byte(std::uint32_t codePoint, std::string const& file,
                SourceLocation const& location) -> unsigned char
{
  if (codePoint < 0x80)
  {
    return static_cast<unsigned char>(codePoint);
  }

  if (codePoint >= 0x0410 && codePoint <= 0x042F)
  {
    return static_cast<unsigned char>(0xC0 + (codePoint - 0x0410));
  }
  if (codePoint >= 0x0430 && codePoint <= 0x044F)
  {
    return static_cast<unsigned char>(0xE0 + (codePoint - 0x0430));
  }

  switch (codePoint)
  {
  case 0x0401:
    return 0xA8; // Ё
  case 0x0451:
    return 0xB8; // ё
  case 0x2116:
    return 0xB9; // №
  case 0x00A0:
    return 0xA0; // non-breaking space
  case 0x00AB:
    return 0xAB; // «
  case 0x00BB:
    return 0xBB; // »
  case 0x2013:
    return 0x96; // –
  case 0x2014:
    return 0x97; // —
  case 0x2018:
    return 0x91; // ‘
  case 0x2019:
    return 0x92; // ’
  case 0x201C:
    return 0x93; // “
  case 0x201D:
    return 0x94; // ”
  case 0x2026:
    return 0x85; // …
  case 0x20AC:
    return 0x88; // €
  case 0x00B0:
    return 0xB0; // °
  case 0x00B1:
    return 0xB1; // ±
  case 0x00B5:
    return 0xB5; // µ
  case 0x00B7:
    return 0xB7; // ·
  default:
    break;
  }

  fail(
      file, location,
      "character U+" +
          [&]() -> std::string
                       {
                         static constexpr std::array<char, 16> digits {
                             '0', '1', '2', '3', '4', '5', '6', '7',
                             '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
                         std::string result(4, '0');
                         for (int i = 3; i >= 0; --i)
                         {
                           result[static_cast<std::size_t>(i)] = digits.at(
                               static_cast<std::size_t>(codePoint & 0xF));
                           codePoint >>= 4;
                         }
                         return result;
                       }()
                       + " cannot be represented in cp1251");
}

auto escapeValue(char c) -> int
{
  switch (c)
  {
  case 'a':
    return 0x07;
  case 'b':
    return 0x08;
  case 't':
    return 0x09;
  case 'n':
    return 0x0A;
  case 'v':
    return 0x0B;
  case 'f':
    return 0x0C;
  case 'r':
    return 0x0D;
  case '\\':
    return '\\';
  case '\'':
    return '\'';
  case '"':
    return '"';
  case '0':
    return 0;
  default:
    return -1;
  }
}

auto parseHexEscape(std::string const& source, std::size_t& pos,
                    std::size_t& line, std::size_t& column,
                    std::string const& file, SourceLocation const& location)
    -> int
{
  int value = 0;
  for (int i = 0; i < 2; ++i)
  {
    auto const digit = pos < source.size() ? source[pos] : '\0';
    int digitValue = -1;
    if (digit >= '0' && digit <= '9')
    {
      digitValue = digit - '0';
    }
    else if (digit >= 'a' && digit <= 'f')
    {
      digitValue = digit - 'a' + 10;
    }
    else if (digit >= 'A' && digit <= 'F')
    {
      digitValue = digit - 'A' + 10;
    }
    if (digitValue < 0)
    {
      fail(file, location, "expected two hexadecimal digits after \\x");
    }
    value = (value * 16) + digitValue;
    ++pos;
    if (digit == '\n')
    {
      ++line;
      column = 1;
    }
    else
    {
      ++column;
    }
  }
  return value;
}

} // namespace lex_detail

auto tokenKindName(TokenKind kind) -> std::string
{
  switch (kind)
  {
  case TokenKind::End:
    return "end of file";
  case TokenKind::Identifier:
    return "identifier";
  case TokenKind::IntLiteral:
    return "integer literal";
  case TokenKind::StringLiteral:
    return "string literal";
  case TokenKind::CharLiteral:
    return "character literal";
  case TokenKind::KwConst:
    return "'const'";
  case TokenKind::KwVar:
    return "'var'";
  case TokenKind::KwFunc:
    return "'func'";
  case TokenKind::KwPreimage:
    return "'preimage'";
  case TokenKind::KwIf:
    return "'if'";
  case TokenKind::KwElse:
    return "'else'";
  case TokenKind::KwWhile:
    return "'while'";
  case TokenKind::KwDo:
    return "'do'";
  case TokenKind::KwFor:
    return "'for'";
  case TokenKind::KwSwitch:
    return "'switch'";
  case TokenKind::KwCase:
    return "'case'";
  case TokenKind::KwDefault:
    return "'default'";
  case TokenKind::KwHalt:
    return "'halt'";
  case TokenKind::KwReturn:
    return "'return'";
  case TokenKind::KwBreak:
    return "'break'";
  case TokenKind::KwContinue:
    return "'continue'";
  case TokenKind::KwTrue:
    return "'true'";
  case TokenKind::KwFalse:
    return "'false'";
  case TokenKind::KwAs:
    return "'as'";
  case TokenKind::KwAuto:
    return "'auto'";
  case TokenKind::KwStruct:
    return "'struct'";
  case TokenKind::LParen:
    return "'('";
  case TokenKind::RParen:
    return "')'";
  case TokenKind::LBrace:
    return "'{'";
  case TokenKind::RBrace:
    return "'}'";
  case TokenKind::LBracket:
    return "'['";
  case TokenKind::RBracket:
    return "']'";
  case TokenKind::Comma:
    return "','";
  case TokenKind::Semicolon:
    return "';'";
  case TokenKind::Colon:
    return "':'";
  case TokenKind::Question:
    return "'?'";
  case TokenKind::At:
    return "'@'";
  case TokenKind::Dot:
    return "'.'";
  case TokenKind::Arrow:
    return "'->'";
  case TokenKind::Plus:
    return "'+'";
  case TokenKind::Minus:
    return "'-'";
  case TokenKind::Star:
    return "'*'";
  case TokenKind::Amp:
    return "'&'";
  case TokenKind::Pipe:
    return "'|'";
  case TokenKind::Caret:
    return "'^'";
  case TokenKind::Tilde:
    return "'~'";
  case TokenKind::Bang:
    return "'!'";
  case TokenKind::PlusPlus:
    return "'++'";
  case TokenKind::MinusMinus:
    return "'--'";
  case TokenKind::ShiftLeft:
    return "'<<'";
  case TokenKind::ShiftRight:
    return "'>>'";
  case TokenKind::LogicalAnd:
    return "'&&'";
  case TokenKind::LogicalOr:
    return "'||'";
  case TokenKind::Assign:
    return "'='";
  case TokenKind::Equal:
    return "'=='";
  case TokenKind::NotEqual:
    return "'!='";
  case TokenKind::Less:
    return "'<'";
  case TokenKind::LessEqual:
    return "'<='";
  case TokenKind::Greater:
    return "'>'";
  case TokenKind::GreaterEqual:
    return "'>='";
  }
  return "token";
}

} // namespace strella
