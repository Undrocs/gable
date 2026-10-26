// Gable compiler -- locating the running executable.
#ifndef GABLE_PATHS_H
#define GABLE_PATHS_H

#include <string>

namespace gable {

/// Absolute path of the running `gable` executable, or an empty string when the
/// platform cannot report it. Used to find an installed runtime without relying
/// on the prefix chosen at configure time.
std::string executablePath();

}  // namespace gable

#endif  // GABLE_PATHS_H
