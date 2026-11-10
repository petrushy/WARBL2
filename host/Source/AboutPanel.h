// The contents of the About window: what the app is, who made it, and whose work it builds on.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class AboutPanel : public juce::Component
{
public:
    AboutPanel()
    {
        const juce::Colour heading (juce::Colours::white), body (0xffd8d8d8), quiet (0xff9a9ca3);
        juce::AttributedString s;
        s.setWordWrap (juce::AttributedString::byWord);
        s.append ("WARBL Stream\n", juce::FontOptions (22.0f, juce::Font::bold), heading);
        s.append ("Version " + juce::JUCEApplication::getInstance()->getApplicationVersion() + "\n\n", juce::FontOptions (13.0f), quiet);
        s.append ("A whistle and recorder to play on a WARBL2. WARBL sends its raw sensor readings over USB, and this app "
                  "simulates the instrument from them: the air jet, the window, the bore and the tone holes. There is no table "
                  "of fingerings and notes, so pitch, half-holing, slides and overblowing all come out of the physics.\n\n",
                  juce::FontOptions (14.0f), body);
        s.append (juce::String::fromUTF8 ("Created by Petrus Hyvönen\n"), juce::FontOptions (15.0f, juce::Font::bold), heading);
        s.append (juce::String::fromUTF8 ("© 2026 Petrus Hyvönen\n\n"), juce::FontOptions (13.0f), quiet);
        s.append ("WARBL and WARBL2 are designed by Andrew Mowry. This project is a fork of the WARBL2 firmware, with the sensor "
                  "stream added, and the app is built with the JUCE framework.\n\n"
                  "Free software under the GNU General Public License, version 3.",
                  juce::FontOptions (14.0f), body);
        text.createLayout (s, (float) kTextWidth);

        for (auto* link : { &warbl, &source, &framework })
        {
            link->setFont (juce::FontOptions (14.0f), false, juce::Justification::centredLeft);
            link->setColour (juce::HyperlinkButton::textColourId, juce::Colour (0xff4fa3e0));
            addAndMakeVisible (*link);
        }
        setSize (kTextWidth + 2 * kMargin, juce::roundToInt (text.getHeight()) + 2 * kMargin + kLinkHeight + kLinkGap);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff1e1f22));
        text.draw (g, getLocalBounds().reduced (kMargin).toFloat());
    }

    void resized() override
    {
        auto row = getLocalBounds().reduced (kMargin).removeFromBottom (kLinkHeight);
        warbl.setBounds (row.removeFromLeft (100));
        source.setBounds (row.removeFromLeft (230));
        framework.setBounds (row);
    }

private:
    static constexpr int kTextWidth = 480, kMargin = 24, kLinkHeight = 24, kLinkGap = 14;

    juce::TextLayout text;
    juce::HyperlinkButton warbl { "warbl.xyz", juce::URL ("https://warbl.xyz") };
    juce::HyperlinkButton source { "github.com/petrushy/WARBL2", juce::URL ("https://github.com/petrushy/WARBL2") };
    juce::HyperlinkButton framework { "juce.com", juce::URL ("https://juce.com") };
};
