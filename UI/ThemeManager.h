#pragma once
#include "LookAndFeel.h"
#include <juce_core/juce_core.h>
#include <map>
#include <functional>
#include <vector>
// DO NOT CONFUSE THIS FOR THEME EDITOR PLS DO NOT EDIT THIS THIS IS NOT THEME EDITOR
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

        struct ThemeInfo
        {
            juce::String name, author, description, version;
            juce::String uiPreset;
            bool builtIn = false;
        };

        // problems don't stop a theme loading, they end up in getLoadReport()
        void scanThemeMods (const juce::File& folder)
        {
            report.clear();
            if (! folder.isDirectory())
                return;

            struct Raw { juce::File file; juce::var data; juce::String name; };
            std::vector<Raw> raws;
            for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.theme.json"))
            {
                auto parsed = juce::JSON::parse (f);
                if (! parsed.isObject())
                {
                    report.add (f.getFileName() + ": not valid JSON, skipped");
                    continue;
                }
                raws.push_back ({ f, parsed, parsed.getProperty ("name", f.getFileNameWithoutExtension().replace (".theme", "")).toString() });
            }

            std::map<std::string, const Raw*> byName;
            for (auto& r : raws)
                byName[r.name.toStdString()] = &r;

            // resolve "extends" (built in or another mod), up to 8 levels
            std::function<bool (const Raw&, Palette&, int)> build = [&] (const Raw& r, Palette& out, int depth) -> bool
            {
                if (depth > 8)
                {
                    report.add (r.file.getFileName() + ": \"extends\" chain is too deep (or loops)");
                    return false;
                }

                Palette base;
                const auto parent = r.data.getProperty ("extends", juce::String()).toString();
                const bool hasParent = parent.isNotEmpty();
                if (hasParent)
                {
                    auto bi = builtins.find (parent.toStdString());
                    auto mi = byName.find (parent.toStdString());
                    if (bi != builtins.end())
                        base = bi->second;
                    else if (mi != byName.end() && mi->second != &r)
                        build (*mi->second, base, depth + 1);
                    else
                        report.add (r.file.getFileName() + ": \"extends\" theme \"" + parent + "\" wasn't found, using the default");
                }

                auto* obj = r.data.getDynamicObject();
                auto has = [&] (const char* key) { return obj != nullptr && obj->hasProperty (key); };
                auto get = [&] (const char* key, juce::Colour fallback)
                {
                    if (! has (key))
                        return fallback;
                    juce::Colour c;
                    if (parseColour (obj->getProperty (key), c))
                        return c;
                    report.add (r.file.getFileName() + ": \"" + juce::String (key) + "\" isn't a colour I understand, ignored");
                    return fallback;
                };

                Palette p = base;
                p.bg = get ("bg", p.bg);
                p.text = get ("text", p.text);
                p.accent = get ("accent", p.accent);
                p.panel = get ("panel", p.panel);
                p.panelAlt = get ("panelAlt", p.panelAlt);
                p.stroke = get ("stroke", p.stroke);
                p.accentDim = get ("accentDim", p.accentDim);
                p.accentGlow = get ("accentGlow", p.accentGlow);
                p.textDim = get ("textDim", p.textDim);
                p.danger = get ("danger", p.danger);

                // fill in what the theme left out from what it gave (not when it extends something)
                if (! hasParent)
                {
                    if (has ("bg") && ! has ("panel"))      p.panel    = p.bg.interpolatedWith (p.text, 0.05f);
                    if (has ("bg") && ! has ("panelAlt"))   p.panelAlt = p.bg.interpolatedWith (p.text, 0.09f);
                    if (has ("bg") && ! has ("stroke"))     p.stroke   = p.bg.interpolatedWith (p.text, 0.17f);
                    if (has ("text") && ! has ("textDim"))  p.textDim  = p.text.interpolatedWith (p.bg, 0.45f);
                    if (has ("accent") && ! has ("accentDim"))  p.accentDim  = p.accent.interpolatedWith (p.bg, 0.45f);
                    if (has ("accent") && ! has ("accentGlow")) p.accentGlow = p.accent.withAlpha (0.33f);
                }
                p.accent2 = has ("accent2") ? get ("accent2", p.accent) : (hasParent ? base.accent2 : p.accent);
                if (hasParent && ! has ("accent2") && base.accent2 == base.accent)
                    p.accent2 = p.accent;

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

                p.cursorStyle = r.data.getProperty ("cursorStyle", p.cursorStyle).toString();
                p.cursorImage = r.data.getProperty ("cursorImage", p.cursorImage).toString();
                p.cursorHotspotX = juce::jlimit (0.0f, 1.0f, (float) (double) r.data.getProperty ("cursorHotspotX", (double) p.cursorHotspotX));
                p.cursorHotspotY = juce::jlimit (0.0f, 1.0f, (float) (double) r.data.getProperty ("cursorHotspotY", (double) p.cursorHotspotY));
                p.cursorSize = juce::jlimit (8, 128, (int) r.data.getProperty ("cursorSize", p.cursorSize));

                out = p;
                return true;
            };

            static const char* knownKeys[] = {
                "schema", "name", "author", "description", "version", "extends", "uiPreset",
                "bg", "panel", "panelAlt", "stroke", "accent", "accentDim", "accentGlow", "text", "textDim", "danger", "accent2",
                "textHover", "buttonHover", "sliderTrack", "sliderThumb", "scrollbarThumb", "tooltipBg", "tooltipText",
                "selectionBg", "linkColour", "focusRing", "cursorStyle", "cursorImage", "cursorHotspotX", "cursorHotspotY", "cursorSize" };

            for (auto& r : raws)
            {
                Palette p;
                if (! build (r, p, 0))
                    continue;

                if (auto* obj = r.data.getDynamicObject())
                    for (auto& prop : obj->getProperties())
                    {
                        bool known = false;
                        for (auto* k : knownKeys)
                            if (prop.name.toString() == k) { known = true; break; }
                        if (! known)
                            report.add (r.file.getFileName() + ": unknown key \"" + prop.name.toString() + "\" (ignored)");
                    }

                ThemeInfo info;
                info.name = r.name;
                info.author = r.data.getProperty ("author", juce::String()).toString();
                info.description = r.data.getProperty ("description", juce::String()).toString();
                info.version = r.data.getProperty ("version", juce::String()).toString();
                info.uiPreset = r.data.getProperty ("uiPreset", juce::String()).toString();

                loadedThemes[r.name.toStdString()] = p;
                infos[r.name.toStdString()] = info;
                themeFiles[r.name.toStdString()] = r.file;
            }
            lastFolderStamp = folderStamp (folder);
        }

        // formats: #rgb #rrggbb #aarrggbb rgb() rgba() or a colour name. 8 digit hex has alpha first.
        static bool parseColour (const juce::var& v, juce::Colour& out)
        {
            if (! v.isString())
                return false;
            auto text = v.toString().trim();
            if (text.isEmpty())
                return false;

            if (text.startsWithIgnoreCase ("rgb"))
            {
                auto inside = text.fromFirstOccurrenceOf ("(", false, false).upToLastOccurrenceOf (")", false, false);
                juce::StringArray parts;
                parts.addTokens (inside, ",", "");
                if (parts.size() < 3)
                    return false;
                auto ch = [&parts] (int i) { return (juce::uint8) juce::jlimit (0, 255, parts[i].trim().getIntValue()); };
                const float a = parts.size() >= 4 ? juce::jlimit (0.0f, 1.0f, parts[3].trim().getFloatValue()) : 1.0f;
                out = juce::Colour (ch (0), ch (1), ch (2), a);
                return true;
            }

            auto hex = text.startsWithChar ('#') ? text.substring (1) : text;
            if (hex.containsOnly ("0123456789abcdefABCDEF"))
            {
                if (hex.length() == 3)
                    hex = juce::String::charToString (hex[0]) + hex[0] + hex[1] + hex[1] + hex[2] + hex[2];
                if (hex.length() == 6)
                    hex = "ff" + hex; // 6 digits would come out fully transparent
                if (hex.length() == 8)
                {
                    out = juce::Colour::fromString (hex);
                    return true;
                }
                return false;
            }

            const auto named = juce::Colours::findColourForName (text, juce::Colour (0x00000000));
            if (named.getAlpha() == 0)
                return false;
            out = named;
            return true;
        }

        const juce::StringArray& getLoadReport() const { return report; }

        ThemeInfo getThemeInfo (const juce::String& name) const
        {
            auto it = infos.find (name.toStdString());
            if (it != infos.end())
                return it->second;
            ThemeInfo info;
            info.name = name;
            info.builtIn = isBuiltIn (name);
            return info;
        }

        // something in the folder changed since the last scan
        bool folderChanged (const juce::File& folder) const { return folderStamp (folder) != lastFolderStamp; }

        // Reload all theme mods from disk
        void reloadThemeMods (const juce::File& folder)
        {
            loadedThemes.clear();
            infos.clear();
            themeFiles.clear();
            scanThemeMods (folder);
        }

        juce::StringArray getThemeNames() const
        {
            juce::StringArray names;
            for (auto& kv : builtins) names.add (kv.first);
            juce::StringArray modNames;
            for (auto& kv : loadedThemes) modNames.add (kv.first);
            modNames.sort (true);
            names.addArray (modNames);
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
            obj->setProperty ("schema", 2);
            obj->setProperty ("name", name);
            auto existing = infos.find (name.toStdString());
            if (existing != infos.end())
            {
                if (existing->second.author.isNotEmpty())      obj->setProperty ("author", existing->second.author);
                if (existing->second.description.isNotEmpty()) obj->setProperty ("description", existing->second.description);
                if (existing->second.version.isNotEmpty())     obj->setProperty ("version", existing->second.version);
                if (existing->second.uiPreset.isNotEmpty())    obj->setProperty ("uiPreset", existing->second.uiPreset);
            }
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
            lastFolderStamp = folderStamp (folder);
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
        juce::StringArray report;
        mutable std::map<std::string, ThemeInfo> infos;
        std::map<std::string, juce::File> themeFiles;
        mutable juce::String lastFolderStamp;

        static juce::String folderStamp (const juce::File& folder)
        {
            juce::String stamp;
            if (folder.isDirectory())
                for (auto& f : folder.findChildFiles (juce::File::findFiles, false, "*.theme.json"))
                    stamp << f.getFileName() << ":" << f.getLastModificationTime().toMilliseconds() << ":" << f.getSize() << ";";
            return stamp;
        }

        mutable std::map<std::string, Palette> loadedThemes; // mutable: saveThemeToFile registers while being logically const w.r.t. builtins
        juce::String lastAppliedThemeName = "Default Green";
    };
}
