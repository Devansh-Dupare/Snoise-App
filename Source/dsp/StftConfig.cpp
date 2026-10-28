#include "StftConfig.h"

int StftConfig::paddedLength (int numSamples) const
{
    const int overlap = fftSize() - hopSize;
    int length = numSamples + 2 * overlap;
    const int remainder = (length - fftSize()) % hopSize;

    if (remainder != 0)
        length += hopSize - remainder;

    return length < fftSize() ? fftSize() : length;
}

int StftConfig::numFrames (int paddedLength) const
{
    return paddedLength < fftSize() ? 0 : 1 + (paddedLength - fftSize()) / hopSize;
}
