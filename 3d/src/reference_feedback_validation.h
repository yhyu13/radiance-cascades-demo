#ifndef REFERENCE_FEEDBACK_VALIDATION_H
#define REFERENCE_FEEDBACK_VALIDATION_H

#include <string>

// Phase 6 validation: gate G7 (temporal hit-chart feedback) and gate G10
// (determinism and stability). Runs the full C5->C0 hierarchy with previous-
// generation C0 feedback, verifies controlled cross-chart bounce propagation,
// reset/invalidation, read/write separation, and multi-frame determinism.
// Returns true only when every predicate passes and the JSON report is written.

bool runReferenceFeedbackValidation(const std::string& reportPath);

#endif
