// Gable compiler -- built-in declarations.
//
// The builtin functions need result types, and inventing a second type syntax
// for them would be both unnecessary and confusing. Instead the result types are
// declared in Gable source below, compiled before any user file, and marked as
// prelude structs: the language sees ordinary structs, while code generation
// uses the C definitions in runtime/include/gable/rt.h, whose field order must
// match the declarations here.
#ifndef GABLE_PRELUDE_H
#define GABLE_PRELUDE_H

#include <string_view>

namespace gable {

/// Source file name used for the prelude in diagnostics.
inline constexpr std::string_view kPreludeFileName = "<gable prelude>";

/// Declarations of every prelude struct. Field order is part of the ABI with
/// the runtime and must match runtime/include/gable/rt.h.
inline constexpr std::string_view kPreludeSource = R"(
struct FileReadResult {
    ok: bool,
    text: str,
    error: str,
}

struct FileWriteResult {
    ok: bool,
    error: str,
}

struct ParseResult {
    ok: bool,
    value: i64,
}

struct FindResult {
    ok: bool,
    index: i64,
}

struct SliceResult {
    ok: bool,
    text: str,
}
)";

}  // namespace gable

#endif  // GABLE_PRELUDE_H
