#include "test_framework.h"

#include <iostream>

namespace gabletest {

std::vector<TestCase>& registry() {
  static std::vector<TestCase> tests;
  return tests;
}

Registrar::Registrar(const char* name, void (*function)()) {
  registry().push_back(TestCase{name, function});
}

}  // namespace gabletest

int main() {
  std::size_t failures = 0;
  std::size_t run = 0;
  for (const gabletest::TestCase& test : gabletest::registry()) {
    ++run;
    try {
      test.function();
    } catch (const gabletest::Failure& failure) {
      ++failures;
      std::cerr << "FAIL " << test.name << "\n     " << failure.message << "\n";
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "FAIL " << test.name << "\n     unexpected exception: " << error.what() << "\n";
    } catch (...) {
      ++failures;
      std::cerr << "FAIL " << test.name << "\n     unexpected unknown exception\n";
    }
  }

  std::cout << (failures == 0 ? "PASS " : "FAILED ") << (run - failures) << "/" << run
            << " unit tests\n";
  return failures == 0 ? 0 : 1;
}
