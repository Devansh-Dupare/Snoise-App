#include "NoiseProfile.h"

#include <cmath>

void NoiseProfile::reset (int numBins)
{
    sum.assign (static_cast<size_t> (numBins), 0.0);
    average.assign (static_cast<size_t> (numBins), 0.0f);
    frameCount = 0;
    ready = false;
}

void NoiseProfile::addFrame (const float* fftData)
{
    for (size_t k = 0; k < sum.size(); ++k)
        sum[k] += std::hypot (fftData[2 * k], fftData[2 * k + 1]);

    ++frameCount;
}

void NoiseProfile::finalise()
{
    if (frameCount == 0)
    {
        ready = false;
        return;
    }

    for (size_t k = 0; k < sum.size(); ++k)
        average[k] = static_cast<float> (sum[k] / frameCount);

    ready = true;
}
