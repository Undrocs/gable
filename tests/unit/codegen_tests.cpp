#include "test_framework.h"
#include "test_helpers.h"

#include <string>
#include <vector>

using gabletest::emitCFor;
using gabletest::emitCForProject;

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

/// Position of `needle` in `text`, or npos.
std::size_t positionOf(const std::string& text, const std::string& needle) {
  return text.find(needle);
}

}  // namespace

GABLE_TEST(codegen_emits_runtime_include_and_entry_point) {
  const std::string c = emitCFor(gabletest::kSimpleProgram());
  GABLE_CHECK(contains(c, "#include \"gable/rt.h\""));
  GABLE_CHECK(contains(c, "static int32_t gable_fn_main(void)"));
  GABLE_CHECK(contains(c, "int main(int argc, char** argv)"));
  GABLE_CHECK(contains(c, "gable_rt_set_args(argc, argv);"));
  GABLE_CHECK(contains(c, "return (int)gable_fn_main();"));
}

GABLE_TEST(codegen_emits_string_literals_with_explicit_length) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  print(\"hello\");\n"
      "  print(\"world\");\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "gable_rt_str(\"hello\", 5)"));
  GABLE_CHECK(contains(c, "gable_rt_str(\"world\", 5)"));
  GABLE_CHECK(contains(c, "gable_rt_print_str("));
}

GABLE_TEST(codegen_escapes_string_contents) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  print(\"quote:\\\" backslash:\\\\ tab:\\t newline:\\n\");\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "\\\""));  // escaped double quote
  GABLE_CHECK(contains(c, "\\\\"));  // escaped backslash
  GABLE_CHECK(contains(c, "\\t"));  // tab
  GABLE_CHECK(contains(c, "\\n"));  // newline
}

// Arithmetic is computed in 64-bit unsigned and reduced to the declared width:
// C would promote narrow operands to `int`, so `(uint8_t)255 + 1` would be 256 and
// a narrow multiplication could overflow `int`, which is undefined.
GABLE_TEST(codegen_computes_arithmetic_at_the_declared_width) {
  const std::string c = emitCFor(
      "fn add(a: i32, b: i32) -> i32 { return a + b; }\n"
      "fn main() -> i32 { return add(1, 2); }\n");
  GABLE_CHECK(contains(c, "(int32_t)gable_rt_trunc_i32((uint64_t)(v0_a) + (uint64_t)(v1_b))"));
}

GABLE_TEST(codegen_reduces_unsigned_arithmetic_to_the_declared_width) {
  const std::string c = emitCFor(
      "fn add(a: u8, b: u8) -> u8 { return a * b; }\n"
      "fn main() -> i32 { return 0; }\n");
  GABLE_CHECK(contains(c, "(uint8_t)gable_rt_trunc_u8((uint64_t)(v0_a) * (uint64_t)(v1_b))"));
}

// A narrowing cast goes through the same helpers, so a converted constant cannot
// trip MSVC's "cast truncates constant value" warning and the behaviour does not
// depend on the C compiler's conversion rules.
GABLE_TEST(codegen_uses_truncation_helpers_for_narrowing_casts) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  let wide: i32 = 200;\n"
      "  let narrow: i8 = wide as i8;\n"
      "  print(narrow);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "(int8_t)gable_rt_trunc_i8((uint64_t)(v0_wide))"));
}

GABLE_TEST(codegen_uses_runtime_helpers_for_division) {
  const std::string c = emitCFor(
      "fn divide(a: i64, b: i64) -> i64 { return a / b; }\n"
      "fn main() -> i32 { return 0; }\n");
  GABLE_CHECK(contains(c, "gable_rt_div_i64"));
}

GABLE_TEST(codegen_uses_unsigned_division_helpers_for_unsigned_types) {
  const std::string c = emitCFor(
      "fn divide(a: u32, b: u32) -> u32 { return a / b; }\n"
      "fn main() -> i32 { return 0; }\n");
  GABLE_CHECK(contains(c, "gable_rt_div_u64"));
}

GABLE_TEST(codegen_evaluates_call_arguments_left_to_right) {
  const std::string c = emitCFor(
      "fn pair(a: i32, b: i32) -> i32 { return a + b; }\n"
      "fn left() -> i32 { print(\"left\"); return 1; }\n"
      "fn right() -> i32 { print(\"right\"); return 2; }\n"
      "fn main() -> i32 { return pair(left(), right()); }\n");
  const std::size_t leftCall = positionOf(c, "gable_fn_left()");
  const std::size_t rightCall = positionOf(c, "gable_fn_right()");
  GABLE_CHECK(leftCall != std::string::npos);
  GABLE_CHECK(rightCall != std::string::npos);
  GABLE_CHECK(leftCall < rightCall);
  // Both results are stored before the call, so the C compiler cannot reorder
  // them by evaluating the call's arguments in another order.
  GABLE_CHECK(contains(c, "int32_t t2 = gable_fn_pair(t0, t1);"));
}

GABLE_TEST(codegen_evaluates_binary_operands_left_to_right) {
  const std::string c = emitCFor(
      "fn left() -> i32 { print(\"left\"); return 1; }\n"
      "fn right() -> i32 { print(\"right\"); return 2; }\n"
      "fn main() -> i32 { return left() / right(); }\n");
  const std::size_t leftCall = positionOf(c, "gable_fn_left()");
  const std::size_t rightCall = positionOf(c, "gable_fn_right()");
  GABLE_CHECK(leftCall != std::string::npos);
  GABLE_CHECK(rightCall != std::string::npos);
  GABLE_CHECK(leftCall < rightCall);
}

GABLE_TEST(codegen_keeps_short_circuit_evaluation) {
  const std::string c = emitCFor(
      "fn check(a: bool, b: bool) -> bool { return a && b; }\n"
      "fn main() -> i32 { return 0; }\n");
  GABLE_CHECK(contains(c, "(v0_a && v1_b)"));
}

GABLE_TEST(codegen_sequences_side_effects_inside_short_circuit) {
  const std::string c = emitCFor(
      "fn value() -> i32 { return 1; }\n"
      "fn truth() -> bool { print(\"right\"); return true; }\n"
      "fn main() -> i32 { let a: i32 = 0;\n"
      "  if a == 0 && truth() { print(\"body\"); }\n  return 0; }\n");
  // The right operand is guarded by an if statement instead of being evaluated
  // unconditionally.
  GABLE_CHECK(contains(c, "if ("));
  GABLE_CHECK(contains(c, "gable_fn_truth()"));
  GABLE_CHECK(positionOf(c, "gable_fn_truth()") < positionOf(c, "gable_rt_print_str(gable_rt_str(\"body\", 4))"));
}

GABLE_TEST(codegen_bounds_checks_array_access) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  let values: [i32; 3] = [1, 2, 3];\n"
      "  return values[1];\n"
      "}\n");
  GABLE_CHECK(contains(c, "gable_rt_index_error"));
  GABLE_CHECK(contains(c, ">= 3"));
  GABLE_CHECK(contains(c, "typedef struct gable_arr_0"));
  GABLE_CHECK(contains(c, "int32_t data[3];"));
}

GABLE_TEST(codegen_emits_struct_typedefs_in_dependency_order) {
  const std::string c = emitCFor(
      "struct Inner { value: i32 }\n"
      "struct Outer { inner: Inner, items: [i32; 2] }\n"
      "fn main() -> i32 { let o: Outer = Outer(Inner(1), [2, 3]); return o.inner.value; }\n");
  const std::size_t inner = positionOf(c, "typedef struct gable_s_Inner");
  const std::size_t outer = positionOf(c, "typedef struct gable_s_Outer");
  const std::size_t array = positionOf(c, "typedef struct gable_arr_");
  GABLE_CHECK(inner != std::string::npos);
  GABLE_CHECK(outer != std::string::npos);
  GABLE_CHECK(inner < outer);
  GABLE_CHECK(array != std::string::npos);
  GABLE_CHECK(array < outer);
}

GABLE_TEST(codegen_uses_runtime_struct_for_builtin_results) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  let read = fs_read_text(\"x\");\n"
      "  if read.ok { print(read.text); }\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "gable_rt_fs_read_text("));
  // The prelude struct is described by the runtime header, not redeclared.
  GABLE_CHECK(!contains(c, "typedef struct gable_rt_FileReadResult"));
}

GABLE_TEST(codegen_maps_break_and_continue) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  for i in 0..10 {\n"
      "    if i == 2 { continue; }\n"
      "    if i == 5 { break; }\n"
      "  }\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "continue;"));
  GABLE_CHECK(contains(c, "break;"));
  GABLE_CHECK(contains(c, "for (;"));
}

GABLE_TEST(codegen_evaluates_range_bound_once) {
  const std::string c = emitCFor(
      "fn bound() -> i32 { return 3; }\n"
      "fn main() -> i32 {\n"
      "  var total: i32 = 0;\n"
      "  for i in 0..bound() { total = total + i; }\n"
      "  return total;\n"
      "}\n");
  const std::size_t call = positionOf(c, "gable_fn_bound()");
  const std::size_t loop = positionOf(c, "for (;");
  GABLE_CHECK(call != std::string::npos);
  GABLE_CHECK(loop != std::string::npos);
  GABLE_CHECK(call < loop);
  // Exactly one call: the bound is not re-evaluated per iteration.
  GABLE_CHECK(c.find("gable_fn_bound()", call + 1) == std::string::npos);
}

GABLE_TEST(codegen_copies_the_iterated_array) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  let values: [i32; 2] = [1, 2];\n"
      "  for value in values { print(value); }\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "= v"));
  GABLE_CHECK(contains(c, "gable_arr_"));
  GABLE_CHECK(contains(c, ".data[t"));
}

GABLE_TEST(codegen_converts_strings_with_runtime_calls) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  let a: str = \"x\" + \"y\";\n"
      "  let b: bool = a == \"xy\";\n"
      "  print(b);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "gable_rt_str_concat("));
  GABLE_CHECK(contains(c, "gable_rt_str_eq("));
}

GABLE_TEST(codegen_specializes_print_by_operand_type) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  print(\"text\");\n"
      "  print(true);\n"
      "  print(42);\n"
      "  let small: u8 = 7;\n"
      "  print(small);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "gable_rt_print_str("));
  GABLE_CHECK(contains(c, "gable_rt_print_bool("));
  GABLE_CHECK(contains(c, "gable_rt_print_i64("));
}

GABLE_TEST(codegen_compiles_multi_file_programs_into_one_unit) {
  const std::string c = emitCForProject(
      {{"main.gable",
        "import \"util.gable\";\n"
        "fn main() -> i32 { return helper(); }\n"},
       {"util.gable", "fn helper() -> i32 { return 0; }\n"}},
      "main.gable");
  GABLE_CHECK(contains(c, "static int32_t gable_fn_helper(void)"));
  GABLE_CHECK(contains(c, "static int32_t gable_fn_main(void)"));
  GABLE_CHECK(contains(c, "Generated by the Gable compiler from main.gable"));
}

GABLE_TEST(codegen_avoids_collisions_with_c_names) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  let int: i32 = 1;\n"
      "  let malloc: i32 = 2;\n"
      "  print(int + malloc);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "v0_int"));
  GABLE_CHECK(contains(c, "v1_malloc"));
}

GABLE_TEST(codegen_reports_the_compiler_version) {
  const std::string c = emitCFor(gabletest::kSimpleProgram());
  GABLE_CHECK(contains(c, "Compiler "));
  GABLE_CHECK(contains(c, "C17 backend"));
}

namespace {

/// Emits C for `program` while pretending the source file is called `name`.
std::string emitCWithSourceName(const std::string& program, const std::string& name) {
  gabletest::Analysis analysis = gabletest::analyze(program);
  if (!analysis.ok() || !analysis.frontend.module) {
    return std::string();
  }
  return gable::emitC(*analysis.frontend.module,
                      gable::CBackendOptions{name, gable::versionString()});
}

/// Everything after the banner comment, which is the part that must not change
/// when only the source name differs.
std::string bodyAfterBanner(const std::string& generatedC) {
  const std::size_t end = generatedC.find("*/");
  return end == std::string::npos ? generatedC : generatedC.substr(end + 2);
}

}  // namespace

// The banner includes the source path, which comes from the user. A path that
// contains a comment terminator used to end the banner early and let the rest of
// the name be compiled as C; that made a file name able to add declarations to
// the generated program. Every hostile name below must leave the translation
// unit itself untouched.
GABLE_TEST(codegen_escapes_hostile_source_names) {
  const std::string program = gabletest::kSimpleProgram();
  const std::string benign = emitCWithSourceName(program, "benign.gable");
  GABLE_CHECK(!benign.empty());

  const std::vector<std::string> hostile = {
      "evil*/int injected = 1; /*.gable",
      "evil*/int injected = 1; //.gable",
      "line\nint injected = 1;\n",
      "cr\rint injected = 1;\r",
      "quote\".gable",
      "back\\slash.gable",
      "*/",
      "*",
      "\\",
      "ütf8-ñame.gable",
      "#include <stdlib.h>.gable",
  };
  for (const std::string& name : hostile) {
    const std::string generated = emitCWithSourceName(program, name);
    GABLE_CHECK(!generated.empty());

    // The banner is one comment: its first line, the two note lines, and the
    // terminator. Everything in it stays inside the comment.
    GABLE_CHECK_EQ(bodyAfterBanner(benign), bodyAfterBanner(generated));

    // Nothing outside a comment mentions the payload.
    const std::string body = bodyAfterBanner(generated);
    GABLE_CHECK(body.find("injected") == std::string::npos);
    GABLE_CHECK(body.find("#include <stdlib.h>") == std::string::npos);

    // The banner is exactly three lines long: the first `*/` closes it, so the
    // prefix before that terminator contains two newlines and no more.
    const std::size_t firstTerminator = generated.find("*/");
    GABLE_CHECK(firstTerminator != std::string::npos);
    std::size_t newlinesBeforeTerminator = 0;
    for (std::size_t i = 0; i < firstTerminator; ++i) {
      if (generated[i] == '\n') {
        ++newlinesBeforeTerminator;
      }
    }
    GABLE_CHECK_EQ(std::size_t(2), newlinesBeforeTerminator);

    // The path is still recognisable, just escaped.
    GABLE_CHECK(generated.find("Generated by the Gable compiler from ") != std::string::npos);
  }
}

// Field names in Gable are identifiers, not C members: the generated member must
// avoid C keywords, C library macros, and the compiler's own generated names.
GABLE_TEST(codegen_mangles_field_names) {
  const std::string c = emitCFor(
      "struct Config { switch: i32, errno: i32, gable_fn_main: i32 }\n"
      "fn main() -> i32 {\n"
      "  var config: Config = Config(1, 2, 3);\n"
      "  config.errno = 20;\n"
      "  print(config.switch + config.errno + config.gable_fn_main);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "int32_t gable_f_switch;"));
  GABLE_CHECK(contains(c, "int32_t gable_f_errno;"));
  GABLE_CHECK(contains(c, "int32_t gable_f_gable_fn_main;"));
  GABLE_CHECK(contains(c, "v0_config).gable_f_errno = 20"));
  // No bare C keyword may appear where a member is declared or read.
  GABLE_CHECK(!contains(c, "int32_t switch;"));
  GABLE_CHECK(!contains(c, "int32_t errno;"));
}

// Prelude structs are defined by gable/rt.h, so their members keep the names the
// header uses.
GABLE_TEST(codegen_uses_prelude_field_names_as_declared) {
  const std::string c = emitCFor(gabletest::kSimpleProgram());
  GABLE_CHECK(contains(c, ".gable_f_") == false);
}
