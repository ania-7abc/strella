cmake_minimum_required(VERSION 3.14)

include(cmake/prelude.cmake)

project(
    strella
    VERSION 0.0.0
    DESCRIPTION "Compiler for the Strella 8-bit computer"
    LANGUAGES CXX
)

include(cmake/project-is-top-level.cmake)
include(cmake/variables.cmake)

# ---- Declare library ----

add_library(
    strella_lib OBJECT
    source/codegen.cpp
    source/codegen_aggregate.cpp
    source/codegen_arith.cpp
    source/codegen_call.cpp
    source/codegen_core.cpp
    source/codegen_emit.cpp
    source/codegen_expr.cpp
    source/codegen_function.cpp
    source/codegen_image.cpp
    source/codegen_stmt.cpp
    source/codegen_temps.cpp
    source/compiler.cpp
    source/disasm.cpp
    source/lexer.cpp
    source/lexer_text.cpp
    source/optimizer.cpp
    source/parser.cpp
    source/parser_decl.cpp
    source/parser_expr.cpp
    source/parser_stmt.cpp
    source/preimage.cpp
    source/sema.cpp
    source/sema_call.cpp
    source/sema_decl.cpp
    source/sema_expr.cpp
    source/sema_stmt.cpp
)

target_include_directories(
    strella_lib ${warning_guard}
    PUBLIC
    "\$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
)

target_compile_features(strella_lib PUBLIC cxx_std_17)

# libpng is optional: it is only needed to load PNG preimages.
find_package(PNG QUIET)
if(PNG_FOUND)
  target_compile_definitions(strella_lib PRIVATE STRELLA_HAVE_LIBPNG)
  target_link_libraries(strella_lib PUBLIC PNG::PNG)
endif()

# ---- Declare executable ----

add_executable(strella_exe source/main.cpp)
add_executable(strella::exe ALIAS strella_exe)

set_property(TARGET strella_exe PROPERTY OUTPUT_NAME strella)

target_compile_features(strella_exe PRIVATE cxx_std_17)

target_link_libraries(strella_exe PRIVATE strella_lib)

# ---- Install rules ----

if(NOT CMAKE_SKIP_INSTALL_RULES)
  include(cmake/install-rules.cmake)
endif()

# ---- Developer mode ----

if(NOT strella_DEVELOPER_MODE)
  return()
elseif(NOT PROJECT_IS_TOP_LEVEL)
  message(
      AUTHOR_WARNING
      "Developer mode is intended for developers of strella"
  )
endif()

include(cmake/dev-mode.cmake)
