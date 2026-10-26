// Entry point for the `gable` compiler driver.
#include <iostream>
#include <string>
#include <vector>

#include "gable/cli.h"

int main(int argc, char** argv) {
  std::vector<std::string> arguments;
  arguments.reserve(argc > 1 ? static_cast<std::size_t>(argc - 1) : 0);
  for (int i = 1; i < argc; ++i) {
    arguments.emplace_back(argv[i]);
  }

  std::ios::sync_with_stdio(false);
  const gable::CliExitCode code = gable::runCommandLine(arguments, std::cout, std::cerr);
  std::cout.flush();
  return static_cast<int>(code);
}
