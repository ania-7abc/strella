#include "strella/lexer.hpp"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "strella/diagnostics.hpp"
#include "strella/location.hpp"
#include "strella/token.hpp"

#include "lexer_internal.hpp"

namespace strella
{

using namespace lex_detail;

Lexer::Lexer(std::string source, std::string file)
    : source_ {std::move(source)}, file_ {std::move(file)}
{
}

auto Lexer::tokenize() -> std::vector<Token>
{
  std::vector<Token> tokens;
  while (true)
  {
    skipWhitespaceAndComments();
    if (atEnd())
    {
      break;
    }

    auto const c = peek();
    if (isIdentifierStart(c))
    {
      tokens.push_back(lexIdentifier());
      continue;
    }
    if (c >= '0' && c <= '9')
    {
      tokens.push_back(lexNumber());
      continue;
    }
    if (c == '"')
    {
      tokens.push_back(lexString());
      continue;
    }
    if (c == '\'')
    {
      tokens.push_back(lexChar());
      continue;
    }
    tokens.push_back(lexOperator());
  }

  tokens.push_back(makeToken(TokenKind::End, ""));
  return tokens;
}

auto Lexer::peek(std::size_t lookahead) const -> char
{
  auto const index = pos_ + lookahead;
  return index < source_.size() ? source_[index] : '\0';
}

auto Lexer::atEnd() const -> bool { return pos_ >= source_.size(); }

auto Lexer::advance() -> char
{
  auto const c = source_[pos_++];
  if (c == '\n')
  {
    ++line_;
    column_ = 1;
  }
  else
  {
    ++column_;
  }
  return c;
}

auto Lexer::location() const -> SourceLocation
{
  return SourceLocation {line_, column_};
}

auto Lexer::makeToken(TokenKind kind, std::string text) const -> Token
{
  return Token {kind, std::move(text), 0, location()};
}

void Lexer::skipWhitespaceAndComments()
{
  while (!atEnd())
  {
    auto const c = peek();
    if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f'
        || c == '\v')
    {
      advance();
      continue;
    }

    if (c == '/' && peek(1) == '/')
    {
      while (!atEnd() && peek() != '\n')
      {
        advance();
      }
      continue;
    }

    if (c == '/' && peek(1) == '*')
    {
      auto const start = location();
      advance();
      advance();
      while (!atEnd() && (peek() != '*' || peek(1) != '/'))
      {
        advance();
      }
      if (atEnd())
      {
        fail(file_, start, "unterminated block comment");
      }
      advance();
      advance();
      continue;
    }

    break;
  }
}

auto Lexer::lexIdentifier() -> Token
{
  auto const start = location();
  std::string text;
  while (!atEnd() && isIdentifierPart(peek()))
  {
    text.push_back(advance());
  }

  auto const kind = keywordKind(text);
  return Token {kind, std::move(text), 0, start};
}

auto Lexer::lexNumber() -> Token
{
  auto const start = location();
  std::string text;
  int base = 10;

  if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X'))
  {
    base = 16;
    text.push_back(advance());
    text.push_back(advance());
    while (std::isxdigit(static_cast<unsigned char>(peek())) != 0)
    {
      text.push_back(advance());
    }
  }
  else if (peek() == '0' && (peek(1) == 'b' || peek(1) == 'B'))
  {
    base = 2;
    text.push_back(advance());
    text.push_back(advance());
    while (peek() == '0' || peek() == '1')
    {
      text.push_back(advance());
    }
  }
  else if (peek() == '0' && (peek(1) == 'o' || peek(1) == 'O'))
  {
    base = 8;
    text.push_back(advance());
    text.push_back(advance());
    while (peek() >= '0' && peek() <= '7')
    {
      text.push_back(advance());
    }
  }
  else
  {
    while (std::isdigit(static_cast<unsigned char>(peek())) != 0)
    {
      text.push_back(advance());
    }
    if (text.size() > 1 && text.front() == '0')
    {
      base = 8;
    }
  }

  auto const digits = text.substr(base == 16 || base == 2 || base == 8 ? 2 : 0);
  if (digits.empty())
  {
    fail(file_, start, "malformed numeric literal");
  }

  std::uint64_t value = 0;
  for (auto const digit : digits)
  {
    int digitValue = 0;
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
    else
    {
      fail(file_, start, "invalid digit in numeric literal");
    }

    if (digitValue >= base)
    {
      fail(file_, start, "invalid digit in numeric literal");
    }
    value = (value * static_cast<std::uint64_t>(base))
            + static_cast<std::uint64_t>(digitValue);
    if (value > 0xFFFFFFFFULL)
    {
      fail(file_, start, "numeric literal is too large");
    }
  }

  return Token {TokenKind::IntLiteral, std::move(text),
                static_cast<std::uint32_t>(value), start};
}

auto Lexer::lexString() -> Token
{
  auto const start = location();
  advance();
  std::string result;

  while (!atEnd() && peek() != '"')
  {
    auto const c = advance();
    if (c == '\n')
    {
      fail(file_, start, "unterminated string literal");
    }

    if (c != '\\')
    {
      if (static_cast<unsigned char>(c) < 0x80)
      {
        result.push_back(c);
      }
      else
      {
        --pos_;
        auto const codePoint = decodeUtf8(source_, pos_, file_, start);
        result.push_back(
            static_cast<char>(cp1251Byte(codePoint, file_, start)));
      }
      continue;
    }

    if (atEnd())
    {
      fail(file_, start, "unterminated string literal");
    }
    auto const escaped = advance();
    if (escaped == 'x')
    {
      auto const value
          = parseHexEscape(source_, pos_, line_, column_, file_, start);
      result.push_back(static_cast<char>(value));
      continue;
    }

    auto const value = escapeValue(escaped);
    if (value < 0)
    {
      fail(file_, start, std::string {"unknown escape sequence \\"} + escaped);
    }
    result.push_back(static_cast<char>(value));
  }

  if (atEnd())
  {
    fail(file_, start, "unterminated string literal");
  }
  advance();

  return Token {TokenKind::StringLiteral, std::move(result), 0, start};
}

auto Lexer::lexChar() -> Token
{
  auto const start = location();
  advance();
  if (atEnd() || peek() == '\n')
  {
    fail(file_, start, "unterminated character literal");
  }

  int value = 0;
  if (peek() == '\\')
  {
    advance();
    if (atEnd())
    {
      fail(file_, start, "unterminated character literal");
    }
    auto const escaped = advance();
    if (escaped == 'x')
    {
      value = parseHexEscape(source_, pos_, line_, column_, file_, start);
    }
    else
    {
      value = escapeValue(escaped);
      if (value < 0)
      {
        fail(file_, start,
             std::string {"unknown escape sequence \\"} + escaped);
      }
    }
  }
  else
  {
    auto const c = peek();
    if (static_cast<unsigned char>(c) < 0x80)
    {
      advance();
      value = static_cast<unsigned char>(c);
    }
    else
    {
      auto const codePoint = decodeUtf8(source_, pos_, file_, start);
      value = cp1251Byte(codePoint, file_, start);
    }
  }

  if (atEnd() || peek() != '\'')
  {
    fail(file_, start, "character literal must contain exactly one character");
  }
  advance();

  return Token {TokenKind::CharLiteral, std::string {},
                static_cast<std::uint32_t>(value), start};
}

auto Lexer::lexOperator() -> Token
{
  auto const start = location();
  auto const c = advance();

  auto const next = [&](char expected) -> bool
  {
    if (peek() == expected)
    {
      advance();
      return true;
    }
    return false;
  };

  switch (c)
  {
  case '(':
    return Token {TokenKind::LParen, "(", 0, start};
  case ')':
    return Token {TokenKind::RParen, ")", 0, start};
  case '{':
    return Token {TokenKind::LBrace, "{", 0, start};
  case '}':
    return Token {TokenKind::RBrace, "}", 0, start};
  case '[':
    return Token {TokenKind::LBracket, "[", 0, start};
  case ']':
    return Token {TokenKind::RBracket, "]", 0, start};
  case ',':
    return Token {TokenKind::Comma, ",", 0, start};
  case ';':
    return Token {TokenKind::Semicolon, ";", 0, start};
  case ':':
    return Token {TokenKind::Colon, ":", 0, start};
  case '?':
    return Token {TokenKind::Question, "?", 0, start};
  case '@':
    return Token {TokenKind::At, "@", 0, start};
  case '.':
    return Token {TokenKind::Dot, ".", 0, start};
  case '-':
    if (next('>'))
    {
      return Token {TokenKind::Arrow, "->", 0, start};
    }
    if (next('-'))
    {
      return Token {TokenKind::MinusMinus, "--", 0, start};
    }
    return Token {TokenKind::Minus, "-", 0, start};
  case '+':
    if (next('+'))
    {
      return Token {TokenKind::PlusPlus, "++", 0, start};
    }
    return Token {TokenKind::Plus, "+", 0, start};
  case '*':
    return Token {TokenKind::Star, "*", 0, start};
  case '&':
    if (next('&'))
    {
      return Token {TokenKind::LogicalAnd, "&&", 0, start};
    }
    return Token {TokenKind::Amp, "&", 0, start};
  case '|':
    if (next('|'))
    {
      return Token {TokenKind::LogicalOr, "||", 0, start};
    }
    return Token {TokenKind::Pipe, "|", 0, start};
  case '^':
    return Token {TokenKind::Caret, "^", 0, start};
  case '~':
    return Token {TokenKind::Tilde, "~", 0, start};
  case '!':
    if (next('='))
    {
      return Token {TokenKind::NotEqual, "!=", 0, start};
    }
    return Token {TokenKind::Bang, "!", 0, start};
  case '<':
    if (next('<'))
    {
      return Token {TokenKind::ShiftLeft, "<<", 0, start};
    }
    if (next('='))
    {
      return Token {TokenKind::LessEqual, "<=", 0, start};
    }
    return Token {TokenKind::Less, "<", 0, start};
  case '>':
    if (next('>'))
    {
      return Token {TokenKind::ShiftRight, ">>", 0, start};
    }
    if (next('='))
    {
      return Token {TokenKind::GreaterEqual, ">=", 0, start};
    }
    return Token {TokenKind::Greater, ">", 0, start};
  case '=':
    if (next('='))
    {
      return Token {TokenKind::Equal, "==", 0, start};
    }
    return Token {TokenKind::Assign, "=", 0, start};
  default:
    break;
  }

  fail(file_, start, std::string {"unexpected character '"} + c + "'");
}

} // namespace strella
