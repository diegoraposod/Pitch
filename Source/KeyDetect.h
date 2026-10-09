#pragma once
// PITCH · listens to what it receives and decides the key, like the web app did:
// spectral peaks between 120 and 1200 Hz are folded into 12 pitch classes, the strongest tonic
// (helped by its fifth and third) wins, then the mode whose notes cover the most energy.
// Runs on the message thread from a copy of the input; nothing here touches the audio thread.

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <vector>
#include "Engine.h"

namespace pitch
{
class KeyDetector
{
public:
    static constexpr int order = 14, size = 1 << order;        // 16384-point FFT: ~3 Hz per bin at 48 kHz

    void reset() { chroma.fill (0); soft.fill (0); tonal = elapsed = 0; peaks = 0; }

    // feed one analysis window (size samples, mono). Returns true when a key has been decided (pc, mode set).
    bool analyse (const float* x, double sr, double windowSeconds, int& pc, int& mode)
    {
        elapsed += windowSeconds;
        float rms = 0; for (int i = 0; i < size; ++i) rms += x[i] * x[i];
        rms = std::sqrt (rms / size);
        if (rms >= 0.0015f)
        {
            for (int i = 0; i < size; ++i) buf[(size_t) i] = x[i] * window[(size_t) i];
            std::fill (buf.begin() + size, buf.end(), 0.0f);
            fft.performFrequencyOnlyForwardTransform (buf.data());
            const int bins = size / 2;
            for (int k = 0; k < bins; ++k) db[(size_t) k] = 20.0f * std::log10 (buf[(size_t) k] * 4.0f / size + 1.0e-9f);
            const int n = addPeaks (sr);
            addSoft (sr);
            peaks += n;
            if (n > 0) tonal += windowSeconds;
        }
        if (tonal >= 4.0 && peaks >= 40 && decide (chroma, false, pc, mode)) return true;
        if (elapsed >= 8.0 && tonal > 0.5) return decide (soft, true, pc, mode);
        return false;
    }

    bool hasHeardEnough() const { return elapsed > 0; }

private:
    juce::dsp::FFT fft { order };
    std::vector<float> buf = std::vector<float> ((size_t) size * 2, 0.0f), db = std::vector<float> ((size_t) size / 2, 0.0f);
    std::vector<float> window = makeWindow();
    std::array<float, 12> chroma {}, soft {};
    double tonal = 0, elapsed = 0; int peaks = 0;

    static std::vector<float> makeWindow()
    {
        std::vector<float> w ((size_t) size);
        for (int i = 0; i < size; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * i / (size - 1));
        return w;
    }

    int addPeaks (double sr)
    {
        const double binHz = sr / size;
        const int kMin = std::max (9, (int) (120 / binHz)), kMax = std::min (size / 2 - 10, (int) (1200 / binHz));
        int count = 0;
        for (int k = kMin; k <= kMax; ++k)
        {
            const float b = db[(size_t) k];
            if (! (b > -80) || ! (b > db[(size_t) k - 1]) || ! (b >= db[(size_t) k + 1])) continue;
            float base = 0; for (int j = -8; j <= 8; ++j) base += db[(size_t) (k + j)]; base /= 17;
            const float prom = b - base;
            if (prom < 9) continue;
            const float a = db[(size_t) k - 1], c = db[(size_t) k + 1], den = a - 2 * b + c;
            const double f = (k + (den != 0 ? 0.5 * (a - c) / den : 0.0)) * binHz;
            const double midi = 69 + 12 * std::log2 (f / 440.0);
            if (std::abs (midi - std::round (midi)) > 0.35) continue;
            chroma[(size_t) ((((int) std::round (midi)) % 12 + 12) % 12)] += std::min (prom, 40.0f);
            ++count;
        }
        return count;
    }

    void addSoft (double sr)
    {
        const double binHz = sr / size;
        const int kMin = std::max (2, (int) (120 / binHz)), kMax = std::min (size / 2 - 2, (int) (1200 / binHz));
        std::vector<float> vals (db.begin() + kMin, db.begin() + kMax + 1);
        std::nth_element (vals.begin(), vals.begin() + (long) vals.size() / 2, vals.end());
        const float med = vals[vals.size() / 2];
        for (int k = kMin; k <= kMax; ++k)
        {
            const float w = db[(size_t) k] - med - 2;
            if (! (w > 0) || ! (db[(size_t) k] > -90)) continue;
            const int midi = (int) std::round (69 + 12 * std::log2 ((k * binHz) / 440.0));
            soft[(size_t) ((midi % 12 + 12) % 12)] += w;
        }
    }

    static bool decide (const std::array<float, 12>& c, bool force, int& pc, int& mode)
    {
        float tot = 0; for (auto v : c) tot += v;
        if (tot <= 0) return false;
        std::array<float, 12> score {};
        for (int p = 0; p < 12; ++p) score[(size_t) p] = c[(size_t) p] + 0.5f * c[(size_t) ((p + 7) % 12)] + 0.35f * c[(size_t) ((p + 4) % 12)];
        int best = 0; for (int p = 1; p < 12; ++p) if (score[(size_t) p] > score[(size_t) best]) best = p;
        float mean = 0; for (auto v : score) mean += v; mean /= 12;
        if (! force && score[(size_t) best] < 1.5f * mean) return false;
        int bestMode = 3; float bestFit = -1e9f;
        for (int m = 0; m < 7; ++m)
        {
            float in = 0; for (int i = 0; i < 7; ++i) in += c[(size_t) ((best + kModes[m][i]) % 12)];
            const float fit = (in - 0.6f * (tot - in)) / tot;
            if (fit > bestFit + 1.0e-4f) { bestFit = fit; bestMode = m; }
        }
        pc = best; mode = bestMode;
        return true;
    }
};
} // namespace pitch
