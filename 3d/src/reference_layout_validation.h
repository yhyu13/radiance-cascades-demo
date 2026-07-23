#ifndef REFERENCE_LAYOUT_VALIDATION_H
#define REFERENCE_LAYOUT_VALIDATION_H

#include <string>

// Phase 3 parity layout kernel validation (gates G2/G3/G4).
// Runs CPU golden checks against the independent fixture table, cross-checks
// the GLSL layout decode against the CPU oracle and the golden fixtures,
// allocates the six atlas pairs, produces band-marker readback evidence for
// all six cascades, and verifies interval/blocker classification.
// Returns true only when every required predicate passes and the JSON report
// was written.

bool runReferenceLayoutValidation(const std::string& reportPath);

#endif
