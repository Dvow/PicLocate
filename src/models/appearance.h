#pragma once
#include "core/core.h"

namespace piclocate {
inline constexpr int AppearanceVersion = 1;
// A compact, normalized descriptor for matching asset variants. No model required.
std::vector<float> appearanceDescriptor(const QImage &source);
float appearanceSimilarity(const float *a, const float *b, SimilarityMode mode);
// Prepare the focused cosine space once. Source and destination may overlap.
// Returns 256 dimensions for shape/color, otherwise Dimensions; zero stays zero.
int projectAppearance(const float *source, float *destination, SimilarityMode mode);
} // namespace piclocate
