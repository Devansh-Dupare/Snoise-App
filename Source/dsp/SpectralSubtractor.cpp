#include "SpectralSubtractor.h"

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>

void SpectralSubtractor::setAlpha (float newAlpha)
{
    alpha = std::isfinite (newAlpha) ? std::clamp (newAlpha, 0.0f, maxAlpha) : 0.0f;
}

void SpectralSubtractor::process (float* fftData, const NoiseProfile& profile, int numBins) const
{
    const auto& Dv = profile.getAverageMagnitude();
    jassert (static_cast<int> (Dv.size()) >= numBins);

    for (int k = 0; k < numBins; ++k)
    {
        float& re = fftData[2 * k];
        float& im = fftData[2 * k + 1];

        const float Dx = std::hypot (re, im);                                  // |X(m,k)|
        const float Ds = std::max (Dx - alpha * Dv[static_cast<size_t> (k)], 0.0f);   // D̂s(m,k)
        float G = Ds / (Dx + epsilon);                                         // G(m,k)

        if (! std::isfinite (G))
        {
            jassertfalse;
            G = 0.0f;
        }

        re *= G;                                                               // Re{Y}
        im *= G;                                                               // Im{Y}
    }
}
