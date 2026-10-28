#include <juce_core/juce_core.h>
#include "dsp/StftConfig.h"

struct StftConfigTests : public juce::UnitTest
{
    StftConfigTests() : juce::UnitTest ("StftConfig", "SNC") {}

    void runTest() override
    {
        StftConfig config;

        beginTest ("bin count and frame count");
        expectEquals (config.numBins(), 1025);

        for (int length : { 1, 2047, 2048, 44100, 100001 })
        {
            const int padded = config.paddedLength (length);
            expect (padded >= length + 2 * (config.fftSize() - config.hopSize));
            expectEquals ((padded - config.fftSize()) % config.hopSize, 0);
            expect (config.numFrames (padded) >= 1);
        }
    }
};

static StftConfigTests stftConfigTests;

int main()
{
    juce::UnitTestRunner runner;
    runner.runAllTests();

    for (int i = 0; i < runner.getNumResults(); ++i)
        if (runner.getResult (i)->failures > 0)
            return 1;

    return 0;
}
