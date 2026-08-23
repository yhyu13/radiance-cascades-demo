#ifndef CHART_PROVIDER_VALIDATION_H
#define CHART_PROVIDER_VALIDATION_H

#include <string>

// Phase 11 M1 CPU gate. No GPU, no shaders, no G0-G10 mutation.
// R4/R5/R6 are checked against synthetic unique-UV2 fixtures. Sponza is
// diagnosed for authored UV2 availability (Q5) and must fail closed.

bool runChartProviderValidation(const std::string& reportPath);

#endif
