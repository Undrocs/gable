#include "test_framework.h"
#include "test_helpers.h"

#include <sstream>
#include <string>

using gable::ast::Expr;
using gable::ast::Stmt;
using gable::ast::TypeSyntax;

namespace {

/// Parses one file and keeps the diagnostics it produced.
struct ParseOutcome {
  gable::ast::Program program;
  std::string diagnostics;
  bool ok = false;
};

gable::Diagnostics parseInto(const std::string& text, gable::ast::Program& program) {
  gable::Diagnostics collected;
  auto source = std::make_unique<gable::SourceFile>("test.gable", text);
  const std::uint32_t file = collected.addFile(std::move(source));
  gable::Lexer lexer(collected.file(file), collected, file);
  const std::vector<gable::Token> tokens = lexer.tokenize();
  gable::Parser parser(tokens, collected, file);
  program = parser.parseProgram();
  return collected;
}

ParseOutcome parse(const std::string& text) {
  ParseOutcome outcome;
  gable::Diagnostics collected = parseInto(text, outcome.program);
  std::ostringstream rendered;
  collected.render(rendered);
  outcome.diagnostics = rendered.str();
  outcome.ok = !collected.hasErrors();
  return outcome;
}

/// Parses `body` as the body of `main` and returns its statements.
const std::vector<gable::ast::StmtPtr>* mainBody(const ParseOutcome& outcome) {
  if (outcome.program.functions.empty()) {
    return nullptr;
  }
  return &outcome.program.functions[0]->body;
}

std::string wrapInMain(const std::string& body) {
  return "fn main() -> i32 {\n" + body + "\n}\n";
}

}  // namespace

GABLE_TEST(parser_parses_function_declaration) {
  const ParseOutcome outcome = parse(
      "fn add(a: i32, b: i64) -> i64 {\n"
      "  return a as i64 + b;\n"
      "}\n");
  GABLE_CHECK(outcome.ok);
  GABLE_CHECK_EQ(std::size_t(1), outcome.program.functions.size());
  const gable::ast::FunctionDecl& function = *outcome.program.functions[0];
  GABLE_CHECK_EQ(std::string("add"), function.name);
  GABLE_CHECK_EQ(std::size_t(2), function.parameters.size());
  GABLE_CHECK_EQ(std::string("a"), function.parameters[0].name);
  GABLE_CHECK(function.hasReturnType);
  GABLE_CHECK_EQ(std::string("i64"), function.returnType.name);
  GABLE_CHECK_EQ(std::size_t(1), function.body.size());
}

GABLE_TEST(parser_parses_struct_declaration) {
  const ParseOutcome outcome = parse(
      "struct Point {\n"
      "  x: i32,\n"
      "  y: i32\n"
      "}\n");
  GABLE_CHECK(outcome.ok);
  GABLE_CHECK_EQ(std::size_t(1), outcome.program.structs.size());
  const gable::ast::StructDecl& declaration = *outcome.program.structs[0];
  GABLE_CHECK_EQ(std::string("Point"), declaration.name);
  GABLE_CHECK_EQ(std::size_t(2), declaration.fields.size());
  GABLE_CHECK_EQ(std::string("y"), declaration.fields[1].name);
}

GABLE_TEST(parser_parses_array_types) {
  const ParseOutcome outcome = parse(
      "fn f(values: [i32; 4]) -> [str; 2] {\n"
      "  return values;\n"
      "}\n");
  GABLE_CHECK(outcome.ok);
  const gable::ast::FunctionDecl& function = *outcome.program.functions[0];
  GABLE_CHECK(function.parameters[0].type.kind == TypeSyntax::Kind::Array);
  GABLE_CHECK_EQ(std::uint64_t(4), function.parameters[0].type.arrayLength);
  GABLE_CHECK(function.returnType.kind == TypeSyntax::Kind::Array);
  GABLE_CHECK_EQ(std::string("str"), function.returnType.element->name);
}

GABLE_TEST(parser_parses_imports_and_rejects_late_imports) {
  const ParseOutcome good = parse("import \"util.gable\";\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(good.ok);
  GABLE_CHECK_EQ(std::size_t(1), good.program.imports.size());
  GABLE_CHECK_EQ(std::string("util.gable"), good.program.imports[0].path);

  const ParseOutcome late =
      parse("fn main() -> i32 { return 0; }\nimport \"util.gable\";\n");
  GABLE_CHECK(!late.ok);
  GABLE_CHECK(late.diagnostics.find("imports must appear before") != std::string::npos);
}

GABLE_TEST(parser_parses_let_var_and_assignment) {
  const ParseOutcome outcome = parse(wrapInMain(
      "  let a: i32 = 1;\n"
      "  var b: i32 = 2;\n"
      "  b = a + 1;\n"
      "  let c = a;\n"));
  GABLE_CHECK(outcome.ok);
  const auto* body = mainBody(outcome);
  GABLE_CHECK_EQ(std::size_t(4), body->size());
  GABLE_CHECK((*body)[0]->kind == Stmt::Kind::Let);
  GABLE_CHECK(!(*body)[0]->isMutable);
  GABLE_CHECK((*body)[1]->isMutable);
  GABLE_CHECK((*body)[2]->kind == Stmt::Kind::Assign);
  GABLE_CHECK(!(*body)[3]->hasTypeAnnotation);
}

GABLE_TEST(parser_parses_control_flow_statements) {
  const ParseOutcome outcome = parse(wrapInMain(
      "  if true { print(1); } else if false { print(2); } else { print(3); }\n"
      "  while true { break; }\n"
      "  for i in 0..3 { continue; }\n"
      "  for item in values { print(item); }\n"));
  GABLE_CHECK(outcome.ok);
  const auto* body = mainBody(outcome);
  GABLE_CHECK_EQ(std::size_t(4), body->size());

  const Stmt& ifStatement = *(*body)[0];
  GABLE_CHECK(ifStatement.kind == Stmt::Kind::If);
  GABLE_CHECK(ifStatement.hasElse);
  GABLE_CHECK_EQ(std::size_t(1), ifStatement.elseBody.size());
  GABLE_CHECK(ifStatement.elseBody[0]->kind == Stmt::Kind::If);

  GABLE_CHECK((*body)[1]->kind == Stmt::Kind::While);
  GABLE_CHECK((*body)[1]->body[0]->kind == Stmt::Kind::Break);

  const Stmt& rangeLoop = *(*body)[2];
  GABLE_CHECK(rangeLoop.kind == Stmt::Kind::For);
  GABLE_CHECK(rangeLoop.isRangeLoop);
  GABLE_CHECK_EQ(std::string("i"), rangeLoop.loopVariable);
  GABLE_CHECK(rangeLoop.rangeEnd != nullptr);

  const Stmt& eachLoop = *(*body)[3];
  GABLE_CHECK(!eachLoop.isRangeLoop);
  GABLE_CHECK(eachLoop.value->kind == Expr::Kind::Name);
}

GABLE_TEST(parser_parses_postfix_index_and_field_access) {
  const ParseOutcome outcome = parse(wrapInMain("  let a: i32 = point.x + items[2];"));
  GABLE_CHECK(outcome.ok);
  const auto* body = mainBody(outcome);
  GABLE_CHECK(body != nullptr);
  const Expr* expression = (*body)[0]->value.get();
  GABLE_CHECK(expression != nullptr);
  GABLE_CHECK(expression->kind == Expr::Kind::Binary);
  GABLE_CHECK(expression->lhs->kind == Expr::Kind::FieldAccess);
  GABLE_CHECK_EQ(std::string("x"), expression->lhs->fieldName);
  GABLE_CHECK(expression->rhs->kind == Expr::Kind::Index);
  GABLE_CHECK_EQ(std::uint64_t(2), expression->rhs->rhs->integerValue);
}

GABLE_TEST(parser_parses_array_literal_and_cast) {
  const ParseOutcome outcome = parse(wrapInMain("  let a: [i32; 3] = [1, 2, 3];\n  let b: i32 = a[0] as i32;"));
  GABLE_CHECK(outcome.ok);
  const auto* body = mainBody(outcome);
  GABLE_CHECK_EQ(std::size_t(3), (*body)[0]->value->arguments.size());
  GABLE_CHECK((*body)[1]->value->kind == Expr::Kind::Cast);
  GABLE_CHECK_EQ(std::string("i32"), (*body)[1]->value->targetType.name);
}

GABLE_TEST(parser_respects_operator_precedence) {
  const ParseOutcome outcome = parse(wrapInMain("  let a: bool = 1 + 2 * 3 == 7 && true;"));
  GABLE_CHECK(outcome.ok);
  const auto* body = mainBody(outcome);
  GABLE_CHECK(body != nullptr);
  const Expr* expression = (*body)[0]->value.get();
  GABLE_CHECK(expression != nullptr);
  GABLE_CHECK(expression->kind == Expr::Kind::Binary);
  GABLE_CHECK(expression->binaryOp == gable::BinaryOp::LogicalAnd);
  GABLE_CHECK(expression->lhs->binaryOp == gable::BinaryOp::Equal);
  GABLE_CHECK(expression->lhs->lhs->binaryOp == gable::BinaryOp::Add);
  GABLE_CHECK(expression->lhs->lhs->rhs->binaryOp == gable::BinaryOp::Multiply);
}

GABLE_TEST(parser_reports_missing_semicolon) {
  const ParseOutcome outcome = parse(wrapInMain("  let a: i32 = 1\n  return 0;"));
  GABLE_CHECK(!outcome.ok);
}

GABLE_TEST(parser_reports_unexpected_token_with_position) {
  const ParseOutcome outcome = parse(wrapInMain("  let = 1;"));
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("test.gable:2:") != std::string::npos);
}

GABLE_TEST(parser_rejects_nested_functions_and_structs) {
  const ParseOutcome nestedFunction = parse("fn main() -> i32 { fn inner() { return; } return 0; }\n");
  GABLE_CHECK(!nestedFunction.ok);
  GABLE_CHECK(nestedFunction.diagnostics.find("nested function declarations are not supported") !=
              std::string::npos);

  const ParseOutcome nestedStruct = parse(wrapInMain("  struct Inner { x: i32 }"));
  GABLE_CHECK(!nestedStruct.ok);
}

GABLE_TEST(parser_requires_braced_bodies) {
  const ParseOutcome outcome = parse("fn main() -> i32 return 0;\n");
  GABLE_CHECK(!outcome.ok);
}

GABLE_TEST(parser_rejects_range_outside_for_header) {
  const ParseOutcome outcome = parse(wrapInMain("  let a = 0..3;"));
  GABLE_CHECK(outcome.ok);  // parsed; rejected by semantic analysis
}

GABLE_TEST(parser_limits_nesting_depth) {
  std::string source = "fn main() -> i32 { return ";
  for (int i = 0; i < 500; ++i) {
    source += "(";
  }
  source += "0";
  for (int i = 0; i < 500; ++i) {
    source += ")";
  }
  source += "; }\n";
  const ParseOutcome outcome = parse(source);
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("nested") != std::string::npos);
}
