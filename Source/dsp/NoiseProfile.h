#pragma once

#include <vector>

// Average noise magnitude spectrum D̄v(k) = (1/M) Σ |V(m,k)| (plan §5, brief §4.2).
class NoiseProfile
{
public:
    void reset (int numBins);

    // Adds |V(m,k)| for k = 0 … numBins - 1 from an interleaved re/im FFT buffer.
    void addFrame (const float* fftData);

    // Divides the accumulated sums by M. Leaves the profile not ready when M == 0.
    void finalise();

    bool isReady() const { return ready; }
    int getFrameCount() const { return frameCount; }
    const std::vector<float>& getAverageMagnitude() const { return average; }

private:
    std::vector<double> sum;
    std::vector<float> average;
    int frameCount = 0;
    bool ready = false;
};
