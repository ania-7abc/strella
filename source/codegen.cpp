#include "strella/codegen.hpp"
#include "codegen_impl.hpp"
#include "strella/ast.hpp"
#include "strella/sema.hpp"
#include <memory>
#include <string>
#include <utility>

namespace strella
{
CodeGen::CodeGen(Program& program, Sema& sema, std::string file,
                 bool displayProtect)
    : impl_ {std::make_unique<Impl>(program, sema, std::move(file),
                                    displayProtect)}
{
}

CodeGen::~CodeGen() = default;

CodeGen::CodeGen(CodeGen&&) noexcept = default;

auto CodeGen::operator=(CodeGen&&) noexcept -> CodeGen& = default;

auto CodeGen::generate() -> CompilationResult { return impl_->generate(); }

} // namespace strella
