#pragma once

// Saturation styles. All run at the oversampled rate.
// Input is already multiplied by the drive gain; output is roughly within +/-1.

#include <cmath>
#include <algorithm>
#include <array>

namespace avel
{
    enum class Style
    {
        clean, warm, gentle,
        cleanTube, warmTube, heavyTube,
        tape, hotTape,
        transformer,
        cleanAmp, crunchAmp, heavyAmp,
        softClip, hardClip,
        rectify, foldback, bitcrush,
        count
    };

    inline const char* styleName (Style s)
    {
        static const char* n[] { "Clean", "Warm", "Gentle", "Clean Tube", "Warm Tube", "Heavy Tube", "Tape", "Hot Tape",
                                 "Transformer", "Clean Amp", "Crunch Amp", "Heavy Amp", "Soft Clip", "Hard Clip",
                                 "Rectify", "Foldback", "Bitcrush" };
        return n[(int) s];
    }

    inline const char* styleCategory (Style s)
    {
        switch (s)
        {
            case Style::clean: case Style::warm: case Style::gentle: return "Saturation";
            case Style::cleanTube: case Style::warmTube: case Style::heavyTube: return "Tube";
            case Style::tape: case Style::hotTape: return "Tape";
            case Style::transformer: return "Transformer";
            case Style::cleanAmp: case Style::crunchAmp: case Style::heavyAmp: return "Amp";
            case Style::softClip: case Style::hardClip: return "Clipping";
            default: return "FX";
        }
    }

    // Pade-type tanh approximation, |error| < 2e-7, clamped beyond the point where it reaches 1
    inline double fastTanh (double x) noexcept
    {
        if (x > 4.97) return 1.0;
        if (x < -4.97) return -1.0;
        const double x2 = x * x;
        return x * (135135.0 + x2 * (17325.0 + x2 * (378.0 + x2))) / (135135.0 + x2 * (62370.0 + x2 * (3150.0 + 28.0 * x2)));
    }

    //==============================================================================
    namespace shape
    {
        inline double softClip (double x) noexcept // cubic, C1 continuous
        {
            if (x > 1.0) return 2.0 / 3.0;
            if (x < -1.0) return -2.0 / 3.0;
            return x - x * x * x / 3.0;
        }

        inline double tubeWarm (double x) noexcept // asymmetric exponential: even harmonics
        {
            return x >= 0.0 ? 1.0 - std::exp (-x) : (std::exp (1.25 * x) - 1.0) / 1.25;
        }

        // 2/pi * atan(pi/2 * x) with a fast atan (max error ~0.004 rad, smooth and monotonic)
        inline double fastAtan (double x) noexcept
        {
            const double a = std::abs (x);
            const double r = a <= 1.0 ? a * (0.7853981633974483 + 0.273 * (1.0 - a))
                                      : 1.5707963267948966 - (1.0 / a) * (0.7853981633974483 + 0.273 * (1.0 - 1.0 / a));
            return std::copysign (r, x);
        }

        inline double atanSat (double x) noexcept { return 0.6366197723675814 * fastAtan (1.5707963267948966 * x); }

        inline double fold (double x) noexcept { return std::sin (x); }
    }

    // Memoryless version of each curve (used for display and for the core of each style)
    inline double staticCurve (Style s, double x, double drive01 = 0.5) noexcept
    {
        switch (s)
        {
            case Style::clean:      return fastTanh (x);
            case Style::warm:       { const double b = 0.25; return (fastTanh (x + b) - fastTanh (b)) * (1.0 / (1.0 - fastTanh (b) * fastTanh (b))) * 0.85; }
            case Style::gentle:     return 1.5 * shape::softClip (x / 1.5) * 1.0;
            case Style::cleanTube:  return x >= 0 ? fastTanh (x) : fastTanh (0.85 * x) / 0.85;
            case Style::warmTube:   return shape::tubeWarm (x);
            case Style::heavyTube:  return fastTanh (2.2 * shape::tubeWarm (1.4 * x)) / 1.3;
            case Style::tape:       return shape::atanSat (x);
            case Style::hotTape:    return shape::atanSat (1.6 * x) * 0.95;
            case Style::transformer:return fastTanh (0.9 * x) / 0.9 * 0.95;
            case Style::cleanAmp:   return fastTanh (1.2 * x + 0.1) - fastTanh (0.1);
            case Style::crunchAmp:  return fastTanh (3.0 * fastTanh (1.5 * x + 0.2) - 0.4) * 0.9;
            case Style::heavyAmp:   return fastTanh (6.0 * fastTanh (3.0 * x + 0.3) - 1.0) * 0.85;
            case Style::softClip:   return 1.5 * shape::softClip (x);
            case Style::hardClip:   return std::clamp (x, -1.0, 1.0);
            case Style::rectify:    { const double t = fastTanh (x); return std::abs (t) * 0.8 + t * 0.2; }
            case Style::foldback:   return shape::fold (x * 1.2);
            case Style::bitcrush:
            {
                const double bits = 12.0 - 10.0 * std::clamp (drive01, 0.0, 1.0);
                const double q = std::pow (2.0, bits - 1.0);
                return std::round (std::clamp (x, -1.0, 1.0) * q) / q;
            }
            case Style::count: break;
        }
        return x;
    }

    //==============================================================================
    struct OnePole
    {
        double a = 0, z = 0;
        void setFreq (double f, double sr) noexcept { a = 1.0 - std::exp (-2.0 * 3.14159265358979 * f / sr); }
        inline double lp (double x) noexcept { z += a * (x - z); return z; }
        inline double hp (double x) noexcept { return x - lp (x); }
        void reset() noexcept { z = 0; }
    };

    struct Peak2 // RBJ peaking filter (used inside the amp models)
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set (double f, double q, double gainDb, double sr) noexcept
        {
            const double A = std::pow (10.0, gainDb / 40.0), w = 2.0 * 3.14159265358979 * f / sr;
            const double al = std::sin (w) / (2.0 * q), c = std::cos (w), a0 = 1.0 + al / A;
            b0 = (1.0 + al * A) / a0; b1 = -2.0 * c / a0; b2 = (1.0 - al * A) / a0; a1 = -2.0 * c / a0; a2 = (1.0 - al / A) / a0;
        }
        inline double process (double x) noexcept
        {
            const double y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void reset() noexcept { z1 = z2 = 0; }
    };

    // Per channel, per band state of a saturation stage
    struct StyleState
    {
        OnePole pre, post, lf, cab, dc, ampHp, interHp;
        Peak2 mid;
        double prev = 0.0, hold = 0.0;
        int holdCount = 0;
        double sr = 44100.0;
        Style configured = Style::count;

        void prepare (double sampleRate) noexcept
        {
            sr = sampleRate;
            configured = Style::count;
            reset();
        }

        void configure (Style s) noexcept
        {
            if (s == configured) return;
            const bool wasSet = configured != Style::count;
            configured = s;
            if (wasSet) reset(); // no state (or DC) leaks from the previous style
            pre.setFreq (3000.0, sr);
            post.setFreq (3000.0, sr);
            lf.setFreq (140.0, sr);
            dc.setFreq (6.0, sr);
            ampHp.setFreq (s == Style::heavyAmp ? 140.0 : 90.0, sr);
            interHp.setFreq (30.0, sr);
            cab.setFreq (s == Style::cleanAmp ? 7500.0 : s == Style::crunchAmp ? 5500.0 : s == Style::hotTape ? 9000.0 : s == Style::tape ? 13000.0 : 4500.0, sr);
            mid.set (s == Style::heavyAmp ? 650.0 : 900.0, 0.8, s == Style::cleanAmp ? 2.0 : 5.0, sr);
        }

        void reset() noexcept
        {
            for (auto* p : { &pre, &post, &lf, &cab, &dc, &ampHp, &interHp }) p->reset();
            mid.reset();
            prev = hold = 0.0;
            holdCount = 0;
        }
    };

    // One sample of a style. drive01 is the drive knob position 0..1 (only some styles use it).
    inline double processStyle (Style s, double x, StyleState& st, double drive01) noexcept
    {
        double y;
        switch (s)
        {
            case Style::tape:
            case Style::hotTape:
            {
                // pre-emphasis -> saturation with a little memory -> de-emphasis -> head loss
                const double e = s == Style::hotTape ? 0.9 : 0.6;
                const double hi = x - st.pre.lp (x);
                const double v = x + e * hi;
                const double sat = staticCurve (s, v);
                const double m = 0.88 * sat + 0.12 * st.prev; // smooth, slightly compressed transients
                st.prev = sat;
                const double hi2 = m - st.post.lp (m);
                y = st.cab.lp (m - (e / (1.0 + e)) * hi2);
                break;
            }

            case Style::transformer:
            {
                // low frequencies saturate harder, like an iron core
                const double l = st.lf.lp (x);
                const double h = x - l;
                y = staticCurve (s, h + fastTanh (1.8 * l) / 1.8 * 1.25);
                break;
            }

            case Style::cleanAmp:
            case Style::crunchAmp:
            case Style::heavyAmp:
            {
                const double in = st.ampHp.hp (x);
                double v = fastTanh ((s == Style::cleanAmp ? 1.2 : 2.0) * in + 0.15) - fastTanh (0.15);
                v = st.interHp.hp (v);
                v = st.mid.process (v);
                if (s != Style::cleanAmp) v = fastTanh ((s == Style::heavyAmp ? 5.0 : 2.5) * v);
                if (s == Style::heavyAmp) v = fastTanh (2.0 * v + 0.2) - fastTanh (0.2);
                y = st.cab.lp (st.cab.lp (v) * 0.5 + v * 0.5) * (s == Style::heavyAmp ? 0.8 : 0.9);
                break;
            }

            case Style::bitcrush:
            {
                // bit depth and a little sample-rate reduction as drive goes up
                const int hold = 1 + (int) std::floor (std::pow (std::clamp (drive01, 0.0, 1.0), 2.0) * 7.0);
                if (st.holdCount <= 0) { st.hold = staticCurve (s, x, drive01); st.holdCount = hold; }
                --st.holdCount;
                y = st.hold;
                break;
            }

            default:
                y = staticCurve (s, x, drive01);
                break;
        }

        // asymmetric styles create DC
        return y - st.dc.lp (y);
    }
}
