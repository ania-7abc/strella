#pragma once

#include <string>

#include "strella/codegen.hpp"
#include "strella/target.hpp"

namespace strella
{

/**
 * @brief Compiles a source string into a memory image for the chosen target.
 *
 * The whole pipeline is run: lexing, parsing, semantic analysis, optimization
 * and code generation.  User-visible problems are reported by throwing
 * CompileError.
 *
 * With Target::K1 (the default) preimages are allowed and display protection
 * is off.  With Target::K32 preimages are rejected and display protection is
 * on: bits 4 and 5 of the 63rd non-zero byte of the unprotected image are
 * checked, and that byte becomes a `nop` if either bit is set.
 */
auto compile(std::string const& source, std::string const& file,
             bool optimize = true,
             Target target = Target::K1) -> CompilationResult;

/**
 * @brief Reads `path` and compiles it for the chosen target.
 */
auto compileFile(std::string const& path, bool optimize = true,
                 Target target = Target::K1) -> CompilationResult;

} // namespace strella
