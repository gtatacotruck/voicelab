#pragma once
#include <juce_audio_devices/juce_audio_devices.h>
#include "../DSP/Common.h"

namespace vlab
{
    class SystemAudioCapture : private juce::AudioIODeviceCallback
    {
    public:
        SystemAudioCapture() : fifo (1 << 17) { ringBuffer.setSize (2, 1 << 17); }
        ~SystemAudioCapture() override { close(); }

        juce::StringArray getAvailableInputNames()
        {
            juce::StringArray names;
            if (auto* type = deviceManager.getCurrentDeviceTypeObject())
                names = type->getDeviceNames (true);
            if (names.isEmpty())
            {
                deviceManager.initialiseWithDefaultDevices (0, 0);
                if (auto* type = deviceManager.getCurrentDeviceTypeObject())
                    names = type->getDeviceNames (true);
            }
            return names;
        }

        juce::String setDevice (const juce::String& deviceName)
        {
            close();
            if (deviceName.isEmpty())
                return {};

            juce::AudioDeviceManager::AudioDeviceSetup setup;
            deviceManager.getAudioDeviceSetup (setup);
            setup.inputDeviceName = deviceName;
            setup.useDefaultInputChannels = true;
            setup.outputDeviceName = {};
            setup.useDefaultOutputChannels = false;

            const auto err = deviceManager.initialise (2, 0, nullptr, true, {}, &setup);
            if (err.isNotEmpty())
                return err;
            deviceManager.addAudioCallback (this);
            active = true;
            return {};
        }

        void close()
        {
            deviceManager.removeAudioCallback (this);
            deviceManager.closeAudioDevice();
            active = false;
        }

        bool isActive() const { return active; }

        void pull (vlab::AudioBuffer& out, int numSamples)
        {
            if (! active)
                return;
            int start1, size1, start2, size2;
            fifo.prepareToRead (numSamples, start1, size1, start2, size2);
            int read = 0;
            for (auto pr : { std::pair (start1, size1), std::pair (start2, size2) })
            {
                for (int i = 0; i < pr.second; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        out.addSample (ch, read + i, ringBuffer.getSample (ch, pr.first + i));
                read += pr.second;
            }
            fifo.finishedRead (size1 + size2);
        }

    private:
        juce::AudioDeviceManager deviceManager;
        juce::AbstractFifo fifo;
        vlab::AudioBuffer ringBuffer;
        std::atomic<bool> active { false };

        void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputChannels,
                                                float* const*, int, int numSamples,
                                                const juce::AudioIODeviceCallbackContext&) override
        {
            int start1, size1, start2, size2;
            fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
            auto writeChunk = [&] (int destStart, int n, int srcOffset)
            {
                for (int i = 0; i < n; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float s = (numInputChannels > 0)
                            ? inputs[juce::jmin (ch, numInputChannels - 1)][srcOffset + i] : 0.0f;
                        ringBuffer.setSample (ch, destStart + i, s);
                    }
            };
            writeChunk (start1, size1, 0);
            writeChunk (start2, size2, size1);
            fifo.finishedWrite (size1 + size2);
        }

        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
    };
}
