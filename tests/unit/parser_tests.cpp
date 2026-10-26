#include "test_framework.h"
#include "test_helpers.h"

#include <sstream>

using gable::ast::Expr;
using gable::ast::Stmt;

namespace {

/// Parses source and reports whether it produced syntax errors.
struct ParseOutcome {
  gable::ast::Program program;
  std::string diagnostics;
  bool ok = false;
};

ParseOutcome parse(const std::string& text) {
  gable::SourceFile source("test.gable", text);
  gable::DiagnosticEngine engine(source);
  gable::Lexer lexer(source, engine);
  std::vector<gable::Token> tokens = lexer.tokenize();
  gable::Parser parser(tokens, engine);
  ParseOutcome outcome;
  outcome.program = parser.parseProgram();
  std::ostringstream rendered;
  engine.render(rendered);
  outcome.diagnostics = rendered.str();
  outcome.ok = !engine.hasErrors();
  return outcome;
}

/// Parses `expression` inside a function body and returns the expression
/// statement, which must be the first statement.
const Expr* singleExpression(const ParseOutcome& outcome) {
  if (outcome.program.functions.empty()) {
    return nullptr;
  }
  const auto& body = outcome.program.functions[0]->body;
  if (body.empty() || body[0]->kind != Stmt::Kind::ExprStmt) {
    return nullptr;
  }
  return body[0]->expression.get();
}

ParseOutcome parseExpressionOnly(const std::string& expression) {
  return parse("fn main() -> i32 { " + expression + "; return 0; }");
}

}  // namespace

GABLE_TEST(parser_accepts_minimal_program) {
  const ParseOutcome outcome = parse(gabletest::kSimpleProgram());
  GABLE_CHECK(outcome.ok);
  GABLE_CHECK_EQ(std::size_t(1), outcome.program.functions.size());
  GABLE_CHECK_EQ(std::string("main"), outcome.program.functions[0]->name);
  GABLE_CHECK_EQ(std::size_t(0), outcome.program.functions[0]->parameters.size());
  GABLE_CHECK(outcome.program.functions[0]->hasReturnType);
}

GABLE_TEST(parser_parses_parameters_and_return_type) {
  const ParseOutcome outcome =
      parse("fn combine(a: i64, b: bool, c: str) -> u32 { return 0; }");
  GABLE_CHECK(outcome.ok);
  const auto& function = *outcome.program.functions[0];
  GABLE_CHECK_EQ(std::size_t(3), function.parameters.size());
  GABLE_CHECK_EQ(std::string("b"), function.parameters[1].name);
  GABLE_CHECK_EQ(std::string("bool"), function.parameters[1].type.name);
  GABLE_CHECK_EQ(std::string("u32"), function.returnType.name);
}

GABLE_TEST(parser_defaults_missing_return_type_to_nothing) {
  const ParseOutcome outcome = parse("fn nothing() { return; }");
  GABLE_CHECK(outcome.ok);
  GABLE_CHECK(!outcome.program.functions[0]->hasReturnType);
}

GABLE_TEST(parser_respects_multiplication_precedence) {
  const ParseOutcome outcome = parseExpressionOnly("1 + 2 * 3");
  GABLE_CHECK(outcome.ok);
  const Expr* expression = singleExpression(outcome);
  GABLE_CHECK(expression != nullptr);
  GABLE_CHECK(expression->kind == Expr::Kind::Binary);
  GABLE_CHECK(expression->binaryOp == gable::BinaryOp::Add);
  GABLE_CHECK(expression->rhs->kind == Expr::Kind::Binary);
  GABLE_CHECK(expression->rhs->binaryOp == gable::BinaryOp::Multiply);
}

GABLE_TEST(parser_parses_left_associative_subtraction) {
  const ParseOutcome outcome = parseExpressionOnly("1 - 2 - 3");
  GABLE_CHECK(outcome.ok);
  const Expr* expression = singleExpression(outcome);
  GABLE_CHECK(expression->binaryOp == gable::BinaryOp::Subtract);
  GABLE_CHECK(expression->lhs->kind == Expr::Kind::Binary);
  GABLE_CHECK(expression->lhs->binaryOp == gable::BinaryOp::Subtract);
  GABLE_CHECK(expression->rhs->kind == Expr::Kind::IntegerLiteral);
}

GABLE_TEST(parser_orders_comparison_below_arithmetic) {
  const ParseOutcome outcome = parseExpressionOnly("1 + 2 < 3 * 4");
  GABLE_CHECK(outcome.ok);
  const Expr* expression = singleExpression(outcome);
  GABLE_CHECK(expression->binaryOp == gable::BinaryOp::Less);
  GABLE_CHECK(expression->lhs->binaryOp == gable::BinaryOp::Add);
  GABLE_CHECK(expression->rhs->binaryOp == gable::BinaryOp::Multiply);
}

GABLE_TEST(parser_orders_logical_operators) {
  const ParseOutcome outcome = parseExpressionOnly("true || false && true");
  GABLE_CHECK(outcome.ok);
  const Expr* expression = singleExpression(outcome);
  GABLE_CHECK(expression->binaryOp == gable::BinaryOp::LogicalOr);
  GABLE_CHECK(expression->rhs->binaryOp == gable::BinaryOp::LogicalAnd);
}

GABLE_TEST(parser_parses_unary_and_parentheses) {
  const ParseOutcome outcome = parseExpressionOnly("-(1 + 2) * 3");
  GABLE_CHECK(outcome.ok);
  const Expr* expression = singleExpression(outcome);
  GABLE_CHECK(expression->binaryOp == gable::BinaryOp::Multiply);
  GABLE_CHECK(expression->lhs->kind == Expr::Kind::Unary);
  GABLE_CHECK(expression->lhs->unaryOp == gable::UnaryOp::Negate);
  GABLE_CHECK(expression->lhs->lhs->binaryOp == gable::BinaryOp::Add);
}

GABLE_TEST(parser_parses_nested_calls) {
  const ParseOutcome outcome = parseExpressionOnly("outer(inner(1), inner(2 + 3))");
  GABLE_CHECK(outcome.ok);
  const Expr* expression = singleExpression(outcome);
  GABLE_CHECK(expression->kind == Expr::Kind::Call);
  GABLE_CHECK_EQ(std::string("outer"), expression->name);
  GABLE_CHECK_EQ(std::size_t(2), expression->arguments.size());
  GABLE_CHECK(expression->arguments[1]->kind == Expr::Kind::Call);
  GABLE_CHECK_EQ(std::string("inner"), expression->arguments[1]->name);
}

GABLE_TEST(parser_parses_if_else_chain) {
  const ParseOutcome outcome = parse(
      "fn main() -> i32 {\n"
      "  if true { return 1; } else if false { return 2; } else { return 3; }\n"
      "}\n");
  GABLE_CHECK(outcome.ok);
  const auto& body = outcome.program.functions[0]->body;
  GABLE_CHECK_EQ(std::size_t(1), body.size());
  GABLE_CHECK(body[0]->kind == Stmt::Kind::If);
  GABLE_CHECK(body[0]->hasElse);
  GABLE_CHECK_EQ(std::size_t(1), body[0]->elseBody.size());
  GABLE_CHECK(body[0]->elseBody[0]->kind == Stmt::Kind::If);
}

GABLE_TEST(parser_parses_while_loop) {
  const ParseOutcome outcome = parse(
      "fn main() -> i32 {\n"
      "  var i: i32 = 0;\n"
      "  while i < 10 { i = i + 1; }\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(outcome.ok);
  GABLE_CHECK(outcome.program.functions[0]->body[1]->kind == Stmt::Kind::While);
}

GABLE_TEST(parser_parses_let_and_var_bindings) {
  const ParseOutcome outcome = parse(
      "fn main() -> i32 {\n"
      "  let a: i32 = 1;\n"
      "  var b = 2;\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(outcome.ok);
  const auto& body = outcome.program.functions[0]->body;
  GABLE_CHECK(!body[0]->isMutable);
  GABLE_CHECK(body[0]->hasTypeAnnotation);
  GABLE_CHECK(body[1]->isMutable);
  GABLE_CHECK(!body[1]->hasTypeAnnotation);
}

GABLE_TEST(parser_reports_missing_semicolon) {
  const ParseOutcome outcome = parse("fn main() -> i32 { return 0 }\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("expected ';'") != std::string::npos);
}

GABLE_TEST(parser_reports_missing_closing_brace) {
  const ParseOutcome outcome = parse("fn main() -> i32 { return 0;\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("expected '}'") != std::string::npos);
}

GABLE_TEST(parser_requires_braced_if_bodies) {
  const ParseOutcome outcome = parse("fn main() -> i32 { if true return 0; return 1; }\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("requires braced bodies") != std::string::npos);
}

GABLE_TEST(parser_requires_braced_else_bodies) {
  const ParseOutcome outcome =
      parse("fn main() -> i32 { if true { return 0; } else return 1; }\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("expected '{' or 'if' after 'else'") != std::string::npos);
}

GABLE_TEST(parser_rejects_non_name_assignment_target) {
  const ParseOutcome outcome = parse("fn main() -> i32 { 1 = 2; return 0; }\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("invalid assignment target") != std::string::npos);
}

GABLE_TEST(parser_rejects_uninitialized_binding) {
  const ParseOutcome outcome = parse("fn main() -> i32 { let x: i32; return 0; }\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("must be initialized") != std::string::npos);
}

GABLE_TEST(parser_rejects_nested_functions) {
  const ParseOutcome outcome =
      parse("fn outer() { fn inner() { return; } return; }\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("nested function declarations") != std::string::npos);
}

GABLE_TEST(parser_rejects_reserved_operators) {
  const ParseOutcome outcome = parseExpressionOnly("1 & 2");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("reserved for a future Gable release") != std::string::npos);
}

GABLE_TEST(parser_rejects_top_level_statements) {
  const ParseOutcome outcome = parse("let x = 1;\nfn main() -> i32 { return 0; }\n");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("expected a function declaration") != std::string::npos);
}

GABLE_TEST(parser_recovers_and_reports_multiple_errors) {
  const ParseOutcome outcome = parse(
      "fn main() -> i32 {\n"
      "  let a = ;\n"
      "  let b = ;\n"
      "  return 0;\n"
      "}\n");
  GABLE_CHECK(!outcome.ok);
  std::size_t count = 0;
  std::size_t position = 0;
  while ((position = outcome.diagnostics.find("expected an expression", position)) !=
         std::string::npos) {
    ++count;
    position += 1;
  }
  GABLE_CHECK_EQ(std::size_t(2), count);
}

GABLE_TEST(parser_rejects_missing_expression_after_operator) {
  const ParseOutcome outcome = parseExpressionOnly("1 + ");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("expected an expression") != std::string::npos);
}

GABLE_TEST(parser_rejects_missing_argument_close) {
  const ParseOutcome outcome = parseExpressionOnly("foo(1, 2");
  GABLE_CHECK(!outcome.ok);
  GABLE_CHECK(outcome.diagnostics.find("expected ')' to close the argument list") !=
              std::string::npos);
}

GABLE_TEST(parser_accepts_empty_statement) {
  const ParseOutcome outcome = parse("fn main() -> i32 { ; return 0; }\n");
  GABLE_CHECK(outcome.ok);
}
