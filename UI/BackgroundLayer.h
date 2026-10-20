#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include <vector>

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wsign-conversion"
  #pragma clang diagnostic ignored "-Wconversion"
  #pragma clang diagnostic ignored "-Wfloat-equal"
  #pragma clang diagnostic ignored "-Wunused-function"
  #pragma clang diagnostic ignored "-Wcast-qual"
  #pragma clang diagnostic ignored "-Wzero-as-null-pointer-constant"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wsign-conversion"
  #pragma GCC diagnostic ignored "-Wconversion"
  #pragma GCC diagnostic ignored "-Wfloat-equal"
  #pragma GCC diagnostic ignored "-Wunused-function"
  #pragma GCC diagnostic ignored "-Wcast-qual"
  #pragma GCC diagnostic ignored "-Wzero-as-null-pointer-constant"
#elif defined(_MSC_VER)
  #pragma warning(push)
  #pragma warning(disable: 4244 4245 4996 4457)
#endif

#define STB_IMAGE_IMPLEMENTATION
#include "../ThirdParty/stb_image.h"

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#elif defined(_MSC_VER)
  #pragma warning(pop)
#endif

namespace vlab::ui
{
    class BackgroundLayer : public juce::Component, private juce::Timer
    {
    public:
        void setOpacity (float a) { opacity = juce::jlimit (0.0f, 1.0f, a); repaint(); }
        float getOpacity() const { return opacity; }

        juce::String loadFile (const juce::File& file)
        {
            stopTimer();
            frames.clear();
            frameDelaysMs.clear();
            currentFrame = 0;
            sourceFile = juce::File();

            if (! file.existsAsFile())
                return "File does not exist";

            juce::String warning;
            if (file.getFileExtension().equalsIgnoreCase (".gif"))
            {
                if (! loadGif (file))
                {
                    warning = tryStaticFallback (file);
                    if (frames.empty())
                        return "Could not decode this GIF, and the static-image fallback also failed";
                }
            }
            else
            {
                auto img = juce::ImageFileFormat::loadFrom (file);
                if (! img.isValid())
                    return "Unsupported or corrupt image file";
                frames.push_back (img);
                frameDelaysMs.push_back (0);
            }

            sourceFile = file;
            if (frames.size() > 1)
                startTimer (frameDelaysMs[0]);
            repaint();
            return warning;
        }

        void clear()
        {
            stopTimer();
            frames.clear();
            frameDelaysMs.clear();
            sourceFile = juce::File();
            repaint();
        }

        bool hasImage() const { return ! frames.empty(); }
        juce::String getSourceName() const { return sourceFile.exists() ? sourceFile.getFileName() : juce::String(); }

        void paint (juce::Graphics& g) override
        {
            if (frames.empty() || opacity <= 0.0f)
                return;
            auto& img = frames[(size_t) currentFrame];
            g.setOpacity (opacity);
            g.drawImage (img, getLocalBounds().toFloat(), juce::RectanglePlacement::fillDestination);
            g.setOpacity (1.0f);
        }

    private:
        std::vector<juce::Image> frames;
        std::vector<int> frameDelaysMs;
        int currentFrame = 0;
        float opacity = 0.5f;
        juce::File sourceFile;

        bool loadGif (const juce::File& file)
        {
            juce::MemoryBlock mb;
            if (! file.loadFileAsData (mb))
                return false;

            int w = 0, h = 0, numFrames = 0, comp = 0;
            int* delays = nullptr;
            unsigned char* pixels = stbi_load_gif_from_memory (
                (const unsigned char*) mb.getData(), (int) mb.getSize(),
                &delays, &w, &h, &numFrames, &comp, 4);

            if (pixels == nullptr || numFrames <= 0)
                return false;

            for (int f = 0; f < numFrames; ++f)
            {
                juce::Image img (juce::Image::ARGB, w, h, true);
                juce::Image::BitmapData bmp (img, juce::Image::BitmapData::writeOnly);
                const unsigned char* src = pixels + (size_t) f * w * h * 4;
                for (int y = 0; y < h; ++y)
                {
                    for (int x = 0; x < w; ++x)
                    {
                        const unsigned char* p = src + ((size_t) y * w + x) * 4;
                        bmp.setPixelColour (x, y, juce::Colour (p[0], p[1], p[2], p[3]));
                    }
                }
                frames.push_back (img);
                frameDelaysMs.push_back (juce::jmax (20, delays[f])); // guard against 0ms delays some GIFs use
            }

            stbi_image_free (pixels);
            return true;
        }

        juce::String tryStaticFallback (const juce::File& file)
        {
            auto img = juce::ImageFileFormat::loadFrom (file);
            if (! img.isValid())
                return {};
            frames.push_back (img);
            frameDelaysMs.push_back (0);
            return "Could not animate this GIF; showing its first frame only.";
        }

        void timerCallback() override
        {
            if (frames.size() <= 1) { stopTimer(); return; }
            currentFrame = (currentFrame + 1) % (int) frames.size();
            repaint();
            startTimer (frameDelaysMs[(size_t) currentFrame]);
        }
    };
}
