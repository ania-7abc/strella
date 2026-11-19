#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "strella/ast.hpp"
#include "strella/target.hpp"

namespace strella
{

/**
 * @brief A byte range that must be written into the memory image at startup.
 */
struct DataInitializer
{
  int address = 0;
  int size = 0;
  std::uint32_t value = 0;
};

/**
 * @brief Resolves names, checks types, allocates common-memory data and
 *        validates the function-annotation and calling restrictions.
 */
class Sema
{
public:
  Sema(Program& program, std::string file, Target target = Target::K1);

  void analyze();

  [[nodiscard]] auto dataEnd() const -> int { return dataCursor_; }
  [[nodiscard]] auto
  dataInitializers() const -> std::vector<DataInitializer> const&
  {
    return dataInitializers_;
  }
  [[nodiscard]] auto
  runtimeGlobalInitializers() const -> std::vector<VarDecl*> const&
  {
    return runtimeGlobalInitializers_;
  }
  [[nodiscard]] auto usesStack() const -> bool { return usesStack_; }
  [[nodiscard]] auto stringAddress(std::string const& text) const -> int;
  [[nodiscard]] auto
  stringData() const -> std::vector<std::pair<int, std::string>> const&
  {
    return stringData_;
  }

private:
  using Scope = std::unordered_map<std::string, Symbol*>;

  void collectFunctions();
  void declareBuiltinConstants();
  void checkPreimage();
  void declareGlobals();
  void checkFunctions();
  void checkCallGraph();
  void finalizeLayout();

  auto declareGlobal(VarDecl& declaration) -> void;
  void checkFunction(FunctionDecl& function);
  void checkStatement(Stmt& statement);
  void checkBlock(Stmt& statement, bool createScope = true);
  void checkDeclaration(Stmt& statement);
  auto checkExpression(Expr& expression) -> Type;
  auto checkCall(Expr& expression) -> Type;
  auto checkBuiltinCall(Expr& expression) -> Type;
  void checkCondition(Expr& expression, char const* what);
  auto checkArrayLiteral(Expr& literal, Type elementType,
                         int expectedLength) -> Type;
  void emitArrayData(Expr const& literal, Type const& elementType, int address);
  auto checkStructLiteral(Expr& literal, Type const& structType) -> Type;
  void emitStructData(Expr const& literal, Type const& structType, int address);
  void checkStructInitializer(ExprPtr& initializer, Type const& destination);
  [[nodiscard]] static auto canAssign(Expr const& expression,
                                      Type const& destination) -> bool;
  static void coerce(ExprPtr& expression, Type const& destination);

  void pushScope();
  void popScope();
  auto lookup(std::string const& name) const -> Symbol*;
  void declareSymbol(Symbol* symbol);
  auto allocateData(int size, SourceLocation const& location) -> int;
  auto allocateString(std::string const& text,
                      SourceLocation const& location) -> int;
  auto makeStaticSymbol(std::string const& name, Type type, Storage storage,
                        SourceLocation const& location) -> Symbol*;

  [[nodiscard]] static auto fitsType(Type const& type,
                                     std::uint32_t value) -> bool;
  static void recordConstant(Expr& expression, std::uint32_t value);
  void addCallEdge(FunctionDecl* callee);
  void dfsRecursion(FunctionDecl* function,
                    std::unordered_map<FunctionDecl*, int>& colors,
                    std::vector<FunctionDecl*>& stack);

  Program& program_;
  std::string file_;
  Target target_ = Target::K1;

  std::vector<Scope> scopes_;
  std::unordered_map<std::string, FunctionDecl*> functions_;
  std::unordered_map<std::string, int> stringAddresses_;
  std::vector<std::pair<int, std::string>> stringData_;
  std::unordered_map<FunctionDecl*, std::vector<FunctionDecl*>> callEdges_;

  int dataCursor_ = 0x17;
  bool usesStack_ = false;
  int currentFrameSize_ = 0;
  FunctionDecl* currentFunction_ = nullptr;
  int loopDepth_ = 0;
  int switchDepth_ = 0;

  std::vector<DataInitializer> dataInitializers_;
  std::vector<VarDecl*> runtimeGlobalInitializers_;
};

} // namespace strella
