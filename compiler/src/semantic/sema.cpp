#include "gable/sema.h"

#include <algorithm>
#include <utility>

namespace gable {
namespace {

/// True if an integer literal of `magnitude` can be represented in `type`.
/// `negate` indicates that the literal is the operand of a unary minus, which
/// makes one additional value representable for signed types (INT_MIN).
bool literalFits(ir::Type type, std::uint64_t magnitude, bool negate) {
  if (!type.isInteger()) {
    return false;
  }
  const unsigned bits = type.bitWidth();
  if (type.isSigned()) {
    // Largest magnitude representable: 2^(bits-1) when negated, 2^(bits-1)-1 otherwise.
    const std::uint64_t half = (bits >= 64) ? (1ull << 63) : (1ull << (bits - 1));
    return negate ? magnitude <= half : magnitude <= half - 1;
  }
  if (negate) {
    return false;
  }
  if (bits >= 64) {
    return true;
  }
  return magnitude <= ((1ull << bits) - 1);
}

/// Two's-complement bit pattern for a value of the given magnitude.
std::uint64_t literalBits(std::uint64_t magnitude, bool negate) {
  return negate ? (0ull - magnitude) : magnitude;
}

}  // namespace

SemanticAnalyzer::SemanticAnalyzer(DiagnosticEngine& diagnostics) : diagnostics_(diagnostics) {}

std::unique_ptr<ir::Module> SemanticAnalyzer::analyze(const ast::Program& program) {
  auto module = std::make_unique<ir::Module>();

  declareFunctions(program);

  // Lower every accepted declaration in source order so that module indices
  // assigned during declareFunctions stay valid for forward and recursive calls.
  for (const auto& declaration : program.functions) {
    const auto it = functions_.find(declaration->name);
    if (it == functions_.end() || it->second.declaration != declaration.get()) {
      continue;  // duplicate or rejected declaration
    }
    auto function = checkFunction(*declaration);
    if (function) {
      module->functions.push_back(std::move(function));
    }
  }

  checkEntryPoint();
  return module;
}

void SemanticAnalyzer::declareFunctions(const ast::Program& program) {
  std::size_t nextIndex = 0;
  for (const auto& declaration : program.functions) {
    if (declaration->name == "print") {
      diagnostics_.error(declaration->nameSpan,
                         "the name 'print' is reserved for the built-in print function");
      continue;
    }

    FunctionInfo info;
    info.declaration = declaration.get();
    info.returnType = declaration->hasReturnType ? resolveType(declaration->returnType)
                                                 : ir::Type::voidType();

    bool signatureValid = true;
    for (const ast::Parameter& parameter : declaration->parameters) {
      const ir::Type type = resolveType(parameter.type);
      if (type.kind() == ir::TypeKind::Void) {
        diagnostics_.error(parameter.type.span,
                           "parameter '" + parameter.name + "' cannot have type 'void'");
        signatureValid = false;
      }
      info.parameterTypes.push_back(type);
    }
    if (!signatureValid) {
      continue;
    }

    const auto existing = functions_.find(declaration->name);
    if (existing != functions_.end()) {
      diagnostics_.error(declaration->nameSpan,
                         "duplicate declaration of function '" + declaration->name + "'");
      diagnostics_.note(existing->second.declaration->nameSpan,
                        "previous declaration of '" + declaration->name + "' is here");
      continue;
    }

    info.moduleIndex = nextIndex++;
    functions_.emplace(declaration->name, std::move(info));
  }
}

void SemanticAnalyzer::checkEntryPoint() {
  const auto it = functions_.find("main");
  if (it == functions_.end()) {
    diagnostics_.error(SourceSpan{}, "no 'main' function found; an executable requires "
                                     "`fn main() -> i32`");
    return;
  }
  const ast::FunctionDecl& declaration = *it->second.declaration;

  if (!declaration.parameters.empty()) {
    diagnostics_.error(declaration.nameSpan, "'main' must not take parameters");
  }
  if (it->second.returnType != ir::Type::i32()) {
    diagnostics_.error(declaration.hasReturnType ? declaration.returnType.span
                                                 : declaration.nameSpan,
                       "'main' must return i32, found '" + it->second.returnType.name() + "'");
  }
}

std::unique_ptr<ir::Function> SemanticAnalyzer::checkFunction(
    const ast::FunctionDecl& declaration) {
  auto function = std::make_unique<ir::Function>();
  function->name = declaration.name;
  function->span = declaration.nameSpan;
  function->returnType = functions_.at(declaration.name).returnType;
  function->isEntryPoint = declaration.name == "main";

  currentFunction_ = function.get();
  currentDeclaration_ = &declaration;
  currentReturnType_ = function->returnType;

  pushScope();

  const std::vector<ir::Type>& parameterTypes = functions_.at(declaration.name).parameterTypes;
  for (std::size_t i = 0; i < declaration.parameters.size(); ++i) {
    const ast::Parameter& parameter = declaration.parameters[i];
    const ir::Type type = parameterTypes[i];

    if (lookup(parameter.name) != nullptr) {
      diagnostics_.error(parameter.span,
                         "duplicate parameter name '" + parameter.name + "' in function '" +
                             declaration.name + "'");
      continue;
    }
    if (parameter.name == "print") {
      diagnostics_.error(parameter.span,
                         "the name 'print' is reserved for the built-in print function");
      continue;
    }

    const auto slot = static_cast<std::uint32_t>(function->locals.size());
    function->locals.push_back(ir::LocalVariable{parameter.name, type, parameter.span, false, true});
    function->parameterCount = static_cast<std::uint32_t>(function->locals.size());
    scopes_.back().emplace(parameter.name, Binding{slot, type, false, true, parameter.span});
  }

  function->body = checkStatements(declaration.body);

  if (currentReturnType_ != ir::Type::voidType() &&
      currentReturnType_.kind() != ir::TypeKind::Error &&
      !alwaysReturns(function->body)) {
    diagnostics_.error(declaration.nameSpan,
                       "function '" + declaration.name +
                           "' can reach the end of its body without returning a value of type '" +
                           currentReturnType_.name() + "'");
  }

  popScope();
  currentFunction_ = nullptr;
  currentDeclaration_ = nullptr;
  currentReturnType_ = ir::Type::voidType();
  return function;
}

std::vector<ir::StmtPtr> SemanticAnalyzer::checkStatements(
    const std::vector<ast::StmtPtr>& statements) {
  std::vector<ir::StmtPtr> lowered;
  lowered.reserve(statements.size());
  for (const auto& statement : statements) {
    ir::StmtPtr result = checkStmt(*statement);
    if (result) {
      lowered.push_back(std::move(result));
    }
  }
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkStmt(const ast::Stmt& stmt) {
  switch (stmt.kind) {
    case ast::Stmt::Kind::Let:
      return checkLet(stmt);
    case ast::Stmt::Kind::Assign:
      return checkAssign(stmt);
    case ast::Stmt::Kind::ExprStmt: {
      auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Expr, stmt.span);
      lowered->expression = checkExpr(*stmt.expression, std::nullopt);
      if (!lowered->expression) {
        return nullptr;
      }
      return lowered;
    }
    case ast::Stmt::Kind::Return:
      return checkReturn(stmt);
    case ast::Stmt::Kind::If:
      return checkIf(stmt);
    case ast::Stmt::Kind::While:
      return checkWhile(stmt);
    case ast::Stmt::Kind::Block:
      return checkBlock(stmt);
  }
  return nullptr;
}

ir::StmtPtr SemanticAnalyzer::checkLet(const ast::Stmt& stmt) {
  if (stmt.name == "print") {
    diagnostics_.error(stmt.nameSpan,
                       "the name 'print' is reserved for the built-in print function");
    return nullptr;
  }
  if (scopes_.back().count(stmt.name) != 0) {
    diagnostics_.error(stmt.nameSpan, "duplicate declaration of '" + stmt.name +
                                          "' in the same scope");
  }

  ir::Type type = ir::Type::error();
  std::optional<ir::Type> expected;

  if (stmt.hasTypeAnnotation) {
    type = resolveType(stmt.typeAnnotation);
    if (type.kind() == ir::TypeKind::Void) {
      diagnostics_.error(stmt.typeAnnotation.span,
                         "a variable cannot have type 'void'");
      type = ir::Type::error();
    } else if (type.kind() != ir::TypeKind::Error) {
      expected = type;
    }
  }

  ir::ExprPtr initializer = checkExpr(*stmt.expression, expected);
  if (!initializer) {
    return nullptr;
  }

  if (!expected) {
    if (initializer->type.kind() == ir::TypeKind::Void && type.isValid()) {
      diagnostics_.error(stmt.expression->span,
                         "cannot initialize '" + stmt.name + "' with a value of type 'void'");
      return nullptr;
    }
    type = initializer->type;
  } else if (initializer->type.kind() != ir::TypeKind::Error && initializer->type != *expected) {
    reportTypeMismatch(stmt.expression->span, "initializer", *expected, initializer->type);
  }

  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Let, stmt.span);
  lowered->slot = static_cast<std::uint32_t>(currentFunction_->locals.size());
  lowered->type = type;
  lowered->isMutable = stmt.isMutable;
  lowered->name = stmt.name;
  lowered->expression = std::move(initializer);

  currentFunction_->locals.push_back(
      ir::LocalVariable{stmt.name, type, stmt.nameSpan, stmt.isMutable, false});
  // The binding becomes visible only after its initializer, so `let x = x;`
  // refers to an outer declaration or reports an undefined name.
  scopes_.back().emplace(stmt.name,
                         Binding{lowered->slot, type, stmt.isMutable, false, stmt.nameSpan});
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkAssign(const ast::Stmt& stmt) {
  Binding* binding = lookup(stmt.name);
  if (binding == nullptr) {
    diagnostics_.error(stmt.nameSpan, "undefined variable '" + stmt.name + "'");
    return nullptr;
  }
  if (!binding->isMutable) {
    if (binding->isParameter) {
      diagnostics_.error(stmt.nameSpan, "cannot assign to parameter '" + stmt.name +
                                            "'; parameters are immutable");
      diagnostics_.note(binding->span, "parameter declared here");
      diagnostics_.note(stmt.nameSpan,
                        "copy the parameter into a 'var' binding and modify that instead");
    } else {
      diagnostics_.error(stmt.nameSpan, "cannot assign to immutable binding '" + stmt.name +
                                            "'; declare it with 'var' to make it mutable");
      diagnostics_.note(binding->span, "binding declared here");
    }
  }

  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Assign, stmt.span);
  lowered->slot = binding->slot;
  lowered->type = binding->type;
  lowered->name = stmt.name;

  const bool known = binding->type.kind() != ir::TypeKind::Error;
  lowered->expression = checkExpr(*stmt.expression, known ? std::optional<ir::Type>(binding->type)
                                                          : std::nullopt);
  if (!lowered->expression) {
    return nullptr;
  }
  if (known && lowered->expression->type.kind() != ir::TypeKind::Error &&
      lowered->expression->type != binding->type) {
    reportTypeMismatch(stmt.expression->span, "assignment", binding->type,
                       lowered->expression->type);
  }
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkReturn(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Return, stmt.span);
  const bool returnsValue = stmt.expression != nullptr;

  if (currentReturnType_ == ir::Type::voidType()) {
    if (returnsValue) {
      diagnostics_.error(stmt.span, "function '" + currentDeclaration_->name +
                                        "' does not return a value; remove the returned expression");
      checkExpr(*stmt.expression, std::nullopt);
    }
    return lowered;
  }

  if (!returnsValue) {
    diagnostics_.error(stmt.span, "function '" + currentDeclaration_->name +
                                      "' must return a value of type '" +
                                      currentReturnType_.name() + "'");
    return lowered;
  }

  lowered->expression = checkExpr(*stmt.expression, currentReturnType_);
  if (lowered->expression && lowered->expression->type.kind() != ir::TypeKind::Error &&
      lowered->expression->type != currentReturnType_) {
    reportTypeMismatch(stmt.expression->span, "return value", currentReturnType_,
                       lowered->expression->type);
  }
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkIf(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::If, stmt.span);

  lowered->expression = checkExpr(*stmt.expression, std::nullopt);
  if (!lowered->expression) {
    return nullptr;
  }
  const ir::Type conditionType = lowered->expression->type;
  if (conditionType.kind() != ir::TypeKind::Error && conditionType != ir::Type::boolean()) {
    diagnostics_.error(stmt.expression->span, "the condition of an 'if' statement must have type "
                                              "'bool', found '" +
                                                  conditionType.name() + "'");
    diagnostics_.note(stmt.expression->span,
                      "compare explicitly, for example `x != 0` or `x == true`");
  }

  pushScope();
  lowered->body = checkStatements(stmt.body);
  popScope();

  if (stmt.hasElse) {
    lowered->hasElse = true;
    pushScope();
    lowered->elseBody = checkStatements(stmt.elseBody);
    popScope();
  }
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkWhile(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::While, stmt.span);

  lowered->expression = checkExpr(*stmt.expression, std::nullopt);
  if (!lowered->expression) {
    return nullptr;
  }
  const ir::Type conditionType = lowered->expression->type;
  if (conditionType.kind() != ir::TypeKind::Error && conditionType != ir::Type::boolean()) {
    diagnostics_.error(stmt.expression->span, "the condition of a 'while' statement must have type "
                                              "'bool', found '" +
                                                  conditionType.name() + "'");
    diagnostics_.note(stmt.expression->span,
                      "compare explicitly, for example `x < 10` or `x == true`");
  }

  pushScope();
  lowered->body = checkStatements(stmt.body);
  popScope();
  return lowered;
}

ir::StmtPtr SemanticAnalyzer::checkBlock(const ast::Stmt& stmt) {
  auto lowered = std::make_unique<ir::Stmt>(ir::Stmt::Kind::Block, stmt.span);
  pushScope();
  lowered->body = checkStatements(stmt.body);
  popScope();
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkExpr(const ast::Expr& expr, std::optional<ir::Type> expected) {
  switch (expr.kind) {
    case ast::Expr::Kind::IntegerLiteral:
      return checkIntegerLiteral(expr, expected);
    case ast::Expr::Kind::BoolLiteral: {
      if (expected && *expected != ir::Type::boolean()) {
        reportTypeMismatch(expr.span, "boolean literal", *expected, ir::Type::boolean());
      }
      auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::BoolLiteral, ir::Type::boolean(),
                                               expr.span);
      lowered->boolValue = expr.boolValue;
      return lowered;
    }
    case ast::Expr::Kind::StringLiteral: {
      if (expected && *expected != ir::Type::str()) {
        reportTypeMismatch(expr.span, "string literal", *expected, ir::Type::str());
      }
      auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::StringLiteral, ir::Type::str(),
                                               expr.span);
      lowered->stringValue = expr.stringValue;
      return lowered;
    }
    case ast::Expr::Kind::Name: {
      const Binding* binding = lookup(expr.name);
      if (binding == nullptr) {
        if (functions_.count(expr.name) != 0) {
          diagnostics_.error(expr.span, "function '" + expr.name + "' cannot be used as a value");
        } else {
          diagnostics_.error(expr.span, "undefined variable '" + expr.name + "'");
        }
        auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Local, ir::Type::error(),
                                                 expr.span);
        lowered->name = expr.name;
        return lowered;
      }
      if (expected && binding->type.kind() != ir::TypeKind::Error && *expected != binding->type) {
        reportTypeMismatch(expr.span, "variable", *expected, binding->type);
      }
      auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Local, binding->type, expr.span);
      lowered->slot = binding->slot;
      lowered->name = expr.name;
      return lowered;
    }
    case ast::Expr::Kind::Unary:
      return checkUnary(expr, expected);
    case ast::Expr::Kind::Binary:
      return checkBinary(expr, expected);
    case ast::Expr::Kind::Call:
      return checkCall(expr, expected);
  }
  return nullptr;
}

std::optional<SemanticAnalyzer::LiteralInfo> SemanticAnalyzer::asIntegerLiteral(
    const ast::Expr& expr) {
  if (expr.kind == ast::Expr::Kind::IntegerLiteral) {
    return LiteralInfo{expr.integerValue, false, expr.unsignedLiteral};
  }
  if (expr.kind == ast::Expr::Kind::Unary && expr.unaryOp == UnaryOp::Negate && expr.lhs &&
      expr.lhs->kind == ast::Expr::Kind::IntegerLiteral) {
    return LiteralInfo{expr.lhs->integerValue, true, expr.lhs->unsignedLiteral};
  }
  return std::nullopt;
}

ir::ExprPtr SemanticAnalyzer::literalAs(const ast::Expr& node, LiteralInfo literal,
                                       ir::Type type) {
  if (literal.hasUnsignedSuffix) {
    diagnostics_.error(node.span,
                       "the 'u' integer suffix is not supported in Gable v0.1; annotate the "
                       "binding or parameter with an unsigned type instead");
    return std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, ir::Type::error(), node.span);
  }
  if (!literalFits(type, literal.magnitude, literal.negate)) {
    diagnostics_.error(node.span,
                       "integer literal " + std::string(literal.negate ? "-" : "") +
                           std::to_string(literal.magnitude) + " does not fit in type '" +
                           type.name() + "'");
    return std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, ir::Type::error(), node.span);
  }
  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, type, node.span);
  lowered->integerValue = literalBits(literal.magnitude, literal.negate);
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkIntegerLiteral(const ast::Expr& expr,
                                                  std::optional<ir::Type> expected) {
  const std::optional<LiteralInfo> literal = asIntegerLiteral(expr);
  if (!literal) {
    return nullptr;
  }

  ir::Type type = ir::Type::i32();
  if (expected) {
    if (expected->kind() == ir::TypeKind::Error) {
      return std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, ir::Type::error(),
                                        expr.span);
    }
    if (!expected->isInteger()) {
      reportTypeMismatch(expr.span, "integer literal", *expected, ir::Type::i32());
      return std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, ir::Type::error(),
                                        expr.span);
    }
    type = *expected;
  }
  return literalAs(expr, *literal, type);
}

ir::ExprPtr SemanticAnalyzer::checkUnary(const ast::Expr& expr, std::optional<ir::Type> expected) {
  if (expr.unaryOp == UnaryOp::LogicalNot) {
    if (expected && *expected != ir::Type::boolean()) {
      reportTypeMismatch(expr.span, "logical expression", *expected, ir::Type::boolean());
      expected = ir::Type::boolean();
    }
    auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Unary, ir::Type::boolean(),
                                              expr.span);
    lowered->unaryOp = UnaryOp::LogicalNot;
    lowered->lhs = checkExpr(*expr.lhs, ir::Type::boolean());
    if (!lowered->lhs) {
      return nullptr;
    }
    if (lowered->lhs->type.kind() != ir::TypeKind::Error &&
        lowered->lhs->type != ir::Type::boolean()) {
      diagnostics_.error(expr.lhs->span, "operator '!' requires a 'bool' operand, found '" +
                                             lowered->lhs->type.name() + "'");
    }
    return lowered;
  }

  // Unary minus. A literal operand is folded and range-checked against the
  // expected type so that the most negative value of a signed type is valid.
  if (const std::optional<LiteralInfo> literal = asIntegerLiteral(expr)) {
    ir::Type type = (expected && expected->isInteger()) ? *expected : ir::Type::i32();
    if (literal->hasUnsignedSuffix) {
      return literalAs(expr, *literal, type);  // reports the unsupported suffix
    }
    if (literal->negate && !type.isSigned() && type.isInteger()) {
      diagnostics_.error(expr.span, "operator '-' cannot be applied to the unsigned type '" +
                                        type.name() + "'");
      return std::make_unique<ir::Expr>(ir::Expr::Kind::IntegerLiteral, ir::Type::error(),
                                        expr.span);
    }
    return literalAs(expr, *literal, type);
  }

  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Unary, ir::Type::error(), expr.span);
  lowered->unaryOp = UnaryOp::Negate;
  lowered->lhs = checkExpr(*expr.lhs, std::nullopt);
  if (!lowered->lhs) {
    return nullptr;
  }
  if (lowered->lhs->type.kind() == ir::TypeKind::Error) {
    return lowered;
  }
  if (!lowered->lhs->type.isInteger()) {
    diagnostics_.error(expr.lhs->span, "operator '-' requires an integer operand, found '" +
                                           lowered->lhs->type.name() + "'");
    lowered->type = ir::Type::error();
    return lowered;
  }
  if (!lowered->lhs->type.isSigned()) {
    diagnostics_.error(expr.span, "operator '-' cannot be applied to the unsigned type '" +
                                      lowered->lhs->type.name() + "'");
    lowered->type = ir::Type::error();
    return lowered;
  }
  lowered->type = lowered->lhs->type;
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkBinary(const ast::Expr& expr, std::optional<ir::Type> expected) {
  const BinaryOp op = expr.binaryOp;
  const bool isComparison = isComparisonOperator(op);
  const bool isLogical = op == BinaryOp::LogicalAnd || op == BinaryOp::LogicalOr;
  const bool isEquality = op == BinaryOp::Equal || op == BinaryOp::NotEqual;

  auto resultType = [&](ir::Type operandType) {
    return isComparison || isLogical ? ir::Type::boolean() : operandType;
  };
  auto fail = [&](ir::Type type) {
    return std::make_unique<ir::Expr>(ir::Expr::Kind::Binary, type, expr.span);
  };

  if (expected && isComparison && *expected != ir::Type::boolean() &&
      expected->kind() != ir::TypeKind::Error) {
    reportTypeMismatch(expr.span, "comparison", *expected, ir::Type::boolean());
  }

  if (isLogical) {
    auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Binary, ir::Type::boolean(),
                                              expr.span);
    lowered->binaryOp = op;
    lowered->lhs = checkExpr(*expr.lhs, ir::Type::boolean());
    lowered->rhs = checkExpr(*expr.rhs, ir::Type::boolean());
    if (!lowered->lhs || !lowered->rhs) {
      return nullptr;
    }
    for (const auto* side : {lowered->lhs.get(), lowered->rhs.get()}) {
      if (side->type.kind() != ir::TypeKind::Error && side->type != ir::Type::boolean()) {
        diagnostics_.error(side->span, std::string("operator '") + binaryOpSpelling(op) +
                                           "' requires 'bool' operands, found '" +
                                           side->type.name() + "'");
        lowered->type = ir::Type::error();
      }
    }
    return lowered;
  }

  const std::optional<LiteralInfo> lhsLiteral = asIntegerLiteral(*expr.lhs);
  const std::optional<LiteralInfo> rhsLiteral = asIntegerLiteral(*expr.rhs);
  const bool lhsIsLiteral = lhsLiteral.has_value();
  const bool rhsIsLiteral = rhsLiteral.has_value();

  std::unique_ptr<ir::Expr> lhs;
  std::unique_ptr<ir::Expr> rhs;
  ir::Type operandType = ir::Type::error();

  if (lhsIsLiteral && rhsIsLiteral) {
    // With no non-literal operand the expected type decides, otherwise i32.
    ir::Type type = (expected && expected->isInteger()) ? *expected : ir::Type::i32();
    lhs = literalAs(*expr.lhs, *lhsLiteral, type);
    rhs = literalAs(*expr.rhs, *rhsLiteral, type);
    operandType = type;
  } else if (lhsIsLiteral) {
    rhs = checkExpr(*expr.rhs, std::nullopt);
    if (!rhs) {
      return nullptr;
    }
    if (rhs->type.kind() == ir::TypeKind::Error) {
      return fail(ir::Type::error());
    }
    if (!rhs->type.isInteger()) {
      diagnostics_.error(expr.span, std::string("operator '") + binaryOpSpelling(op) +
                                        "' requires integer operands, found '" +
                                        rhs->type.name() + "'");
      return fail(ir::Type::error());
    }
    lhs = literalAs(*expr.lhs, *lhsLiteral, rhs->type);
    operandType = rhs->type;
  } else if (rhsIsLiteral) {
    lhs = checkExpr(*expr.lhs, std::nullopt);
    if (!lhs) {
      return nullptr;
    }
    if (lhs->type.kind() == ir::TypeKind::Error) {
      return fail(ir::Type::error());
    }
    if (!lhs->type.isInteger()) {
      diagnostics_.error(expr.span, std::string("operator '") + binaryOpSpelling(op) +
                                        "' requires integer operands, found '" +
                                        lhs->type.name() + "'");
      return fail(ir::Type::error());
    }
    rhs = literalAs(*expr.rhs, *rhsLiteral, lhs->type);
    operandType = lhs->type;
  } else {
    lhs = checkExpr(*expr.lhs, std::nullopt);
    rhs = checkExpr(*expr.rhs, std::nullopt);
    if (!lhs || !rhs) {
      return nullptr;
    }
    if (lhs->type.kind() == ir::TypeKind::Error || rhs->type.kind() == ir::TypeKind::Error) {
      return fail(ir::Type::error());
    }
    if (lhs->type != rhs->type) {
      diagnostics_.error(expr.span, std::string("operands of '") + binaryOpSpelling(op) +
                                        "' have different types: '" + lhs->type.name() +
                                        "' and '" + rhs->type.name() +
                                        "'; Gable v0.1 does not convert between integer types "
                                        "implicitly");
      return fail(ir::Type::error());
    }
    operandType = lhs->type;
  }

  if (operandType.kind() == ir::TypeKind::Error) {
    return fail(ir::Type::error());
  }

  const bool bothBool = operandType == ir::Type::boolean();
  if (isEquality && bothBool) {
    // bool == bool is allowed.
  } else if (operandType == ir::Type::str()) {
    diagnostics_.error(expr.span,
                       "operator '" + std::string(binaryOpSpelling(op)) +
                           "' cannot be applied to values of type 'str'; string comparison is "
                           "not available in Gable v0.1");
    return fail(ir::Type::error());
  } else if (!operandType.isInteger()) {
    diagnostics_.error(expr.span, std::string("operator '") + binaryOpSpelling(op) +
                                      "' requires integer operands, found '" +
                                      operandType.name() + "'");
    return fail(ir::Type::error());
  }

  if ((op == BinaryOp::Divide || op == BinaryOp::Remainder) && rhs->kind == ir::Expr::Kind::IntegerLiteral &&
      rhs->integerValue == 0) {
    diagnostics_.error(expr.rhs->span, std::string(op == BinaryOp::Divide ? "division" : "remainder") +
                                           " by zero");
    return fail(ir::Type::error());
  }

  const ir::Type result = resultType(operandType);
  if (expected && expected->kind() != ir::TypeKind::Error && *expected != result) {
    reportTypeMismatch(expr.span, "expression", *expected, result);
  }

  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Binary, result, expr.span);
  lowered->binaryOp = op;
  lowered->lhs = std::move(lhs);
  lowered->rhs = std::move(rhs);
  return lowered;
}

ir::ExprPtr SemanticAnalyzer::checkCall(const ast::Expr& expr, std::optional<ir::Type> expected) {
  if (expr.name == "print") {
    auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Call, ir::Type::voidType(),
                                              expr.span);
    lowered->callsPrintBuiltin = true;

    if (expr.arguments.size() != 1) {
      diagnostics_.error(expr.span, "print expects exactly 1 argument, found " +
                                        std::to_string(expr.arguments.size()));
      return lowered;
    }
    ir::ExprPtr argument = checkExpr(*expr.arguments[0], std::nullopt);
    if (!argument) {
      return nullptr;
    }
    const ir::Type argumentType = argument->type;
    if (argumentType.kind() != ir::TypeKind::Error && !argumentType.isInteger() &&
        argumentType != ir::Type::boolean() && argumentType != ir::Type::str()) {
      diagnostics_.error(expr.arguments[0]->span, "print cannot print a value of type '" +
                                                      argumentType.name() + "'");
    }
    lowered->arguments.push_back(std::move(argument));

    if (expected && *expected != ir::Type::voidType() && expected->kind() != ir::TypeKind::Error) {
      reportTypeMismatch(expr.span, "call to print", *expected, ir::Type::voidType());
    }
    return lowered;
  }

  const auto it = functions_.find(expr.name);
  if (it == functions_.end()) {
    diagnostics_.error(expr.span, "unknown function '" + expr.name + "'");
    if (lookup(expr.name) != nullptr) {
      diagnostics_.note(expr.span, "a variable named '" + expr.name +
                                       "' exists, but Gable v0.1 cannot call function values");
    }
    for (const auto& argument : expr.arguments) {
      checkExpr(*argument, std::nullopt);
    }
    return std::make_unique<ir::Expr>(ir::Expr::Kind::Call, ir::Type::error(), expr.span);
  }

  const FunctionInfo& callee = it->second;
  auto lowered = std::make_unique<ir::Expr>(ir::Expr::Kind::Call, callee.returnType, expr.span);
  lowered->calleeIndex = static_cast<std::uint32_t>(callee.moduleIndex);
  lowered->name = expr.name;

  if (expr.arguments.size() != callee.parameterTypes.size()) {
    diagnostics_.error(expr.span, "function '" + expr.name + "' expects " +
                                      std::to_string(callee.parameterTypes.size()) +
                                      " argument(s), found " +
                                      std::to_string(expr.arguments.size()));
  }

  const std::size_t checked = std::min(expr.arguments.size(), callee.parameterTypes.size());
  for (std::size_t i = 0; i < checked; ++i) {
    const ir::Type parameterType = callee.parameterTypes[i];
    ir::ExprPtr argument = checkExpr(
        *expr.arguments[i],
        parameterType.kind() != ir::TypeKind::Error ? std::optional<ir::Type>(parameterType)
                                                    : std::nullopt);
    if (!argument) {
      return nullptr;
    }
    if (parameterType.kind() != ir::TypeKind::Error &&
        argument->type.kind() != ir::TypeKind::Error && argument->type != parameterType) {
      diagnostics_.error(expr.arguments[i]->span,
                         "argument " + std::to_string(i + 1) + " of '" + expr.name +
                             "' has type '" + argument->type.name() + "', expected '" +
                             parameterType.name() + "'");
    }
    lowered->arguments.push_back(std::move(argument));
  }
  // Check the remaining arguments (if the arity is wrong) to surface their own
  // errors, but never use them.
  for (std::size_t i = checked; i < expr.arguments.size(); ++i) {
    checkExpr(*expr.arguments[i], std::nullopt);
  }

  if (expected && *expected != callee.returnType && expected->kind() != ir::TypeKind::Error &&
      callee.returnType.kind() != ir::TypeKind::Error) {
    reportTypeMismatch(expr.span, "call result", *expected, callee.returnType);
  }
  return lowered;
}

void SemanticAnalyzer::pushScope() { scopes_.emplace_back(); }

void SemanticAnalyzer::popScope() {
  if (!scopes_.empty()) {
    scopes_.pop_back();
  }
}

SemanticAnalyzer::Binding* SemanticAnalyzer::lookup(const std::string& name) {
  for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
    const auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

ir::Type SemanticAnalyzer::resolveType(const ast::TypeSyntax& syntax) {
  if (!ir::Type::isKnownSourceName(syntax.name)) {
    diagnostics_.error(syntax.span, "unknown type '" + syntax.name + "'");
    return ir::Type::error();
  }
  return ir::Type::fromSourceName(syntax.name);
}

void SemanticAnalyzer::reportTypeMismatch(SourceSpan span, const std::string& context,
                                          ir::Type expected, ir::Type actual) {
  diagnostics_.error(span, "type mismatch in " + context + ": expected '" + expected.name() +
                               "', found '" + actual.name() + "'");
}

bool SemanticAnalyzer::alwaysReturns(const std::vector<ir::StmtPtr>& statements) const {
  for (const auto& statement : statements) {
    if (stmtAlwaysReturns(*statement)) {
      return true;
    }
  }
  return false;
}

bool SemanticAnalyzer::stmtAlwaysReturns(const ir::Stmt& stmt) const {
  switch (stmt.kind) {
    case ir::Stmt::Kind::Return:
      return true;
    case ir::Stmt::Kind::Block:
      return alwaysReturns(stmt.body);
    case ir::Stmt::Kind::If:
      if (!stmt.hasElse) {
        return false;
      }
      return alwaysReturns(stmt.body) && alwaysReturns(stmt.elseBody);
    case ir::Stmt::Kind::While:
    case ir::Stmt::Kind::Let:
    case ir::Stmt::Kind::Assign:
    case ir::Stmt::Kind::Expr:
      return false;
  }
  return false;
}

}  // namespace gable
