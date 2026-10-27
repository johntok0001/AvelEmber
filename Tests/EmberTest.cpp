// Offline tests for Avel Ember.
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <iostream>

using namespace avel;

static int failures = 0;
static void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << std::endl;
    if (! ok) ++failures;
}
static void pump (int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

struct Rig
{
    EmberProcessor p;
    double sr = 48000.0;
    int block = 512;
    Rig (int os = 0)
    {
        set ("os", (float) os);
        p.setRateAndBufferSizeDetails (sr, block);
        p.prepareToPlay (sr, block);
    }
    void set (const juce::String& id, float v) { p.setParam (id, v); }
    juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& in)
    {
        juce::AudioBuffer<float> out (in);
        juce::MidiBuffer m;
        for (int pos = 0; pos < out.getNumSamples(); pos += block)
        {
            const int n = juce::jmin (block, out.getNumSamples() - pos);
            juce::AudioBuffer<float> v (out.getArrayOfWritePointers(), 2, pos, n);
            p.processBlock (v, m);
        }
        return out;
    }
};

static juce::AudioBuffer<float> sine (double sr, double secs, double hz, float amp)
{
    juce::AudioBuffer<float> b (2, (int) (sr * secs));
    for (int i = 0; i < b.getNumSamples(); ++i)
    {
        const float v = amp * (float) std::sin (2.0 * 3.14159265358979 * hz * i / sr);
        b.setSample (0, i, v); b.setSample (1, i, v);
    }
    return b;
}

static double rmsDb (const juce::AudioBuffer<float>& b, int ch, int from, int to = -1)
{
    if (to < 0) to = b.getNumSamples();
    double s = 0;
    for (int i = from; i < to; ++i) s += (double) b.getSample (ch, i) * b.getSample (ch, i);
    return 10.0 * std::log10 (s / (to - from) + 1e-30);
}

// amplitude (dB re full scale sine) of one frequency via a single-bin DFT with a Hann window
static double toneDb (const juce::AudioBuffer<float>& b, double hz, double sr, int from, int len)
{
    double re = 0, im = 0, wsum = 0;
    for (int i = 0; i < len; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (2.0 * 3.14159265358979 * i / (len - 1));
        const double x = b.getSample (0, from + i) * w;
        const double ph = 2.0 * 3.14159265358979 * hz * (from + i) / sr;
        re += x * std::cos (ph); im -= x * std::sin (ph);
        wsum += w;
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / wsum + 1e-20);
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::String shotDir = argc > 1 ? argv[1] : juce::File::getCurrentWorkingDirectory().getFullPathName();

    std::cout << "Crossovers" << std::endl;
    for (int os : { 0, 2 })
    {
        Rig r (os);
        r.set ("numBands", 6);
        for (int b = 0; b < 6; ++b) r.set (bandId (b, "bypass"), 1);
        double worst = 0;
        for (double f : { 40.0, 118.0, 300.0, 700.0, 1500.0, 2600.0, 6000.0, 11000.0, 16000.0 })
        {
            auto in = sine (r.sr, 0.5, f, 0.3f);
            auto out = r.run (in);
            worst = juce::jmax (worst, std::abs (rmsDb (out, 0, 12000) - rmsDb (in, 0, 12000)));
        }
        check (worst < 0.05, "6 bypassed bands sum flat (os " + juce::String (1 << os) + "x): worst " + juce::String (worst, 3) + " dB");
    }

    std::cout << "Saturation" << std::endl;
    {
        Rig r (2);
        r.set (bandId (0, "style"), (float) Style::warmTube);
        r.set (bandId (0, "drive"), 18);
        r.set ("autoGain", 0);
        auto in = sine (r.sr, 1.0, 1000.0, 0.3f);
        auto out = r.run (in);
        const double h1 = toneDb (out, 1000, r.sr, 24000, 16384), h2 = toneDb (out, 2000, r.sr, 24000, 16384), h3 = toneDb (out, 3000, r.sr, 24000, 16384);
        check (h2 - h1 > -30.0, "Warm Tube makes even harmonics: H2 " + juce::String (h2 - h1, 1) + " dBc, H3 " + juce::String (h3 - h1, 1) + " dBc");

        r.set (bandId (0, "style"), (float) Style::clean);
        out = r.run (in); out = r.run (in);
        const double c1 = toneDb (out, 1000, r.sr, 24000, 16384), c2 = toneDb (out, 2000, r.sr, 24000, 16384), c3 = toneDb (out, 3000, r.sr, 24000, 16384);
        check (c2 - c1 < -60.0 && c3 - c1 > -30.0, "Clean (symmetric) makes odd harmonics only: H2 " + juce::String (c2 - c1, 1) + " / H3 " + juce::String (c3 - c1, 1) + " dBc");

        // every style is stable and bounded
        bool ok = true;
        for (int s = 0; s < (int) Style::count; ++s)
        {
            r.set (bandId (0, "style"), (float) s);
            r.set (bandId (0, "drive"), 36);
            r.set (bandId (0, "fb"), 100);
            auto o = r.run (sine (r.sr, 0.3, 220.0, 0.9f));
            const float mag = o.getMagnitude (0, o.getNumSamples());
            if (! std::isfinite (mag) || mag > 8.0f) { ok = false; std::cout << "    style " << styleName ((Style) s) << " peak " << mag << std::endl; }
        }
        check (ok, "all 17 styles stay finite and bounded at max drive + feedback");
        r.set (bandId (0, "fb"), 0);
    }

    std::cout << "Oversampling" << std::endl;
    {
        // hard clip of a 7 kHz tone: aliases land between harmonics; oversampling must reduce them
        auto aliasDb = [] (int os)
        {
            Rig r (os);
            r.set (bandId (0, "style"), (float) Style::hardClip);
            r.set (bandId (0, "drive"), 24);
            r.set ("autoGain", 0);
            auto out = r.run (sine (r.sr, 1.0, 7000.0, 0.5f));
            // energy of non-harmonic components: sum a few alias frequencies of 7k*k folded around 24k
            double worst = -200;
            for (int k = 2; k <= 15; ++k)
            {
                double f = 7000.0 * k;
                while (f > 24000.0) f = std::abs (48000.0 - f);
                if (std::fmod (f, 7000.0) < 50.0 || 7000.0 - std::fmod (f, 7000.0) < 50.0) continue;
                worst = juce::jmax (worst, toneDb (out, f, 48000.0, 20000, 16384));
            }
            return worst;
        };
        const double a1 = aliasDb (0), a8 = aliasDb (3);
        check (a8 < a1 - 20.0, "8x oversampling cuts aliasing: " + juce::String (a1, 1) + " dB -> " + juce::String (a8, 1) + " dB");

        Rig r (2);
        check (r.p.getLatencySamples() > 0, "oversampling reports latency: " + juce::String (r.p.getLatencySamples()) + " samples");

        // parallel mix is phase aligned: bypassed band + 50 % mix = delayed input
        r.set (bandId (0, "bypass"), 1);
        r.set ("mix", 50);
        auto in = sine (r.sr, 1.0, 3000.0, 0.3f);
        auto out = r.run (in);
        check (std::abs (rmsDb (out, 0, 24000) - rmsDb (in, 0, 24000)) < 0.1, "dry/wet mix with oversampling has no comb filtering");
    }

    std::cout << "Auto gain / bands" << std::endl;
    {
        Rig r (1);
        r.set (bandId (0, "style"), (float) Style::heavyTube);
        r.set (bandId (0, "drive"), 30);
        auto in = sine (r.sr, 2.0, 500.0, 0.2f);
        auto out = r.run (in);
        const double d = rmsDb (out, 0, 48000) - rmsDb (in, 0, 48000);
        check (std::abs (d) < 2.0, "auto gain keeps loudness at +30 dB drive: " + juce::String (d, 2) + " dB");
        {
            // no loudness jump right after inserting the plugin
            Rig f (1);
            f.set (bandId (0, "drive"), 24);
            auto o = f.run (sine (f.sr, 0.3, 500.0, 0.2f));
            const double first = rmsDb (o, 0, 0, 4800), ref = rmsDb (sine (f.sr, 0.3, 500.0, 0.2f), 0, 0, 4800);
            check (std::abs (first - ref) < 2.5, "first 100 ms after insert are not louder: " + juce::String (first - ref, 2) + " dB");
        }

        r.set ("numBands", 2); r.set (xoId (0), 1000); r.set (bandId (1, "mute"), 1);
        r.set (bandId (0, "bypass"), 1);
        r.run (sine (r.sr, 0.3, 100, 0));
        auto lo = r.run (sine (r.sr, 0.5, 100.0, 0.3f)), hi = r.run (sine (r.sr, 0.5, 8000.0, 0.3f));
        check (std::abs (rmsDb (lo, 0, 12000) + 13.5) < 0.6 && rmsDb (hi, 0, 12000) < -60.0, "muting the upper band removes 8 kHz, keeps 100 Hz");

        r.set (bandId (1, "mute"), 0); r.set (bandId (1, "stereo"), (float) Placement::side); r.set (bandId (1, "drive"), 30);
        auto mono = sine (r.sr, 0.5, 4000.0, 0.3f);
        auto o = r.run (mono); o = r.run (mono);
        double err = 0; for (int i = 12000; i < o.getNumSamples(); ++i) err = juce::jmax (err, (double) std::abs (o.getSample (0, i) - o.getSample (1, i)));
        check (err < 1e-4 && std::abs (rmsDb (o, 0, 12000) - rmsDb (mono, 0, 12000)) < 0.2, "Side band leaves a mono signal untouched");
    }

    std::cout << "Editing" << std::endl;
    {
        Rig r;
        r.set (bandId (0, "style"), (float) Style::tape);
        r.set (bandId (0, "drive"), 11);
        const int b = r.p.splitBandAt (1000.0f);
        check (b == 1 && r.p.getNumBands() == 2 && std::abs (r.p.getCrossover (0) - 1000.0f) < 1.0f, "split at 1 kHz creates band 2");
        check (r.p.band[1].getStyle() == Style::tape && std::abs (r.p.band[1].drive->load() - 11.0f) < 0.01f, "new band inherits the settings");
        r.p.splitBandAt (200.0f);
        check (r.p.getNumBands() == 3 && std::abs (r.p.getCrossover (0) - 200.0f) < 1.0f && std::abs (r.p.getCrossover (1) - 1000.0f) < 1.0f, "split below keeps crossovers sorted");
        r.set (bandId (2, "drive"), 3.0f);
        r.p.removeCrossover (0);
        check (r.p.getNumBands() == 2 && std::abs (r.p.getCrossover (0) - 1000.0f) < 1.0f && std::abs (r.p.band[1].drive->load() - 3.0f) < 0.01f, "merge shifts the upper bands down");

        juce::MemoryBlock mb;
        r.p.getStateInformation (mb);
        EmberProcessor q;
        q.setStateInformation (mb.getData(), (int) mb.getSize());
        check (q.getNumBands() == 2 && q.band[0].getStyle() == Style::tape, "state round trip");

        r.p.loadFactoryPreset (5);
        check (r.p.getNumBands() == 4, "preset 'Vocal Presence' has 4 bands");
    }

    std::cout << "Performance" << std::endl;
    {
        for (int os : { 0, 2, 3 })
        {
            Rig r (os);
            r.p.loadFactoryPreset (5);
            r.set ("numBands", 6);
            auto in = sine (r.sr, 10.0, 440.0, 0.3f);
            {
                Rig one (os);
                one.set (bandId (0, "bypass"), 1);
                const auto t1 = juce::Time::getMillisecondCounterHiRes();
                one.run (in);
                std::cout << "    1 bypassed band (oversampling only) " << (1 << os) << "x: " << juce::String (100.0 * (juce::Time::getMillisecondCounterHiRes() - t1) / 10000.0, 2) << " % CPU" << std::endl;
            }
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            r.run (in);
            const double ms = juce::Time::getMillisecondCounterHiRes() - t0;
            std::cout << "    6 bands, oversampling " << (1 << os) << "x: " << juce::String (100.0 * ms / 10000.0, 2) << " % CPU" << std::endl;
        }
    }

    std::cout << "UI snapshots" << std::endl;
    {
        auto shot = [&] (int preset, int sel, const juce::String& name)
        {
            Rig r (2);
            r.p.loadFactoryPreset (preset);
            std::unique_ptr<juce::AudioProcessorEditor> ed (r.p.createEditor());
            ed->setSize (1180, 720);
            dynamic_cast<EmberEditor*> (ed.get())->getGraph().select (sel);
            juce::Random rnd (1);
            juce::MidiBuffer m;
            juce::AudioBuffer<float> buf (2, 1024);
            float b0 = 0, b1 = 0, b2 = 0; double ph = 0;
            for (int f = 0; f < 50; ++f)
            {
                for (int i = 0; i < 1024; ++i)
                {
                    const float w = rnd.nextFloat() * 2 - 1;
                    b0 = 0.99765f * b0 + w * 0.0990460f; b1 = 0.96300f * b1 + w * 0.2965164f; b2 = 0.57000f * b2 + w * 1.0526913f;
                    ph += 2 * 3.14159265 * 110.0 / 48000.0;
                    const float v = 0.05f * (b0 + b1 + b2 + w * 0.18f) + 0.25f * (float) std::sin (ph);
                    buf.setSample (0, i, v); buf.setSample (1, i, v * 0.9f);
                }
                r.p.processBlock (buf, m);
                pump (17);
            }
            auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
            juce::File out (shotDir + "/" + name); out.deleteFile();
            juce::FileOutputStream fos (out); juce::PNGImageFormat().writeImageToStream (img, fos);
        };
        shot (0, 0, "ember_default.png");
        shot (5, 2, "ember_bands.png");
        shot (6, 1, "ember_amp.png");
        check (true, "snapshots written");
    }

    std::cout << (failures == 0 ? "ALL TESTS PASSED" : juce::String (failures) + " TEST(S) FAILED") << std::endl;
    return failures == 0 ? 0 : 1;
}
