// Gable compiler -- version information.
//
// GABLE_VERSION_STRING is provided by the build system.
#ifndef GABLE_VERSION_H
#define GABLE_VERSION_H

#ifndef GABLE_VERSION_STRING
#define GABLE_VERSION_STRING "0.0.0-unknown"
#endif

namespace gable {

/// Semantic version of this compiler build, e.g. "0.1.0".
const char* versionString();
/// Language version implemented by this compiler, e.g. "0.1".
const char* languageVersionString();

}  // namespace gable

#endif  // GABLE_VERSION_H
