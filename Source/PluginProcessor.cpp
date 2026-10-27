#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace avel;

EmberProcessor::EmberProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undo, "PARAMS", createLayout())
{
    for (int b = 0; b < maxBands; ++b)
    {
        band[(size_t) b].attach (apvts, b);
        bandLevel[(size_t) b].store (-100.0f);
    }
    for (auto& p : peaks) p.store (0.0f);

    numBandsParam = apvts.getRawParameterValue ("numBands");
    for (int i = 0; i < maxCrossovers; ++i) xoParam[(size_t) i] = apvts.getRawParameterValue (xoId (i));
    inputParam = apvts.getRawParameterValue ("input");
    outputParam = apvts.getRawParameterValue ("output");
    mixParam = apvts.getRawParameterValue ("mix");
    osParam = apvts.getRawParameterValue ("os");
    autoGainParam = apvts.getRawParameterValue ("autoGain");
    apvts.addParameterListener ("os", this);

    uiState.setProperty ("width", 1180, nullptr);
    uiState.setProperty ("height", 720, nullptr);
    uiState.setProperty ("selected", 0, nullptr);
}

EmberProcessor::~EmberProcessor()
{
    cancelPendingUpdate();
    apvts.removeParameterListener ("os", this);
}

bool EmberProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != juce::AudioChannelSet::stereo() && out != juce::AudioChannelSet::mono()) return false;
    return layouts.getMainInputChannelSet() == out;
}

//==============================================================================
void EmberProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    sr = sampleRate > 0 ? sampleRate : 44100.0;
    maxBlock = juce::jmax (32, samplesPerBlock);

    for (int i = 1; i < 4; ++i)
    {
        oversamplers[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>> (
            2, (size_t) i, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, true);
        oversamplers[(size_t) i]->initProcessing ((size_t) maxBlock);
    }

    for (auto& b : bandBuf)
        for (auto& ch : b) ch.assign ((size_t) maxBlock * 8, 0.0f);

    for (auto& d : dryRing) d.assign (8192, 0.0f);
    dryPos = 0;

    osIndex = -1; // forces prepareStates on the first block
    inGain.cur = juce::Decibels::decibelsToGain ((double) inputParam->load());
    outGain.cur = juce::Decibels::decibelsToGain ((double) outputParam->load());
    globalMix.cur = mixParam->load() / 100.0;
    setLatencySamples (currentLatency());
}

int EmberProcessor::currentLatency() const
{
    const int os = juce::jlimit (0, 3, (int) osParam->load());
    if (os == 0 || oversamplers[(size_t) os] == nullptr) return 0;
    return juce::roundToInt (oversamplers[(size_t) os]->getLatencyInSamples());
}

void EmberProcessor::handleAsyncUpdate()
{
    setLatencySamples (currentLatency());
}

void EmberProcessor::prepareStates (double rate)
{
    for (auto& s : splitter) s.prepare (rate);
    for (auto& r : rt) r.prepare (rate);
    for (int i = 0; i < maxCrossovers; ++i) xoSmoothed[(size_t) i] = xoParam[(size_t) i]->load();
    lastNumBands = -1;
    for (auto& o : oversamplers) if (o) o->reset();
}

//==============================================================================
void EmberProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin (2, buffer.getNumChannels());
    if (n == 0 || numCh == 0) return;
    for (int ch = numCh; ch < buffer.getNumChannels(); ++ch) buffer.clear (ch, 0, n);
    const bool stereo = numCh > 1;

    // very large host blocks: process in chunks
    if (n > maxBlock)
    {
        juce::MidiBuffer dummy;
        for (int pos = 0; pos < n; pos += maxBlock)
        {
            const int m = juce::jmin (maxBlock, n - pos);
            juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), numCh, pos, m);
            processBlock (part, dummy);
        }
        return;
    }

    const int os = juce::jlimit (0, 3, (int) osParam->load());
    const double rate = sr * (1 << os);
    if (os != osIndex)
    {
        osIndex = os;
        prepareStates (rate);
    }

    float* L = buffer.getWritePointer (0);
    float* R = stereo ? buffer.getWritePointer (1) : nullptr;

    // input gain, dry copy, input metering
    const double cIn = 1.0 - std::exp (-1.0 / (0.02 * sr));
    const double inTarget = juce::Decibels::decibelsToGain ((double) inputParam->load());
    float inPeak[2] { 0, 0 };
    const int ringMask = (int) dryRing[0].size() - 1;
    for (int i = 0; i < n; ++i)
    {
        const float g = (float) inGain.next (inTarget, cIn);
        L[i] *= g;
        if (stereo) R[i] *= g;
        const int w = (dryPos + i) & ringMask;
        dryRing[0][(size_t) w] = L[i];
        dryRing[1][(size_t) w] = stereo ? R[i] : L[i];
        inPeak[0] = juce::jmax (inPeak[0], std::abs (L[i]));
        inPeak[1] = juce::jmax (inPeak[1], std::abs (stereo ? R[i] : L[i]));
    }
    preScope.push (L, R, n);

    // up-sample, saturate in bands, down-sample
    if (os == 0)
    {
        processBands (L, R, n, stereo, rate);
    }
    else
    {
        auto& ovs = *oversamplers[(size_t) os];
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), 2, (size_t) n);
        if (! stereo)
        {
            // the oversampler is set up for two channels: run the mono signal on both
            juce::AudioBuffer<float> tmp (2, n);
            tmp.copyFrom (0, 0, L, n);
            tmp.copyFrom (1, 0, L, n);
            juce::dsp::AudioBlock<float> tb (tmp);
            auto up = ovs.processSamplesUp (tb);
            processBands (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples(), true, rate);
            ovs.processSamplesDown (tb);
            juce::FloatVectorOperations::copy (L, tmp.getReadPointer (0), n);
        }
        else
        {
            auto up = ovs.processSamplesUp (block);
            processBands (up.getChannelPointer (0), up.getChannelPointer (1), (int) up.getNumSamples(), true, rate);
            ovs.processSamplesDown (block);
        }
    }

    // global mix against the latency-aligned dry signal, output gain
    const int lat = os == 0 ? 0 : juce::roundToInt (oversamplers[(size_t) os]->getLatencyInSamples());
    const double outTarget = juce::Decibels::decibelsToGain ((double) outputParam->load());
    const double mixTarget = mixParam->load() / 100.0;
    float outPk[2] { 0, 0 };
    for (int i = 0; i < n; ++i)
    {
        const int r = (dryPos + i - lat) & ringMask;
        const double m = globalMix.next (mixTarget, cIn);
        const double g = outGain.next (outTarget, cIn);
        L[i] = (float) ((dryRing[0][(size_t) r] + m * (L[i] - dryRing[0][(size_t) r])) * g);
        if (stereo) R[i] = (float) ((dryRing[1][(size_t) r] + m * (R[i] - dryRing[1][(size_t) r])) * g);
        outPk[0] = juce::jmax (outPk[0], std::abs (L[i]));
        outPk[1] = juce::jmax (outPk[1], std::abs (stereo ? R[i] : L[i]));
    }
    dryPos = (dryPos + n) & ringMask;
    postScope.push (L, R, n);

    auto storeMax = [] (std::atomic<float>& a, float v) { auto p = a.load(); while (v > p && ! a.compare_exchange_weak (p, v)) {} };
    storeMax (peaks[0], inPeak[0]); storeMax (peaks[1], inPeak[1]);
    storeMax (peaks[2], outPk[0]);  storeMax (peaks[3], outPk[1]);
}

void EmberProcessor::processBands (float* L, float* R, int n, bool stereo, double rate)
{
    const int nb = getNumBands();

    // crossovers: sorted, at least 1/6 octave apart, smoothed (~30 ms per block)
    double xo[maxCrossovers];
    const double cx = 1.0 - std::exp (-(double) n / (0.03 * rate));
    double lo = 20.0;
    for (int i = 0; i < nb - 1; ++i)
    {
        double f = juce::jlimit (lo * 1.12, 20000.0, (double) xoParam[(size_t) i]->load());
        auto& s = xoSmoothed[(size_t) i];
        s = std::exp (std::log (s) + (std::log (f) - std::log (s)) * cx);
        xo[i] = s;
        lo = f;
    }

    if (nb != lastNumBands)
    {
        for (auto& sp : splitter) sp.reset();
        for (int b = 0; b < maxBands; ++b) if (b >= lastNumBands) rt[(size_t) b].initialised = false;
        lastNumBands = nb;
    }
    for (auto& sp : splitter) sp.setCrossovers (nb, xo);

    // split
    double out[maxBands];
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* x = ch == 0 ? L : R;
        for (int i = 0; i < n; ++i)
        {
            splitter[(size_t) ch].process (x[i], out);
            for (int b = 0; b < nb; ++b) bandBuf[(size_t) b][(size_t) ch][(size_t) i] = (float) out[b];
        }
        if (! stereo) break;
    }

    bool anySolo = false;
    for (int b = 0; b < nb; ++b) anySolo |= band[(size_t) b].solo->load() > 0.5f;

    const bool autoGain = autoGainParam->load() > 0.5f;
    const double cs = 1.0 - std::exp (-1.0 / (0.02 * rate));        // parameter smoothing
    const double atk = 1.0 - std::exp (-1.0 / (0.005 * rate));      // dynamics detector
    const double rel = 1.0 - std::exp (-1.0 / (0.08 * rate));
    const double cag = 1.0 - std::exp (-1.0 / (0.35 * rate));       // auto gain power averages
    const double cagG = 1.0 - std::exp (-1.0 / (0.15 * rate));

    juce::FloatVectorOperations::clear (L, n);
    if (stereo) juce::FloatVectorOperations::clear (R, n);

    for (int b = 0; b < nb; ++b)
    {
        auto& p = band[(size_t) b];
        auto& r = rt[(size_t) b];
        float* bl = bandBuf[(size_t) b][0].data();
        float* br = stereo ? bandBuf[(size_t) b][1].data() : bl;

        const bool muted = p.mute->load() > 0.5f || (anySolo && p.solo->load() < 0.5f);
        const bool bypassed = p.bypass->load() > 0.5f;
        const Style style = p.getStyle();
        const Placement pl = p.placement();

        const double driveDb = p.drive->load();
        if (! r.initialised)
        {
            r.drive.cur = juce::Decibels::decibelsToGain (driveDb); r.mix.cur = p.mix->load() / 100.0;
            r.level.cur = juce::Decibels::decibelsToGain ((double) p.level->load());
            r.tone.cur = std::pow (10.0, (p.tone->load() / 100.0) * 9.0 / 20.0);
            r.fb.cur = 0.92 * p.fb->load() / 100.0; r.dyn.cur = p.dyn->load() / 100.0;
            r.initialised = true;
        }

        for (auto& s : r.st) s.configure (style);
        const double fbCentre = std::sqrt ((double) bandLow (b) * bandHigh (b));
        for (auto& f : r.fbFilter) f.setFreq (fbCentre, rate);

        float levelPeak = 0.0f;

        // targets in the linear domain; per-sample one-pole smoothing (no exp/log per sample)
        const double driveT = juce::Decibels::decibelsToGain (driveDb);
        const double levelT = juce::Decibels::decibelsToGain ((double) p.level->load());
        const double toneT = std::pow (10.0, (p.tone->load() / 100.0) * 9.0 / 20.0);
        const double mixT = p.mix->load() / 100.0, fbT = 0.92 * p.fb->load() / 100.0, dynT = p.dyn->load() / 100.0;
        const double drive01 = driveDb / 36.0;
        // small-signal gain of the curve at about -18 dBFS, inverted
        const double probe = 0.125;
        const double staticT = juce::jlimit (0.02, 4.0, probe / juce::jmax (1.0e-4, std::abs (staticCurve (style, probe * driveT, drive01))));
        if (r.staticG.cur <= 0.0) r.staticG.cur = autoGain ? staticT : 1.0;
        double gd = r.dynGain;
        // skip the tone and feedback filters while they are neutral
        const bool toneOn = std::abs (toneT - 1.0) > 1.0e-9 || std::abs (r.tone.cur - 1.0) > 1.0e-5;
        const bool fbOn = fbT > 0.0 || r.fb.cur > 1.0e-5;
        if (! toneOn) for (int ch = 0; ch < 2; ++ch) { r.tonePre[(size_t) ch].z = 0.0; r.tonePost[(size_t) ch].z = 0.0; }
        if (! fbOn) { r.fbState = { 0.0, 0.0 }; for (auto& f : r.fbFilter) f.reset(); }

        for (int i = 0; i < n; ++i)
        {
            const double drive = r.drive.next (driveT, cs);
            const double mix = r.mix.next (mixT, cs);
            const double lvl = r.level.next (levelT, cs);
            const double tG = r.tone.next (toneT, cs);
            const double fbAmt = r.fb.next (fbT, cs);
            const double dyn = r.dyn.next (dynT, cs);

            const double xl = bl[i], xr = br[i];
            levelPeak = juce::jmax (levelPeak, (float) juce::jmax (std::abs (xl), std::abs (xr)));

            if (muted) { bl[i] = 0.0f; if (stereo) br[i] = 0.0f; continue; }
            if (bypassed) { L[i] += (float) xl; if (stereo) R[i] += (float) xr; continue; }

            // signals this band actually processes
            double a = xl, c = xr;
            int chans = 2;
            switch (pl)
            {
                case Placement::stereo: chans = stereo ? 2 : 1; break;
                case Placement::mid:    a = stereo ? 0.5 * (xl + xr) : xl; chans = 1; break;
                case Placement::side:   a = 0.5 * (xl - xr); chans = stereo ? 1 : 0; break;
                case Placement::left:   chans = 1; break;
                case Placement::right:  a = xr; chans = stereo ? 1 : 0; break;
            }

            // dynamics: level-dependent drive (positive = more even, negative = more dynamic)
            const double pw = 0.5 * (xl * xl + xr * xr);
            r.env += (pw > r.env ? atk : rel) * (pw - r.env);
            if ((i & 15) == 0)
            {
                if (std::abs (dyn) > 1.0e-4)
                {
                    const double envDb = 10.0 * std::log10 (r.env + 1.0e-12);
                    gd = juce::Decibels::decibelsToGain (juce::jlimit (-18.0, 18.0, -dyn * 0.6 * (envDb + 18.0)));
                }
                else gd = 1.0;
            }

            double inP = 0.0, outP = 0.0;
            auto run = [&] (int ch, double x) noexcept
            {
                double v = x;
                if (toneOn)
                {
                    const double lo1 = r.tonePre[(size_t) ch].lp (x);
                    v = lo1 / tG + (x - lo1) * tG;
                }
                if (fbOn) v += fbAmt * r.fbState[(size_t) ch];
                double y = processStyle (style, v * drive * gd, r.st[(size_t) ch], drive01) / gd;
                if (fbOn)
                {
                    // feedback through a band-pass at the band's centre: resonant, but bounded
                    double fl, fbp, fh;
                    r.fbFilter[(size_t) ch].tick (y, fl, fbp, fh);
                    r.fbState[(size_t) ch] = fastTanh (fbp * r.fbFilter[(size_t) ch].k);
                }
                if (toneOn)
                {
                    const double lo2 = r.tonePost[(size_t) ch].lp (y);
                    y = lo2 * tG + (y - lo2) / tG;
                }
                inP += x * x;
                outP += y * y;
                return y;
            };

            double ya = a, yc = c;
            if (chans >= 1) ya = run (0, a);
            if (chans == 2) yc = run (1, c);

            // auto gain: an instant estimate from the curve (follows the drive knob without lag)
            // times a slow correction measured on the real signal
            const double gS = r.staticG.next (autoGain ? staticT : 1.0, cs);
            r.inPow += cag * (inP - r.inPow);
            r.outPow += cag * (outP * gS * gS - r.outPow);
            const double corrT = autoGain ? juce::jlimit (0.25, 4.0, std::sqrt ((r.inPow + 1.0e-12) / (r.outPow + 1.0e-12))) : 1.0;
            r.autoG += cagG * (corrT - r.autoG);
            const double post = gS * r.autoG * lvl;

            double ol = xl, orr = xr;
            switch (pl)
            {
                case Placement::stereo: ol = ya * post; orr = stereo ? yc * post : ol; break;
                case Placement::mid:    { const double m = ya * post, s = 0.5 * (xl - xr); ol = m + s; orr = m - s; if (! stereo) { ol = m; } break; }
                case Placement::side:   { if (chans == 0) break; const double m = 0.5 * (xl + xr), s = ya * post; ol = m + s; orr = m - s; break; }
                case Placement::left:   ol = ya * post; break;
                case Placement::right:  if (chans > 0) orr = ya * post; break;
            }

            L[i] += (float) (xl + mix * (ol - xl));
            if (stereo) R[i] += (float) (xr + mix * (orr - xr));
        }

        r.dynGain = gd;
        bandLevel[(size_t) b].store (levelPeak > 1.0e-6f ? 20.0f * std::log10 (levelPeak) : -100.0f);
    }
}

//==============================================================================
void EmberProcessor::setParam (const juce::String& id, float plainValue)
{
    if (auto* p = param (id))
    {
        p->beginChangeGesture();
        p->setValueNotifyingHost (p->convertTo0to1 (plainValue));
        p->endChangeGesture();
    }
}

static const char* bandParamIds[] { "style", "drive", "dyn", "tone", "fb", "mix", "level", "stereo", "mute", "solo", "bypass" };

int EmberProcessor::splitBandAt (float freq)
{
    const int nb = getNumBands();
    if (nb >= maxBands) return -1;

    // which band contains freq
    int b = 0;
    while (b < nb - 1 && freq > getCrossover (b)) ++b;
    const float lo = bandLow (b), hi = bandHigh (b);
    if (freq < lo * 1.15f || freq > hi / 1.15f) return -1;

    undo.beginNewTransaction ("Split band");
    // shift crossovers above and bands above b up by one
    for (int i = nb - 1; i > b; --i)
        setParam (xoId (i), getCrossover (i - 1));
    for (int k = nb; k > b + 1; --k)
        for (auto* id : bandParamIds)
            setParam (bandId (k, id), apvts.getRawParameterValue (bandId (k - 1, id))->load());
    for (auto* id : bandParamIds)
        setParam (bandId (b + 1, id), apvts.getRawParameterValue (bandId (b, id))->load());
    setParam (bandId (b + 1, "solo"), 0.0f);
    setParam (xoId (b), freq);
    setParam ("numBands", (float) (nb + 1));
    return b + 1;
}

void EmberProcessor::removeCrossover (int index)
{
    const int nb = getNumBands();
    if (nb <= 1 || index < 0 || index >= nb - 1) return;

    undo.beginNewTransaction ("Merge bands");
    // band index+1 disappears, everything above moves down
    for (int i = index; i < nb - 2; ++i)
        setParam (xoId (i), getCrossover (i + 1));
    for (int k = index + 1; k < nb - 1; ++k)
        for (auto* id : bandParamIds)
            setParam (bandId (k, id), apvts.getRawParameterValue (bandId (k + 1, id))->load());
    setParam ("numBands", (float) (nb - 1));
}

void EmberProcessor::setCrossoverFreq (int index, float freq)
{
    const int nb = getNumBands();
    const float lo = index > 0 ? getCrossover (index - 1) * 1.12f : 20.0f;
    const float hi = index < nb - 2 ? getCrossover (index + 1) / 1.12f : 20000.0f;
    if (auto* p = param (xoId (index)))
        p->setValueNotifyingHost (p->convertTo0to1 (juce::jlimit (lo, hi, freq)));
}

//==============================================================================
namespace
{
    struct PB { Style style; float drive, dyn = 0, tone = 0, fb = 0, mix = 100, level = 0; Placement pl = Placement::stereo; };
    struct Preset { const char* name; std::vector<float> xo; std::vector<PB> bands; float mix = 100; };

    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> p {
            { "Default", {}, { { Style::warmTube, 6 } } },
            { "Warm Glue", {}, { { Style::warmTube, 4, 30, -10 } } },
            { "Tape Master", {}, { { Style::tape, 7, 20, 10 } } },
            { "Analog Bus", { 180 }, { { Style::transformer, 6, 20 }, { Style::cleanTube, 5, 10, 15 } } },
            { "Punchy Bass", { 110, 900 }, { { Style::transformer, 9, -20 }, { Style::warmTube, 12, 0, 20 }, { Style::clean, 0, 0, 0, 0, 100, -2 } } },
            { "Vocal Presence", { 250, 3000, 9000 }, { { Style::clean, 0 }, { Style::warmTube, 5, 30 }, { Style::tape, 8, 20, 20 }, { Style::gentle, 4, 0, 30 } } },
            { "Crunch Guitar", { 150 }, { { Style::clean, 0 }, { Style::crunchAmp, 14, -10, 10 } } },
            { "Heavy Drive", {}, { { Style::heavyAmp, 18, -20 } }, 100 },
            { "Parallel Grit", {}, { { Style::hardClip, 20, 0, 20 } }, 35 },
            { "Lo-Fi", { 300, 5000 }, { { Style::warm, 6 }, { Style::bitcrush, 20 }, { Style::clean, 0, 0, 0, 0, 100, -12 } } },
            { "Wide Sizzle", { 4000 }, { { Style::clean, 0 }, { Style::tape, 10, 0, 30, 0, 100, 0, Placement::side } } },
            { "Octave Fuzz", {}, { { Style::rectify, 12, 0, 20 } }, 60 },
            { "Wavefolder", {}, { { Style::foldback, 14, 0, -10, 20 } }, 70 },
        };
        return p;
    }
}

juce::StringArray EmberProcessor::getFactoryPresetNames()
{
    juce::StringArray s;
    for (auto& p : presets()) s.add (p.name);
    return s;
}

void EmberProcessor::loadFactoryPreset (int index)
{
    const auto& all = presets();
    if (index < 0 || index >= (int) all.size()) return;
    const auto& pr = all[(size_t) index];

    undo.beginNewTransaction ("Load preset");
    for (int b = 0; b < maxBands; ++b)
        for (auto* id : bandParamIds)
            if (auto* p = param (bandId (b, id))) { p->beginChangeGesture(); p->setValueNotifyingHost (p->getDefaultValue()); p->endChangeGesture(); }
    for (int i = 0; i < maxCrossovers; ++i)
        setParam (xoId (i), i < (int) pr.xo.size() ? pr.xo[(size_t) i] : defaultCrossovers[i]);
    int b = 0;
    for (auto& pb : pr.bands)
    {
        setParam (bandId (b, "style"), (float) pb.style);
        setParam (bandId (b, "drive"), pb.drive);
        setParam (bandId (b, "dyn"), pb.dyn);
        setParam (bandId (b, "tone"), pb.tone);
        setParam (bandId (b, "fb"), pb.fb);
        setParam (bandId (b, "mix"), pb.mix);
        setParam (bandId (b, "level"), pb.level);
        setParam (bandId (b, "stereo"), (float) pb.pl);
        ++b;
    }
    setParam ("numBands", (float) juce::jmax (1, (int) pr.bands.size()));
    setParam ("mix", pr.mix);
    setParam ("input", 0.0f);
    setParam ("output", 0.0f);
    currentPresetName = pr.name;
}

juce::File EmberProcessor::getUserPresetFolder()
{
    auto f = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Avel Ember").getChildFile ("Presets");
    f.createDirectory();
    return f;
}

bool EmberProcessor::saveUserPreset (const juce::File& file)
{
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return false;
    xml->setTagName ("EmberPreset");
    if (! xml->writeTo (file)) return false;
    currentPresetName = file.getFileNameWithoutExtension();
    return true;
}

bool EmberProcessor::loadUserPreset (const juce::File& file)
{
    auto xml = juce::XmlDocument::parse (file);
    if (xml == nullptr || ! xml->hasTagName ("EmberPreset")) return false;
    xml->setTagName (apvts.state.getType().toString());
    undo.beginNewTransaction ("Load preset");
    apvts.replaceState (juce::ValueTree::fromXml (*xml));
    currentPresetName = file.getFileNameWithoutExtension();
    return true;
}

void EmberProcessor::switchAB (int slot)
{
    if (slot == abSlot) return;
    abStates[abSlot] = apvts.copyState();
    if (abStates[slot].isValid()) apvts.replaceState (abStates[slot].createCopy());
    abSlot = slot;
    undo.clearUndoHistory();
}

void EmberProcessor::copyABToOther() { abStates[1 - abSlot] = apvts.copyState(); }

//==============================================================================
void EmberProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("AvelEmber");
    root.setProperty ("version", 1, nullptr);
    root.setProperty ("preset", currentPresetName, nullptr);
    root.setProperty ("abSlot", abSlot, nullptr);
    root.appendChild (apvts.copyState(), nullptr);
    root.appendChild (uiState.createCopy(), nullptr);
    for (int i = 0; i < 2; ++i)
        if (i != abSlot && abStates[i].isValid())
        {
            juce::ValueTree ab ("AB" + juce::String (i));
            ab.appendChild (abStates[i].createCopy(), nullptr);
            root.appendChild (ab, nullptr);
        }
    if (auto xml = root.createXml()) copyXmlToBinary (*xml, destData);
}

void EmberProcessor::setStateInformation (const void* data, int size)
{
    auto xml = getXmlFromBinary (data, size);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("AvelEmber")) return;
    if (auto p = root.getChildWithName (apvts.state.getType()); p.isValid()) apvts.replaceState (p);
    if (auto u = root.getChildWithName ("UI"); u.isValid()) uiState.copyPropertiesFrom (u, nullptr);
    currentPresetName = root.getProperty ("preset", "Default").toString();
    abSlot = juce::jlimit (0, 1, (int) root.getProperty ("abSlot", 0));
    for (int i = 0; i < 2; ++i)
    {
        abStates[i] = {};
        if (auto ab = root.getChildWithName ("AB" + juce::String (i)); ab.isValid() && ab.getNumChildren() > 0)
            abStates[i] = ab.getChild (0).createCopy();
    }
    undo.clearUndoHistory();
    triggerAsyncUpdate();
}

juce::AudioProcessorEditor* EmberProcessor::createEditor() { return new EmberEditor (*this); }
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new EmberProcessor(); }
