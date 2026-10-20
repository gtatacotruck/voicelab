#pragma once
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <functional>
#include <utility>

namespace vlab
{
    struct UpdateInfo
    {
        juce::String latestVersion;
        juce::String downloadUrl;
        juce::String releaseNotes;
        bool mandatory = false;
    };

    class UpdateChecker : private juce::Thread
    {
    public:
        UpdateChecker() : juce::Thread ("UpdateChecker") {}
        ~UpdateChecker() override { stopThread (4000); }

        void checkAsync (const juce::String& currentVersionIn, std::function<void (UpdateInfo)> onUpdateAvailable)
        {
            if (isThreadRunning())
                return; // already checking, let that one finish rather than racing a second fetch
            currentVersion = currentVersionIn;
            callback = std::move (onUpdateAvailable);
            startThread();
        }

        static bool isNewer (const juce::String& remote, const juce::String& local)
        {
            const auto remoteParts = juce::StringArray::fromTokens (remote, ".", "");
            const auto localParts  = juce::StringArray::fromTokens (local, ".", "");
            const int n = juce::jmax (remoteParts.size(), localParts.size());
            for (int i = 0; i < n; ++i)
            {
                const int r = i < remoteParts.size() ? remoteParts[i].getIntValue() : 0;
                const int l = i < localParts.size()  ? localParts[i].getIntValue()  : 0;
                if (r != l)
                    return r > l;
            }
            return false;
        }

    private:
        juce::String currentVersion;
        std::function<void (UpdateInfo)> callback;

        void run() override
        {
            const juce::URL url ("https://vlab-two.vercel.app/version.json");

            auto stream = url.createInputStream (false, nullptr, nullptr, juce::String(), 5000);
            if (stream == nullptr || threadShouldExit())
                return;

            const auto text = stream->readEntireStreamAsString();
            if (threadShouldExit() || text.isEmpty())
                return;

            const auto parsed = juce::JSON::parse (text);
            if (! parsed.isObject())
                return;

            UpdateInfo info;
            info.latestVersion = parsed.getProperty ("latest_version", "").toString();
            info.downloadUrl   = parsed.getProperty ("download_url", "").toString();
            info.releaseNotes  = parsed.getProperty ("release_notes", "").toString();
            info.mandatory     = (bool) parsed.getProperty ("mandatory", false);

            if (info.latestVersion.isEmpty() || info.downloadUrl.isEmpty())
                return; // malformed response, nothing sensible to show
            if (! isNewer (info.latestVersion, currentVersion))
                return; // already up to date (or somehow newer than the site, e.g. a dev build)

            if (threadShouldExit())
                return;

            auto cb = callback;
            juce::MessageManager::callAsync ([cb, info] { if (cb) cb (info); });
        }
    };
}
