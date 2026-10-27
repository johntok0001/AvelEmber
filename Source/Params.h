#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/Crossover.h"
#include "DSP/Shapers.h"

namespace avel
{
    enum class Placement { stereo, mid, side, left, right };

    inline juce::StringArray styleNames()
    {
        juce::StringArray s;
        for (int i = 0; i < (int) Style::count; ++i) s.add (styleName ((Style) i));
        return s;
    }

    inline const juce::StringArray& placementNames()
    {
        static const juce::StringArray n { "Stereo", "Mid", "Side", "Left", "Right" };
        return n;
    }

    inline juce::String bandId (int band, const char* name) { return "b" + juce::String (band + 1) + "_" + name; }
    inline juce::String xoId (int i) { return "xo" + juce::String (i + 1); }

    inline juce::NormalisableRange<float> logRange (float lo, float hi)
    {
        return { lo, hi,
                 [] (float s, float e, float v) { return s * std::pow (e / s, v); },
                 [] (float s, float e, float v) { return std::log (v / s) / std::log (e / s); },
                 [] (float s, float e, float v) { return juce::jlimit (s, e, v); } };
    }

    inline juce::String formatFreq (float hz)
    {
        if (hz >= 10000.0f) return juce::String (hz / 1000.0f, 1) + " kHz";
        if (hz >= 1000.0f)  return juce::String (hz / 1000.0f, 2) + " kHz";
        return juce::String (juce::roundToInt (hz)) + " Hz";
    }

    inline juce::String formatDb (float db, int decimals = 1)
    {
        const auto v = std::abs (db) < 0.05f ? 0.0f : db;
        return (v > 0 ? "+" : "") + juce::String (v, decimals) + " dB";
    }

    inline juce::String formatPct (float v) { return juce::String (juce::roundToInt (v)) + " %"; }

    constexpr float defaultCrossovers[maxCrossovers] { 120.0f, 700.0f, 2500.0f, 6000.0f, 12000.0f };

    inline juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout layout;
        auto dbAttr = [] (int d) { return AudioParameterFloatAttributes().withStringFromValueFunction ([d] (float v, int) { return formatDb (v, d); }); };
        auto pctAttr = AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatPct (v); });

        layout.add (std::make_unique<AudioParameterInt> (ParameterID { "numBands", 1 }, "Bands", 1, maxBands, 1));

        for (int i = 0; i < maxCrossovers; ++i)
            layout.add (std::make_unique<AudioParameterFloat> (ParameterID { xoId (i), 1 }, "Crossover " + String (i + 1),
                logRange (20.0f, 20000.0f), defaultCrossovers[i],
                AudioParameterFloatAttributes().withStringFromValueFunction ([] (float v, int) { return formatFreq (v); })));

        for (int b = 0; b < maxBands; ++b)
        {
            auto group = std::make_unique<AudioProcessorParameterGroup> ("band" + String (b + 1), "Band " + String (b + 1), " | ");
            const String n = "Band " + String (b + 1) + " ";
            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "style"), 1 }, n + "Style", styleNames(), (int) Style::warmTube));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "drive"), 1 }, n + "Drive",
                NormalisableRange<float> (0.0f, 36.0f, 0.01f), 6.0f, dbAttr (1)));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "dyn"), 1 }, n + "Dynamics",
                NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, pctAttr));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "tone"), 1 }, n + "Tone",
                NormalisableRange<float> (-100.0f, 100.0f, 0.1f), 0.0f, pctAttr));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "fb"), 1 }, n + "Feedback",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), 0.0f, pctAttr));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "mix"), 1 }, n + "Mix",
                NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pctAttr));
            group->addChild (std::make_unique<AudioParameterFloat> (ParameterID { bandId (b, "level"), 1 }, n + "Level",
                NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dbAttr (1)));
            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "stereo"), 1 }, n + "Stereo", placementNames(), 0));
            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "mute"), 1 }, n + "Mute", StringArray { "Off", "On" }, 0));
            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "solo"), 1 }, n + "Solo", StringArray { "Off", "On" }, 0));
            group->addChild (std::make_unique<AudioParameterChoice> (ParameterID { bandId (b, "bypass"), 1 }, n + "Bypass", StringArray { "Off", "On" }, 0));
            layout.add (std::move (group));
        }

        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "input", 1 }, "Input", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dbAttr (1)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "output", 1 }, "Output", NormalisableRange<float> (-24.0f, 24.0f, 0.01f), 0.0f, dbAttr (1)));
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { "mix", 1 }, "Mix", NormalisableRange<float> (0.0f, 100.0f, 0.1f), 100.0f, pctAttr));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "os", 1 }, "Oversampling", StringArray { "Off", "2x", "4x", "8x" }, 1));
        layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "autoGain", 1 }, "Auto Gain", StringArray { "Off", "On" }, 1));
        return layout;
    }

    struct BandParams
    {
        std::atomic<float>* style = nullptr;
        std::atomic<float>* drive = nullptr;
        std::atomic<float>* dyn = nullptr;
        std::atomic<float>* tone = nullptr;
        std::atomic<float>* fb = nullptr;
        std::atomic<float>* mix = nullptr;
        std::atomic<float>* level = nullptr;
        std::atomic<float>* stereo = nullptr;
        std::atomic<float>* mute = nullptr;
        std::atomic<float>* solo = nullptr;
        std::atomic<float>* bypass = nullptr;

        void attach (juce::AudioProcessorValueTreeState& s, int b)
        {
            style = s.getRawParameterValue (bandId (b, "style"));
            drive = s.getRawParameterValue (bandId (b, "drive"));
            dyn = s.getRawParameterValue (bandId (b, "dyn"));
            tone = s.getRawParameterValue (bandId (b, "tone"));
            fb = s.getRawParameterValue (bandId (b, "fb"));
            mix = s.getRawParameterValue (bandId (b, "mix"));
            level = s.getRawParameterValue (bandId (b, "level"));
            stereo = s.getRawParameterValue (bandId (b, "stereo"));
            mute = s.getRawParameterValue (bandId (b, "mute"));
            solo = s.getRawParameterValue (bandId (b, "solo"));
            bypass = s.getRawParameterValue (bandId (b, "bypass"));
        }

        Style getStyle() const noexcept { return (Style) juce::jlimit (0, (int) Style::count - 1, (int) style->load()); }
        Placement placement() const noexcept { return (Placement) juce::jlimit (0, 4, (int) stereo->load()); }
    };
}
