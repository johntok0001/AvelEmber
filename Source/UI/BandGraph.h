#pragma once

#include <juce_dsp/juce_dsp.h>
#include "Theme.h"
#include "../PluginProcessor.h"

namespace avel::ui
{
    // Spectrum with the band split: drag dividers to move crossovers, drag a band's node
    // up/down for drive, double-click to split a band.
    class BandGraph : public juce::Component
    {
    public:
        explicit BandGraph (EmberProcessor& p) : proc (p)
        {
            setWantsKeyboardFocus (true);
            fftBuf.assign ((size_t) fftSize * 2, 0.0f);
            scopeTmp.assign ((size_t) fftSize, 0.0f);
            prefix.assign ((size_t) fftSize / 2 + 2, 0.0);
        }

        std::function<void (int)> onSelect;
        void setBottomReserve (float px) { bottomReserve = px; repaint(); }
        int getSelected() const noexcept { return selected; }
        void select (int b)
        {
            b = juce::jlimit (0, proc.getNumBands() - 1, b);
            if (b != selected) { selected = b; proc.uiState.setProperty ("selected", b, nullptr); }
            if (onSelect) onSelect (b);
            repaint();
        }

        void tick()
        {
            if (selected >= proc.getNumBands()) select (proc.getNumBands() - 1);
            updateSpectrum();
            for (int b = 0; b < maxBands; ++b)
            {
                const float lvl = proc.getBandLevel (b);
                approach (pulse[(size_t) b], juce::jlimit (0.0f, 1.0f, (lvl + 48.0f) / 48.0f), lvl > pulse[(size_t) b] ? 0.5f : 0.08f);
                approach (hoverAmt[(size_t) b], b == hoverNode || (dragNode == b) ? 1.0f : 0.0f, 0.3f);
            }
            repaint();
        }

        void resized() override
        {
            plot = getLocalBounds().toFloat().reduced (1.0f).withTrimmedBottom (22.0f);
            numPts = juce::jmax (16, (int) plot.getWidth() / 2);
            ptFreq.resize ((size_t) numPts);
            for (int i = 0; i < numPts; ++i) ptFreq[(size_t) i] = (float) freqForX (plot.getX() + plot.getWidth() * i / (numPts - 1));
            preDb.assign ((size_t) numPts, -120.0f);
            postDb.assign ((size_t) numPts, -120.0f);
        }

        void paint (juce::Graphics& g) override
        {
            auto bounds = getLocalBounds().toFloat();
            const float radius = 14.0f;
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff17171a), 0, bounds.getY(), juce::Colour (0xff0c0c0e), 0, bounds.getBottom(), false));
            g.fillRoundedRectangle (bounds, radius);

            {
                juce::Graphics::ScopedSaveState ss (g);
                juce::Path clip; clip.addRoundedRectangle (bounds, radius);
                g.reduceClipRegion (clip);

                juce::ColourGradient glow (juce::Colour (0xffff9f0a).withAlpha (0.035f), bounds.getCentreX(), bounds.getY() - bounds.getHeight() * 0.2f,
                                           juce::Colours::transparentWhite, bounds.getCentreX(), bounds.getY() + bounds.getHeight() * 0.9f, true);
                g.setGradientFill (glow);
                g.fillRect (bounds);

                drawRegions (g);
                drawGrid (g);
                drawSpectrum (g);
                drawDividers (g);
                drawNodes (g);
                drawInfo (g);
            }
            g.setColour (col::stroke);
            g.drawRoundedRectangle (bounds.reduced (0.5f), radius, 1.0f);
        }

        //==============================================================================
        void mouseMove (const juce::MouseEvent& e) override
        {
            mousePos = e.position;
            hoverNode = nodeAt (e.position);
            hoverDivider = hoverNode < 0 ? dividerAt (e.position) : -1;
            setMouseCursor (hoverNode >= 0 ? juce::MouseCursor::UpDownResizeCursor
                          : hoverDivider >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }

        void mouseExit (const juce::MouseEvent&) override { hoverNode = hoverDivider = -1; mousePos = { -1, -1 }; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            grabKeyboardFocus();
            const int node = nodeAt (e.position);
            const int div = node < 0 ? dividerAt (e.position) : -1;

            if (e.mods.isPopupMenu())
            {
                if (div >= 0) { showDividerMenu (div); return; }
                const int b = node >= 0 ? node : bandAtX (e.position.x);
                select (b);
                showBandMenu (b, e.position);
                return;
            }

            if (node >= 0)
            {
                select (node);
                proc.undo.beginNewTransaction ("Drive");
                dragNode = node;
                dragStart = proc.band[(size_t) node].drive->load();
                proc.param (bandId (node, "drive"))->beginChangeGesture();
                return;
            }
            if (div >= 0)
            {
                proc.undo.beginNewTransaction ("Crossover");
                dragDivider = div;
                proc.param (xoId (div))->beginChangeGesture();
                return;
            }
            select (bandAtX (e.position.x));
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            mousePos = e.position;
            if (dragNode >= 0)
            {
                const double fine = e.mods.isShiftDown() ? 0.15 : 1.0;
                const double v = juce::jlimit (0.0, 36.0, dragStart - e.getDistanceFromDragStartY() * fine * 36.0 / driveSpan());
                auto* p = proc.param (bandId (dragNode, "drive"));
                p->setValueNotifyingHost (p->convertTo0to1 ((float) v));
            }
            else if (dragDivider >= 0)
            {
                proc.setCrossoverFreq (dragDivider, (float) freqForX (e.position.x));
            }
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragNode >= 0) proc.param (bandId (dragNode, "drive"))->endChangeGesture();
            if (dragDivider >= 0) proc.param (xoId (dragDivider))->endChangeGesture();
            dragNode = dragDivider = -1;
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            const int node = nodeAt (e.position);
            if (node >= 0)
            {
                proc.undo.beginNewTransaction ("Bypass band");
                proc.setParam (bandId (node, "bypass"), proc.band[(size_t) node].bypass->load() > 0.5f ? 0.0f : 1.0f);
                return;
            }
            const int div = dividerAt (e.position);
            if (div >= 0) { proc.removeCrossover (div); return; }
            const int nb = proc.splitBandAt ((float) freqForX (e.position.x));
            if (nb >= 0) select (nb);
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            const int b = bandAtX (e.position.x);
            const auto now = juce::Time::getMillisecondCounter();
            if (now - lastWheel > 600) proc.undo.beginNewTransaction ("Drive");
            lastWheel = now;
            const float d = (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
            auto* p = proc.param (bandId (b, "drive"));
            p->beginChangeGesture();
            p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (0.0f, 36.0f, proc.band[(size_t) b].drive->load() + d * (e.mods.isShiftDown() ? 1.5f : 6.0f))));
            p->endChangeGesture();
        }

        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k.getModifiers().isCommandDown() && (k.getKeyCode() == 'Z' || k.getKeyCode() == 'z'))
            {
                if (k.getModifiers().isShiftDown()) proc.undo.redo(); else proc.undo.undo();
                return true;
            }
            if ((k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey) && proc.getNumBands() > 1)
            {
                proc.removeCrossover (selected > 0 ? selected - 1 : 0);
                select (juce::jmax (0, selected - 1));
                return true;
            }
            if (k == juce::KeyPress::leftKey)  { select (selected - 1); return true; }
            if (k == juce::KeyPress::rightKey) { select (selected + 1); return true; }
            return false;
        }

    private:
        static constexpr double fMin = 20.0, fMax = 20000.0;
        static constexpr int fftOrder = 13, fftSize = 1 << fftOrder;

        float xForFreq (double f) const noexcept { return plot.getX() + plot.getWidth() * (float) (std::log (f / fMin) / std::log (fMax / fMin)); }
        double freqForX (float x) const noexcept { return fMin * std::pow (fMax / fMin, (double) ((x - plot.getX()) / plot.getWidth())); }
        // drive nodes live in the visible area above the band panel
        float driveBottom() const noexcept { return plot.getBottom() - bottomReserve - 26.0f; }
        float driveSpan() const noexcept { return juce::jmax (60.0f, driveBottom() - (plot.getY() + 92.0f)); }
        float yForDrive (double db) const noexcept { return driveBottom() - (float) (db / 36.0) * driveSpan(); }
        float yForSpectrum (double db) const noexcept { return plot.getBottom() - (float) ((db + 96.0) / 108.0) * plot.getHeight(); }
        double sampleRate() const noexcept { return proc.getSampleRate() > 0 ? proc.getSampleRate() : 48000.0; }

        float bandX0 (int b) const noexcept { return b == 0 ? plot.getX() : xForFreq (proc.getCrossover (b - 1)); }
        float bandX1 (int b) const noexcept { return b >= proc.getNumBands() - 1 ? plot.getRight() : xForFreq (proc.getCrossover (b)); }
        double bandCentre (int b) const noexcept { return std::sqrt ((double) proc.bandLow (b) * proc.bandHigh (b)); }
        juce::Point<float> nodePos (int b) const noexcept
        {
            return { 0.5f * (bandX0 (b) + bandX1 (b)), yForDrive (proc.band[(size_t) b].drive->load()) };
        }

        int bandAtX (float x) const noexcept
        {
            for (int b = 0; b < proc.getNumBands(); ++b) if (x < bandX1 (b)) return b;
            return proc.getNumBands() - 1;
        }

        int nodeAt (juce::Point<float> p) const noexcept
        {
            for (int b = 0; b < proc.getNumBands(); ++b) if (nodePos (b).getDistanceFrom (p) < 13.0f) return b;
            return -1;
        }

        int dividerAt (juce::Point<float> p) const noexcept
        {
            if (! plot.contains (p)) return -1;
            for (int i = 0; i < proc.getNumBands() - 1; ++i) if (std::abs (xForFreq (proc.getCrossover (i)) - p.x) < 6.0f) return i;
            return -1;
        }

        void updateSpectrum()
        {
            const double sr = sampleRate();
            const int half = fftSize / 2;
            const double binHz = sr / fftSize;
            auto analyse = [&] (const ScopeRing& ring, std::vector<float>& out)
            {
                ring.readLatest (scopeTmp.data(), fftSize);
                std::copy (scopeTmp.begin(), scopeTmp.end(), fftBuf.begin());
                std::fill (fftBuf.begin() + fftSize, fftBuf.end(), 0.0f);
                window.multiplyWithWindowingTable (fftBuf.data(), (size_t) fftSize);
                fft.performFrequencyOnlyForwardTransform (fftBuf.data(), true);
                const double norm = 2.0 / (fftSize * 0.3587);
                prefix[0] = 0.0;
                for (int k = 0; k <= half; ++k) { const double a = fftBuf[(size_t) k] * norm; prefix[(size_t) k + 1] = prefix[(size_t) k] + a * a; }
                const double bw = std::pow (2.0, 1.0 / 12.0);
                for (int i = 0; i < numPts; ++i)
                {
                    const double f = ptFreq[(size_t) i];
                    double lo = f / bw / binHz, hi = f * bw / binHz;
                    if (hi - lo < 1.0) { const double c = 0.5 * (lo + hi); lo = c - 0.5; hi = c + 0.5; }
                    auto cum = [&] (double bin)
                    {
                        bin = juce::jlimit (0.0, (double) half, bin + 0.5);
                        const int i0 = (int) bin;
                        return prefix[(size_t) i0] + (prefix[(size_t) juce::jmin (i0 + 1, half + 1)] - prefix[(size_t) i0]) * (bin - i0);
                    };
                    const double pw = juce::jmax (1.0e-14, (cum (hi) - cum (lo)) / (hi - lo));
                    const float db = f < 0.5 * sr ? (float) (10.0 * std::log10 (pw) + 4.5 * std::log2 (f / 1000.0)) : -120.0f;
                    float& v = out[(size_t) i];
                    v += (db - v) * (db > v ? 0.6f : 0.12f);
                }
            };
            analyse (proc.preScope, preDb);
            analyse (proc.postScope, postDb);
        }

        juce::Path spectrumPath (const std::vector<float>& v) const
        {
            juce::Path p;
            for (int i = 0; i < numPts; ++i)
            {
                const float x = plot.getX() + plot.getWidth() * i / (numPts - 1);
                const float y = juce::jlimit (plot.getY(), plot.getBottom(), yForSpectrum (v[(size_t) i]));
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            return p;
        }

        void drawRegions (juce::Graphics& g)
        {
            const int nb = proc.getNumBands();
            for (int b = 0; b < nb; ++b)
            {
                const auto c = col::band (b);
                auto r = juce::Rectangle<float>::leftTopRightBottom (bandX0 (b), plot.getY(), bandX1 (b), plot.getBottom());
                const bool sel = b == selected;
                const bool hov = bandAtX (mousePos.x) == b && plot.contains (mousePos);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (sel ? 0.10f : hov ? 0.06f : 0.035f), 0, r.getY(),
                                                         c.withAlpha (sel ? 0.03f : 0.01f), 0, r.getBottom(), false));
                g.fillRect (r);
                g.setColour (c.withAlpha (sel ? 0.95f : 0.35f));
                g.fillRect (r.withHeight (sel ? 3.0f : 2.0f));
            }
        }

        void drawGrid (juce::Graphics& g)
        {
            for (double d : { 10.0, 100.0, 1000.0, 10000.0 })
                for (int m = 1; m <= 9; ++m)
                {
                    const double f = d * m;
                    if (f < fMin || f > fMax) continue;
                    g.setColour (juce::Colours::white.withAlpha (m == 1 ? 0.06f : 0.025f));
                    g.fillRect (juce::Rectangle<float> (xForFreq (f) - 0.5f, plot.getY(), 1.0f, plot.getHeight()));
                }
            g.setFont (font (10.5f, Weight::medium));
            const std::pair<double, const char*> fl[] { { 30, "30" }, { 50, "50" }, { 100, "100" }, { 200, "200" }, { 500, "500" },
                                                        { 1000, "1k" }, { 2000, "2k" }, { 5000, "5k" }, { 10000, "10k" } };
            for (auto& [f, t] : fl)
            {
                g.setColour (col::label3);
                g.drawText (t, juce::Rectangle<float> (xForFreq (f) - 24, plot.getBottom() + 3, 48, 16), juce::Justification::centred);
            }
        }

        void drawSpectrum (juce::Graphics& g)
        {
            auto under = [this] (juce::Path p) { p.lineTo (plot.getRight(), plot.getBottom()); p.lineTo (plot.getX(), plot.getBottom()); p.closeSubPath(); return p; };
            auto pre = spectrumPath (preDb);
            g.setColour (juce::Colour (0xff5e6a85).withAlpha (0.08f));
            g.fillPath (under (pre));

            auto post = spectrumPath (postDb);
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xffffd9b0).withAlpha (0.09f), 0, plot.getY() + plot.getHeight() * 0.3f,
                                                     juce::Colour (0xffffd9b0).withAlpha (0.0f), 0, plot.getBottom(), false));
            g.fillPath (under (post));
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.strokePath (post, juce::PathStrokeType (1.0f, juce::PathStrokeType::curved));
        }

        void drawDividers (juce::Graphics& g)
        {
            for (int i = 0; i < proc.getNumBands() - 1; ++i)
            {
                const float x = xForFreq (proc.getCrossover (i));
                const bool hot = i == hoverDivider || i == dragDivider;
                g.setColour (juce::Colours::white.withAlpha (hot ? 0.55f : 0.16f));
                g.fillRect (juce::Rectangle<float> (x - (hot ? 1.0f : 0.5f), plot.getY(), hot ? 2.0f : 1.0f, plot.getHeight()));

                auto handle = juce::Rectangle<float> (8, 34).withCentre ({ x, plot.getY() + 64 });
                g.setColour (juce::Colour (0xff2c2c2f));
                g.fillRoundedRectangle (handle, 4.0f);
                g.setColour (juce::Colours::white.withAlpha (hot ? 0.8f : 0.3f));
                g.drawRoundedRectangle (handle, 4.0f, 1.0f);
                for (int k = -1; k <= 1; ++k)
                    g.fillRect (juce::Rectangle<float> (x - 1.5f, handle.getCentreY() + k * 5.0f - 0.5f, 3.0f, 1.0f));

                if (hot)
                {
                    const juce::String t = formatFreq (proc.getCrossover (i));
                    const auto f = font (11.5f, Weight::semibold);
                    const float w = juce::GlyphArrangement::getStringWidth (f, t) + 20;
                    auto pill = juce::Rectangle<float> (w, 24).withCentre ({ x, plot.getY() + 30 }).constrainedWithin (plot.reduced (4));
                    g.setColour (juce::Colour (0xf0232326));
                    g.fillRoundedRectangle (pill, 12.0f);
                    g.setColour (col::strokeHi);
                    g.drawRoundedRectangle (pill.reduced (0.5f), 12.0f, 1.0f);
                    g.setColour (col::label);
                    g.setFont (f);
                    g.drawText (t, pill, juce::Justification::centred);
                }
            }
        }

        void drawNodes (juce::Graphics& g)
        {
            const int nb = proc.getNumBands();
            bool anySolo = false;
            for (int b = 0; b < nb; ++b) anySolo |= proc.band[(size_t) b].solo->load() > 0.5f;

            for (int b = 0; b < nb; ++b)
            {
                const auto& p = proc.band[(size_t) b];
                const bool muted = p.mute->load() > 0.5f || (anySolo && p.solo->load() < 0.5f);
                const bool bypass = p.bypass->load() > 0.5f;
                const auto c = muted ? col::label3 : col::band (b);
                const auto pos = nodePos (b);
                const bool sel = b == selected;
                const float h = hoverAmt[(size_t) b];

                // drive column
                const float colBottom = driveBottom() + 18.0f;
                g.setGradientFill (juce::ColourGradient (c.withAlpha (sel ? 0.30f : 0.16f), 0, pos.y, c.withAlpha (0.0f), 0, colBottom, false));
                g.fillRoundedRectangle (juce::Rectangle<float> (pos.x - 13, pos.y, 26, juce::jmax (0.0f, colBottom - pos.y)), 13.0f);
                // drive scale tick at 0 dB
                g.setColour (c.withAlpha (0.25f));
                g.fillRect (juce::Rectangle<float> (pos.x - 9, driveBottom(), 18, 1.5f));

                // level pulse
                const float pr = 9.0f + 10.0f * pulse[(size_t) b];
                if (! muted && ! bypass)
                {
                    g.setColour (c.withAlpha (0.10f + 0.12f * pulse[(size_t) b]));
                    g.fillEllipse (juce::Rectangle<float> (2 * pr, 2 * pr).withCentre (pos));
                }

                const float r = 8.0f + 2.0f * h + (sel ? 1.0f : 0.0f);
                juce::Path dot; dot.addEllipse (juce::Rectangle<float> (2 * r, 2 * r).withCentre (pos));
                juce::DropShadow (juce::Colours::black.withAlpha (0.6f), 8, { 0, 2 }).drawForPath (g, dot);
                if (bypass || muted)
                {
                    g.setColour (juce::Colour (0xff2c2c2e)); g.fillPath (dot);
                    g.setColour (c.withAlpha (0.8f)); g.strokePath (dot, juce::PathStrokeType (1.5f));
                }
                else
                {
                    g.setGradientFill (juce::ColourGradient (c.brighter (0.25f), pos.x, pos.y - r, c.darker (0.15f), pos.x, pos.y + r, false));
                    g.fillPath (dot);
                }
                g.setColour (juce::Colours::white.withAlpha (sel ? 0.95f : 0.3f));
                g.strokePath (dot, juce::PathStrokeType (sel ? 1.6f : 1.0f));
                if (p.solo->load() > 0.5f)
                {
                    g.setColour (col::yellow);
                    g.drawEllipse (juce::Rectangle<float> (2 * r + 8, 2 * r + 8).withCentre (pos), 1.5f);
                }

                g.setColour (bypass || muted ? col::label2 : juce::Colours::white);
                g.setFont (font (10.0f, Weight::semibold));
                g.drawText (juce::String (b + 1), juce::Rectangle<float> (2 * r, 2 * r).withCentre (pos).translated (0, 0.5f), juce::Justification::centred);

                // labels
                const float w = bandX1 (b) - bandX0 (b);
                if (w > 70.0f)
                {
                    auto lab = juce::Rectangle<float> (juce::jmin (w - 8, 150.0f), 34).withCentre ({ pos.x, pos.y - r - 24 });
                    g.setFont (font (11.5f, Weight::semibold));
                    g.setColour (muted ? col::label3 : col::label);
                    g.drawText (styleName (p.getStyle()), lab.removeFromTop (16), juce::Justification::centred, true);
                    g.setFont (font (10.5f, Weight::medium));
                    g.setColour (col::label2);
                    juce::String sub = bypass ? "Bypassed" : muted ? "Muted" : formatDb (p.drive->load());
                    if (p.placement() != Placement::stereo) sub << "  " << placementNames()[(int) p.placement()];
                    g.drawText (sub, lab, juce::Justification::centred, true);
                }
            }
        }

        void drawInfo (juce::Graphics& g)
        {
            if (proc.getNumBands() == 1 && plot.contains (mousePos) && hoverNode < 0)
            {
                const juce::String t = "Double-click to split into bands";
                const auto f = font (11.5f, Weight::medium);
                const float w = juce::GlyphArrangement::getStringWidth (f, t) + 26;
                auto pill = juce::Rectangle<float> (w, 26).withCentre ({ plot.getCentreX(), plot.getY() + 30 });
                g.setColour (juce::Colour (0xcc1c1c1f));
                g.fillRoundedRectangle (pill, 13);
                g.setColour (col::stroke);
                g.drawRoundedRectangle (pill.reduced (0.5f), 13, 1);
                g.setColour (col::label2);
                g.setFont (f);
                g.drawText (t, pill, juce::Justification::centred);
            }
            if (plot.contains (mousePos) && dragNode < 0 && dragDivider < 0)
            {
                g.setFont (font (10.5f, Weight::medium));
                g.setColour (col::label3);
                g.drawText (formatFreq ((float) freqForX (mousePos.x)), juce::Rectangle<float> (plot.getX() + 14, plot.getBottom() - 26, 200, 18), juce::Justification::centredLeft);
            }
        }

        //==============================================================================
        void showBandMenu (int b, juce::Point<float>)
        {
            const auto& p = proc.band[(size_t) b];
            juce::PopupMenu styles;
            juce::String lastCat;
            for (int i = 0; i < (int) Style::count; ++i)
            {
                const juce::String cat = styleCategory ((Style) i);
                if (cat != lastCat) { styles.addSectionHeader (cat); lastCat = cat; }
                styles.addItem (100 + i, styleName ((Style) i), true, i == (int) p.getStyle());
            }
            juce::PopupMenu place;
            for (int i = 0; i < placementNames().size(); ++i) place.addItem (200 + i, placementNames()[i], true, i == (int) p.placement());

            juce::PopupMenu m;
            m.addSectionHeader ("Band " + juce::String (b + 1) + "  " + formatFreq (proc.bandLow (b)) + " - " + formatFreq (proc.bandHigh (b)));
            m.addSubMenu ("Style", styles);
            m.addSubMenu ("Stereo", place);
            m.addSeparator();
            m.addItem (1, "Solo", true, p.solo->load() > 0.5f);
            m.addItem (2, "Mute", true, p.mute->load() > 0.5f);
            m.addItem (3, "Bypass", true, p.bypass->load() > 0.5f);
            m.addSeparator();
            m.addItem (4, "Split Band Here", proc.getNumBands() < maxBands);
            m.addItem (5, "Merge with Next Band", b < proc.getNumBands() - 1);
            m.addItem (6, "Merge with Previous Band", b > 0);
            const float f = (float) freqForX (mousePos.x);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                             [this, b, f] (int r)
                             {
                                 if (r <= 0) return;
                                 const auto& p = proc.band[(size_t) b];
                                 proc.undo.beginNewTransaction ("Edit band");
                                 if (r >= 100 && r < 200) proc.setParam (bandId (b, "style"), (float) (r - 100));
                                 else if (r >= 200) proc.setParam (bandId (b, "stereo"), (float) (r - 200));
                                 else if (r == 1) proc.setParam (bandId (b, "solo"), p.solo->load() > 0.5f ? 0.0f : 1.0f);
                                 else if (r == 2) proc.setParam (bandId (b, "mute"), p.mute->load() > 0.5f ? 0.0f : 1.0f);
                                 else if (r == 3) proc.setParam (bandId (b, "bypass"), p.bypass->load() > 0.5f ? 0.0f : 1.0f);
                                 else if (r == 4) { const int nb = proc.splitBandAt (f); if (nb >= 0) select (nb); }
                                 else if (r == 5) proc.removeCrossover (b);
                                 else if (r == 6) { proc.removeCrossover (b - 1); select (b - 1); }
                             });
        }

        void showDividerMenu (int i)
        {
            juce::PopupMenu m;
            m.addSectionHeader ("Crossover " + formatFreq (proc.getCrossover (i)));
            m.addItem (1, "Remove (merge bands)");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this).withMousePosition(),
                             [this, i] (int r) { if (r == 1) proc.removeCrossover (i); });
        }

        EmberProcessor& proc;
        juce::Rectangle<float> plot;
        float bottomReserve = 0.0f;
        int selected = 0;
        int numPts = 0;
        std::vector<float> ptFreq, preDb, postDb, fftBuf, scopeTmp;
        std::vector<double> prefix;
        juce::dsp::FFT fft { fftOrder };
        juce::dsp::WindowingFunction<float> window { (size_t) fftSize, juce::dsp::WindowingFunction<float>::blackmanHarris, false };

        juce::Point<float> mousePos { -1, -1 };
        int hoverNode = -1, hoverDivider = -1, dragNode = -1, dragDivider = -1;
        double dragStart = 0.0;
        juce::uint32 lastWheel = 0;
        std::array<float, maxBands> pulse {}, hoverAmt {};
    };
}
