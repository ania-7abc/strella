#pragma once

#include <string>
#include <vector>

#include "strella/token.hpp"

namespace strella
{

/**
 * @brief Splits source text into tokens.
 */
class Lexer
{
public:
  Lexer(std::string source, std::string file);

  auto tokenize() -> std::vector<Token>;

private:
  [[nodiscard]] auto peek(std::size_t lookahead = 0) const -> char;
  [[nodiscard]] auto atEnd() const -> bool;
  auto advance() -> char;
  void skipWhitespaceAndComments();
  auto lexIdentifier() -> Token;
  auto lexNumber() -> Token;
  auto lexString() -> Token;
  auto lexChar() -> Token;
  auto lexOperator() -> Token;
  [[nodiscard]] auto location() const -> SourceLocation;
  [[nodiscard]] auto makeToken(TokenKind kind, std::string text) const -> Token;

  std::string source_;
  std::string file_;
  std::size_t pos_ = 0;
  std::size_t line_ = 1;
  std::size_t column_ = 1;
};

} // namespace strella
