#pragma once

#include "NoiseProfile.h"

// Magnitude spectral subtraction with phase preserved (plan §5, brief §4.3-4.5):
//   Ds = max(Dx - alpha * Dv, 0),  G = Ds / (Dx + eps),  Y = G * X.
class SpectralSubtractor
{
public:
    static constexpr float maxAlpha = 5.0f;

    void setAlpha (float newAlpha);
    float getAlpha() const { return alpha; }
    void setEpsilon (float newEpsilon) { epsilon = newEpsilon; }

    // Scales Re/Im of bins 0 … numBins - 1 of an interleaved FFT buffer in place.
    void process (float* fftData, const NoiseProfile& profile, int numBins) const;

private:
    float alpha = 1.0f, epsilon = 1.0e-10f;
};
