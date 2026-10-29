// Diagnostic tests: the messages a beginner is most likely to meet.
//
// Each test is a small program someone learning Gable could plausibly write. The
// property asserted is what the message has to do: point at the mistake, name
// what Gable expected, and — when there is one obvious repair — say it. A
// message that is accurate but unhelpful fails these tests.
#include "test_framework.h"
#include "test_helpers.h"

#include <string>

using gabletest::Analysis;
using gabletest::analyze;
using gabletest::mentions;

namespace {

std::string inMain(const std::string& body) {
  return "fn main() {\n" + body + "\n}\n";
}

}  // namespace

// --------------------------------------------------------------------------
// Comparing and assigning
// --------------------------------------------------------------------------

// `=` where `==` belongs is the first mistake most people make, and it used to
// report a missing brace plus a stray statement after it.
GABLE_TEST(diagnostics_explain_assignment_in_an_if_condition) {
  const Analysis analysis = analyze(inMain("  var x = 1\n  if x = 2 { print(\"two\") }"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'=' assigns a value; compare with '=='"));
  GABLE_CHECK(mentions(analysis, "condition of an 'if' statement"));
  // One mistake, one error: the body and the rest of the function still parse.
  GABLE_CHECK_EQ(std::size_t(1), analysis.errorCount());
}

GABLE_TEST(diagnostics_explain_assignment_in_a_while_condition) {
  const Analysis analysis = analyze(inMain("  var x = 1\n  while x = 2 { x = x + 1 }"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "condition of a 'while' statement"));
}

GABLE_TEST(diagnostics_explain_assigning_to_an_immutable_binding) {
  const Analysis analysis = analyze(inMain("  let x = 1\n  x = 2"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "immutable binding 'x'"));
  GABLE_CHECK(mentions(analysis, "declare it with 'var'"));
  GABLE_CHECK(mentions(analysis, "binding declared here"));
}

GABLE_TEST(diagnostics_explain_assigning_to_a_parameter) {
  const Analysis analysis = analyze(
      "fn bump(count: int) -> int {\n  count = count + 1\n  return count\n}\n"
      "fn main() { print(bump(1)) }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot assign to parameter 'count'"));
  GABLE_CHECK(mentions(analysis, "copy the parameter into a 'var' binding"));
}

GABLE_TEST(diagnostics_explain_assigning_to_an_undeclared_name) {
  const Analysis analysis = analyze(inMain("  total = 2"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "undefined variable 'total'"));
  GABLE_CHECK(mentions(analysis, "var total = 0"));
}

// --------------------------------------------------------------------------
// Fields, calls, and parentheses
// --------------------------------------------------------------------------

GABLE_TEST(diagnostics_explain_calling_a_field) {
  const Analysis analysis = analyze(inMain("  let names: [str] = []\n  print(names.len())"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'len' is a value, not a method"));
  GABLE_CHECK_EQ(std::size_t(1), analysis.errorCount());
}

GABLE_TEST(diagnostics_explain_calling_a_struct_field) {
  const Analysis analysis = analyze(
      "struct Point { x: int, y: int }\n"
      "fn main() {\n  let p = Point(1, 2)\n  print(p.x())\n}\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'x' is a field of struct 'Point', not a method"));
}

GABLE_TEST(diagnostics_explain_an_unknown_field) {
  const Analysis analysis = analyze(
      "struct Point { x: int, y: int }\n"
      "fn main() -> int {\n  let p = Point(1, 2)\n  return p.z\n}\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "struct 'Point' has no field 'z'"));
  GABLE_CHECK(mentions(analysis, "available fields: x: int, y: int"));
  // The failed access is one mistake; the return type must not add a second.
  GABLE_CHECK_EQ(std::size_t(1), analysis.errorCount());
}

GABLE_TEST(diagnostics_explain_an_unknown_method) {
  const Analysis analysis = analyze(inMain("  let text = \"x\"\n  print(text.push(\"y\"))"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'str' has no method 'push'"));
  GABLE_CHECK(mentions(analysis, "a str supports '.len', '.trim()'"));
}

GABLE_TEST(diagnostics_explain_a_missing_argument) {
  const Analysis analysis = analyze(
      "fn add(a: int, b: int) -> int { return a + b }\n"
      "fn main() { print(add(1)) }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "expects 2 argument(s), found 1"));
}

GABLE_TEST(diagnostics_explain_a_wrong_argument_type) {
  const Analysis analysis = analyze(
      "fn twice(value: int) -> int { return value + value }\n"
      "fn main() { print(twice(\"two\")) }\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "argument 1 of 'twice' has type 'str', expected 'int'"));
}

// --------------------------------------------------------------------------
// Types and conversions
// --------------------------------------------------------------------------

GABLE_TEST(diagnostics_explain_a_missing_number_conversion) {
  const Analysis analysis = analyze(inMain("  let count: i32 = 1\n  let total: int = count"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "expected 'int', found 'i32'"));
  GABLE_CHECK(mentions(analysis, "does not convert between number types"));
  GABLE_CHECK(mentions(analysis, "'value as int'"));
}

GABLE_TEST(diagnostics_explain_a_literal_that_does_not_fit) {
  const Analysis analysis = analyze(inMain("  let small: i8 = 300"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "does not fit in type 'i8'"));
}

GABLE_TEST(diagnostics_explain_a_non_boolean_condition) {
  const Analysis analysis = analyze(inMain("  let count = 1\n  if count { print(count) }"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "must have type 'bool'"));
  GABLE_CHECK(mentions(analysis, "compare explicitly"));
}

GABLE_TEST(diagnostics_explain_converting_between_text_and_numbers) {
  const Analysis analysis = analyze(inMain("  let text = \"1\"\n  let value = text as i32"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "str_of_i64 or parse_i64"));
}

// --------------------------------------------------------------------------
// Loops and collections
// --------------------------------------------------------------------------

GABLE_TEST(diagnostics_explain_a_for_loop_without_a_range) {
  const Analysis analysis = analyze(inMain("  for i in 5 { print(i) }"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "iterates over an array, a list, or a range such as 0..n"));
}

GABLE_TEST(diagnostics_explain_printing_a_list) {
  const Analysis analysis = analyze(inMain("  var names: [str] = []\n  print(names)"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot print a value of type '[str]'"));
  GABLE_CHECK(mentions(analysis, "for item in values"));
}

GABLE_TEST(diagnostics_explain_an_empty_list_literal) {
  const Analysis analysis = analyze(inMain("  let values = []"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "cannot infer the type of an array literal"));
}

GABLE_TEST(diagnostics_explain_using_break_outside_a_loop) {
  const Analysis analysis = analyze(inMain("  break"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "'break' is only allowed inside a loop"));
}

// --------------------------------------------------------------------------
// One mistake is one message
// --------------------------------------------------------------------------

// Cascades are how a diagnostic list becomes unreadable: the parser
// resynchronises where everything afterwards also looks wrong.
GABLE_TEST(diagnostics_do_not_cascade_after_two_statements_on_one_line) {
  const Analysis analysis = analyze(inMain("  let a = 1 let b = 2"));
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(mentions(analysis, "expected a line break"));
  GABLE_CHECK(analysis.errorCount() <= 1);
}

GABLE_TEST(diagnostics_do_not_cascade_after_a_missing_brace) {
  const Analysis analysis = analyze(
      "fn main() {\n  if true {\n    print(\"a\")\n}\n");
  GABLE_CHECK(!analysis.ok());
  GABLE_CHECK(analysis.errorCount() <= 2);
}
