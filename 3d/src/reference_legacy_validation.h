#ifndef REFERENCE_LEGACY_VALIDATION_H
#define REFERENCE_LEGACY_VALIDATION_H

#include <string>

// Legacy Cornell validation: layout decode agreement (CPU oracle vs GLSL),
// final-view pixel match against the CPU oracle, and light visibility.
// Returns true only when all predicates pass and the JSON report is written.

bool runReferenceLegacyValidation(const std::string& reportPath);

#endif
