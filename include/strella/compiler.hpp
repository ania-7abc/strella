#pragma once

#include <string>

#include "strella/codegen.hpp"

namespace strella
{

/**
 * @brief Compiles a source string into a 32 KB memory image.
 *
 * The whole pipeline is run: lexing, parsing, semantic analysis, optimization
 * and code generation.  User-visible problems are reported by throwing
 * CompileError.
 *
 * `displayProtect` checks bits 4 and 5 of the 63rd non-zero byte of the
 * unprotected image.  If either bit is set, a `nop` is written there and the
 * code layout leaves the byte free; otherwise the image is left unchanged.
 */
auto compile(std::string const& source, std::string const& file,
             bool optimize = true,
             bool displayProtect = true) -> CompilationResult;

/**
 * @brief Reads `path` and compiles it.
 */
auto compileFile(std::string const& path, bool optimize = true,
                 bool displayProtect = true) -> CompilationResult;

} // namespace strella
