#ifndef REFERENCE_FINAL_VALIDATION_H
#define REFERENCE_FINAL_VALIDATION_H

#include <string>

// Phase 7 validation: gate G9 (final consumer). Runs the converged reference
// pipeline, renders the final LightingView through the completed C0 read
// view, and verifies pixel-equivalence with the CPU oracle, baseline
// equivalence with reference disabled, surface classification, distinct
// resource identities, and absence of upper-cascade stubs. Also emits a PNG
// artifact for human inspection.

bool runReferenceFinalValidation(const std::string& reportPath);

#endif
