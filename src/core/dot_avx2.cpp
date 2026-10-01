#include <immintrin.h>
namespace piclocate {
float dotAvx2(const float *a, const float *b, int n) {
    __m256 s0 = _mm256_setzero_ps(), s1 = s0, s2 = s0, s3 = s0;
    int i = 0;
    for (; i + 31 < n; i += 32) {
        s0 = _mm256_add_ps(s0, _mm256_mul_ps(_mm256_loadu_ps(a + i), _mm256_loadu_ps(b + i)));
        s1 = _mm256_add_ps(s1,
                           _mm256_mul_ps(_mm256_loadu_ps(a + i + 8), _mm256_loadu_ps(b + i + 8)));
        s2 = _mm256_add_ps(s2,
                           _mm256_mul_ps(_mm256_loadu_ps(a + i + 16), _mm256_loadu_ps(b + i + 16)));
        s3 = _mm256_add_ps(s3,
                           _mm256_mul_ps(_mm256_loadu_ps(a + i + 24), _mm256_loadu_ps(b + i + 24)));
    }
    alignas(32) float lanes[8];
    _mm256_store_ps(lanes, _mm256_add_ps(_mm256_add_ps(s0, s1), _mm256_add_ps(s2, s3)));
    float sum = 0;
    for (float value : lanes)
        sum += value;
    for (; i < n; ++i)
        sum += a[i] * b[i];
    return sum;
}
} // namespace piclocate
