// Usage: gable_unit_tests [name-filter]
//
// Without an argument every registered test runs. With an argument only tests
// whose name contains it run, which makes a single regression easy to reproduce
// without waiting for the whole suite.
#include "test_framework.h"

#include <cstring>
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

int main(int argc, char** argv) {
  const char* filter = argc > 1 ? argv[1] : nullptr;
  std::size_t failures = 0;
  std::size_t run = 0;
  for (const gabletest::TestCase& test : gabletest::registry()) {
    if (filter != nullptr && std::strstr(test.name, filter) == nullptr) {
      continue;
    }
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

  if (run == 0) {
    std::cerr << "no test matches '" << (filter == nullptr ? "" : filter) << "'\n";
    return 2;
  }
  std::cout << (failures == 0 ? "PASS " : "FAILED ") << (run - failures) << "/" << run
            << " unit tests\n";
  return failures == 0 ? 0 : 1;
}
