// Gable compiler -- operators shared by the AST and the typed IR.
#ifndef GABLE_OPERATORS_H
#define GABLE_OPERATORS_H

namespace gable {

enum class UnaryOp { Negate, LogicalNot };

enum class BinaryOp {
  Add,
  Subtract,
  Multiply,
  Divide,
  Remainder,
  Equal,
  NotEqual,
  Less,
  LessEqual,
  Greater,
  GreaterEqual,
  LogicalAnd,
  LogicalOr,
};

const char* unaryOpSpelling(UnaryOp op);
const char* binaryOpSpelling(BinaryOp op);

/// True for operators that produce a bool from two integer operands.
bool isComparisonOperator(BinaryOp op);

/// True for arithmetic operators (+, -, *, /, %).
bool isArithmeticOperator(BinaryOp op);

}  // namespace gable

#endif  // GABLE_OPERATORS_H
