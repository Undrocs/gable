// Tests for `gable fmt`: the formatter may rewrite whitespace and nothing else.
//
// Every case here is about two properties at once — the exact layout the
// formatter produces, and the promise that its output means the same program.
// The second is why the cases keep tokens where they were: a line break ends a
// statement in Gable, so a formatter that re-wrapped code could change it.
#include <string>

#include "gable/format.h"
#include "test_framework.h"

using gable::FormatOutcome;
using gable::formatSource;

namespace {

/// Formats `source` and returns the text, asserting success.
std::string format(const std::string& source) {
  const FormatOutcome outcome = formatSource(source);
  GABLE_CHECK(outcome.ok);
  return outcome.text;
}

}  // namespace

GABLE_TEST(format_indents_blocks_by_four_spaces) {
  const std::string input =
      "fn main() {\n"
      "  if true {\n"
      "        print(\"x\")\n"
      "  }\n"
      "}\n";
  const std::string expected =
      "fn main() {\n"
      "    if true {\n"
      "        print(\"x\")\n"
      "    }\n"
      "}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_aligns_a_closing_brace_with_its_block) {
  const std::string input = "fn main() {\n        print(1)\n    }\n";
  const std::string expected = "fn main() {\n    print(1)\n}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_collapses_blank_lines_and_drops_leading_ones) {
  const std::string input = "\n\nfn a() {\n}\n\n\n\nfn b() {\n}\n";
  const std::string expected = "fn a() {\n}\n\nfn b() {\n}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_spaces_operators_on_both_sides) {
  const std::string input = "fn main() {\n    let n=1+2*3\n}\n";
  const std::string expected = "fn main() {\n    let n = 1 + 2 * 3\n}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_keeps_a_prefix_minus_attached_and_a_binary_one_spaced) {
  const std::string input = "fn main() {\n    let a = -1\n    let b = a -1\n    let c = a- 1\n}\n";
  const std::string expected =
      "fn main() {\n"
      "    let a = -1\n"
      "    let b = a - 1\n"
      "    let c = a - 1\n"
      "}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_attaches_calls_indexes_and_field_access) {
  const std::string input =
      "fn main() {\n"
      "    let xs : [ int ] = [ 1 , 2 ]\n"
      "    print( xs [ 0 ] .len )\n"
      "}\n";
  const std::string expected =
      "fn main() {\n"
      "    let xs: [int] = [1, 2]\n"
      "    print(xs[0].len)\n"
      "}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_leaves_a_line_break_where_the_programmer_put_it) {
  // Tokens never move between lines: this loop stays on one line, and the two
  // statements stay on two.
  const std::string input = "fn main() {\n  for i in 0..2 {print(i)}\n  print(1)\n}\n";
  const std::string expected =
      "fn main() {\n"
      "    for i in 0..2 { print(i) }\n"
      "    print(1)\n"
      "}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_indents_the_continuation_of_a_multi_line_list) {
  const std::string input =
      "fn main() {\n"
      "    let xs = [\n"
      "      1,\n"
      "      2,\n"
      "    ]\n"
      "}\n";
  const std::string expected =
      "fn main() {\n"
      "    let xs = [\n"
      "        1,\n"
      "        2,\n"
      "    ]\n"
      "}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_keeps_a_line_comment_on_its_line) {
  const std::string input = "// about the file\nfn main() {\n      //   indented comment\n    print(1)// after code\n}\n";
  const std::string expected =
      "// about the file\n"
      "fn main() {\n"
      "    //   indented comment\n"
      "    print(1) // after code\n"
      "}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_keeps_a_block_comment_as_written) {
  const std::string input =
      "/* one line */\n"
      "fn main() {\n"
      "  /* first\n"
      "     second */\n"
      "  print(1)\n"
      "}\n";
  const std::string expected =
      "/* one line */\n"
      "fn main() {\n"
      "    /* first\n"
      "     second */\n"
      "    print(1)\n"
      "}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_does_not_touch_the_inside_of_a_string) {
  const std::string input =
      "fn main() {\n"
      "  let message = \"keep   this   spacing\"\n"
      "  print(\"  ${ message }  \")\n"
      "}\n";
  const std::string expected =
      "fn main() {\n"
      "    let message = \"keep   this   spacing\"\n"
      "    print(\"  ${ message }  \")\n"
      "}\n";
  GABLE_CHECK(format(input) == expected);
}

GABLE_TEST(format_is_stable_when_applied_twice) {
  const std::string messy =
      "struct  Point{\n"
      "  x:int\n"
      "}\n"
      "\n"
      "\n"
      "fn main(){\n"
      "  let p=Point(1)\n"
      "    if p.x==1{\n"
      "       print(\"one\")\n"
      "  }\n"
      "}\n";
  const std::string once = format(messy);
  const std::string twice = format(once);
  GABLE_CHECK(once == twice);
}

GABLE_TEST(format_reports_an_unterminated_string_instead_of_writing) {
  const FormatOutcome outcome = formatSource("fn main() {\n    print(\"unterminated)\n}\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(!outcome.error.empty());
  GABLE_CHECK(outcome.text.empty());
}

GABLE_TEST(format_reports_an_unterminated_block_comment) {
  const FormatOutcome outcome = formatSource("fn main() {\n}\n/* never closed\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.error.find("comment") != std::string::npos);
}
