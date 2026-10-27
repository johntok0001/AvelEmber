#pragma once

// Phase-coherent multiband splitter: Linkwitz-Riley 24 dB/oct (two cascaded Butterworth
// TPT state-variable filters). Every band is passed through the all-passes of the
// crossovers it did not go through, so the sum of all bands is a pure all-pass:
// flat magnitude, no comb filtering, even while crossovers move.

#include <array>
#include <cmath>
#include <algorithm>

namespace avel
{
    constexpr int maxBands = 6;
    constexpr int maxCrossovers = maxBands - 1;
    constexpr double xoPi = 3.14159265358979323846;

    // Zavalishin TPT state-variable filter, k = sqrt(2) (Butterworth)
    struct SVF
    {
        double g = 0, k = 1.4142135623730951, a1 = 0, a2 = 0, a3 = 0;
        double s1 = 0, s2 = 0;

        void setFreq (double f, double sr) noexcept
        {
            g = std::tan (xoPi * std::clamp (f, 10.0, 0.49 * sr) / sr);
            a1 = 1.0 / (1.0 + g * (g + k));
            a2 = g * a1;
            a3 = g * a2;
        }

        void copyCoeffs (const SVF& o) noexcept { g = o.g; k = o.k; a1 = o.a1; a2 = o.a2; a3 = o.a3; }

        // returns lp, bp, hp
        inline void tick (double x, double& lp, double& bp, double& hp) noexcept
        {
            const double v3 = x - s2;
            const double v1 = a1 * s1 + a2 * v3;
            const double v2 = s2 + a2 * s1 + a3 * v3;
            s1 = 2.0 * v1 - s1;
            s2 = 2.0 * v2 - s2;
            lp = v2; bp = v1; hp = x - k * v1 - v2;
        }

        inline double lowpass (double x) noexcept  { double l, b, h; tick (x, l, b, h); return l; }
        inline double highpass (double x) noexcept { double l, b, h; tick (x, l, b, h); return h; }
        inline double allpass (double x) noexcept  { double l, b, h; tick (x, l, b, h); return l - k * b + h; }

        void reset() noexcept { s1 = s2 = 0; }
    };

    // One crossover point: LR4 low / high outputs
    struct LR4
    {
        SVF lp1, lp2, hp1, hp2;

        void setFreq (double f, double sr) noexcept
        {
            lp1.setFreq (f, sr);
            lp2.copyCoeffs (lp1); hp1.copyCoeffs (lp1); hp2.copyCoeffs (lp1);
        }

        inline void split (double x, double& low, double& high) noexcept
        {
            low = lp2.lowpass (lp1.lowpass (x));
            high = hp2.highpass (hp1.highpass (x));
        }

        void reset() noexcept { lp1.reset(); lp2.reset(); hp1.reset(); hp2.reset(); }
    };

    // Splits one channel into up to 6 bands
    class MultibandSplitter
    {
    public:
        void prepare (double sampleRate) { sr = sampleRate; reset(); }

        void setCrossovers (int numBandsIn, const double* freqs) noexcept
        {
            numBands = std::clamp (numBandsIn, 1, maxBands);
            for (int i = 0; i < numBands - 1; ++i)
            {
                xo[(size_t) i].setFreq (freqs[i], sr);
                for (int b = 0; b < maxBands; ++b)
                    comp[(size_t) b][(size_t) i].copyCoeffs (xo[(size_t) i].lp1);
            }
        }

        // out[b] receives band b
        inline void process (double x, double* out) noexcept
        {
            double rest = x;
            for (int i = 0; i < numBands - 1; ++i)
            {
                double low, high;
                xo[(size_t) i].split (rest, low, high);
                // band i has not seen crossovers i+1 .. n-2: add their all-pass response
                for (int j = i + 1; j < numBands - 1; ++j)
                    low = comp[(size_t) i][(size_t) j].allpass (low);
                out[i] = low;
                rest = high;
            }
            out[numBands - 1] = rest;
        }

        void reset() noexcept
        {
            for (auto& x : xo) x.reset();
            for (auto& b : comp) for (auto& a : b) a.reset();
        }

    private:
        double sr = 44100.0;
        int numBands = 1;
        std::array<LR4, maxCrossovers> xo;
        std::array<std::array<SVF, maxCrossovers>, maxBands> comp;
    };
}
