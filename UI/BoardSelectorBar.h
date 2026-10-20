#pragma once
#include <juce_gui_extra/juce_gui_extra.h>
#include "LookAndFeel.h"
#include "../Soundboard/SoundboardCore.h"
#include <vector>
#include <memory>

namespace vlab::ui
{
    class BoardSelectorBar : public juce::Component
    {
    public:
        std::function<void (int)> onSelect;              // board index clicked
        std::function<void()> onAdd;                      // "+ New Board" clicked
        std::function<void (int, const juce::String&)> onRename;   // idx, new name
        std::function<void (int)> onChangeIcon;            // idx, caller should launch a FileChooser
        std::function<void (int)> onDelete;                // idx

        BoardSelectorBar()
        {
            addAndMakeVisible (viewport);
            viewport.setViewedComponent (&content, false);
            viewport.setScrollBarsShown (false, true);

            addAndMakeVisible (addButton);
            addButton.setButtonText ("+ New Board");
            addButton.getProperties().set ("primary", true);
            addButton.onClick = [this] { if (onAdd) onAdd(); };
        }

        void refresh (vlab::SoundboardCore& sb)
        {
            chips.clear(); // destroying each Chip removes it from `content` automatically

            const int count = sb.getBoardCount();
            const int active = sb.getActiveBoardIndex();
            for (int i = 0; i < count; ++i)
            {
                auto chip = std::make_unique<Chip>();
                chip->name = juce::String (sb.getBoardName (i));
                chip->selected = (i == active);
                chip->canDelete = count > 1;

                const auto iconPath = sb.getBoardIconPath (i);
                if (! iconPath.empty())
                {
                    juce::File f (iconPath);
                    if (f.existsAsFile())
                        chip->icon = juce::ImageCache::getFromFile (f);
                }

                chip->onClick      = [this, i] { if (onSelect) onSelect (i); };
                chip->onRename      = [this, i, sb = &sb] { promptRename (i, juce::String (sb->getBoardName (i))); };
                chip->onChangeIcon = [this, i] { if (onChangeIcon) onChangeIcon (i); };
                chip->onDelete     = [this, i] { if (onDelete) onDelete (i); };

                content.addAndMakeVisible (*chip);
                chips.push_back (std::move (chip));
            }
            resized();
        }

        void resized() override
        {
            auto a = getLocalBounds();
            addButton.setBounds (a.removeFromRight (130).reduced (0, 8));
            a.removeFromRight (10);
            viewport.setBounds (a);

            const int chipW = 148, chipH = getHeight() - 8, gap = 8;
            int x = 0;
            for (auto& c : chips)
            {
                c->setBounds (x, 4, chipW, chipH);
                x += chipW + gap;
            }
            content.setSize (juce::jmax (viewport.getWidth(), x), getHeight());
        }

        void paint (juce::Graphics&) override {}

    private:
        struct Chip : public juce::Component
        {
            juce::String name;
            juce::Image icon;
            bool selected = false, hovering = false, canDelete = true;

            std::function<void()> onClick, onRename, onChangeIcon, onDelete;

            void mouseEnter (const juce::MouseEvent&) override { hovering = true; repaint(); }
            void mouseExit (const juce::MouseEvent&) override { hovering = false; repaint(); }

            void mouseUp (const juce::MouseEvent& e) override
            {
                if (e.mods.isPopupMenu())
                {
                    juce::PopupMenu m;
                    m.addItem (1, "Rename Board");
                    m.addItem (2, "Change Picture");
                    m.addSeparator();
                    m.addItem (3, "Delete Board", canDelete);
                    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                        [this] (int result)
                        {
                            if (result == 1) { if (onRename) onRename(); }
                            else if (result == 2) { if (onChangeIcon) onChangeIcon(); }
                            else if (result == 3) { if (onDelete) onDelete(); }
                        });
                }
                else if (e.mouseWasClicked())
                {
                    if (onClick) onClick();
                }
            }

            void paint (juce::Graphics& g) override
            {
                const auto& p = palette();
                auto b = getLocalBounds().toFloat().reduced (2.0f);

                g.setColour (selected ? p.accentDim.withAlpha (0.35f) : (hovering ? p.panelAlt.brighter (0.08f) : p.panelAlt));
                g.fillRoundedRectangle (b, 10.0f);
                g.setColour (selected ? p.accent : p.stroke);
                g.drawRoundedRectangle (b.reduced (0.5f), 10.0f, selected ? 1.6f : 1.0f);

                auto area = b.reduced (6.0f);
                auto avatar = area.removeFromLeft (area.getHeight());
                if (icon.isValid())
                {
                    juce::Path clip;
                    clip.addRoundedRectangle (avatar, 6.0f);
                    g.saveState();
                    g.reduceClipRegion (clip);
                    g.drawImage (icon, avatar, juce::RectanglePlacement::centred | juce::RectanglePlacement::fillDestination);
                    g.restoreState();
                }
                else
                {
                    g.setColour (p.accentDim);
                    g.fillRoundedRectangle (avatar, 6.0f);
                    g.setColour (p.bg);
                    g.setFont (juce::Font (avatar.getHeight() * 0.45f, juce::Font::bold));
                    const juce::String initial = name.isNotEmpty() ? name.substring (0, 1).toUpperCase() : juce::String ("?");
                    g.drawText (initial, avatar.toNearestInt(), juce::Justification::centred);
                }

                area.removeFromLeft (8.0f);
                g.setColour (selected ? p.text : p.textDim);
                g.setFont (juce::Font (13.0f, selected ? juce::Font::bold : juce::Font::plain));
                g.drawFittedText (name, area.toNearestInt(), juce::Justification::centredLeft, 1);
            }
        };

        void promptRename (int idx, const juce::String& currentName)
        {
            auto* aw = new juce::AlertWindow ("Rename Board", "Give this soundboard a name:", juce::AlertWindow::NoIcon);
            aw->addTextEditor ("name", currentName);
            aw->addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
            aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            aw->enterModalState (true, juce::ModalCallbackFunction::create (
                [this, aw, idx] (int result)
                {
                    if (result == 1)
                    {
                        const auto newName = aw->getTextEditorContents ("name").trim();
                        if (newName.isNotEmpty() && onRename)
                            onRename (idx, newName);
                    }
                    delete aw;
                }), false);
        }

        juce::Viewport viewport;
        juce::Component content;
        juce::TextButton addButton;
        std::vector<std::unique_ptr<Chip>> chips;
    };
}
