#pragma once

#include "strella/ast.hpp"

namespace strella
{

/**
 * @brief Runs AST-level optimizations on a checked program.
 *
 * The pass only rewrites control flow whose behavior is already known at
 * compile time.  It never changes the observable evaluation order: constant
 * conditions cannot have side effects, and code after an unconditional
 * transfer is unreachable.
 *
 * @return Number of applied transformations.
 */
auto optimizeProgram(Program& program) -> int;

} // namespace strella
