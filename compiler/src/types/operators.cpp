#include "gable/operators.h"

namespace gable {

const char* unaryOpSpelling(UnaryOp op) {
  switch (op) {
    case UnaryOp::Negate:
      return "-";
    case UnaryOp::LogicalNot:
      return "!";
  }
  return "?";
}

const char* binaryOpSpelling(BinaryOp op) {
  switch (op) {
    case BinaryOp::Add: return "+";
    case BinaryOp::Subtract: return "-";
    case BinaryOp::Multiply: return "*";
    case BinaryOp::Divide: return "/";
    case BinaryOp::Remainder: return "%";
    case BinaryOp::Equal: return "==";
    case BinaryOp::NotEqual: return "!=";
    case BinaryOp::Less: return "<";
    case BinaryOp::LessEqual: return "<=";
    case BinaryOp::Greater: return ">";
    case BinaryOp::GreaterEqual: return ">=";
    case BinaryOp::LogicalAnd: return "&&";
    case BinaryOp::LogicalOr: return "||";
  }
  return "?";
}

bool isComparisonOperator(BinaryOp op) {
  switch (op) {
    case BinaryOp::Equal:
    case BinaryOp::NotEqual:
    case BinaryOp::Less:
    case BinaryOp::LessEqual:
    case BinaryOp::Greater:
    case BinaryOp::GreaterEqual:
      return true;
    default:
      return false;
  }
}

bool isArithmeticOperator(BinaryOp op) {
  switch (op) {
    case BinaryOp::Add:
    case BinaryOp::Subtract:
    case BinaryOp::Multiply:
    case BinaryOp::Divide:
    case BinaryOp::Remainder:
      return true;
    default:
      return false;
  }
}

}  // namespace gable
