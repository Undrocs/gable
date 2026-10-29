#include "test_framework.h"
#include "test_helpers.h"

using gabletest::analyze;
using gabletest::analyzeProject;
using gabletest::mentions;

namespace {

const char* kPrefix = "fn main() -> int {\n";

std::string inMain(const std::string& body) { return std::string(kPrefix) + body + "\n}\n"; }

}  // namespace

GABLE_TEST(sema_accepts_valid_program) {
  const auto analysis = analyze(
      "fn add(a: i32, b: i32) -> i32 { return a + b; }\n"
      "fn main() -> int {\n"
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
      "  print(a);\n"
      "  return 0;"));
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
      "fn main() { print(bump(1)); }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot assign to parameter 'n'"));
  GABLE_CHECK(mentions(analysis, "copy the parameter into a 'var' binding"));
}

GABLE_TEST(sema_accepts_assignment_to_var) {
  const auto analysis = analyze(inMain("  var a: i32 = 1;\n  a = 2;\n  print(a);\n  return 0;"));
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_assignment_type_mismatch) {
  const auto analysis = analyze(inMain("  var a: i32 = 1;\n  a = true;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "type mismatch in assignment"));
}

GABLE_TEST(sema_rejects_mixed_integer_arithmetic) {
  const auto analysis = analyze(
      "fn main() -> int {\n"
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
      "fn main() -> int {\n"
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
  GABLE_CHECK(mentions(analysis, "type mismatch in left operand of logical operator"));
}

GABLE_TEST(sema_rejects_unsupported_string_operators) {
  const auto analysis = analyze(inMain("  let a: bool = \"x\" < \"y\";\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot be applied to values of type 'str'"));
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
  const auto analysis = analyze("fn f() -> i32 { return true; }\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "type mismatch in return value"));
}

GABLE_TEST(sema_rejects_missing_return_value) {
  const auto analysis = analyze("fn f() -> i32 { return; }\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "must return a value of type 'i32'"));
}

GABLE_TEST(sema_rejects_value_from_void_function) {
  const auto analysis =
      analyze("fn f() { return; }\nfn main() -> int { let x: i32 = f(); return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "expected 'i32', found 'void'"));
}

GABLE_TEST(sema_detects_missing_return_path) {
  const auto analysis =
      analyze("fn f(a: i32) -> i32 {\n  if a > 0 { return 1; }\n}\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "can reach the end of its body without returning"));
}

GABLE_TEST(sema_accepts_if_else_with_returns_on_both_paths) {
  const auto analysis = analyze(
      "fn f(a: i32) -> i32 {\n  if a > 0 { return 1; } else { return 2; }\n}\n"
      "fn main() { print(f(3)); }\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_accepts_while_true_without_return_check) {
  // A while loop is not treated as a terminating construct, so a function
  // ending in one still requires a return.
  const auto analysis = analyze(
      "fn f() -> i32 {\n  while true { return 1; }\n  return 0;\n}\n"
      "fn main() { print(f()); }\n");
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
      "fn main() -> int { return f(1); }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "expects 2 argument(s), found 1"));
}

GABLE_TEST(sema_rejects_wrong_argument_type) {
  const auto analysis = analyze(
      "fn f(a: i32) -> i32 { return a; }\n"
      "fn main() -> int { return f(true); }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "has type 'bool', expected 'i32'"));
}

GABLE_TEST(sema_rejects_duplicate_function) {
  const auto analysis =
      analyze("fn f() { return; }\nfn f() { return; }\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "duplicate declaration of function 'f'"));
}

GABLE_TEST(sema_rejects_duplicate_parameter_names) {
  const auto analysis =
      analyze("fn f(a: i32, a: i32) -> i32 { return a; }\nfn main() -> int { return 0; }\n");
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
      analyze("fn f(a: void) { return; }\nfn main() -> int { return 0; }\n");
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
  const auto analysis = analyze("fn f() { return; }\nfn main() -> int { let a: i32 = f;\n return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot be used as a value"));
}

GABLE_TEST(sema_allows_recursion_and_forward_calls) {
  const auto analysis = analyze(
      "fn main() -> int { print(later(3)); return 0; }\n"
      "fn later(n: i32) -> i32 { if n <= 1 { return 1; } return n * later(n - 1); }\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_requires_main) {
  const auto analysis = analyze("fn other() { return; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "no 'main' function found"));
}

GABLE_TEST(sema_rejects_main_with_parameters) {
  const auto analysis = analyze("fn main(argc: int) -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'main' must not take parameters"));
}

GABLE_TEST(sema_accepts_main_without_a_result) {
  // `fn main()` exits with status 0; only an explicit `-> int` returns a status.
  const auto analysis = analyze("fn main() { print(\"done\"); }\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_accepts_main_returning_int) {
  const auto analysis = analyze("fn main() -> int { return 3; }\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_main_with_a_non_integer_result) {
  const auto analysis = analyze("fn main() -> bool { return true; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'main' must return 'int'"));
}

GABLE_TEST(sema_reserves_print) {
  const auto function = analyze("fn print(x: i32) { return; }\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!function.ok());
  GABLE_CHECK(mentions(function, "reserved for a built-in function"));

  const auto variable = analyze(inMain("  let print: i32 = 1;\n  return 0;"));
  GABLE_CHECK(!variable.ok());
  GABLE_CHECK(mentions(variable, "reserved for a built-in function"));
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
      "fn main() -> int {\n"
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
      "fn main() -> int {\n"
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

// --------------------------------------------------------------------------
// v0.2: structs
// --------------------------------------------------------------------------

GABLE_TEST(sema_accepts_struct_declaration_construction_and_field_access) {
  const auto analysis = analyze(
      "struct Point { x: i32, y: i32 }\n"
      "fn main() -> int {\n"
      "  var p: Point = Point(1, 2);\n"
      "  p.x = p.x + p.y;\n"
      "  print(p.x);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_struct_with_wrong_field_count) {
  const auto analysis = analyze(
      "struct Point { x: i32, y: i32 }\n"
      "fn main() -> int { let p: Point = Point(1); return p.x; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "has 2 field(s) but 1 value(s) were given"));
}

GABLE_TEST(sema_rejects_struct_field_type_mismatch) {
  const auto analysis = analyze(
      "struct Point { x: i32, y: i32 }\n"
      "fn main() -> int { let p: Point = Point(true, 2); return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "field 'x' of struct 'Point' has type 'i32'"));
}

GABLE_TEST(sema_rejects_unknown_field) {
  const auto analysis = analyze(
      "struct Point { x: i32 }\n"
      "fn main() -> int { let p: Point = Point(1); return p.z; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "struct 'Point' has no field 'z'"));
  GABLE_CHECK(mentions(analysis, "available fields: x: i32"));
}

GABLE_TEST(sema_rejects_duplicate_field) {
  const auto analysis = analyze("struct P { x: i32, x: i32 }\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "duplicate field 'x' in struct 'P'"));
}

GABLE_TEST(sema_rejects_struct_that_contains_itself) {
  const auto analysis =
      analyze("struct Node { value: i32, next: Node }\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "contains itself by value"));
}

GABLE_TEST(sema_rejects_struct_field_of_void_type) {
  const auto analysis = analyze("struct Empty { x: void }\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot have type 'void'"));
}

GABLE_TEST(sema_accepts_nested_structs) {
  const auto analysis = analyze(
      "struct Inner { value: i32 }\n"
      "struct Outer { inner: Inner, flag: bool }\n"
      "fn main() -> int {\n"
      "  let o: Outer = Outer(Inner(7), true);\n"
      "  print(o.inner.value);\n"
      "  print(o.flag);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_printing_a_struct) {
  const auto analysis = analyze(
      "struct Point { x: i32 }\n"
      "fn main() -> int { let p: Point = Point(1); print(p); return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot print a value of type 'Point'"));
}

GABLE_TEST(sema_rejects_value_struct_comparison) {
  const auto analysis = analyze(
      "struct Point { x: i32 }\n"
      "fn main() -> int { let a: Point = Point(1); let b: Point = Point(1);\n"
      "  if a == b { return 1; } return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot be applied to values of type 'Point'"));
}

// --------------------------------------------------------------------------
// v0.2: arrays
// --------------------------------------------------------------------------

GABLE_TEST(sema_accepts_fixed_size_arrays) {
  const auto analysis = analyze(
      "fn main() -> int {\n"
      "  var values: [i32; 3] = [1, 2, 3];\n"
      "  values[0] = values[1] + values[2];\n"
      "  print(values[0]);\n"
      "  print(values.len);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_array_literal_without_type) {
  const auto analysis = analyze(inMain("  let values = [1, 2, 3];\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot infer the type of an array literal"));
}

GABLE_TEST(sema_rejects_array_literal_with_wrong_length) {
  const auto analysis = analyze(inMain("  let values: [i32; 4] = [1, 2, 3];\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "needs 4 element(s), found 3"));
}

GABLE_TEST(sema_rejects_array_literal_element_type) {
  const auto analysis = analyze(inMain("  let values: [i32; 2] = [1, true];\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "array element"));
}

GABLE_TEST(sema_rejects_indexing_a_non_array) {
  const auto analysis = analyze(inMain("  let text: str = \"abc\";\n  print(text[0]);\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "only arrays and lists can be indexed"));
  GABLE_CHECK(mentions(analysis, "str_slice"));
}

GABLE_TEST(sema_rejects_non_integer_index) {
  const auto analysis = analyze(inMain("  let values: [i32; 2] = [1, 2];\n  print(values[true]);\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "index must be an integer"));
}

GABLE_TEST(sema_accepts_nested_arrays_and_array_fields) {
  const auto analysis = analyze(
      "struct Grid { cells: [i32; 4] }\n"
      "fn main() -> int {\n"
      "  let rows: [[i32; 2]; 2] = [[1, 2], [3, 4]];\n"
      "  let grid: Grid = Grid([1, 2, 3, 4]);\n"
      "  print(rows[1][0]);\n"
      "  print(grid.cells.len);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_zero_length_array) {
  const auto analysis = analyze(inMain("  let values: [i32; 0] = [1];\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "array length must be greater than zero"));
}

// --------------------------------------------------------------------------
// v0.2: strings
// --------------------------------------------------------------------------

GABLE_TEST(sema_accepts_string_concatenation_equality_and_length) {
  const auto analysis = analyze(inMain(
      "  let a: str = \"x\" + \"y\";\n"
      "  let b: bool = a == \"xy\";\n"
      "  let c: bool = a != \"xy\";\n"
      "  print(a.len);\n"
      "  print(b && c);\n"
      "  return 0;"));
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_string_arithmetic) {
  const auto analysis = analyze(inMain("  let a: str = \"x\" - \"y\";\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot be applied to values of type 'str'"));
}

GABLE_TEST(sema_rejects_length_of_an_integer) {
  const auto analysis = analyze(inMain("  let a: i32 = 1;\n  print(a.len);\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'len' is only defined for strings, arrays, and lists"));
}

// --------------------------------------------------------------------------
// v0.2: conversions
// --------------------------------------------------------------------------

GABLE_TEST(sema_accepts_explicit_integer_conversions) {
  const auto analysis = analyze(
      "fn main() -> int {\n"
      "  let big: i64 = 300;\n"
      "  let small: i32 = big as i32;\n"
      "  let unsigned: u8 = small as u8;\n"
      "  let back: i64 = unsigned as i64;\n"
      "  print(back);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_non_integer_casts) {
  const auto analysis = analyze(inMain("  let a: i64 = \"x\" as i64;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "is not an integer conversion"));
}

GABLE_TEST(sema_rejects_implicit_wide_to_narrow_conversion) {
  const auto analysis = analyze(inMain("  let a: i64 = 1;\n  let b: i32 = a;\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "type mismatch in initializer"));
}

// --------------------------------------------------------------------------
// v0.2: loops
// --------------------------------------------------------------------------

GABLE_TEST(sema_accepts_for_loops_over_ranges_and_arrays) {
  const auto analysis = analyze(
      "fn main() -> int {\n"
      "  var total: int = 0;\n"
      "  for i in 0..10 {\n"
      "    if i == 3 { continue; }\n"
      "    if i == 8 { break; }\n"
      "    total = total + i;\n"
      "  }\n"
      "  let values: [int; 2] = [1, 2];\n"
      "  for value in values { total = total + value; }\n"
      "  print(total);\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_break_and_continue_outside_loops) {
  const auto broken = analyze(inMain("  break;\n  return 0;"));
  GABLE_CHECK(!broken.ok());
  GABLE_CHECK(mentions(broken, "'break' is only allowed inside a loop"));

  const auto continued = analyze(inMain("  continue;\n  return 0;"));
  GABLE_CHECK(!continued.ok());
  GABLE_CHECK(mentions(continued, "'continue' is only allowed inside a loop"));
}

GABLE_TEST(sema_rejects_for_over_a_non_array) {
  const auto analysis = analyze(inMain("  for c in \"abc\" { print(c); }\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "iterates over an array, a list, or a range"));
}

GABLE_TEST(sema_rejects_range_of_non_integers) {
  const auto analysis = analyze(inMain("  for x in 0.0..1.0 { print(x); }\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
}

GABLE_TEST(sema_rejects_assigning_to_loop_variable) {
  const auto analysis = analyze(inMain("  for i in 0..3 { i = 5; }\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot assign to immutable binding 'i'"));
}

GABLE_TEST(sema_scopes_loop_variable_to_the_body) {
  const auto analysis = analyze(inMain("  for i in 0..3 { print(i); }\n  return i;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "undefined variable 'i'"));
}

// --------------------------------------------------------------------------
// v0.2: builtins
// --------------------------------------------------------------------------

GABLE_TEST(sema_accepts_argument_and_file_builtins) {
  const auto analysis = analyze(
      "fn main() -> int {\n"
      "  if arg_count() > 1 {\n"
      "    let first: str = arg(1);\n"
      "    print(first);\n"
      "  }\n"
      "  let written = fs_write_text(\"/tmp/gable-test.txt\", \"text\");\n"
      "  if !written.ok { eprint(written.error); }\n"
      "  let read = fs_read_text(\"/tmp/gable-test.txt\");\n"
      "  if read.ok { print(read.text); }\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_checks_builtin_argument_types) {
  const auto analysis = analyze(inMain("  print(arg(\"one\"));\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "argument 1 of 'arg' has type 'str', expected 'int'"));
}

GABLE_TEST(sema_checks_builtin_arity) {
  const auto analysis = analyze(inMain("  print(arg_count(1));\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "expects 0 argument(s), found 1"));
}

GABLE_TEST(sema_rejects_redeclaring_a_builtin) {
  const auto analysis =
      analyze("fn str_trim(text: str) -> str { return text; }\nfn main() -> int { return 0; }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "reserved for a built-in function"));
}

GABLE_TEST(sema_rejects_unknown_field_on_a_builtin_result) {
  const auto analysis = analyze(inMain("  let read = fs_read_text(\"x\");\n  print(read.contents);\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "struct 'FileReadResult' has no field 'contents'"));
}

// --------------------------------------------------------------------------
// v0.2: multiple files
// --------------------------------------------------------------------------

GABLE_TEST(sema_resolves_declarations_across_files) {
  const auto analysis = analyzeProject(
      {{"main.gable",
        "import \"util.gable\";\n"
        "fn main() -> int { print(doubled(21)); return 0; }\n"},
       {"util.gable", "fn doubled(value: i32) -> i32 { return value * 2; }\n"}},
      "main.gable");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_shares_structs_across_files) {
  const auto analysis = analyzeProject(
      {{"main.gable",
        "import \"types.gable\";\n"
        "fn main() -> int { let p: Point = Point(1, 2); print(p.x); return 0; }\n"},
       {"types.gable", "struct Point { x: i32, y: i32 }\n"}},
      "main.gable");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_reports_missing_import_file) {
  const auto analysis =
      analyzeProject({{"main.gable", "import \"nope.gable\";\nfn main() -> int { return 0; }\n"}},
                     "main.gable");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot import 'nope.gable'"));
  GABLE_CHECK(mentions(analysis, "resolved relative to the importing file"));
}

GABLE_TEST(sema_detects_import_cycles) {
  const auto analysis = analyzeProject(
      {{"a.gable", "import \"b.gable\";\nfn main() -> int { return 0; }\n"},
       {"b.gable", "import \"a.gable\";\n"}},
      "a.gable");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "import cycle"));
}

GABLE_TEST(sema_loads_each_file_once) {
  const auto analysis = analyzeProject(
      {{"main.gable",
        "import \"left.gable\";\n"
        "import \"right.gable\";\n"
        "fn main() { print(shared()); }\n"},
       {"left.gable", "import \"shared.gable\";\n"},
       {"right.gable", "import \"shared.gable\";\n"},
       {"shared.gable", "fn shared() -> i32 { return 0; }\n"}},
      "main.gable");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_duplicate_function_across_files) {
  const auto analysis = analyzeProject(
      {{"main.gable",
        "import \"other.gable\";\n"
        "fn twice() -> i32 { return 1; }\n"
        "fn main() -> int { return twice(); }\n"},
       {"other.gable", "fn twice() -> i32 { return 2; }\n"}},
      "main.gable");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "duplicate declaration of function 'twice'"));
  GABLE_CHECK(mentions(analysis, "previous declaration"));
}

GABLE_TEST(sema_diagnostics_name_the_offending_file) {
  const auto analysis = analyzeProject(
      {{"main.gable", "import \"broken.gable\";\nfn main() -> int { return missing(); }\n"},
       {"broken.gable", "fn other() -> i32 { return 0; }\n"}},
      "main.gable");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "unknown function 'missing'"));
  GABLE_CHECK(mentions(analysis, "main.gable:"));
}

// --------------------------------------------------------------------------
// v0.3: growable lists
// --------------------------------------------------------------------------

GABLE_TEST(sema_accepts_a_list_type_and_list_push) {
  const auto analysis = analyze(inMain(
      "  var names: [str] = [];\n"
      "  list_push(names, \"a\");\n"
      "  print(names.len);\n"
      "  print(names[0]);\n"
      "  for name in names { print(name); }\n"
      "  return 0;"));
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_checks_the_element_type_of_list_push) {
  const auto analysis = analyze(inMain("  var counts: [i64] = [];\n  list_push(counts, \"many\");\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "type mismatch in list element"));
  GABLE_CHECK(mentions(analysis, "expected 'int', found 'str'"));
}

GABLE_TEST(sema_lowers_string_interpolation) {
  const auto analysis = analyze(inMain(
      "  let count: int = 2;\n"
      "  let done: bool = true;\n"
      "  print(\"rows: $count, done: $done\");\n"
      "  return 0;"));
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_rejects_interpolating_a_struct) {
  const auto analysis = analyze(
      "struct Point { x: int }\n"
      "fn main() {\n"
      "  let p = Point(1);\n"
      "  print(\"point: $p\");\n"
      "}\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot put a value of type 'Point' into a string literal"));
}

GABLE_TEST(sema_rejects_list_push_on_a_non_list) {
  const auto analysis = analyze(inMain("  let count: i32 = 1;\n  list_push(count, 2);\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "list_push needs a list"));
  GABLE_CHECK(mentions(analysis, "[str]"));
}

GABLE_TEST(sema_requires_an_expected_type_for_a_list_literal) {
  const auto analysis = analyze(inMain("  let values = [];\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot infer the type of an array literal"));
  GABLE_CHECK(mentions(analysis, "[i32]"));
}

GABLE_TEST(sema_rejects_wrong_element_in_a_list_literal) {
  const auto analysis = analyze(inMain("  let values: [i32] = [1, \"two\"];\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "type mismatch in list element"));
}

GABLE_TEST(sema_iterates_a_list_with_its_element_type) {
  const auto analysis = analyze(
      "struct Item { name: str }\n"
      "fn main() -> int {\n"
      "  var items: [Item] = [];\n"
      "  for item in items { print(item.name); }\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(analysis.ok());
}

GABLE_TEST(sema_reports_the_type_of_a_list_in_messages) {
  const auto analysis = analyze(inMain("  var values: [str] = [];\n  print(values);\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'[str]'"));
  GABLE_CHECK(mentions(analysis, "for item in values"));
}

GABLE_TEST(sema_types_split_and_join) {
  GABLE_CHECK(analyze(inMain(
                   "  let parts = str_split(\"a,b\", \",\");\n"
                   "  print(parts.len);\n"
                   "  print(parts[0]);\n"
                   "  print(str_join(parts, \"-\"));\n"
                   "  return 0;"))
                   .ok());
}

GABLE_TEST(sema_rejects_joining_numbers) {
  const auto analysis = analyze(inMain("  let numbers: [i64] = [];\n  print(str_join(numbers, \",\"));\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "argument 1 of 'str_join'"));
  GABLE_CHECK(mentions(analysis, "'[str]'"));
}

GABLE_TEST(sema_rejects_splitting_a_non_string) {
  const auto analysis = analyze(inMain("  print(str_split(1, \",\"));\n  return 0;"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "argument 1 of 'str_split'"));
}
