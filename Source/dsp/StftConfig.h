#pragma once

// STFT settings shared by noise-profile estimation and processing (plan §2).
struct StftConfig
{
    int fftOrder = 11;   // N = 2048
    int hopSize  = 512;  // H (75% overlap)

    int fftSize() const { return 1 << fftOrder; }
    int numBins() const { return fftSize() / 2 + 1; }

    // Front pad of N - H zeros, end pad so the last frame is complete.
    int paddedLength (int numSamples) const;
    // Takes the padded length, not the raw sample count.
    int numFrames (int paddedLength) const;
};
