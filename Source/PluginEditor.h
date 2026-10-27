#pragma once

#include "PluginProcessor.h"
#include "UI/Theme.h"
#include "UI/BandGraph.h"
#include "UI/BandPanel.h"

namespace avel::ui
{
    // Input / output meters with Input, Mix and Output dials
    class IOColumn : public juce::Component
    {
    public:
        explicit IOColumn (EmberProcessor& p) : proc (p)
        {
            input.setFormatter ([] (float v) { return formatDb (v); });
            output.setFormatter ([] (float v) { return formatDb (v); });
            mix.setFormatter ([] (float v) { return formatPct (v); });
            input.bind (proc.param ("input"), &proc.undo);
            mix.bind (proc.param ("mix"), &proc.undo);
            output.bind (proc.param ("output"), &proc.undo);
            for (auto* d : { &input, &mix, &output }) { d->setAccent (col::label); addAndMakeVisible (*d); }
        }

        void tick()
        {
            for (int w = 0; w < 2; ++w)
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float pk = proc.takePeak (w, ch);
                    const float db = pk > 1.0e-6f ? 20.0f * std::log10 (pk) : -100.0f;
                    float& l = level[w][ch];
                    l = db > l ? db : juce::jmax (db, l - 0.9f);
                }
            repaint (meterArea);
        }

        void paint (juce::Graphics& g) override
        {
            auto r = meterArea.toFloat();
            auto toY = [&] (float db) { return r.getBottom() - juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 66.0f) * r.getHeight(); };
            const float w = 4.0f, gap = 3.0f;
            for (int which = 0; which < 2; ++which)
            {
                const float x0 = r.getX() + which * (2 * w + gap + 12);
                for (int ch = 0; ch < 2; ++ch)
                {
                    auto bar = juce::Rectangle<float> (x0 + ch * (w + gap), r.getY(), w, r.getHeight());
                    g.setColour (juce::Colours::white.withAlpha (0.06f));
                    g.fillRoundedRectangle (bar, 2.0f);
                    g.setGradientFill (juce::ColourGradient (col::red, 0, toY (3.0f), which == 0 ? col::label2 : col::green, 0, toY (-30.0f), false));
                    g.fillRoundedRectangle (bar.withTop (toY (level[which][ch])), 2.0f);
                }
                g.setColour (col::label3);
                g.setFont (font (9.5f, Weight::semibold, 0.06f));
                g.drawText (which == 0 ? "IN" : "OUT", juce::Rectangle<float> (x0 - 6, r.getY() - 18, 2 * w + gap + 12, 12), juce::Justification::centred);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            output.setBounds (r.removeFromBottom (92));
            r.removeFromBottom (8);
            mix.setBounds (r.removeFromBottom (92));
            r.removeFromBottom (8);
            input.setBounds (r.removeFromBottom (92));
            r.removeFromBottom (16);
            meterArea = r.withTrimmedTop (30).withSizeKeepingCentre (2 * (2 * 4 + 3) + 12, r.getHeight() - 30);
        }

    private:
        EmberProcessor& proc;
        Dial input { "Input", true }, mix { "Mix" }, output { "Output", true };
        juce::Rectangle<int> meterArea;
        float level[2][2] { { -100, -100 }, { -100, -100 } };
    };
}

class EmberEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit EmberEditor (EmberProcessor&);
    ~EmberEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress& k) override { return graph.keyPressed (k); }

    avel::ui::BandGraph& getGraph() noexcept { return graph; }

private:
    void timerCallback() override;
    void showPresetMenu();

    EmberProcessor& proc;
    avel::ui::LookAndFeel lnf;
    avel::ui::BandGraph graph;
    avel::ui::BandPanel panel;
    avel::ui::IOColumn io;

    avel::ui::IconButton presetBtn, undoBtn, redoBtn, abCopyBtn;
    avel::ui::Segmented osSeg, abSeg;
    avel::ui::Switch autoGainSwitch;
    juce::Rectangle<int> logoArea, autoGainLabel, osLabel, graphArea;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::TooltipWindow tooltips { this, 600 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberEditor)
};
