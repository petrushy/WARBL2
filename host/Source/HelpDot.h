// A small round "?" next to a setting: clicking it opens a little window explaining the setting.

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class HelpDot : public juce::Component
{
public:
    // `describe` is asked when the dot is clicked, so the text can be set after the dot is made.
    HelpDot (juce::String titleToShow, std::function<juce::String()> describe)
        : title (std::move (titleToShow)), description (std::move (describe))
    {
        setRepaintsOnMouseActivity (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
    }

    void paint (juce::Graphics& g) override
    {
        const auto circle = getLocalBounds().toFloat().withSizeKeepingCentre (14.0f, 14.0f);
        const auto colour = juce::Colour (0xff4fa3e0).withAlpha (isMouseOver() ? 1.0f : 0.65f);
        g.setColour (colour);
        g.drawEllipse (circle.reduced (0.5f), 1.0f);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText ("?", circle, juce::Justification::centred);
    }

    void mouseUp (const juce::MouseEvent& e) override
    {
        if (getLocalBounds().contains (e.getPosition()))
            open();
    }

    void open()
    {
        auto* window = getTopLevelComponent();
        juce::CallOutBox::launchAsynchronously (std::make_unique<Text> (title, description()), window->getLocalArea (this, getLocalBounds()), window);
    }

private:
    class Text : public juce::Component
    {
    public:
        Text (const juce::String& heading, const juce::String& body)
        {
            juce::AttributedString s;
            s.setWordWrap (juce::AttributedString::byWord);
            s.append (heading + "\n", juce::FontOptions (15.0f, juce::Font::bold), juce::Colours::white);
            s.append (body.isNotEmpty() ? body : juce::String ("No description yet."), juce::FontOptions (14.0f), juce::Colour (0xffd8d8d8));
            layout.createLayout (s, (float) kTextWidth);
            setSize (kTextWidth + 2 * kMargin, juce::roundToInt (layout.getHeight()) + 2 * kMargin);
        }

        void paint (juce::Graphics& g) override
        {
            layout.draw (g, getLocalBounds().reduced (kMargin).toFloat());
        }

    private:
        static constexpr int kTextWidth = 340, kMargin = 8;
        juce::TextLayout layout;
    };

    juce::String title;
    std::function<juce::String()> description;
};
