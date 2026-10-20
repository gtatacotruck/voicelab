#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <string>
#include <vector>
#include <cstring>
#include <cstdint>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <sys/socket.h>
 #include <sys/un.h>
 #include <unistd.h>
 #include <poll.h>
 #include <cerrno>
 #include <cstring>
#endif

namespace vlab
{
    // Shown as a clickable button on the Rich Presence card.
    static constexpr const char* kWebsiteUrl = "https://vlab-two.vercel.app/#download";

    class DiscordRichPresence : private juce::Thread
    {
    public:
        explicit DiscordRichPresence (juce::String applicationIdIn)
            : juce::Thread ("DiscordRPC"), applicationId (std::move (applicationIdIn))
        {
        }

        ~DiscordRichPresence() override
        {
            stopThread (2000);
            disconnect();
        }

        void start()
        {
            if (! isThreadRunning())
                startThread();
        }

        void setEnabled (bool shouldBeEnabled)
        {
            enabled = shouldBeEnabled;
            notify(); // wake the background thread immediately instead of waiting out its poll interval
        }

        bool isEnabled() const { return enabled; }

        void setPresence (const juce::String& details, const juce::String& state, bool showElapsed = true)
        {
            {
                const std::lock_guard<std::mutex> lock (activityMutex);
                pendingDetails = details;
                pendingState = state;
                pendingClock = showElapsed ? Clock::SinceLaunch : Clock::None;
            }
            activityDirty = true;
            notify();
        }

        void setPresenceWithRange (const juce::String& details, const juce::String& state,
                                    juce::int64 startEpochSeconds, juce::int64 endEpochSeconds)
        {
            {
                const std::lock_guard<std::mutex> lock (activityMutex);
                pendingDetails = details;
                pendingState = state;
                pendingClock = Clock::ExplicitRange;
                pendingRangeStart = startEpochSeconds;
                pendingRangeEnd = endEpochSeconds;
            }
            activityDirty = true;
            notify();
        }

    private:
        enum class Clock { None, SinceLaunch, ExplicitRange };

        // ── background connection/keep-alive loop ─────────────────────────
        void run() override
        {
            while (! threadShouldExit())
            {
                if (! enabled)
                {
                    if (connected)
                    {
                        sendClearActivity();
                        disconnect();
                    }
                    wait (500);
                    continue;
                }

                if (! connected)
                {
                    if (tryConnect())
                    {
                        connected = true;
                        activityDirty = true; // force a (re-)send of the current presence
                    }
                    else
                    {
                        wait (2000);
                        continue;
                    }
                }

                if (connected && ! isSocketStillAlive())
                {
                    disconnect();
                    continue;
                }

                if (connected && activityDirty.exchange (false))
                {
                    juce::String details, state;
                    Clock clock;
                    juce::int64 rangeStart = 0, rangeEnd = 0;
                    {
                        const std::lock_guard<std::mutex> lock (activityMutex);
                        details = pendingDetails;
                        state = pendingState;
                        clock = pendingClock;
                        rangeStart = pendingRangeStart;
                        rangeEnd = pendingRangeEnd;
                    }

                    if (! sendSetActivity (details, state, clock, rangeStart, rangeEnd))
                        disconnect();
                }

                wait (100);
            }

            if (connected)
            {
                sendClearActivity();
                disconnect();
            }
        }

        // ── connection setup ───────────────────────────────────────────────
        bool tryConnect()
        {
           #if JUCE_WINDOWS
            for (int i = 0; i < 10; ++i)
            {
                const auto pipeName = juce::String ("\\\\.\\pipe\\discord-ipc-") + juce::String (i);
                HANDLE h = CreateFileW (pipeName.toWideCharPointer(),
                                         GENERIC_READ | GENERIC_WRITE,
                                         0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (h != INVALID_HANDLE_VALUE)
                {
                    pipeHandle = h;
                    if (sendHandshake() && waitForReady())
                        return true;

                    CloseHandle (pipeHandle);
                    pipeHandle = INVALID_HANDLE_VALUE;
                }
            }
            return false;
           #else
            juce::StringArray baseDirs;
            for (auto* envVar : { "XDG_RUNTIME_DIR", "TMPDIR", "TMP", "TEMP" })
                if (auto* v = ::getenv (envVar))
                    if (v[0] != 0)
                        baseDirs.add (v);
            baseDirs.add ("/tmp");

            for (const auto& dir : baseDirs)
            {
                for (int i = 0; i < 10; ++i)
                {
                    const auto path = juce::File (dir).getChildFile (juce::String ("discord-ipc-") + juce::String (i))
                                                        .getFullPathName();

                    int fd = (int) socket (AF_UNIX, SOCK_STREAM, 0);
                    if (fd < 0)
                        continue;

                    sockaddr_un addr {};
                    addr.sun_family = AF_UNIX;
                    std::strncpy (addr.sun_path, path.toRawUTF8(), sizeof (addr.sun_path) - 1);

                    if (connect (fd, (sockaddr*) &addr, sizeof (addr)) == 0)
                    {
                        socketFd = fd;
                        if (sendHandshake() && waitForReady())
                            return true;

                        ::close (fd);
                        socketFd = -1;
                    }
                    else
                    {
                        ::close (fd);
                    }
                }
            }
            return false;
           #endif
        }

        bool isSocketStillAlive()
        {
           #if JUCE_WINDOWS
            DWORD bytesAvail = 0;
            BOOL ok = PeekNamedPipe (pipeHandle, nullptr, 0, nullptr, &bytesAvail, nullptr);
            return ok != FALSE;
           #else
            pollfd pfd { socketFd, POLLIN, 0 };
            int r = poll (&pfd, 1, 0);
            if (r < 0)
                return false;
            if (r > 0 && (pfd.revents & (POLLHUP | POLLERR | POLLNVAL)) != 0)
                return false;
            if (r > 0 && (pfd.revents & POLLIN) != 0)
            {
                char scratch[256];
                ssize_t n = recv (socketFd, scratch, sizeof (scratch), 0);
                if (n <= 0 && errno != EAGAIN && errno != EWOULDBLOCK)
                    return false;
            }
            return true;
           #endif
        }

        void disconnect()
        {
           #if JUCE_WINDOWS
            if (pipeHandle != INVALID_HANDLE_VALUE)
            {
                CloseHandle (pipeHandle);
                pipeHandle = INVALID_HANDLE_VALUE;
            }
           #else
            if (socketFd >= 0)
            {
                ::close (socketFd);
                socketFd = -1;
            }
           #endif
            connected = false;
        }

        // ── protocol framing ───────────────────────────────────────────────
        static void appendLE32 (juce::MemoryBlock& block, uint32_t value)
        {
            uint8_t bytes[4] = { (uint8_t) (value & 0xff), (uint8_t) ((value >> 8) & 0xff),
                                  (uint8_t) ((value >> 16) & 0xff), (uint8_t) ((value >> 24) & 0xff) };
            block.append (bytes, 4);
        }

        bool writeFrame (uint32_t opcode, const juce::String& jsonPayload)
        {
            const auto payloadUtf8 = jsonPayload.toRawUTF8();
            const auto payloadLen = (uint32_t) strlen (payloadUtf8);

            juce::MemoryBlock frame;
            appendLE32 (frame, opcode);
            appendLE32 (frame, payloadLen);
            frame.append (payloadUtf8, payloadLen);

            return writeRaw (frame.getData(), frame.getSize());
        }

        bool writeRaw (const void* data, size_t numBytes)
        {
            const auto* bytes = (const char*) data;
            size_t sent = 0;
           #if JUCE_WINDOWS
            while (sent < numBytes)
            {
                DWORD written = 0;
                if (! WriteFile (pipeHandle, bytes + sent, (DWORD) (numBytes - sent), &written, nullptr) || written == 0)
                    return false;
                sent += written;
            }
            return true;
           #else
            while (sent < numBytes)
            {
                ssize_t n = send (socketFd, bytes + sent, numBytes - sent, 0);
                if (n <= 0)
                    return false;
                sent += (size_t) n;
            }
            return true;
           #endif
        }

        bool waitForReady()
        {
           #if JUCE_WINDOWS
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                DWORD bytesAvail = 0;
                if (PeekNamedPipe (pipeHandle, nullptr, 0, nullptr, &bytesAvail, nullptr) && bytesAvail >= 8)
                {
                    char header[8];
                    DWORD read = 0;
                    if (ReadFile (pipeHandle, header, 8, &read, nullptr) && read == 8)
                    {
                        uint32_t len = (uint8_t) header[4] | ((uint8_t) header[5] << 8)
                                     | ((uint8_t) header[6] << 16) | ((uint8_t) header[7] << 24);
                        std::vector<char> discard (len);
                        DWORD got = 0;
                        if (len > 0)
                            ReadFile (pipeHandle, discard.data(), len, &got, nullptr);
                        return true; // any well-formed frame back counts as accepted
                    }
                    return false;
                }
                juce::Thread::sleep (50);
            }
            return false;
           #else
            pollfd pfd { socketFd, POLLIN, 0 };
            for (int attempt = 0; attempt < 10; ++attempt)
            {
                int r = poll (&pfd, 1, 50);
                if (r > 0 && (pfd.revents & POLLIN) != 0)
                {
                    char header[8];
                    ssize_t n = recv (socketFd, header, 8, 0);
                    if (n == 8)
                    {
                        uint32_t len = (uint8_t) header[4] | ((uint8_t) header[5] << 8)
                                     | ((uint8_t) header[6] << 16) | ((uint8_t) header[7] << 24);
                        std::vector<char> discard (len);
                        size_t got = 0;
                        while (got < len)
                        {
                            ssize_t m = recv (socketFd, discard.data() + got, len - got, 0);
                            if (m <= 0) break;
                            got += (size_t) m;
                        }
                        return true; // any well-formed frame back counts as accepted
                    }
                    return false;
                }
                if (r < 0)
                    return false;
            }
            return false;
           #endif
        }

        bool sendHandshake()
        {
            auto* obj = new juce::DynamicObject();
            obj->setProperty ("v", 1);
            obj->setProperty ("client_id", applicationId);
            const juce::var payload (obj);
            return writeFrame (0, juce::JSON::toString (payload, true));
        }

        bool sendSetActivity (const juce::String& details, const juce::String& state, Clock clock,
                               juce::int64 rangeStart, juce::int64 rangeEnd)
        {
            auto* activity = new juce::DynamicObject();
            if (details.isNotEmpty()) activity->setProperty ("details", details);
            if (state.isNotEmpty())   activity->setProperty ("state", state);

            if (clock != Clock::None)
            {
                auto* timestamps = new juce::DynamicObject();
                if (clock == Clock::SinceLaunch)
                {
                    timestamps->setProperty ("start", (juce::int64) startedAtSeconds);
                }
                else // ExplicitRange, Spotify-style progress bar + remaining-time countdown
                {
                    timestamps->setProperty ("start", rangeStart);
                    timestamps->setProperty ("end", rangeEnd);
                }
                activity->setProperty ("timestamps", juce::var (timestamps));
            }

            auto* websiteButton = new juce::DynamicObject();
            websiteButton->setProperty ("label", "Website");
            websiteButton->setProperty ("url", kWebsiteUrl);
            activity->setProperty ("buttons", juce::var (juce::Array<juce::var> { juce::var (websiteButton) }));

            auto* args = new juce::DynamicObject();
            args->setProperty ("pid", (int) currentProcessId());
            args->setProperty ("activity", juce::var (activity));

            auto* root = new juce::DynamicObject();
            root->setProperty ("cmd", "SET_ACTIVITY");
            root->setProperty ("args", juce::var (args));
            root->setProperty ("nonce", juce::Uuid().toString());

            const juce::var payload (root);
            return writeFrame (1, juce::JSON::toString (payload, true));
        }

        bool sendClearActivity()
        {
            auto* args = new juce::DynamicObject();
            args->setProperty ("pid", (int) currentProcessId());
            args->setProperty ("activity", juce::var()); // null activity == clear

            auto* root = new juce::DynamicObject();
            root->setProperty ("cmd", "SET_ACTIVITY");
            root->setProperty ("args", juce::var (args));
            root->setProperty ("nonce", juce::Uuid().toString());

            const juce::var payload (root);
            return writeFrame (1, juce::JSON::toString (payload, true));
        }

        static uint32_t currentProcessId()
        {
           #if JUCE_WINDOWS
            return (uint32_t) GetCurrentProcessId();
           #else
            return (uint32_t) getpid();
           #endif
        }

        const juce::String applicationId;
        const juce::int64 startedAtSeconds { juce::Time::getCurrentTime().toMilliseconds() / 1000 };

        std::atomic<bool> connected { false };
        std::atomic<bool> enabled { true };
        std::atomic<bool> activityDirty { false };

        std::mutex activityMutex;
        juce::String pendingDetails, pendingState;
        Clock pendingClock = Clock::SinceLaunch;
        juce::int64 pendingRangeStart = 0, pendingRangeEnd = 0;

       #if JUCE_WINDOWS
        HANDLE pipeHandle = INVALID_HANDLE_VALUE;
       #else
        int socketFd = -1;
       #endif
    };
}
