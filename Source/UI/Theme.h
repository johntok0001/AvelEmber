#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "BinaryData.h"

// Avel EQ design system: dark "pro app" look (graphite glass, system colours, Inter type).
namespace avel::ui
{
    //==============================================================================
    namespace col
    {
        const juce::Colour bg0       { 0xff0b0b0d };
        const juce::Colour bg1       { 0xff151518 };
        const juce::Colour bg2       { 0xff1c1c1f };
        const juce::Colour glass     { 0xe6202023 };
        const juce::Colour glassHi   { 0xff2c2c2f };
        const juce::Colour stroke    { 0x17ffffff };
        const juce::Colour strokeHi  { 0x26ffffff };
        const juce::Colour fill      { 0x33767680 };   // iOS tertiary fill
        const juce::Colour fillHi    { 0x59767680 };
        const juce::Colour thumb     { 0xff636366 };
        const juce::Colour label     { 0xfff5f5f7 };
        const juce::Colour label2    { 0x99ebebf5 };
        const juce::Colour label3    { 0x4debebf5 };
        const juce::Colour label4    { 0x29ebebf5 };
        const juce::Colour accent    { 0xff0a84ff };
        const juce::Colour green     { 0xff30d158 };
        const juce::Colour red       { 0xffff453a };
        const juce::Colour yellow    { 0xffffd60a };

        inline juce::Colour band (int index)
        {
            static const juce::Colour c[] {
                juce::Colour (0xff0a84ff), juce::Colour (0xffff9f0a), juce::Colour (0xff30d158), juce::Colour (0xffbf5af2),
                juce::Colour (0xffff375f), juce::Colour (0xff64d2ff), juce::Colour (0xffffd60a), juce::Colour (0xff5e5ce6),
                juce::Colour (0xff66d4cf), juce::Colour (0xffff6961), juce::Colour (0xffac8e68), juce::Colour (0xffda8fff)
            };
            return c[(size_t) ((index % 12) + 12) % 12];
        }
    }

    //==============================================================================
    enum class Weight { regular, medium, semibold };

    inline juce::Typeface::Ptr typeface (Weight w)
    {
        static juce::Typeface::Ptr r = juce::Typeface::createSystemTypefaceFor (BinaryData::InterRegular_otf, BinaryData::InterRegular_otfSize);
        static juce::Typeface::Ptr m = juce::Typeface::createSystemTypefaceFor (BinaryData::InterMedium_otf, BinaryData::InterMedium_otfSize);
        static juce::Typeface::Ptr s = juce::Typeface::createSystemTypefaceFor (BinaryData::InterSemiBold_otf, BinaryData::InterSemiBold_otfSize);
        return w == Weight::regular ? r : w == Weight::medium ? m : s;
    }

    inline juce::Font font (float size, Weight w = Weight::medium, float kerning = 0.0f)
    {
        return juce::Font (juce::FontOptions (typeface (w)).withHeight (size).withKerningFactor (kerning));
    }

    //==============================================================================
    // Soft shadow + translucent fill + hairline border + top sheen
    inline void drawGlass (juce::Graphics& g, juce::Rectangle<float> r, float radius, float shadow = 1.0f)
    {
        if (shadow > 0.0f)
        {
            juce::Path p;
            p.addRoundedRectangle (r, radius);
            juce::DropShadow (juce::Colours::black.withAlpha (0.55f * shadow), (int) (30 * shadow), { 0, (int) (12 * shadow) }).drawForPath (g, p);
            juce::DropShadow (juce::Colours::black.withAlpha (0.35f * shadow), 4, { 0, 1 }).drawForPath (g, p);
        }

        g.setGradientFill (juce::ColourGradient (juce::Colour (0xf0262629), r.getX(), r.getY(),
                                                 juce::Colour (0xf01b1b1e), r.getX(), r.getBottom(), false));
        g.fillRoundedRectangle (r, radius);

        // sheen
        g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.06f), r.getX(), r.getY(),
                                                 juce::Colours::transparentWhite, r.getX(), r.getY() + juce::jmin (40.0f, r.getHeight() * 0.5f), false));
        g.fillRoundedRectangle (r.reduced (1.0f), radius - 1.0f);

        g.setColour (col::stroke);
        g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (0.07f));
        g.drawLine (r.getX() + radius, r.getY() + 0.75f, r.getRight() - radius, r.getY() + 0.75f, 1.0f);
    }

    // Exponential approach used for all small animations
    inline bool approach (float& v, float target, float speed = 0.25f)
    {
        if (std::abs (v - target) < 0.002f) { const bool changed = v != target; v = target; return changed; }
        v += (target - v) * speed;
        return true;
    }

    //==============================================================================
    // Icons (24x24 design grid, drawn as strokes)
    namespace icon
    {
        using P = juce::Path;

        inline P undo()
        {
            P p;
            p.startNewSubPath (9, 6); p.lineTo (5, 10); p.lineTo (9, 14);
            p.startNewSubPath (5, 10); p.lineTo (14, 10);
            p.cubicTo (17.5f, 10, 20, 12.5f, 20, 15); p.cubicTo (20, 17.5f, 17.5f, 20, 14, 20); p.lineTo (11, 20);
            return p;
        }

        inline P redo()
        {
            auto p = undo();
            p.applyTransform (juce::AffineTransform::scale (-1, 1).translated (25, 0));
            return p;
        }

        inline P trash()
        {
            P p;
            p.startNewSubPath (4.5f, 7); p.lineTo (19.5f, 7);
            p.startNewSubPath (9.5f, 7); p.lineTo (10, 4.5f); p.lineTo (14, 4.5f); p.lineTo (14.5f, 7);
            p.startNewSubPath (6.5f, 7); p.lineTo (7.5f, 19.5f); p.lineTo (16.5f, 19.5f); p.lineTo (17.5f, 7);
            p.startNewSubPath (10.25f, 10.5f); p.lineTo (10.5f, 16.5f);
            p.startNewSubPath (13.75f, 10.5f); p.lineTo (13.5f, 16.5f);
            return p;
        }

        inline P headphones()
        {
            P p;
            p.startNewSubPath (4.5f, 15);
            p.cubicTo (4.5f, 8, 8, 4.5f, 12, 4.5f); p.cubicTo (16, 4.5f, 19.5f, 8, 19.5f, 15);
            p.addRoundedRectangle (4.0f, 13.5f, 4.0f, 6.5f, 1.6f);
            p.addRoundedRectangle (16.0f, 13.5f, 4.0f, 6.5f, 1.6f);
            return p;
        }

        inline P power()
        {
            P p;
            p.addCentredArc (12, 13, 7, 7, 0, juce::degreesToRadians (35.0f), juce::degreesToRadians (325.0f), true);
            p.startNewSubPath (12, 4); p.lineTo (12, 11.5f);
            return p;
        }

        inline P chevronDown()
        {
            P p;
            p.startNewSubPath (7, 10); p.lineTo (12, 15); p.lineTo (17, 10);
            return p;
        }

        inline P chevronLeft()  { P p; p.startNewSubPath (14, 6); p.lineTo (8, 12); p.lineTo (14, 18); return p; }
        inline P chevronRight() { P p; p.startNewSubPath (10, 6); p.lineTo (16, 12); p.lineTo (10, 18); return p; }

        inline P copy()
        {
            P p;
            p.addRoundedRectangle (8.5f, 8.5f, 11.0f, 11.0f, 2.5f);
            p.startNewSubPath (5, 15); p.lineTo (5, 7); p.quadraticTo (5, 5, 7, 5); p.lineTo (15, 5);
            return p;
        }

        inline P close()
        {
            P p;
            p.startNewSubPath (7, 7); p.lineTo (17, 17);
            p.startNewSubPath (17, 7); p.lineTo (7, 17);
            return p;
        }

        inline P record() { P p; p.addEllipse (7.5f, 7.5f, 9.0f, 9.0f); return p; }

        inline P pencil()
        {
            P p;
            p.startNewSubPath (5, 19); p.lineTo (6, 15); p.lineTo (15.5f, 5.5f); p.lineTo (18.5f, 8.5f); p.lineTo (9, 18); p.closeSubPath();
            p.startNewSubPath (13.5f, 7.5f); p.lineTo (16.5f, 10.5f);
            return p;
        }

        inline P match()
        {
            P p;
            p.startNewSubPath (3, 15); p.cubicTo (7, 15, 8, 7, 12, 7); p.cubicTo (16, 7, 17, 12, 21, 12);
            p.startNewSubPath (3, 11); p.cubicTo (7, 11, 9, 17, 13, 17); p.cubicTo (17, 17, 18, 10, 21, 10);
            return p;
        }

        inline P file()
        {
            P p;
            p.startNewSubPath (7, 4); p.lineTo (14, 4); p.lineTo (18, 8); p.lineTo (18, 20); p.lineTo (7, 20); p.closeSubPath();
            p.startNewSubPath (14, 4); p.lineTo (14, 8); p.lineTo (18, 8);
            return p;
        }

        // filter shape glyphs on a 24x16 grid
        inline P filterShape (int type)
        {
            P p;
            switch (type)
            {
                case 0: p.startNewSubPath (2, 12); p.cubicTo (8, 12, 9, 3, 12, 3); p.cubicTo (15, 3, 16, 12, 22, 12); break;   // bell
                case 1: p.startNewSubPath (2, 4); p.lineTo (7, 4); p.cubicTo (11, 4, 12, 12, 16, 12); p.lineTo (22, 12); break; // low shelf
                case 2: p.startNewSubPath (2, 12); p.lineTo (8, 12); p.cubicTo (12, 12, 13, 4, 17, 4); p.lineTo (22, 4); break; // high shelf
                case 3: p.startNewSubPath (4, 15); p.cubicTo (7, 7, 9, 5, 13, 5); p.lineTo (22, 5); break;                      // low cut
                case 4: p.startNewSubPath (2, 5); p.lineTo (11, 5); p.cubicTo (15, 5, 17, 7, 20, 15); break;                     // high cut
                case 5: p.startNewSubPath (2, 5); p.lineTo (9, 5); p.cubicTo (11, 5, 11.5f, 14, 12, 14); p.cubicTo (12.5f, 14, 13, 5, 15, 5); p.lineTo (22, 5); break; // notch
                case 6: p.startNewSubPath (3, 15); p.cubicTo (8, 13, 10, 4, 12, 4); p.cubicTo (14, 4, 16, 13, 21, 15); break;   // band pass
                case 7: p.startNewSubPath (2, 12); p.lineTo (7, 12); p.cubicTo (10, 12, 14, 4, 17, 4); p.lineTo (22, 4);
                        p.startNewSubPath (12, 2.5f); p.lineTo (12, 13.5f); break;                                               // tilt
                default: break;
            }
            return p;
        }

        inline void draw (juce::Graphics& g, const P& p, juce::Rectangle<float> area, juce::Colour c, float stroke = 1.6f, float designSize = 24.0f)
        {
            const float s = juce::jmin (area.getWidth(), area.getHeight()) / designSize;
            auto t = juce::AffineTransform::scale (s).translated (area.getCentreX() - designSize * 0.5f * s, area.getCentreY() - designSize * 0.5f * s);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded), t);
        }
    }

    //==============================================================================
    class LookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeel()
        {
            setDefaultSansSerifTypeface (typeface (Weight::medium));
            setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xf5242427));
            setColour (juce::PopupMenu::textColourId, col::label);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, col::accent);
            setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
            setColour (juce::PopupMenu::headerTextColourId, col::label3);
            setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff2c2c2e));
            setColour (juce::TextEditor::textColourId, col::label);
            setColour (juce::TextEditor::highlightColourId, col::accent.withAlpha (0.5f));
            setColour (juce::TextEditor::outlineColourId, juce::Colours::transparentBlack);
            setColour (juce::TextEditor::focusedOutlineColourId, col::accent);
            setColour (juce::CaretComponent::caretColourId, col::accent);
            setColour (juce::Label::textColourId, col::label);
            setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xf02a2a2d));
            setColour (juce::TooltipWindow::textColourId, col::label);
            setColour (juce::TooltipWindow::outlineColourId, col::stroke);
            setColour (juce::AlertWindow::backgroundColourId, juce::Colour (0xff1f1f22));
            setColour (juce::AlertWindow::textColourId, col::label);
            setColour (juce::TextButton::buttonColourId, juce::Colour (0xff3a3a3c));
            setColour (juce::TextButton::textColourOffId, col::label);
        }

        juce::Font getPopupMenuFont() override { return font (13.5f, Weight::regular); }
        juce::Font getLabelFont (juce::Label& l) override { return l.getFont(); }
        juce::Font getAlertWindowMessageFont() override { return font (13.0f, Weight::regular); }
        juce::Font getAlertWindowTitleFont() override { return font (15.0f, Weight::semibold); }
        juce::Font getTextButtonFont (juce::TextButton&, int) override { return font (13.0f, Weight::medium); }

        void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
        {
            g.fillAll (juce::Colour (0xff232326));
            g.setColour (col::strokeHi);
            g.drawRect (0, 0, w, h, 1);
        }

        void drawPopupMenuItem (juce::Graphics& g, const juce::Rectangle<int>& area, bool isSeparator, bool isActive, bool isHighlighted,
                                bool isTicked, bool hasSubMenu, const juce::String& text, const juce::String& shortcutKeyText,
                                const juce::Drawable* iconDrawable, const juce::Colour* textColour) override
        {
            juce::ignoreUnused (iconDrawable, textColour);
            if (isSeparator)
            {
                g.setColour (col::stroke);
                g.fillRect (area.reduced (10, 0).withHeight (1).withY (area.getCentreY()));
                return;
            }

            auto r = area.reduced (5, 1).toFloat();
            if (isHighlighted && isActive)
            {
                g.setColour (col::accent);
                g.fillRoundedRectangle (r, 5.0f);
            }

            auto t = r.reduced (8, 0);
            if (isTicked)
            {
                juce::Path c;
                c.startNewSubPath (0, 5); c.lineTo (3.5f, 8.5f); c.lineTo (10, 1);
                g.setColour (isHighlighted ? juce::Colours::white : col::label);
                g.strokePath (c, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded),
                              juce::AffineTransform::translation (t.getX(), t.getCentreY() - 5));
            }
            t.removeFromLeft (18);

            g.setColour (! isActive ? col::label3 : isHighlighted ? juce::Colours::white : col::label);
            g.setFont (getPopupMenuFont());
            g.drawFittedText (text, t.toNearestInt(), juce::Justification::centredLeft, 1);

            if (shortcutKeyText.isNotEmpty())
            {
                g.setColour (isHighlighted ? juce::Colours::white.withAlpha (0.8f) : col::label3);
                g.drawText (shortcutKeyText, t, juce::Justification::centredRight);
            }

            if (hasSubMenu)
                icon::draw (g, icon::chevronRight(), t.removeFromRight (14).withSizeKeepingCentre (12, 12),
                            isHighlighted ? juce::Colours::white : col::label2, 1.6f);
        }

        void getIdealPopupMenuItemSize (const juce::String& text, bool isSeparator, int standardHeight, int& w, int& h) override
        {
            juce::ignoreUnused (standardHeight);
            if (isSeparator) { w = 50; h = 9; return; }
            h = 26;
            w = juce::GlyphArrangement::getStringWidthInt (getPopupMenuFont(), text) + 60;
        }

        int getPopupMenuBorderSize() override { return 5; }

        void drawTooltip (juce::Graphics& g, const juce::String& text, int w, int h) override
        {
            auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h);
            g.setColour (juce::Colour (0xff2a2a2d));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (col::strokeHi);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            g.setColour (col::label);
            g.setFont (font (12.0f, Weight::regular));
            g.drawFittedText (text, r.reduced (10, 4).toNearestInt(), juce::Justification::centredLeft, 6);
        }

        juce::Rectangle<int> getTooltipBounds (const juce::String& text, juce::Point<int> pos, juce::Rectangle<int> parent) override
        {
            const auto f = font (12.0f, Weight::regular);
            juce::AttributedString s;
            s.append (text, f);
            juce::TextLayout tl;
            tl.createLayout (s, 280.0f);
            const int w = (int) tl.getWidth() + 22, h = (int) tl.getHeight() + 10;
            return juce::Rectangle<int> (pos.x - w / 2, pos.y + 18, w, h).constrainedWithin (parent);
        }
    };

    //==============================================================================
    // iOS-style segmented control. Items are text or filter glyphs.
    class Segmented : public juce::Component, public juce::TooltipClient, private juce::Timer
    {
    public:
        std::function<void (int)> onChange;

        void setItems (juce::StringArray items, bool glyphs = false)
        {
            labels = std::move (items);
            useGlyphs = glyphs;
            repaint();
        }

        void setTooltips (juce::StringArray t) { tips = std::move (t); }

        void setSelected (int index, bool animate = true)
        {
            if (index == selected) return;
            selected = index;
            if (! animate || thumbX < 0) thumbX = (float) index;
            startTimerHz (60);
            repaint();
        }

        int getSelected() const noexcept { return selected; }
        void setAccent (juce::Colour c) { accent = c; repaint(); }
        void setAccentSelected (bool b) { accentThumb = b; repaint(); }

        // Bind to a choice/bool parameter; can be re-bound at any time
        void bind (juce::RangedAudioParameter* p, juce::UndoManager* um = nullptr)
        {
            attachment.reset();
            undo = um;
            if (p == nullptr) return;
            attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { setSelected (juce::roundToInt (v)); }, um);
            attachment->sendInitialUpdate();
            onChange = [this] (int i) { if (undo) undo->beginNewTransaction(); if (attachment) attachment->setValueAsCompleteGesture ((float) i); };
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const float rad = juce::jmin (8.0f, r.getHeight() * 0.5f);
            g.setColour (col::fill);
            g.fillRoundedRectangle (r, rad);

            const int n = juce::jmax (1, labels.size());
            const float w = r.getWidth() / n;

            if (selected >= 0)
            {
                auto t = juce::Rectangle<float> (r.getX() + thumbX * w, r.getY(), w, r.getHeight()).reduced (2.0f);
                juce::Path tp; tp.addRoundedRectangle (t, rad - 2.0f);
                juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 6, { 0, 2 }).drawForPath (g, tp);
                g.setColour (accentThumb ? accent : col::thumb);
                g.fillPath (tp);
                g.setColour (juce::Colours::white.withAlpha (0.08f));
                g.strokePath (tp, juce::PathStrokeType (0.8f));
            }

            for (int i = 0; i < n; ++i)
            {
                auto cell = juce::Rectangle<float> (r.getX() + i * w, r.getY(), w, r.getHeight());
                if (i > 0 && i != selected && i - 1 != selected)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.08f));
                    g.fillRect (juce::Rectangle<float> (cell.getX() - 0.5f, cell.getY() + r.getHeight() * 0.28f, 1.0f, r.getHeight() * 0.44f));
                }

                const bool on = i == selected;
                const auto c = on ? (accentThumb ? juce::Colours::white : col::label) : (i == hover ? col::label : col::label2);
                if (useGlyphs)
                {
                    icon::draw (g, icon::filterShape (labels[i].getIntValue()), cell.withSizeKeepingCentre (26, 17), on && ! accentThumb ? accent : c, 1.5f, 24.0f);
                }
                else
                {
                    g.setColour (c);
                    g.setFont (font (juce::jmin (12.5f, r.getHeight() * 0.48f), on ? Weight::semibold : Weight::medium));
                    g.drawText (labels[i], cell.reduced (2, 0), juce::Justification::centred, false);
                }
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { setHover (indexAt (e.x)); }
        void mouseExit (const juce::MouseEvent&) override { setHover (-1); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = indexAt (e.x);
            if (i >= 0 && i != selected && isEnabled())
            {
                setSelected (i);
                if (onChange) onChange (i);
            }
        }

        juce::String getTooltip() override
        {
            return (hover >= 0 && hover < tips.size()) ? tips[hover] : juce::String();
        }

    private:
        int indexAt (int x) const { return labels.isEmpty() ? -1 : juce::jlimit (0, labels.size() - 1, x * labels.size() / juce::jmax (1, getWidth())); }
        void setHover (int h) { if (h != hover) { hover = h; repaint(); } }
        void timerCallback() override { if (! approach (thumbX, (float) selected, 0.3f)) stopTimer(); repaint(); }

        juce::StringArray labels, tips;
        bool useGlyphs = false, accentThumb = false;
        juce::Colour accent = col::accent;
        int selected = -1, hover = -1;
        float thumbX = -1.0f;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        juce::UndoManager* undo = nullptr;
    };

    //==============================================================================
    // iOS-style switch
    class Switch : public juce::Component, private juce::Timer
    {
    public:
        std::function<void (bool)> onChange;

        void setOn (bool b, bool animate = true)
        {
            if (b == on) return;
            on = b;
            if (! animate) pos = on ? 1.0f : 0.0f;
            startTimerHz (60);
            repaint();
        }

        bool isOn() const noexcept { return on; }
        void setAccent (juce::Colour c) { accent = c; repaint(); }

        void bind (juce::RangedAudioParameter* p, juce::UndoManager* um = nullptr)
        {
            attachment.reset();
            if (p == nullptr) return;
            attachment = std::make_unique<juce::ParameterAttachment> (*p, [this] (float v) { setOn (v > 0.5f); }, um);
            attachment->sendInitialUpdate();
            onChange = [this, um] (bool b) { if (um) um->beginNewTransaction(); if (attachment) attachment->setValueAsCompleteGesture (b ? 1.0f : 0.0f); };
        }

        void paint (juce::Graphics& g) override
        {
            auto r = juce::Rectangle<float> (0, 0, 36, 21).withCentre (getLocalBounds().toFloat().getCentre());
            const float rad = r.getHeight() * 0.5f;
            g.setColour (col::fillHi.interpolatedWith (accent, pos));
            g.fillRoundedRectangle (r, rad);

            const float d = r.getHeight() - 4.0f;
            auto k = juce::Rectangle<float> (r.getX() + 2.0f + pos * (r.getWidth() - d - 4.0f), r.getY() + 2.0f, d, d);
            juce::Path kp; kp.addEllipse (k);
            juce::DropShadow (juce::Colours::black.withAlpha (0.35f), 5, { 0, 2 }).drawForPath (g, kp);
            g.setColour (juce::Colours::white);
            g.fillPath (kp);
            if (! isEnabled())
            {
                g.setColour (col::bg1.withAlpha (0.5f));
                g.fillRoundedRectangle (r, rad);
            }
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (! isEnabled()) return;
            setOn (! on);
            if (onChange) onChange (on);
        }

    private:
        void timerCallback() override { if (! approach (pos, on ? 1.0f : 0.0f, 0.3f)) stopTimer(); repaint(); }

        bool on = false;
        float pos = 0.0f;
        juce::Colour accent = col::green;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };

    //==============================================================================
    // Round icon / text button with hover and optional toggle state
    class IconButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        std::function<void()> onClick;

        void setIcon (juce::Path p, float strokeWidth = 1.6f) { path = std::move (p); stroke = strokeWidth; repaint(); }
        void setText (juce::String t) { text = std::move (t); repaint(); }
        void setToggled (bool t) { toggled = t; repaint(); }
        bool isToggled() const noexcept { return toggled; }
        void setToggleColour (juce::Colour c) { toggleColour = c; repaint(); }
        void setBackgroundVisible (bool b) { background = b; repaint(); }
        void setPrimary (juce::Colour c) { primary = true; primaryColour = c; repaint(); }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat();
            const float rad = juce::jmin (8.0f, r.getHeight() * 0.5f);

            if (primary)
            {
                juce::Path pp; pp.addRoundedRectangle (r.reduced (0.5f), rad);
                if (isEnabled()) juce::DropShadow (primaryColour.withAlpha (0.35f), 10, { 0, 3 }).drawForPath (g, pp);
                g.setGradientFill (juce::ColourGradient (primaryColour.brighter (0.12f + 0.1f * hoverAmt), r.getX(), r.getY(),
                                                         primaryColour.darker (0.08f), r.getX(), r.getBottom(), false));
                g.setOpacity (isEnabled() ? 1.0f : 0.35f);
                g.fillPath (pp);
                g.setOpacity (1.0f);
                g.setColour (juce::Colours::white.withAlpha (isEnabled() ? 1.0f : 0.5f));
                g.setFont (font (13.0f, Weight::semibold));
                g.drawText (text, r, juce::Justification::centred, false);
                return;
            }

            if (toggled)
            {
                g.setColour (toggleColour.withAlpha (0.22f));
                g.fillRoundedRectangle (r, rad);
            }
            else if (background || hoverAmt > 0.0f)
            {
                g.setColour (col::fill.withMultipliedAlpha (background ? 1.0f + hoverAmt * 0.6f : hoverAmt));
                g.fillRoundedRectangle (r, rad);
            }

            const auto c = ! isEnabled() ? col::label4 : toggled ? toggleColour : col::label2.interpolatedWith (col::label, hoverAmt);

            if (text.isNotEmpty())
            {
                auto tr = r.reduced (8, 0);
                if (! path.isEmpty())
                {
                    icon::draw (g, path, tr.removeFromRight (14).withSizeKeepingCentre (14, 14), c.withAlpha (0.8f), stroke);
                    tr.removeFromRight (2);
                }
                g.setColour (c);
                g.setFont (font (12.5f, Weight::medium));
                g.drawText (text, tr, path.isEmpty() ? juce::Justification::centred : juce::Justification::centredLeft, true);
            }
            else
            {
                const float s = juce::jmin (r.getWidth(), r.getHeight()) * 0.66f;
                icon::draw (g, path, r.withSizeKeepingCentre (s, s), c, stroke);
            }
        }

        void mouseEnter (const juce::MouseEvent&) override { hovering = true; startTimerHz (60); }
        void mouseExit (const juce::MouseEvent&) override { hovering = false; startTimerHz (60); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (isEnabled() && getLocalBounds().contains (e.getPosition()) && onClick) onClick();
        }

    private:
        void timerCallback() override { if (! approach (hoverAmt, hovering ? 1.0f : 0.0f, 0.3f)) stopTimer(); repaint(); }

        juce::Path path;
        juce::String text;
        float stroke = 1.6f, hoverAmt = 0.0f;
        bool hovering = false, toggled = false, background = false, primary = false;
        juce::Colour toggleColour = col::accent, primaryColour = col::accent;
    };

    //==============================================================================
    // Rotary dial with value read-out (double-click: reset, click the value: type it in)
    class Dial : public juce::Component, private juce::Timer
    {
    public:
        Dial (juce::String captionText, bool isBipolar = false) : caption (std::move (captionText)), bipolar (isBipolar)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
            slider.setMouseDragSensitivity (220);
            slider.setVelocityBasedMode (false);
            slider.setLookAndFeel (&dialLnf);
            slider.onValueChange = [this] { refreshText(); if (plain && plainChanged) plainChanged (slider.getValue()); };
            slider.onDragStart = [this] { if (undo) undo->beginNewTransaction(); };
            dialLnf.owner = this;
            addAndMakeVisible (slider);

            value.setJustificationType (juce::Justification::centred);
            value.setFont (font (12.0f, Weight::semibold));
            value.setColour (juce::Label::textColourId, col::label);
            value.setEditable (false, true, false);
            value.setColour (juce::Label::backgroundWhenEditingColourId, juce::Colour (0xff2c2c2e));
            value.setColour (juce::Label::outlineWhenEditingColourId, col::accent);
            value.onTextChange = [this]
            {
                if (param != nullptr)
                {
                    if (undo) undo->beginNewTransaction();
                    const float v = param->convertFrom0to1 (param->getValueForText (value.getText()));
                    slider.setValue (v, juce::sendNotificationSync);
                }
                else if (plain)
                {
                    slider.setValue (value.getText().retainCharacters ("0123456789.-").getDoubleValue(), juce::sendNotificationSync);
                }
                refreshText();
            };
            addAndMakeVisible (value);
        }

        ~Dial() override { slider.setLookAndFeel (nullptr); }

        void bind (juce::RangedAudioParameter* p, juce::UndoManager* um)
        {
            attachment.reset();
            param = p;
            undo = um;
            if (p == nullptr) return;
            const auto& range = p->getNormalisableRange();
            slider.setNormalisableRange ({ (double) range.start, (double) range.end,
                                           [range] (double, double, double v) { return (double) range.convertFrom0to1 ((float) v); },
                                           [range] (double, double, double v) { return (double) range.convertTo0to1 ((float) v); },
                                           [range] (double, double, double v) { return (double) range.snapToLegalValue ((float) v); } });
            slider.setDoubleClickReturnValue (true, p->convertFrom0to1 (p->getDefaultValue()));
            attachment = std::make_unique<juce::SliderParameterAttachment> (*p, slider, um);
            refreshText();
        }

        // a dial that is not bound to a plugin parameter
        void bindPlain (juce::NormalisableRange<double> range, double value, double defaultValue, std::function<void (double)> onChange)
        {
            attachment.reset();
            param = nullptr;
            plain = true;
            slider.setNormalisableRange (range);
            slider.setDoubleClickReturnValue (true, defaultValue);
            slider.setValue (value, juce::dontSendNotification);
            plainChanged = std::move (onChange);
            refreshText();
        }

        void setAccent (juce::Colour c) { accent = c; repaint(); slider.repaint(); }
        void setFormatter (std::function<juce::String (float)> f) { formatter = std::move (f); refreshText(); }
        void setDimmed (bool d) { dimmed = d; slider.repaint(); value.setAlpha (d ? 0.45f : 1.0f); repaint(); }
        juce::Slider& getSlider() noexcept { return slider; }

        void paint (juce::Graphics& g) override
        {
            g.setColour (col::label3);
            g.setFont (font (10.0f, Weight::semibold, 0.06f));
            g.drawText (caption.toUpperCase(), getLocalBounds().removeFromTop (14), juce::Justification::centred);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromTop (15);
            value.setBounds (r.removeFromBottom (18));
            slider.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), r.getHeight()), juce::jmin (r.getWidth(), r.getHeight())));
        }

    private:
        struct DialLnf : juce::LookAndFeel_V4
        {
            Dial* owner = nullptr;

            void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
            {
                const auto acc = owner->dimmed ? col::label3 : owner->accent;
                auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (2.0f);
                const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
                const auto c = r.getCentre();
                const float ang = a0 + pos * (a1 - a0);
                const float trackW = juce::jmax (2.5f, rad * 0.11f);
                const float arcR = rad - trackW * 0.5f;

                juce::Path track;
                track.addCentredArc (c.x, c.y, arcR, arcR, 0, a0, a1, true);
                g.setColour (juce::Colours::white.withAlpha (0.09f));
                g.strokePath (track, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

                const float from = owner->bipolar ? (a0 + a1) * 0.5f : a0;
                if (std::abs (ang - from) > 0.01f)
                {
                    juce::Path v;
                    v.addCentredArc (c.x, c.y, arcR, arcR, 0, juce::jmin (from, ang), juce::jmax (from, ang), true);
                    g.setColour (acc.withAlpha (0.25f));
                    g.strokePath (v, juce::PathStrokeType (trackW + 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (acc);
                    g.strokePath (v, juce::PathStrokeType (trackW, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }

                // knob face
                const float fr = rad - trackW - 4.0f;
                juce::Path face; face.addEllipse (c.x - fr, c.y - fr, fr * 2, fr * 2);
                juce::DropShadow (juce::Colours::black.withAlpha (0.5f), 8, { 0, 3 }).drawForPath (g, face);
                g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3d3d41), c.x, c.y - fr,
                                                         juce::Colour (0xff262629), c.x, c.y + fr, false));
                g.fillPath (face);
                g.setColour (juce::Colours::white.withAlpha (s.isMouseOverOrDragging() ? 0.16f : 0.09f));
                g.strokePath (face, juce::PathStrokeType (1.0f));

                // indicator
                const float i0 = fr * 0.35f, i1 = fr * 0.82f;
                juce::Line<float> l (c.getPointOnCircumference (i0, ang), c.getPointOnCircumference (i1, ang));
                g.setColour (owner->dimmed ? col::label3 : col::label);
                g.drawLine (l, 2.0f);
            }
        };

        void refreshText()
        {
            if (plain)
            {
                value.setText (formatter ? formatter ((float) slider.getValue()) : juce::String (slider.getValue(), 1), juce::dontSendNotification);
                return;
            }
            if (param == nullptr) return;
            const float v = (float) slider.getValue();
            value.setText (formatter ? formatter (v) : param->getText (param->convertTo0to1 (v), 16), juce::dontSendNotification);
        }

        void timerCallback() override {}

        DialLnf dialLnf;
        juce::Slider slider;
        juce::Label value;
        juce::String caption;
        bool bipolar = false, dimmed = false;
        juce::Colour accent = col::accent;
        juce::RangedAudioParameter* param = nullptr;
        juce::UndoManager* undo = nullptr;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
        std::function<juce::String (float)> formatter;
        bool plain = false;
        std::function<void (double)> plainChanged;
    };
}
