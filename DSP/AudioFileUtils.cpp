#pragma once
#include "Common.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <limits>
#include <memory>

namespace vlab::dsp
{
    inline std::shared_ptr<vlab::AudioBuffer> resampleLinear (const vlab::AudioBuffer& src,
                                                                double srcRate, double dstRate)
    {
        const double factor = dstRate / srcRate;
        const int inLen = src.getNumSamples();
        const int outLen = std::max (1, (int) (inLen * factor));
        auto out = std::make_shared<vlab::AudioBuffer> (2, outLen);
        for (int i = 0; i < outLen; ++i)
        {
            const double xPos = (outLen > 1) ? (double) i * (inLen - 1) / (double) (outLen - 1) : 0.0;
            const int i0 = juce::jlimit (0, inLen - 1, (int) xPos);
            const int i1 = juce::jlimit (0, inLen - 1, i0 + 1);
            const double frac = xPos - i0;
            for (int ch = 0; ch < 2; ++ch)
            {
                const float a = src.getSample (ch, i0), b = src.getSample (ch, i1);
                out->setSample (ch, i, (float) (a * (1.0 - frac) + b * frac));
            }
        }
        return out;
    }

    // Returns null when the file can't be read, would not fit in maxDecodedBytes (0 = no limit),
    // or the machine runs out of memory part way through, instead of letting bad_alloc kill the app.
    inline std::shared_ptr<vlab::AudioBuffer> loadAudioFileAsStereo (const juce::File& file,
                                                                      juce::AudioFormatManager& formatManager,
                                                                      size_t maxDecodedBytes = 0)
    {
        try
        {
            std::unique_ptr<juce::AudioFormatReader> reader (formatManager.createReaderFor (file));
            if (reader == nullptr)
                return nullptr;

            const juce::int64 length = reader->lengthInSamples;
            if (length <= 0 || length > (juce::int64) (std::numeric_limits<int>::max() / 8))
                return nullptr;

            const bool needsResample = reader->sampleRate > 0.0 && ! juce::approximatelyEqual (reader->sampleRate, vlab::kSampleRate);
            const double ratio = needsResample ? vlab::kSampleRate / reader->sampleRate : 1.0;
            const double decodedBytes = (double) length * 2.0 * sizeof (float);
            const double peakBytes = needsResample ? decodedBytes + decodedBytes * ratio : decodedBytes;
            if (maxDecodedBytes > 0 && peakBytes > (double) maxDecodedBytes)
                return nullptr;

            const int numSamples = (int) length;
            auto stereo = std::make_shared<vlab::AudioBuffer> (2, numSamples);
            if (! reader->read (stereo.get(), 0, numSamples, 0, true, true))
                return nullptr;
            if (reader->numChannels == 1)
                stereo->copyFrom (1, 0, *stereo, 0, 0, numSamples);

            if (needsResample)
                stereo = resampleLinear (*stereo, reader->sampleRate, vlab::kSampleRate);

            return stereo;
        }
        catch (const std::exception&)
        {
            return nullptr;
        }
    }
}
