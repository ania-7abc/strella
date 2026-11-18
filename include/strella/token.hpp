#pragma once

#include <cstdint>
#include <string>

#include "strella/location.hpp"

namespace strella
{

enum class TokenKind : std::uint8_t
{
  End,
  Identifier,
  IntLiteral,
  StringLiteral,
  CharLiteral,

  KwConst,
  KwVar,
  KwFunc,
  KwPreimage,
  KwIf,
  KwElse,
  KwWhile,
  KwDo,
  KwFor,
  KwSwitch,
  KwCase,
  KwDefault,
  KwHalt,
  KwReturn,
  KwBreak,
  KwContinue,
  KwTrue,
  KwFalse,
  KwAs,
  KwAuto,
  KwStruct,

  LParen,
  RParen,
  LBrace,
  RBrace,
  LBracket,
  RBracket,
  Comma,
  Semicolon,
  Colon,
  Question,
  At,
  Dot,
  Arrow,

  Plus,
  Minus,
  Star,
  Amp,
  Pipe,
  Caret,
  Tilde,
  Bang,
  PlusPlus,
  MinusMinus,

  ShiftLeft,
  ShiftRight,
  LogicalAnd,
  LogicalOr,

  Assign,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual
};

struct Token
{
  TokenKind kind = TokenKind::End;
  std::string text;
  std::uint32_t intValue = 0;
  SourceLocation location;
};

[[nodiscard]] auto tokenKindName(TokenKind kind) -> std::string;

} // namespace strella
