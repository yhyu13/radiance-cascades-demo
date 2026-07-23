#ifndef REFERENCE_TRANSPORT_VALIDATION_H
#define REFERENCE_TRANSPORT_VALIDATION_H

#include <string>

// Phase 4 local single-cascade transport validation (gates G5 payload
// contract and G8 material/direct-light transport). Evaluates independent
// golden fixtures through the CPU oracle and the GLSL transport shader,
// then runs full-band transport for all six cascades and verifies payload
// semantics in readback evidence. Temporal feedback and upper merge remain
// disabled. Returns true only when every predicate passes and the JSON
// report was written.

bool runReferenceTransportValidation(const std::string& reportPath);

#endif
