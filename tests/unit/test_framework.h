// Gable compiler -- minimal test framework.
//
// Tests register themselves at static initialization time; main() runs them in
// registration order and reports failures with file and line information.
#ifndef GABLE_TESTS_TEST_FRAMEWORK_H
#define GABLE_TESTS_TEST_FRAMEWORK_H

#include <exception>
#include <sstream>
#include <string>
#include <vector>

namespace gabletest {

struct TestCase {
  const char* name;
  void (*function)();
};

std::vector<TestCase>& registry();

struct Registrar {
  Registrar(const char* name, void (*function)());
};

/// Thrown by a failing check; caught by the runner.
struct Failure : std::exception {
  explicit Failure(std::string text) : message(std::move(text)) {}
  const char* what() const noexcept override { return message.c_str(); }
  std::string message;
};

}  // namespace gabletest

#define GABLE_TEST(name)                                                   \
  static void name();                                                      \
  static ::gabletest::Registrar gable_test_registrar_##name(#name, name);   \
  static void name()

#define GABLE_CHECK(condition)                                                          \
  do {                                                                                  \
    if (!(condition)) {                                                                 \
      std::ostringstream gable_check_stream;                                            \
      gable_check_stream << __FILE__ << ":" << __LINE__ << ": check failed: "           \
                         << #condition;                                                 \
      throw ::gabletest::Failure(gable_check_stream.str());                             \
    }                                                                                   \
  } while (false)

#define GABLE_CHECK_EQ(expected, actual)                                                \
  do {                                                                                  \
    const auto& gable_check_expected = (expected);                                      \
    const auto& gable_check_actual = (actual);                                          \
    if (!(gable_check_expected == gable_check_actual)) {                                \
      std::ostringstream gable_check_stream;                                            \
      gable_check_stream << __FILE__ << ":" << __LINE__ << ": expected "                \
                         << #expected << " == " << #actual;                             \
      throw ::gabletest::Failure(gable_check_stream.str());                             \
    }                                                                                   \
  } while (false)

#endif  // GABLE_TESTS_TEST_FRAMEWORK_H
