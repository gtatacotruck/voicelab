#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "../DSP/Common.h"

namespace vlab
{
    class MonitorOutput : private juce::AudioIODeviceCallback
    {
    public:
        MonitorOutput() : fifo (1 << 16) { ringBuffer.setSize (2, 1 << 16); }
        ~MonitorOutput() override { close(); }

        void setDevice (const juce::String& deviceName)
        {
            close();
            if (deviceName.isEmpty()) return;

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup (setup);
            setup.outputDeviceName = deviceName;
            setup.useDefaultOutputChannels = true;
            setup.bufferSize = 256;
            deviceManager.initialise (0, 2, nullptr, true, {}, &setup);
            deviceManager.addAudioCallback (this);
        }

        void close()
        {
            deviceManager.removeAudioCallback (this);
            deviceManager.closeAudioDevice();
        }

        void setVolume (float newVolume) { volume = juce::jlimit (0.0f, 1.5f, newVolume); }
        float getVolume() const { return volume.load(); }

        void push (const vlab::AudioBuffer& buf, int numSamples)
        {
            const int maxBacklog = numSamples * 4;
            if (fifo.getNumReady() > maxBacklog)
            {
                int dStart1, dSize1, dStart2, dSize2;
                fifo.prepareToRead (fifo.getNumReady() - maxBacklog, dStart1, dSize1, dStart2, dSize2);
                fifo.finishedRead (dSize1 + dSize2);
            }

            int start1, size1, start2, size2;
            fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
            if (size1 > 0) { ringBuffer.copyFrom (0, start1, buf, 0, 0, size1); ringBuffer.copyFrom (1, start1, buf, 1, 0, size1); }
            if (size2 > 0) { ringBuffer.copyFrom (0, start2, buf, 0, size1, size2); ringBuffer.copyFrom (1, start2, buf, 1, size1, size2); }
            fifo.finishedWrite (size1 + size2);
        }

        static juce::StringArray availableDeviceNames()
        {
            juce::AudioDeviceManager tmp;
            juce::StringArray names;
            if (auto* type = tmp.getCurrentDeviceTypeObject())
                names = type->getDeviceNames (false);
            return names;
        }

    private:
        juce::AudioDeviceManager deviceManager;
        juce::AbstractFifo fifo;
        vlab::AudioBuffer ringBuffer;
        std::atomic<float> volume { 1.0f };

        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs,
                                                int numOutputChannels, int numSamples,
                                                const juce::AudioIODeviceCallbackContext&) override
        {
            const float vol = volume.load();
            int start1, size1, start2, size2;
            fifo.prepareToRead (numSamples, start1, size1, start2, size2);
            int written = 0;
            for (auto pr : { std::pair (start1, size1), std::pair (start2, size2) })
            {
                for (int i = 0; i < pr.second; ++i)
                    for (int ch = 0; ch < numOutputChannels && ch < 2; ++ch)
                        outputs[ch][written + i] = ringBuffer.getSample (ch, pr.first + i) * vol;
                written += pr.second;
            }
            for (int ch = 0; ch < numOutputChannels; ++ch)
                for (int i = written; i < numSamples; ++i)
                    outputs[ch][i] = 0.0f; // underrun -> silence, not garbage
            fifo.finishedRead (size1 + size2);
        }

        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
    };
}
