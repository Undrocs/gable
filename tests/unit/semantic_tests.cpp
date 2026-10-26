#include "test_framework.h"
#include "test_helpers.h"

using gabletest::analyze;
using gabletest::mentions;

namespace {

const char* kPrefix = "fn main() -> i32 {\n";

std::string inMain(const std::string& body) { return std::string(kPrefix) + body + "\n}\n"; }

}  // namespace

GABLE_TEST(sema_accepts_valid_program) {
  const auto analysis = analyze(
      "fn add(a: i32, b: i32) -> i32 { return a + b; }\n"
      "fn main() -> i32 {\n"
      "  var x: i32 = add(1, 2);\n"
      "  var y: i64 = 10;\n"
      "  y = y * 2;\n"
      "  if x > 0 && y > 0 { print(x); print(y); } else { print(\"none\"); }\n"
      "  while x < 3 { x = x + 1; }\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
  GABLE_CHECK_EQ(std::size_t(0), analysis.errorCount());
}

GABLE_TEST(sema_rejects_undefined_variable) {
  const auto analysis = analyze(inMain("  return missing;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "undefined variable 'missing'"));
}

GABLE_TEST(sema_rejects_duplicate_declaration_in_same_scope) {
  const auto analysis = analyze(inMain("  let a: i32 = 1;\n  let a: i32 = 2;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "duplicate declaration of 'a'"));
}

GABLE_TEST(sema_allows_shadowing_in_nested_block) {
  const auto analysis = analyze(inMain(
      "  let a: i32 = 1;\n"
      "  { let a: i32 = 2; print(a); }\n"
      "  return a;"));
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_assignment_to_immutable_binding) {
  const auto analysis = analyze(inMain("  let a: i32 = 1;\n  a = 2;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot assign to immutable binding 'a'"));
}

GABLE_TEST(sema_rejects_assignment_to_parameter) {
  const auto analysis = analyze(
      "fn bump(n: i32) -> i32 {\n  n = n + 1;\n  return n;\n}\n"
      "fn main() -> i32 { return bump(1); }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot assign to parameter 'n'"));
  GABLE_CHECK(mentions(analysis, "copy the parameter into a 'var' binding"));
}

GABLE_TEST(sema_accepts_assignment_to_var) {
  const auto analysis = analyze(inMain("  var a: i32 = 1;\n  a = 2;\n  return a;"));
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_assignment_type_mismatch) {
  const auto analysis = analyze(inMain("  var a: i32 = 1;\n  a = true;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "type mismatch in assignment"));
}

GABLE_TEST(sema_rejects_mixed_integer_arithmetic) {
  const auto analysis = analyze(
      "fn main() -> i32 {\n"
      "  let a: i32 = 1;\n"
      "  let b: i64 = 2;\n"
      "  let c: i64 = a + b;\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "have different types"));
}

GABLE_TEST(sema_types_literals_from_context) {
  const auto analysis = analyze(
      "fn take(x: i64) -> i64 { return x; }\n"
      "fn main() -> i32 {\n"
      "  let a: i64 = 3000000000;\n"
      "  let b: i64 = a + 1;\n"
      "  let c: i64 = take(-5);\n"
      "  print(b); print(c);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_literal_that_does_not_fit) {
  const auto analysis = analyze(inMain("  let a: i32 = 3000000000;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "does not fit in type 'i32'"));
}

GABLE_TEST(sema_accepts_minimum_i64_literal) {
  const auto analysis =
      analyze(inMain("  let a: i64 = -9223372036854775808;\n  print(a);\n  return 0;"));
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_overflowing_negative_literal) {
  const auto analysis =
      analyze(inMain("  let a: i32 = -2147483649;\n  print(a);\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "does not fit in type 'i32'"));
}

GABLE_TEST(sema_rejects_non_boolean_condition) {
  const auto analysis = analyze(inMain("  if 1 { return 0; }\n  return 1;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "must have type 'bool'"));
}

GABLE_TEST(sema_rejects_logical_operands_of_wrong_type) {
  const auto analysis = analyze(inMain("  let a: i32 = 1;\n  let b: bool = a && true;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "requires 'bool' operands"));
}

GABLE_TEST(sema_rejects_string_comparison) {
  const auto analysis = analyze(inMain("  let a: bool = \"x\" == \"y\";\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "string comparison is not available"));
}

GABLE_TEST(sema_rejects_unary_minus_on_bool) {
  const auto analysis = analyze(inMain("  let a: i32 = -true;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "requires an integer operand"));
}

GABLE_TEST(sema_rejects_unary_minus_on_unsigned) {
  const auto analysis = analyze(inMain("  let a: u32 = 1;\n  let b: u32 = -a;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot be applied to the unsigned type 'u32'"));
}

GABLE_TEST(sema_rejects_constant_division_by_zero) {
  const auto analysis = analyze(inMain("  let a: i32 = 4 / 0;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "division by zero"));
}

GABLE_TEST(sema_rejects_constant_remainder_by_zero) {
  const auto analysis = analyze(inMain("  let a: i32 = 4 % 0;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "remainder by zero"));
}

GABLE_TEST(sema_rejects_wrong_return_type) {
  const auto analysis = analyze("fn f() -> i32 { return true; }\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "type mismatch in return value"));
}

GABLE_TEST(sema_rejects_missing_return_value) {
  const auto analysis = analyze("fn f() -> i32 { return; }\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "must return a value of type 'i32'"));
}

GABLE_TEST(sema_rejects_value_from_void_function) {
  const auto analysis =
      analyze("fn f() { return; }\nfn main() -> i32 { let x: i32 = f(); return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "expected 'i32', found 'void'"));
}

GABLE_TEST(sema_detects_missing_return_path) {
  const auto analysis =
      analyze("fn f(a: i32) -> i32 {\n  if a > 0 { return 1; }\n}\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "can reach the end of its body without returning"));
}

GABLE_TEST(sema_accepts_if_else_with_returns_on_both_paths) {
  const auto analysis = analyze(
      "fn f(a: i32) -> i32 {\n  if a > 0 { return 1; } else { return 2; }\n}\n"
      "fn main() -> i32 { return f(3); }\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_accepts_while_true_without_return_check) {
  // A while loop is not treated as a terminating construct, so a function
  // ending in one still requires a return.
  const auto analysis = analyze(
      "fn f() -> i32 {\n  while true { return 1; }\n  return 0;\n}\n"
      "fn main() -> i32 { return f(); }\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_unknown_function) {
  const auto analysis = analyze(inMain("  let a: i32 = nope(1);\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "unknown function 'nope'"));
}

GABLE_TEST(sema_rejects_wrong_argument_count) {
  const auto analysis = analyze(
      "fn f(a: i32, b: i32) -> i32 { return a + b; }\n"
      "fn main() -> i32 { return f(1); }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "expects 2 argument(s), found 1"));
}

GABLE_TEST(sema_rejects_wrong_argument_type) {
  const auto analysis = analyze(
      "fn f(a: i32) -> i32 { return a; }\n"
      "fn main() -> i32 { return f(true); }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "has type 'bool', expected 'i32'"));
}

GABLE_TEST(sema_rejects_duplicate_function) {
  const auto analysis =
      analyze("fn f() { return; }\nfn f() { return; }\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "duplicate declaration of function 'f'"));
}

GABLE_TEST(sema_rejects_duplicate_parameter_names) {
  const auto analysis =
      analyze("fn f(a: i32, a: i32) -> i32 { return a; }\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "duplicate parameter name 'a'"));
}

GABLE_TEST(sema_rejects_unknown_type) {
  const auto analysis = analyze(inMain("  let a: integer = 1;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "unknown type 'integer'"));
}

GABLE_TEST(sema_rejects_void_variable) {
  const auto analysis = analyze(inMain("  let a: void = 1;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot have type 'void'"));
}

GABLE_TEST(sema_rejects_void_parameter) {
  const auto analysis =
      analyze("fn f(a: void) { return; }\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot have type 'void'"));
}

GABLE_TEST(sema_rejects_use_before_declaration) {
  const auto analysis = analyze(inMain("  a = 1;\n  let a: i32 = 2;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "undefined variable 'a'"));
}

GABLE_TEST(sema_rejects_self_referential_initializer) {
  const auto analysis = analyze(inMain("  let a: i32 = a;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "undefined variable 'a'"));
}

GABLE_TEST(sema_rejects_variable_used_outside_block) {
  const auto analysis = analyze(inMain("  { let a: i32 = 1; }\n  return a;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "undefined variable 'a'"));
}

GABLE_TEST(sema_rejects_function_used_as_value) {
  const auto analysis = analyze("fn f() { return; }\nfn main() -> i32 { let a: i32 = f;\n return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot be used as a value"));
}

GABLE_TEST(sema_allows_recursion_and_forward_calls) {
  const auto analysis = analyze(
      "fn main() -> i32 { print(later(3)); return 0; }\n"
      "fn later(n: i32) -> i32 { if n <= 1 { return 1; } return n * later(n - 1); }\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_requires_main) {
  const auto analysis = analyze("fn other() { return; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "no 'main' function found"));
}

GABLE_TEST(sema_rejects_main_with_parameters) {
  const auto analysis = analyze("fn main(argc: i32) -> i32 { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'main' must not take parameters"));
}

GABLE_TEST(sema_rejects_main_returning_void) {
  const auto analysis = analyze("fn main() { return; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'main' must return i32"));
}

GABLE_TEST(sema_requires_main_to_return_i32) {
  const auto analysis = analyze("fn main() -> i64 { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'main' must return i32, found 'i64'"));
}

GABLE_TEST(sema_reserves_print) {
  const auto function = analyze("fn print(x: i32) { return; }\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(!function.ok());
  GABLE_CHECK(mentions(function, "reserved for the built-in print function"));

  const auto variable = analyze(inMain("  let print: i32 = 1;\n  return 0;"));
  GABLE_CHECK(!variable.ok());
  GABLE_CHECK(mentions(variable, "reserved for the built-in print function"));
}

GABLE_TEST(sema_checks_print_arity_and_types) {
  const auto none = analyze(inMain("  print();\n  return 0;"));
  GABLE_CHECK(!none.ok());
  GABLE_CHECK(mentions(none, "print expects exactly 1 argument, found 0"));

  const auto many = analyze(inMain("  print(1, 2);\n  return 0;"));
  GABLE_CHECK(!many.ok());
  GABLE_CHECK(mentions(many, "print expects exactly 1 argument, found 2"));
}

GABLE_TEST(sema_rejects_using_void_print_result) {
  const auto analysis = analyze(inMain("  let a: i32 = print(\"x\");\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "found 'void'"));
}

GABLE_TEST(sema_rejects_unsigned_suffix) {
  const auto analysis = analyze(inMain("  let a: u32 = 5u;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "the 'u' integer suffix is not supported"));
}

GABLE_TEST(sema_accepts_all_integer_types_and_bool_and_str) {
  const auto analysis = analyze(
      "fn main() -> i32 {\n"
      "  let a: i8 = -128;\n"
      "  let b: i16 = -32768;\n"
      "  let c: i32 = -2147483648;\n"
      "  let d: i64 = -9223372036854775808;\n"
      "  let e: u8 = 255;\n"
      "  let f: u16 = 65535;\n"
      "  let g: u32 = 4294967295;\n"
      "  let h: u64 = 18446744073709551615;\n"
      "  let i: bool = true;\n"
      "  let j: str = \"text\";\n"
      "  print(a); print(b); print(c); print(d);\n"
      "  print(e); print(f); print(g); print(h);\n"
      "  print(i); print(j);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_out_of_range_unsigned_literal) {
  const auto analysis = analyze(inMain("  let a: u8 = 256;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "does not fit in type 'u8'"));
}

GABLE_TEST(sema_allows_comparisons_of_matching_types) {
  const auto analysis = analyze(
      "fn main() -> i32 {\n"
      "  let a: i64 = 5;\n"
      "  let b: bool = a >= 5 && a != 3 && a < 10 && a <= 5;\n"
      "  let c: bool = b == true;\n"
      "  print(c);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_reports_all_errors_in_one_run) {
  const auto analysis = analyze(inMain(
      "  let a: i32 = true;\n"
      "  b = 1;\n"
      "  let c: nope = 1;\n"
      "  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(analysis.errorCount() >= 3);
  GABLE_CHECK(mentions(analysis, "type mismatch in initializer"));
  GABLE_CHECK(mentions(analysis, "undefined variable 'b'"));
  GABLE_CHECK(mentions(analysis, "unknown type 'nope'"));
}

GABLE_TEST(sema_diagnostics_include_source_context) {
  const auto analysis = analyze(inMain("  return missing;"));
  GABLE_CHECK(analysis.rendered.find("test.gable:2:10: error:") != std::string::npos);
  GABLE_CHECK(analysis.rendered.find("return missing;") != std::string::npos);
}
