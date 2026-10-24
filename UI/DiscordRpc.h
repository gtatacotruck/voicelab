#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

#if JUCE_WINDOWS
 #include <windows.h>
#endif

// discord rich presence over discord's local pipe (discord-ipc-0..9), no SDK. runs on its own thread so
// a missing or slow discord can't hang the UI.
namespace vlab
{
    class DiscordRpc : private juce::Thread
    {
    public:
        struct Activity
        {
            juce::String details, state;
            juce::int64 startSeconds = 0; // unix time, 0 = no timer
            juce::String largeImageKey, largeImageText;
            juce::String smallImageKey, smallImageText;

            bool operator== (const Activity& o) const
            {
                return details == o.details && state == o.state && startSeconds == o.startSeconds
                    && largeImageKey == o.largeImageKey && largeImageText == o.largeImageText
                    && smallImageKey == o.smallImageKey && smallImageText == o.smallImageText;
            }
        };

        DiscordRpc() : juce::Thread ("vlab discord rpc") { startThread(); }
        ~DiscordRpc() override
        {
            signalThreadShouldExit();
            notify();
            stopThread (2500);
        }

        void configure (bool shouldRun, const juce::String& clientId)
        {
            const juce::ScopedLock sl (lock);
            const auto trimmed = clientId.trim();
            if (wantRun != shouldRun || id != trimmed)
            {
                wantRun = shouldRun;
                id = trimmed;
                reconnect = true;
            }
            notify();
        }

        void setActivity (const Activity& a)
        {
            const juce::ScopedLock sl (lock);
            if (a == pending)
                return;
            pending = a;
            dirty = true;
            notify();
        }

        juce::String getStatus() const
        {
            const juce::ScopedLock sl (lock);
            return status;
        }

    private:
        juce::CriticalSection lock;
        bool wantRun = false, reconnect = false, dirty = false;
        juce::String id;
        Activity pending;
        juce::String status { "Off" };

        void setStatus (const juce::String& s)
        {
            const juce::ScopedLock sl (lock);
            status = s;
        }

#if JUCE_WINDOWS
        HANDLE pipe = INVALID_HANDLE_VALUE;

        bool isConnected() const { return pipe != INVALID_HANDLE_VALUE; }

        void closePipe()
        {
            if (pipe != INVALID_HANDLE_VALUE)
            {
                CloseHandle (pipe);
                pipe = INVALID_HANDLE_VALUE;
            }
        }

        bool writeAll (const void* data, DWORD size)
        {
            DWORD written = 0;
            return WriteFile (pipe, data, size, &written, nullptr) && written == size;
        }

        bool readAll (void* data, DWORD size, int timeoutMs)
        {
            auto* out = static_cast<char*> (data);
            DWORD got = 0;
            const auto deadline = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
            while (got < size)
            {
                DWORD avail = 0;
                if (! PeekNamedPipe (pipe, nullptr, 0, nullptr, &avail, nullptr))
                    return false;
                if (avail == 0)
                {
                    if (juce::Time::getMillisecondCounter() > deadline)
                        return false;
                    juce::Thread::sleep (20);
                    continue;
                }
                DWORD n = 0;
                if (! ReadFile (pipe, out + got, juce::jmin (avail, size - got), &n, nullptr) || n == 0)
                    return false;
                got += n;
            }
            return true;
        }

        bool sendFrame (int opcode, const juce::String& json)
        {
            const auto utf8 = json.toRawUTF8();
            const int len = (int) std::strlen (utf8);
            std::vector<char> buf (8 + (size_t) len);
            std::memcpy (buf.data(), &opcode, 4);
            std::memcpy (buf.data() + 4, &len, 4);
            std::memcpy (buf.data() + 8, utf8, (size_t) len);
            return writeAll (buf.data(), (DWORD) buf.size());
        }

        bool readFrame (int& opcode, juce::String& body, int timeoutMs)
        {
            int header[2] = { 0, 0 };
            if (! readAll (header, 8, timeoutMs))
                return false;
            opcode = header[0];
            const int len = header[1];
            if (len < 0 || len > 1 << 20)
                return false;
            std::vector<char> data ((size_t) len + 1, 0);
            if (len > 0 && ! readAll (data.data(), (DWORD) len, 2000))
                return false;
            body = juce::String::fromUTF8 (data.data(), len);
            return true;
        }

        bool openPipe()
        {
            for (int i = 0; i < 10; ++i)
            {
                const auto name = "\\\\.\\pipe\\discord-ipc-" + juce::String (i);
                pipe = CreateFileA (name.toRawUTF8(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
                if (pipe != INVALID_HANDLE_VALUE)
                    return true;
            }
            return false;
        }

        bool connect (const juce::String& clientId)
        {
            if (! openPipe())
            {
                setStatus ("Discord isn't running");
                return false;
            }

            auto* hello = new juce::DynamicObject();
            hello->setProperty ("v", 1);
            hello->setProperty ("client_id", clientId);
            if (! sendFrame (0, juce::JSON::toString (juce::var (hello), true)))
            {
                closePipe();
                setStatus ("Couldn't talk to Discord");
                return false;
            }

            int op = 0;
            juce::String body;
            if (! readFrame (op, body, 3000))
            {
                closePipe();
                setStatus ("Discord didn't answer");
                return false;
            }

            const auto reply = juce::JSON::parse (body);
            if (op == 2 || reply.getProperty ("evt", {}).toString() == "ERROR") // usually a bad application id
            {
                closePipe();
                setStatus ("Discord rejected the application ID");
                return false;
            }

            setStatus ("Connected");
            return true;
        }

        bool pushActivity (const Activity& a, bool clear)
        {
            auto* args = new juce::DynamicObject();
            args->setProperty ("pid", (int) GetCurrentProcessId());

            if (! clear)
            {
                auto limit = [] (const juce::String& s) { return s.length() > 120 ? s.substring (0, 120) : s; };
                auto* act = new juce::DynamicObject();
                if (a.details.length() >= 2) act->setProperty ("details", limit (a.details));
                if (a.state.length() >= 2)   act->setProperty ("state", limit (a.state));
                if (a.startSeconds > 0)
                {
                    auto* ts = new juce::DynamicObject();
                    ts->setProperty ("start", (juce::int64) a.startSeconds);
                    act->setProperty ("timestamps", juce::var (ts));
                }
                if (a.largeImageKey.isNotEmpty() || a.smallImageKey.isNotEmpty())
                {
                    auto* assets = new juce::DynamicObject();
                    if (a.largeImageKey.isNotEmpty())  assets->setProperty ("large_image", a.largeImageKey);
                    if (a.largeImageText.length() >= 2) assets->setProperty ("large_text", a.largeImageText);
                    if (a.smallImageKey.isNotEmpty())  assets->setProperty ("small_image", a.smallImageKey);
                    if (a.smallImageText.length() >= 2) assets->setProperty ("small_text", a.smallImageText);
                    act->setProperty ("assets", juce::var (assets));
                }
                args->setProperty ("activity", juce::var (act));
            }

            auto* cmd = new juce::DynamicObject();
            cmd->setProperty ("cmd", "SET_ACTIVITY");
            cmd->setProperty ("args", juce::var (args));
            cmd->setProperty ("nonce", juce::Uuid().toString());
            return sendFrame (1, juce::JSON::toString (juce::var (cmd), true));
        }

        void drainIncoming()
        {
            // replies are ignored, just keep the pipe from filling up
            for (int guard = 0; guard < 8; ++guard)
            {
                DWORD avail = 0;
                if (! PeekNamedPipe (pipe, nullptr, 0, nullptr, &avail, nullptr))
                {
                    closePipe();
                    setStatus ("Lost connection to Discord");
                    return;
                }
                if (avail == 0)
                    return;
                int op = 0;
                juce::String body;
                if (! readFrame (op, body, 500) || op == 2)
                {
                    closePipe();
                    setStatus ("Lost connection to Discord");
                    return;
                }
            }
        }

        void run() override
        {
            juce::uint32 lastAttempt = 0, lastSend = 0;
            Activity sent;
            bool everSent = false;

            while (! threadShouldExit())
            {
                bool run, doReconnect, haveDirty;
                juce::String clientId;
                Activity toSend;
                {
                    const juce::ScopedLock sl (lock);
                    run = wantRun && id.isNotEmpty();
                    doReconnect = reconnect;
                    reconnect = false;
                    haveDirty = dirty;
                    clientId = id;
                    toSend = pending;
                    if (! wantRun) status = "Off";
                    else if (id.isEmpty()) status = "Enter a Discord application ID";
                }

                if (doReconnect && isConnected())
                {
                    pushActivity ({}, true);
                    closePipe();
                    everSent = false;
                }

                if (! run)
                {
                    if (isConnected())
                    {
                        pushActivity ({}, true);
                        closePipe();
                    }
                    wait (1000);
                    continue;
                }

                const auto now = juce::Time::getMillisecondCounter();
                if (! isConnected())
                {
                    if (lastAttempt == 0 || now - lastAttempt > 12000 || doReconnect)
                    {
                        lastAttempt = now;
                        if (connect (clientId))
                        {
                            everSent = false;
                            haveDirty = true;
                        }
                    }
                }
                else
                {
                    drainIncoming();
                    // discord rate limits updates
                    if (isConnected() && (haveDirty || ! everSent) && now - lastSend >= 5000)
                    {
                        if (pushActivity (toSend, false))
                        {
                            lastSend = now;
                            sent = toSend;
                            everSent = true;
                            const juce::ScopedLock sl (lock);
                            if (pending == toSend)
                                dirty = false;
                        }
                        else
                        {
                            closePipe();
                            setStatus ("Lost connection to Discord");
                        }
                    }
                }

                wait (1000);
            }

            if (isConnected())
            {
                pushActivity ({}, true);
                closePipe();
            }
        }
#else
        void run() override
        {
            setStatus ("Discord presence is Windows only");
            while (! threadShouldExit())
                wait (2000);
        }
#endif
    };
}
