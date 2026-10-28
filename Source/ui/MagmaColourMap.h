#pragma once

#include <juce_graphics/juce_graphics.h>
#include <array>

// 256-entry Magma lookup table, interpolated from 11 evenly spaced stops of the Magma colour map.
namespace MagmaColourMap
{
    inline const std::array<juce::PixelARGB, 256>& table()
    {
        static const std::array<juce::PixelARGB, 256> lut = []
        {
            constexpr juce::uint32 stops[] = { 0x000004, 0x140e36, 0x3b0f70, 0x641a80, 0x8c2981, 0xb73779,
                                               0xde4968, 0xf7705c, 0xfe9f6d, 0xfecf92, 0xfcfdbf };
            constexpr int numSegments = 10;
            std::array<juce::PixelARGB, 256> out {};

            for (int i = 0; i < 256; ++i)
            {
                const float position = static_cast<float> (i) / 255.0f * static_cast<float> (numSegments);
                const int segment = juce::jmin (static_cast<int> (position), numSegments - 1);
                const float fraction = position - static_cast<float> (segment);
                const auto a = juce::Colour (0xff000000u | stops[segment]);
                const auto b = juce::Colour (0xff000000u | stops[segment + 1]);
                out[static_cast<size_t> (i)] = a.interpolatedWith (b, fraction).getPixelARGB();
            }

            return out;
        }();

        return lut;
    }

    // Maps dB in [minDb, maxDb] to a colour; values outside are clamped.
    inline juce::PixelARGB fromDecibels (float db, float minDb = -90.0f, float maxDb = 0.0f)
    {
        const float t = juce::jlimit (0.0f, 1.0f, (db - minDb) / (maxDb - minDb));
        return table()[static_cast<size_t> (juce::roundToInt (t * 255.0f))];
    }
}
