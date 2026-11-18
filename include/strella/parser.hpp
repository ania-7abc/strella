#pragma once

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "strella/ast.hpp"
#include "strella/token.hpp"

namespace strella
{

/**
 * @brief Recursive-descent parser for the language described in STRELLA.md.
 */
class Parser
{
public:
  Parser(std::vector<Token> tokens, std::string file);

  auto parse() -> Program;

private:
  [[nodiscard]] auto peek(std::size_t lookahead = 0) const -> Token const&;
  [[nodiscard]] auto check(TokenKind kind) const -> bool;
  [[nodiscard]] auto match(TokenKind kind) -> bool;
  auto advance() -> Token const&;
  auto expect(TokenKind kind, std::string const& message) -> Token const&;
  [[noreturn]] void error(std::string const& message) const;
  [[noreturn]] void errorAt(SourceLocation const& loc,
                            std::string const& message) const;

  [[nodiscard]] auto isTypeStart() const -> bool;
  auto parseType() -> Type;
  auto parsePreimage() -> Preimage;
  auto parseTypeAlias() -> void;
  auto parseStruct() -> std::shared_ptr<StructInfo>;
  auto parseStaticAssert() -> StmtPtr;
  auto parseGlobalDeclaration() -> std::unique_ptr<VarDecl>;
  auto parseVarDeclaration() -> std::unique_ptr<VarDecl>;
  auto
  parseFunction(FunctionAnnotation annotation) -> std::unique_ptr<FunctionDecl>;
  auto parseBlock() -> StmtPtr;
  auto parseStatement() -> StmtPtr;
  auto parseSwitchCase(std::vector<SwitchCase>& cases) -> void;
  auto parseExpression() -> ExprPtr;
  auto parseAssignment() -> ExprPtr;
  auto parseTernary() -> ExprPtr;
  auto parseLogicalOr() -> ExprPtr;
  auto parseLogicalAnd() -> ExprPtr;
  auto parseBitwiseOr() -> ExprPtr;
  auto parseBitwiseXor() -> ExprPtr;
  auto parseBitwiseAnd() -> ExprPtr;
  auto parseEquality() -> ExprPtr;
  auto parseRelational() -> ExprPtr;
  auto parseShift() -> ExprPtr;
  auto parseAdditive() -> ExprPtr;
  auto parseUnary() -> ExprPtr;
  auto parseCastSuffix(ExprPtr expression) -> ExprPtr;
  auto parsePostfix() -> ExprPtr;
  auto parsePrimary() -> ExprPtr;

  static auto makeExpression(ExprKind kind, SourceLocation loc) -> ExprPtr;
  static auto makeStatement(StmtKind kind, SourceLocation loc) -> StmtPtr;

  std::vector<Token> tokens_;
  std::string file_;
  std::size_t position_ = 0;
  std::unordered_map<std::string, Type> aliases_;
  std::unordered_map<std::string, std::shared_ptr<StructInfo>> structs_;
};

} // namespace strella
