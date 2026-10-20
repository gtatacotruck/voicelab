#pragma once
#include "LookAndFeel.h"
#include <juce_core/juce_core.h>
#include <map>

namespace vlab::ui
{
    class ThemeManager
    {
    public:
        explicit ThemeManager (LookAndFeel& lookAndFeelIn) : lnf (lookAndFeelIn)
        {
            builtins["Default Green"] = Palette{};

            Palette blue;
            blue.bg = juce::Colour (0xff050a14); blue.panel = juce::Colour (0xff0d1526);
            blue.panelAlt = juce::Colour (0xff111c33); blue.stroke = juce::Colour (0xff1c2c4a);
            blue.accent = juce::Colour (0xff3fa6ff); blue.accentDim = juce::Colour (0xff1d5a99);
            blue.accentGlow = juce::Colour (0x553fa6ff); blue.text = juce::Colour (0xffe8f0ff);
            blue.textDim = juce::Colour (0xff7d92b8); blue.danger = juce::Colour (0xffff5c5c);
            builtins["Ocean Blue"] = blue;

            Palette purple;
            purple.bg = juce::Colour (0xff0e0a16); purple.panel = juce::Colour (0xff17101f);
            purple.panelAlt = juce::Colour (0xff1e1428); purple.stroke = juce::Colour (0xff33243f);
            purple.accent = juce::Colour (0xffb478ff); purple.accentDim = juce::Colour (0xff6d3fa3);
            purple.accentGlow = juce::Colour (0x55b478ff); purple.text = juce::Colour (0xfff0e8ff);
            purple.textDim = juce::Colour (0xff9c8ab8); purple.danger = juce::Colour (0xffff5c7a);
            builtins["Amethyst"] = purple;

            Palette crimson;
            crimson.bg = juce::Colour (0xff140a0a); crimson.panel = juce::Colour (0xff1f1010);
            crimson.panelAlt = juce::Colour (0xff281414); crimson.stroke = juce::Colour (0xff3f2020);
            crimson.accent = juce::Colour (0xffff5c5c); crimson.accentDim = juce::Colour (0xffa33f3f);
            crimson.accentGlow = juce::Colour (0x55ff5c5c); crimson.text = juce::Colour (0xffffe8e8);
            crimson.textDim = juce::Colour (0xffb88a8a); crimson.danger = juce::Colour (0xffff8f3f);
            builtins["Crimson"] = crimson;

            Palette mono;
            mono.bg = juce::Colour (0xff0a0a0a); mono.panel = juce::Colour (0xff141414);
            mono.panelAlt = juce::Colour (0xff1c1c1c); mono.stroke = juce::Colour (0xff333333);
            mono.accent = juce::Colour (0xffe0e0e0); mono.accentDim = juce::Colour (0xff8a8a8a);
            mono.accentGlow = juce::Colour (0x55e0e0e0); mono.text = juce::Colour (0xfff5f5f5);
            mono.textDim = juce::Colour (0xff8f8f8f); mono.danger = juce::Colour (0xffff6b6b);
            builtins["Monochrome"] = mono;

            Palette sunset;
            sunset.bg = juce::Colour (0xff160c08); sunset.panel = juce::Colour (0xff231208);
            sunset.panelAlt = juce::Colour (0xff2f180a); sunset.stroke = juce::Colour (0xff4a2810);
            sunset.accent = juce::Colour (0xffff8f3f); sunset.accentDim = juce::Colour (0xffa35a20);
            sunset.accentGlow = juce::Colour (0x55ff8f3f); sunset.text = juce::Colour (0xfffff0e0);
            sunset.textDim = juce::Colour (0xffcaa080); sunset.danger = juce::Colour (0xffff4f4f);
            builtins["Sunset Orange"] = sunset;

            Palette cyberpunk;
            cyberpunk.bg = juce::Colour (0xff0a0510); cyberpunk.panel = juce::Colour (0xff150a20);
            cyberpunk.panelAlt = juce::Colour (0xff1e0f2c); cyberpunk.stroke = juce::Colour (0xff3a1f52);
            cyberpunk.accent = juce::Colour (0xffff2ea6); cyberpunk.accentDim = juce::Colour (0xff991a66);
            cyberpunk.accentGlow = juce::Colour (0x55ff2ea6); cyberpunk.text = juce::Colour (0xfff5e0ff);
            cyberpunk.textDim = juce::Colour (0xffb08ac4); cyberpunk.danger = juce::Colour (0xff3ffff0);
            builtins["Cyberpunk Magenta"] = cyberpunk;

            Palette forest;
            forest.bg = juce::Colour (0xff0a120a); forest.panel = juce::Colour (0xff121f12);
            forest.panelAlt = juce::Colour (0xff182818); forest.stroke = juce::Colour (0xff2a4028);
            forest.accent = juce::Colour (0xff6fcf5a); forest.accentDim = juce::Colour (0xff3f8a32);
            forest.accentGlow = juce::Colour (0x556fcf5a); forest.text = juce::Colour (0xffe8f5e0);
            forest.textDim = juce::Colour (0xff9cb894); forest.danger = juce::Colour (0xffe0a83f);
            builtins["Forest"] = forest;

            Palette gold;
            gold.bg = juce::Colour (0xff120f08); gold.panel = juce::Colour (0xff1d1810);
            gold.panelAlt = juce::Colour (0xff272016); gold.stroke = juce::Colour (0xff443826);
            gold.accent = juce::Colour (0xffe8c04a); gold.accentDim = juce::Colour (0xff9c7f28);
            gold.accentGlow = juce::Colour (0x55e8c04a); gold.text = juce::Colour (0xfffff8e0);
            gold.textDim = juce::Colour (0xffbfae82); gold.danger = juce::Colour (0xffe0603f);
            builtins["Gold"] = gold;

            Palette teal;
            teal.bg = juce::Colour (0xff071414); teal.panel = juce::Colour (0xff0d2020);
            teal.panelAlt = juce::Colour (0xff122c2c); teal.stroke = juce::Colour (0xff1f4747);
            teal.accent = juce::Colour (0xff3fe0d0); teal.accentDim = juce::Colour (0xff238f85);
            teal.accentGlow = juce::Colour (0x553fe0d0); teal.text = juce::Colour (0xffe0fffa);
            teal.textDim = juce::Colour (0xff86bcb5); teal.danger = juce::Colour (0xffff6a6a);
            builtins["Teal"] = teal;

            Palette matrix;
            matrix.bg = juce::Colour (0xff000500); matrix.panel = juce::Colour (0xff030f03);
            matrix.panelAlt = juce::Colour (0xff061a06); matrix.stroke = juce::Colour (0xff0f3a0f);
            matrix.accent = juce::Colour (0xff33ff33); matrix.accentDim = juce::Colour (0xff1a991a);
            matrix.accentGlow = juce::Colour (0x5533ff33); matrix.text = juce::Colour (0xffd0ffd0);
            matrix.textDim = juce::Colour (0xff5fa85f); matrix.danger = juce::Colour (0xffff4444);
            builtins["Matrix"] = matrix;

            Palette lightMode;
            lightMode.bg = juce::Colour (0xfff2f2f2); lightMode.panel = juce::Colour (0xffffffff);
            lightMode.panelAlt = juce::Colour (0xffe8e8e8); lightMode.stroke = juce::Colour (0xffcfcfcf);
            lightMode.accent = juce::Colour (0xff1d8a45); lightMode.accentDim = juce::Colour (0xff8fd0a8);
            lightMode.accentGlow = juce::Colour (0x551d8a45); lightMode.text = juce::Colour (0xff141414);
            lightMode.textDim = juce::Colour (0xff5f5f5f); lightMode.danger = juce::Colour (0xffc0392b);
            builtins["Daylight (Light)"] = lightMode;

            Palette icePink;
            icePink.bg = juce::Colour (0xff100a12); icePink.panel = juce::Colour (0xff1c1220);
            icePink.panelAlt = juce::Colour (0xff261a2c); icePink.stroke = juce::Colour (0xff3f2c48);
            icePink.accent = juce::Colour (0xffff9fd0); icePink.accentDim = juce::Colour (0xffb0568c);
            icePink.accentGlow = juce::Colour (0x55ff9fd0); icePink.text = juce::Colour (0xfffff0f8);
            icePink.textDim = juce::Colour (0xffc79cb8); icePink.danger = juce::Colour (0xffff5c5c);
            builtins["Bubblegum"] = icePink;

            Palette dominos;
            dominos.bg = juce::Colour (0xff050e1c); dominos.panel = juce::Colour (0xff0a1c33);
            dominos.panelAlt = juce::Colour (0xff0f2540); dominos.stroke = juce::Colour (0xff1c3a5c);
            dominos.accent = juce::Colour (0xffe31837); dominos.accentDim = juce::Colour (0xff8a0f21);
            dominos.accentGlow = juce::Colour (0x66e31837); dominos.text = juce::Colour (0xfff5f8ff);
            dominos.textDim = juce::Colour (0xff8fa8c4); dominos.danger = juce::Colour (0xffffcc00);
            builtins["Domino's"] = dominos;
        }

        void scanThemeMods (const juce::File& folder)
        {
            if (! folder.isDirectory())
                return;
            for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.theme.json"))
            {
                auto parsed = juce::JSON::parse (f);
                if (! parsed.isObject())
                    continue;
                Palette p; // start from default, override what's present
                auto get = [&parsed] (const char* key, juce::Colour fallback)
                {
                    auto v = parsed.getProperty (key, juce::var());
                    if (! v.isString())
                        return fallback;

                    auto hex = v.toString().trim();
                    if (hex.startsWithChar ('#')) hex = hex.substring (1);
                    if (hex.length() == 6) hex = "ff" + hex; // assume opaque
                    if (hex.length() != 8) return fallback;

                    return juce::Colour::fromString (hex);
                };
                p.bg = get ("bg", p.bg); p.panel = get ("panel", p.panel);
                p.panelAlt = get ("panelAlt", p.panelAlt); p.stroke = get ("stroke", p.stroke);
                p.accent = get ("accent", p.accent); p.accentDim = get ("accentDim", p.accentDim);
                p.accentGlow = get ("accentGlow", p.accentGlow); p.text = get ("text", p.text);
                p.textDim = get ("textDim", p.textDim); p.danger = get ("danger", p.danger);
                p.accent2 = get ("accent2", p.accent);

                p.textHover = get ("textHover", p.textHover);
                p.buttonHover = get ("buttonHover", p.buttonHover);
                p.sliderTrack = get ("sliderTrack", p.sliderTrack);
                p.sliderThumb = get ("sliderThumb", p.sliderThumb);
                p.scrollbarThumb = get ("scrollbarThumb", p.scrollbarThumb);
                p.tooltipBg = get ("tooltipBg", p.tooltipBg);
                p.tooltipText = get ("tooltipText", p.tooltipText);
                p.selectionBg = get ("selectionBg", p.selectionBg);
                p.linkColour = get ("linkColour", p.linkColour);
                p.focusRing = get ("focusRing", p.focusRing);

                p.cursorStyle = parsed.getProperty ("cursorStyle", p.cursorStyle).toString();
                p.cursorImage = parsed.getProperty ("cursorImage", p.cursorImage).toString();
                p.cursorHotspotX = (float) (double) parsed.getProperty ("cursorHotspotX", (double) p.cursorHotspotX);
                p.cursorHotspotY = (float) (double) parsed.getProperty ("cursorHotspotY", (double) p.cursorHotspotY);
                p.cursorSize = (int) parsed.getProperty ("cursorSize", p.cursorSize);

                const juce::String name = parsed.getProperty ("name", f.getFileNameWithoutExtension()).toString();
                loadedThemes[name.toStdString()] = p;

                juce::MessageManager::getInstance()->runDispatchLoopUntil (2);
            }
        }

        // Reload all theme mods from disk
        void reloadThemeMods (const juce::File& folder)
        {
            loadedThemes.clear();
            scanThemeMods (folder);
        }

        juce::StringArray getThemeNames() const
        {
            juce::StringArray names;
            for (auto& kv : builtins) names.add (kv.first);
            for (auto& kv : loadedThemes) names.add (kv.first);
            return names;
        }

        bool applyTheme (const juce::String& name)
        {
            auto it = builtins.find (name.toStdString());
            if (it != builtins.end())
            {
                setPalette (it->second);
                lnf.applyPaletteColours();
                lastAppliedThemeName = name;
                return true;
            }
            auto it2 = loadedThemes.find (name.toStdString());
            if (it2 != loadedThemes.end())
            {
                setPalette (it2->second);
                lnf.applyPaletteColours();
                lastAppliedThemeName = name;
                return true;
            }
            return false;
        }

        void previewPalette (const Palette& p)
        {
            setPalette (p);
            lnf.applyPaletteColours();
        }

        void registerCustomTheme (const juce::String& name, const Palette& p)
        {
            loadedThemes[name.toStdString()] = p;
        }

        bool isBuiltIn (const juce::String& name) const { return builtins.count (name.toStdString()) != 0; }

        juce::File saveThemeToFile (const juce::String& name, const Palette& p, const juce::File& folder) const
        {
            if (name.trim().isEmpty())
                return {};
            folder.createDirectory();
            const juce::String safeName = juce::File::createLegalFileName (name);
            auto file = folder.getChildFile (safeName + ".theme.json");

            auto obj = std::make_unique<juce::DynamicObject>();
            obj->setProperty ("name", name);
            auto hex = [] (juce::Colour c) { return "#" + c.toDisplayString (true).toLowerCase(); };
            obj->setProperty ("bg", hex (p.bg));
            obj->setProperty ("panel", hex (p.panel));
            obj->setProperty ("panelAlt", hex (p.panelAlt));
            obj->setProperty ("stroke", hex (p.stroke));
            obj->setProperty ("accent", hex (p.accent));
            obj->setProperty ("accentDim", hex (p.accentDim));
            obj->setProperty ("accentGlow", hex (p.accentGlow));
            obj->setProperty ("text", hex (p.text));
            obj->setProperty ("textDim", hex (p.textDim));
            obj->setProperty ("danger", hex (p.danger));
            obj->setProperty ("accent2", hex (p.accent2));

            auto writeIfSet = [&] (const char* key, juce::Colour c)
            {
                if (c.getAlpha() > 0)
                    obj->setProperty (key, hex (c));
            };
            writeIfSet ("textHover", p.textHover);
            writeIfSet ("buttonHover", p.buttonHover);
            writeIfSet ("sliderTrack", p.sliderTrack);
            writeIfSet ("sliderThumb", p.sliderThumb);
            writeIfSet ("scrollbarThumb", p.scrollbarThumb);
            writeIfSet ("tooltipBg", p.tooltipBg);
            writeIfSet ("tooltipText", p.tooltipText);
            writeIfSet ("selectionBg", p.selectionBg);
            writeIfSet ("linkColour", p.linkColour);
            writeIfSet ("focusRing", p.focusRing);

            if (p.cursorStyle != "default")
            {
                obj->setProperty ("cursorStyle", p.cursorStyle);
                if (p.cursorStyle == "custom")
                {
                    juce::String storedImagePath = p.cursorImage;
                    juce::File srcImage (p.cursorImage);
                    if (srcImage.existsAsFile())
                    {
                        auto destImage = folder.getChildFile (safeName + "_cursor" + srcImage.getFileExtension());
                        if (srcImage != destImage)
                            srcImage.copyFileTo (destImage);
                        storedImagePath = destImage.getFileName();
                    }
                    obj->setProperty ("cursorImage", storedImagePath);
                    obj->setProperty ("cursorHotspotX", (double) p.cursorHotspotX);
                    obj->setProperty ("cursorHotspotY", (double) p.cursorHotspotY);
                    obj->setProperty ("cursorSize", p.cursorSize);
                }
            }

            juce::var root (obj.release());
            file.replaceWithText (juce::JSON::toString (root));
            loadedThemes[name.toStdString()] = p; // also register in-memory immediately
            return file;
        }

        // Get palette by name, returns false if not found
        bool getPaletteByName (const juce::String& name, Palette& outPalette) const
        {
            auto it = builtins.find (name.toStdString());
            if (it != builtins.end()) { outPalette = it->second; return true; }
            auto it2 = loadedThemes.find (name.toStdString());
            if (it2 != loadedThemes.end()) { outPalette = it2->second; return true; }
            return false;
        }

        juce::String getLastAppliedThemeName() const { return lastAppliedThemeName; }

        // ---- persistence: remember the chosen theme across restarts ----
        void savePreference (const juce::File& prefFile) const
        {
            auto obj = std::make_unique<juce::DynamicObject>();
            obj->setProperty ("theme", lastAppliedThemeName);
            prefFile.replaceWithText (juce::JSON::toString (juce::var (obj.release())));
        }

        juce::String loadPreference (const juce::File& prefFile) const
        {
            if (! prefFile.existsAsFile())
                return {};
            auto parsed = juce::JSON::parse (prefFile);
            return parsed.getProperty ("theme", juce::String()).toString();
        }

    private:
        LookAndFeel& lnf;
        std::map<std::string, Palette> builtins;
        mutable std::map<std::string, Palette> loadedThemes; // mutable: saveThemeToFile registers while being logically const w.r.t. builtins
        juce::String lastAppliedThemeName = "Default Green";
    };
}
