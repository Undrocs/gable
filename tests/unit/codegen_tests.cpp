#include "test_framework.h"
#include "test_helpers.h"

#include <string>

using gabletest::emitCFor;

namespace {

bool contains(const std::string& text, const std::string& needle) {
  return text.find(needle) != std::string::npos;
}

}  // namespace

GABLE_TEST(codegen_emits_runtime_include_and_entry_point) {
  const std::string c = emitCFor(gabletest::kSimpleProgram());
  GABLE_CHECK(contains(c, "#include <gable/rt.h>"));
  GABLE_CHECK(contains(c, "int32_t gable_fn_main(void)"));
  GABLE_CHECK(contains(c, "int main(void)"));
  GABLE_CHECK(contains(c, "return (int)gable_fn_main();"));
}

GABLE_TEST(codegen_emits_string_literals_once) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  print(\"hello\");\n"
      "  print(\"hello\");\n"
      "  print(\"world\");\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "static const char gable_str_0[] = \"hello\";"));
  GABLE_CHECK(contains(c, "static const char gable_str_1[] = \"world\";"));
  const std::size_t first = c.find("gable_str_0[]");
  GABLE_CHECK(first != std::string::npos);
  GABLE_CHECK(c.find("gable_str_0[]", first + 1) == std::string::npos);
  GABLE_CHECK(contains(c, "gable_rt_print_str(gable_str_0)"));
  GABLE_CHECK(contains(c, "gable_rt_print_str(gable_str_1)"));
}

GABLE_TEST(codegen_escapes_string_contents) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  print(\"quote:\\\" backslash:\\\\ tab:\\t newline:\\n\");\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "\\\""));    // escaped double quote
  GABLE_CHECK(contains(c, "\\\\"));    // escaped backslash
  GABLE_CHECK(contains(c, "\\011"));    // tab
  GABLE_CHECK(contains(c, "\\012"));    // newline
}

GABLE_TEST(codegen_wraps_signed_arithmetic_in_unsigned) {
  const std::string c = emitCFor(
      "fn f(a: i32, b: i32) -> i32 { return a + b; }\n"
      "fn main() -> i32 { return f(1, 2); }\n");
  GABLE_CHECK(contains(c, "(int32_t)((uint32_t)(gable_v0_a) + (uint32_t)(gable_v1_b))"));
}

GABLE_TEST(codegen_uses_runtime_helpers_for_division) {
  const std::string c = emitCFor(
      "fn f(a: i64, b: i64) -> i64 { return a / b; }\n"
      "fn g(a: i64, b: i64) -> i64 { return a % b; }\n"
      "fn h(a: u32, b: u32) -> u32 { return a / b; }\n"
      "fn main() -> i32 { return 0; }\n");
  GABLE_CHECK(contains(c, "gable_rt_div_i64("));
  GABLE_CHECK(contains(c, "gable_rt_rem_i64("));
  GABLE_CHECK(contains(c, "gable_rt_div_u64("));
  GABLE_CHECK(contains(c, "(uint32_t)gable_rt_div_u64("));
}

GABLE_TEST(codegen_emits_narrowing_casts_for_small_types) {
  const std::string c = emitCFor(
      "fn f(a: i8, b: i8) -> i8 { return a * b; }\n"
      "fn main() -> i32 { return 0; }\n");
  GABLE_CHECK(contains(c, "int8_t gable_fn_f("));
  GABLE_CHECK(contains(c, "(int8_t)((uint8_t)(gable_v0_a) * (uint8_t)(gable_v1_b))"));
}

GABLE_TEST(codegen_dispatches_print_by_type) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  print(\"text\");\n"
      "  print(true);\n"
      "  print(1);\n"
      "  let u: u64 = 2;\n"
      "  print(u);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "gable_rt_print_str("));
  GABLE_CHECK(contains(c, "gable_rt_print_bool(true)"));
  GABLE_CHECK(contains(c, "gable_rt_print_i64("));
  GABLE_CHECK(contains(c, "gable_rt_print_u64("));
}

GABLE_TEST(codegen_emits_control_flow) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  var i: i32 = 0;\n"
      "  while i < 3 {\n"
      "    if i == 1 { print(\"one\"); } else { print(i); }\n"
      "    i = i + 1;\n"
      "  }\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "while ("));
  GABLE_CHECK(contains(c, "if ("));
  GABLE_CHECK(contains(c, "} else {"));
}

GABLE_TEST(codegen_declares_locals_at_function_scope) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  let a: i64 = 1;\n"
      "  { let b: i64 = 2; print(b); }\n"
      "  print(a);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "int64_t gable_v0_a;"));
  GABLE_CHECK(contains(c, "int64_t gable_v1_b;"));
  GABLE_CHECK(contains(c, "gable_v1_b = (int64_t)2;"));
}

GABLE_TEST(codegen_emits_forward_declarations_for_recursion) {
  const std::string c = emitCFor(
      "fn main() -> i32 { return count(3); }\n"
      "fn count(n: i32) -> i32 { if n <= 0 { return 0; } return count(n - 1); }\n");
  const std::size_t declaration = c.find("int32_t gable_fn_count(int32_t gable_v0_n);");
  const std::size_t definition = c.find("int32_t gable_fn_count(int32_t gable_v0_n) {");
  GABLE_CHECK(declaration != std::string::npos);
  GABLE_CHECK(definition != std::string::npos);
  GABLE_CHECK(declaration < definition);
}

GABLE_TEST(codegen_emits_boolean_literals) {
  const std::string c = emitCFor(
      "fn main() -> i32 {\n"
      "  let a: bool = true;\n"
      "  let b: bool = false;\n"
      "  print(a || b);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(contains(c, "gable_v0_a = true;"));
  GABLE_CHECK(contains(c, "gable_v1_b = false;"));
  GABLE_CHECK(contains(c, "(gable_v0_a) || (gable_v1_b)"));
}

GABLE_TEST(codegen_marks_generated_file) {
  const std::string c = emitCFor(gabletest::kSimpleProgram());
  GABLE_CHECK(contains(c, "Generated by the Gable compiler"));
  GABLE_CHECK(contains(c, "Do not edit"));
}

GABLE_TEST(codegen_output_is_stable) {
  const std::string first = emitCFor(gabletest::kSimpleProgram());
  const std::string second = emitCFor(gabletest::kSimpleProgram());
  GABLE_CHECK_EQ(first, second);
}
