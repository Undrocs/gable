// Gable compiler -- C17 backend.
//
// Lowers the typed IR to C17 that is compiled by the host C toolchain and
// linked against the Gable runtime. The generated code:
//
//   * names every user function `gable_fn_<name>` so it cannot collide with
//     the runtime or the platform C library,
//   * performs signed arithmetic through the matching unsigned type, so that
//     Gable's defined wrapping overflow never becomes C undefined behaviour,
//   * calls runtime helpers for division (zero check and INT_MIN / -1),
//   * never relies on C operator precedence: subexpressions are parenthesized.
#ifndef GABLE_C_BACKEND_H
#define GABLE_C_BACKEND_H

#include <string>

#include "gable/ir.h"

namespace gable {

struct CBackendOptions {
  /// Written into the banner comment.
  std::string sourceName;
  std::string compilerVersion;
};

/// Emits one C17 translation unit for `module`.
std::string emitC(const ir::Module& module, const CBackendOptions& options);

}  // namespace gable

#endif  // GABLE_C_BACKEND_H
