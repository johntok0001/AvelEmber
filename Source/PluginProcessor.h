#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "Params.h"
#include "DSP/Crossover.h"
#include "DSP/Shapers.h"
#include "DSP/ScopeRing.h"

class EmberProcessor : public juce::AudioProcessor,
                       private juce::AudioProcessorValueTreeState::Listener,
                       private juce::AsyncUpdater
{
public:
    EmberProcessor();
    ~EmberProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Avel Ember"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    //==============================================================================
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState apvts;
    std::array<avel::BandParams, avel::maxBands> band;

    int getNumBands() const noexcept { return juce::jlimit (1, avel::maxBands, (int) numBandsParam->load()); }
    float getCrossover (int i) const noexcept { return xoParam[(size_t) i]->load(); }
    // lower / upper edge of a band in Hz
    float bandLow (int b) const noexcept { return b == 0 ? 20.0f : getCrossover (b - 1); }
    float bandHigh (int b) const noexcept { return b >= getNumBands() - 1 ? 20000.0f : getCrossover (b); }

    // editing (message thread, one undo step each)
    int splitBandAt (float freq);
    void removeCrossover (int index);
    void setCrossoverFreq (int index, float freq);
    void setParam (const juce::String& id, float plainValue);
    juce::RangedAudioParameter* param (const juce::String& id) const { return apvts.getParameter (id); }

    // presets / A-B
    static juce::StringArray getFactoryPresetNames();
    void loadFactoryPreset (int index);
    static juce::File getUserPresetFolder();
    bool saveUserPreset (const juce::File&);
    bool loadUserPreset (const juce::File&);
    juce::String currentPresetName { "Default" };
    int getABSlot() const { return abSlot; }
    void switchAB (int slot);
    void copyABToOther();

    // UI feedback
    float getBandLevel (int b) const noexcept { return bandLevel[(size_t) b].load(); }
    float takePeak (int which, int ch) noexcept { return peaks[(size_t) (which * 2 + ch)].exchange (0.0f); }
    avel::ScopeRing preScope, postScope;
    juce::ValueTree uiState { "UI" };

private:
    struct Smooth
    {
        double cur = 0.0;
        inline double next (double target, double c) noexcept { cur += c * (target - cur); return cur; }
    };

    struct BandRuntime
    {
        std::array<avel::StyleState, 2> st;
        std::array<avel::OnePole, 2> tonePre, tonePost;
        std::array<avel::SVF, 2> fbFilter;
        std::array<double, 2> fbState { 0.0, 0.0 };
        double env = 0.0, inPow = 1.0e-6, outPow = 1.0e-6, autoG = 1.0, dynGain = 1.0;
        Smooth drive, mix, level, tone, fb, dyn, staticG;
        bool initialised = false;

        void prepare (double sr) noexcept
        {
            for (auto& s : st) s.prepare (sr);
            for (auto& t : tonePre) { t.setFreq (1000.0, sr); t.reset(); }
            for (auto& t : tonePost) { t.setFreq (1000.0, sr); t.reset(); }
            for (auto& f : fbFilter) { f.k = 1.0 / 1.5; f.reset(); }
            fbState = { 0.0, 0.0 };
            env = 0.0; inPow = outPow = 1.0e-6; autoG = 1.0; dynGain = 1.0; staticG.cur = 0.0;
            initialised = false;
        }
    };

    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void prepareStates (double rate);
    void processBands (float* L, float* R, int n, bool stereo, double rate);
    int currentLatency() const;

    std::atomic<float>* numBandsParam = nullptr;
    std::array<std::atomic<float>*, avel::maxCrossovers> xoParam {};
    std::atomic<float>* inputParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* osParam = nullptr;
    std::atomic<float>* autoGainParam = nullptr;

    double sr = 44100.0;
    int maxBlock = 512;
    int osIndex = -1;
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 4> oversamplers;

    std::array<avel::MultibandSplitter, 2> splitter;
    std::array<double, avel::maxCrossovers> xoSmoothed {};
    int lastNumBands = -1;
    std::array<BandRuntime, avel::maxBands> rt;
    std::array<std::array<std::vector<float>, 2>, avel::maxBands> bandBuf;

    // latency-compensated dry path for the global mix
    std::array<std::vector<float>, 2> dryRing;
    int dryPos = 0;
    Smooth inGain, outGain, globalMix;

    std::array<std::atomic<float>, avel::maxBands> bandLevel {};
    std::array<std::atomic<float>, 4> peaks {};

    int abSlot = 0;
    juce::ValueTree abStates[2];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EmberProcessor)
};
