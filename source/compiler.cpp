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
#include "strella/target.hpp"

namespace strella
{

auto compile(std::string const& source, std::string const& file, bool optimize,
             Target target) -> CompilationResult
{
  Lexer lexer {source, file};
  auto tokens = lexer.tokenize();

  Parser parser {std::move(tokens), file};
  auto program = parser.parse();

  Sema sema {program, file, target};
  sema.analyze();

  auto const optimizations = optimize ? optimizeProgram(program) : 0;

  CodeGen codegen {program, sema, file, target};
  auto result = codegen.generate();
  result.optimizations = optimizations;
  return result;
}

auto compileFile(std::string const& path, bool optimize,
                 Target target) -> CompilationResult
{
  std::ifstream const input {path, std::ios::binary};
  if (!input)
  {
    throw CompileError {"error: cannot open '" + path + "'"};
  }

  std::ostringstream buffer;
  buffer << input.rdbuf();
  return compile(buffer.str(), path, optimize, target);
}

} // namespace strella
