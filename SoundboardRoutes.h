#pragma once
#include <juce_core/juce_core.h>
#include <vector>

// a route is a spot in the voice chain where the soundboard gets mixed in, everything after that spot
// affects it too. route 1 = after the FX, route 2 = start of the chain.
namespace vlab
{
    enum SoundboardInjectPoint : int
    {
        kSbStartOfChain = 0,
        kSbAfterEqComp,
        kSbAfterPitch,
        kSbAfterDrive,
        kSbAfterGlitch,
        kSbAfterModDelay,
        kSbAfterLimiterMods,
        kSbAfterExperimental,
        kSbBeforeMaster,
        kSbNumPoints
    };

    inline const char* soundboardInjectPointName (int p)
    {
        static const char* names[] = {
            "Start of chain (all voice FX apply)",
            "After Mic Gain / EQ / Compressor",
            "After Pitch / Vibrato",
            "After Distortion / Bitcrush / Tremolo",
            "After Glitch FX",
            "After Chorus ... Delay",
            "After Loud Mic limiter / DLL mods",
            "After Experimental FX (voice FX skipped)",
            "End of chain (before Master Out Gain)",
        };
        return names[juce::jlimit (0, (int) kSbNumPoints - 1, p)];
    }

    struct SoundboardRoute
    {
        juce::String name;
        int injectPoint = kSbAfterExperimental;
        bool builtIn = false;
    };

    class SoundboardRoutes
    {
    public:
        static constexpr int kRoute1 = 0, kRoute2 = 1;

        SoundboardRoutes()
        {
            routes.push_back ({ "Route 1 (after FX)",   kSbAfterExperimental, true });
            routes.push_back ({ "Route 2 (through FX)", kSbStartOfChain,      true });
        }

        int size() const { return (int) routes.size(); }
        const SoundboardRoute& get (int i) const { return routes[(size_t) juce::jlimit (0, size() - 1, i)]; }

        int getSelected() const { return selected; }
        void setSelected (int i) { selected = juce::jlimit (0, size() - 1, i); }
        const SoundboardRoute& getSelectedRoute() const { return get (selected); }

        int indexOfName (const juce::String& name) const
        {
            for (int i = 0; i < size(); ++i)
                if (routes[(size_t) i].name.equalsIgnoreCase (name))
                    return i;
            return -1;
        }

        int add (const juce::String& name, int injectPoint)
        {
            const auto clean = name.trim();
            if (clean.isEmpty() || indexOfName (clean) >= 0)
                return -1;
            routes.push_back ({ clean, juce::jlimit (0, (int) kSbNumPoints - 1, injectPoint), false });
            return size() - 1;
        }

        bool remove (int i)
        {
            if (i < 0 || i >= size() || routes[(size_t) i].builtIn)
                return false;
            routes.erase (routes.begin() + i);
            if (selected >= size()) selected = size() - 1;
            else if (selected > i)  --selected;
            else if (selected == i) selected = kRoute1;
            return true;
        }

        juce::var toVar() const
        {
            auto* o = new juce::DynamicObject();
            o->setProperty ("selected", getSelectedRoute().name);
            juce::Array<juce::var> custom;
            for (auto& r : routes)
            {
                if (r.builtIn) continue;
                auto* c = new juce::DynamicObject();
                c->setProperty ("name", r.name);
                c->setProperty ("point", r.injectPoint);
                custom.add (juce::var (c));
            }
            o->setProperty ("custom", custom);
            return juce::var (o);
        }

        void fromVar (const juce::var& v)
        {
            if (! v.isObject())
                return;
            if (auto* arr = v.getProperty ("custom", juce::var()).getArray())
                for (auto& item : *arr)
                    add (item.getProperty ("name", juce::String()).toString(),
                         (int) item.getProperty ("point", (int) kSbAfterExperimental));
            const int idx = indexOfName (v.getProperty ("selected", juce::String()).toString());
            selected = idx >= 0 ? idx : kRoute1;
        }

    private:
        std::vector<SoundboardRoute> routes;
        int selected = kRoute1;
    };
}
