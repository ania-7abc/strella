#include "strella/compiler.hpp"

#include <fstream>
#include <ios>
#include <sstream>
#include <string>
#include <utility>

#include "strella/codegen.hpp"
#include "strella/diagnostics.hpp"
#include "strella/lexer.hpp"
#include "strella/optimizer.hpp"
#include "strella/parser.hpp"
#include "strella/sema.hpp"

namespace strella
{

auto compile(std::string const& source, std::string const& file, bool optimize,
             bool displayProtect) -> CompilationResult
{
  Lexer lexer {source, file};
  auto tokens = lexer.tokenize();

  Parser parser {std::move(tokens), file};
  auto program = parser.parse();

  Sema sema {program, file};
  sema.analyze();

  auto const optimizations = optimize ? optimizeProgram(program) : 0;

  CodeGen codegen {program, sema, file, displayProtect};
  auto result = codegen.generate();
  result.optimizations = optimizations;
  return result;
}

auto compileFile(std::string const& path, bool optimize, bool displayProtect)
    -> CompilationResult
{
  std::ifstream const input {path, std::ios::binary};
  if (!input)
  {
    throw CompileError {"error: cannot open '" + path + "'"};
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return compile(buffer.str(), path, optimize, displayProtect);
}

} // namespace strella
