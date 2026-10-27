#pragma once

#include "Theme.h"
#include "../PluginProcessor.h"

namespace avel::ui
{
    // Shows the band's transfer curve with a live dot at the current input level
    class TransferCurve : public juce::Component
    {
    public:
        explicit TransferCurve (EmberProcessor& p) : proc (p) {}
        void setBand (int b) { band = b; }

        void tick()
        {
            if (band < 0) return;
            const float lvl = proc.getBandLevel (band);
            const float target = lvl > -90.0f ? juce::Decibels::decibelsToGain (lvl) : 0.0f;
            approach (level, target, target > level ? 0.5f : 0.1f);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (1.0f);
            g.setColour (juce::Colour (0xff111113));
            g.fillRoundedRectangle (r, 12.0f);
            g.setColour (col::stroke);
            g.drawRoundedRectangle (r, 12.0f, 1.0f);
            if (band < 0) return;

            auto a = r.reduced (10.0f);
            const auto c = a.getCentre();
            const float half = a.getWidth() * 0.5f;
            const float range = 1.3f;
            auto toPt = [&] (double x, double y) { return juce::Point<float> (c.x + (float) (x / range) * half, c.y - (float) (y / range) * half); };

            g.setColour (juce::Colours::white.withAlpha (0.05f));
            g.fillRect (juce::Rectangle<float> (a.getX(), c.y - 0.5f, a.getWidth(), 1.0f));
            g.fillRect (juce::Rectangle<float> (c.x - 0.5f, a.getY(), 1.0f, a.getHeight()));
            {
                juce::Path diag; diag.startNewSubPath (toPt (-range, -range)); diag.lineTo (toPt (range, range));
                juce::Path dashed; const float d[] { 3.0f, 4.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, diag, d, 2);
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.fillPath (dashed);
            }

            const auto& p = proc.band[(size_t) band];
            const Style s = p.getStyle();
            const double driveDb = p.drive->load();
            const double drive = juce::Decibels::decibelsToGain (driveDb);
            // normalise like auto gain would for a sine at -12 dB, so the shape is what you see
            const double ref = 0.25;
            const double small = std::abs (staticCurve (s, ref * drive, driveDb / 36.0)) / ref;
            const double norm = proc.apvts.getRawParameterValue ("autoGain")->load() > 0.5f && small > 1.0e-6 ? 1.0 / small : 1.0;

            juce::Path curve;
            const int N = 160;
            for (int i = 0; i <= N; ++i)
            {
                const double x = -range + 2.0 * range * i / N;
                const double y = juce::jlimit (-1.6, 1.6, staticCurve (s, x * drive, driveDb / 36.0) * norm);
                const auto pt = toPt (x, y);
                if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
            }
            const auto colour = col::band (band);
            g.setColour (colour.withAlpha (0.25f));
            g.strokePath (curve, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (colour);
            g.strokePath (curve, juce::PathStrokeType (2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // live level dot
            const double x = juce::jlimit (0.0, (double) range, (double) level);
            const auto pt = toPt (x, juce::jlimit (-1.6, 1.6, staticCurve (s, x * drive, driveDb / 36.0) * norm));
            g.setColour (juce::Colours::white.withAlpha (0.2f));
            g.fillEllipse (juce::Rectangle<float> (16, 16).withCentre (pt));
            g.setColour (juce::Colours::white);
            g.fillEllipse (juce::Rectangle<float> (7, 7).withCentre (pt));

            g.setColour (col::label3);
            g.setFont (font (9.5f, Weight::semibold, 0.06f));
            g.drawText ("TRANSFER", r.reduced (10, 6), juce::Justification::topLeft);
        }

    private:
        EmberProcessor& proc;
        int band = -1;
        float level = 0.0f;
    };

    //==============================================================================
    class BandPanel : public juce::Component
    {
    public:
        explicit BandPanel (EmberProcessor& p) : proc (p), transfer (p)
        {
            styleBtn.setIcon (icon::chevronDown(), 1.5f);
            styleBtn.setBackgroundVisible (true);
            styleBtn.setTooltip ("Saturation style");
            styleBtn.onClick = [this] { showStyleMenu(); };
            addAndMakeVisible (styleBtn);

            placeSeg.setItems ({ "Stereo", "M", "S", "L", "R" });
            placeSeg.setTooltips ({ "Stereo", "Mid only", "Side only", "Left only", "Right only" });
            addAndMakeVisible (placeSeg);

            soloBtn.setText ("S");
            soloBtn.setToggleColour (col::yellow);
            soloBtn.setTooltip ("Solo band");
            muteBtn.setText ("M");
            muteBtn.setToggleColour (col::red);
            muteBtn.setTooltip ("Mute band");
            bypassBtn.setIcon (icon::power(), 1.6f);
            bypassBtn.setTooltip ("Bypass band (no saturation)");
            for (auto* b : { &soloBtn, &muteBtn, &bypassBtn }) { b->setBackgroundVisible (true); addAndMakeVisible (*b); }
            soloBtn.onClick = [this] { toggle ("solo"); };
            muteBtn.onClick = [this] { toggle ("mute"); };
            bypassBtn.onClick = [this] { toggle ("bypass"); };

            drive.setFormatter ([] (float v) { return formatDb (v); });
            dyn.setFormatter ([] (float v) { return (v > 0.5f ? "+" : "") + formatPct (v); });
            tone.setFormatter ([] (float v) { return (v > 0.5f ? "+" : "") + formatPct (v); });
            fb.setFormatter ([] (float v) { return formatPct (v); });
            mix.setFormatter ([] (float v) { return formatPct (v); });
            level.setFormatter ([] (float v) { return formatDb (v); });
            for (auto* d : { &drive, &dyn, &tone, &fb, &mix, &level }) addAndMakeVisible (*d);
            dyn.getSlider().setTooltip ("Dynamics: positive = more even saturation, negative = louder parts saturate harder");
            tone.getSlider().setTooltip ("Tone: brighter or darker colour of the saturation");
            fb.getSlider().setTooltip ("Feedback: resonant feedback around the band's centre");

            addAndMakeVisible (transfer);
        }

        void setBand (int b)
        {
            if (b == bandIndex) return;
            bandIndex = b;
            auto* um = &proc.undo;
            placeSeg.bind (proc.param (bandId (b, "stereo")), um);
            drive.bind (proc.param (bandId (b, "drive")), um);
            dyn.bind (proc.param (bandId (b, "dyn")), um);
            tone.bind (proc.param (bandId (b, "tone")), um);
            fb.bind (proc.param (bandId (b, "fb")), um);
            mix.bind (proc.param (bandId (b, "mix")), um);
            level.bind (proc.param (bandId (b, "level")), um);
            const auto c = col::band (b);
            for (auto* d : { &drive, &dyn, &tone, &fb, &mix, &level }) d->setAccent (c);
            transfer.setBand (b);
            refresh();
            repaint();
        }

        void refresh()
        {
            if (bandIndex < 0) return;
            const auto& p = proc.band[(size_t) bandIndex];
            styleBtn.setText (styleName (p.getStyle()));
            soloBtn.setToggled (p.solo->load() > 0.5f);
            muteBtn.setToggled (p.mute->load() > 0.5f);
            bypassBtn.setToggled (p.bypass->load() > 0.5f);
            bypassBtn.setToggleColour (col::label2);
            const bool active = p.bypass->load() < 0.5f && p.mute->load() < 0.5f;
            for (auto* d : { &drive, &dyn, &tone, &fb, &mix, &level }) d->setDimmed (! active);
            transfer.tick();
            const juce::String range = formatFreq (proc.bandLow (bandIndex)) + "  -  " + formatFreq (proc.bandHigh (bandIndex));
            if (range != lastRange) { lastRange = range; repaint(); }
        }

        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds().toFloat().reduced (16.0f, 4.0f).withTrimmedBottom (14.0f);
            drawGlass (g, r, 18.0f);
            if (bandIndex < 0) return;
            const auto c = col::band (bandIndex);
            auto head = nameArea.toFloat();
            g.setColour (c.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (18, 18).withCentre ({ head.getX() + 9, head.getCentreY() }));
            g.setColour (c);
            g.fillEllipse (juce::Rectangle<float> (10, 10).withCentre ({ head.getX() + 9, head.getCentreY() }));
            g.setColour (col::label);
            g.setFont (font (13.5f, Weight::semibold));
            g.drawText ("Band " + juce::String (bandIndex + 1), head.withTrimmedLeft (24), juce::Justification::centredLeft);
            g.setColour (col::label3);
            g.setFont (font (11.0f, Weight::medium));
            g.drawText (lastRange, rangeArea, juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (16, 4).withTrimmedBottom (14).reduced (18, 12);
            auto top = r.removeFromTop (30);
            nameArea = top.removeFromLeft (70);
            rangeArea = top.removeFromLeft (130).toFloat();
            top.removeFromLeft (8);
            styleBtn.setBounds (top.removeFromLeft (170));
            bypassBtn.setBounds (top.removeFromRight (30));
            top.removeFromRight (6);
            muteBtn.setBounds (top.removeFromRight (30));
            top.removeFromRight (6);
            soloBtn.setBounds (top.removeFromRight (30));
            top.removeFromRight (14);
            placeSeg.setBounds (top.removeFromRight (juce::jmin (220, top.getWidth())));

            r.removeFromTop (10);
            transfer.setBounds (r.removeFromRight (r.getHeight() + 30).withSizeKeepingCentre (r.getHeight() + 30, r.getHeight()));
            r.removeFromRight (16);
            const int kw = juce::jmin (82, r.getWidth() / 6);
            drive.setBounds (r.removeFromLeft (kw + 14));
            for (auto* d : { &dyn, &tone, &fb, &mix, &level }) d->setBounds (r.removeFromLeft (kw));
        }

    private:
        void toggle (const char* id)
        {
            proc.undo.beginNewTransaction();
            proc.setParam (bandId (bandIndex, id), apvtsValue (id) > 0.5f ? 0.0f : 1.0f);
        }

        float apvtsValue (const char* id) const { return proc.apvts.getRawParameterValue (bandId (bandIndex, id))->load(); }

        void showStyleMenu()
        {
            juce::PopupMenu m;
            juce::String lastCat;
            const int cur = (int) proc.band[(size_t) bandIndex].getStyle();
            for (int i = 0; i < (int) Style::count; ++i)
            {
                const juce::String cat = styleCategory ((Style) i);
                if (cat != lastCat) { m.addSectionHeader (cat); lastCat = cat; }
                m.addItem (1 + i, styleName ((Style) i), true, i == cur);
            }
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&styleBtn),
                             [this] (int r)
                             {
                                 if (r <= 0) return;
                                 proc.undo.beginNewTransaction ("Style");
                                 proc.setParam (bandId (bandIndex, "style"), (float) (r - 1));
                             });
        }

        EmberProcessor& proc;
        int bandIndex = -1;
        IconButton styleBtn, soloBtn, muteBtn, bypassBtn;
        Segmented placeSeg;
        Dial drive { "Drive" }, dyn { "Dynamics", true }, tone { "Tone", true }, fb { "Feedback" }, mix { "Mix" }, level { "Level", true };
        TransferCurve transfer;
        juce::Rectangle<int> nameArea;
        juce::Rectangle<float> rangeArea;
        juce::String lastRange;
    };
}
