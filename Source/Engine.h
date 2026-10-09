#pragma once
// PITCH · audio engine. Real-time safe: no allocation, no locks, no I/O inside process().

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <vector>
#include "World.h"

namespace pitch
{
constexpr double kTwoPi = 6.283185307179586;

// ---------- small building blocks ----------
struct Glide            // one-pole smoother: every level, pitch and cutoff glides, so nothing ever steps or clicks
{
    float y = 0, target = 0, coef = 0.001f;
    void setTime (double seconds, double sr) { coef = (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-4, seconds) * sr))); }
    void snap (float v) { y = target = v; }
    inline float next() { y += (target - y) * coef; return y; }
};

struct Osc
{
    double phase = 0;
    inline float next (double hz, double sr) { phase += hz / sr; phase -= std::floor (phase); return (float) std::sin (kTwoPi * phase); }
};

struct Noise
{
    uint32_t s = 0x9E3779B9u;
    inline float next() { s ^= s << 13; s ^= s >> 17; s ^= s << 5; return (float) ((int32_t) s) * (1.0f / 2147483648.0f); }
    inline float uni() { return 0.5f + 0.5f * next(); }
};

struct Svf              // topology-preserving state-variable filter (stable at any cutoff)
{
    float a1 = 0, a2 = 0, a3 = 0, k = 1, ic1 = 0, ic2 = 0;
    void set (float fc, float q, double sr)
    {
        fc = clampf (fc, 20.0f, (float) sr * 0.45f);
        const float g = (float) std::tan (juce::MathConstants<double>::pi * fc / sr);
        k = 1.0f / q; a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    inline void tick (float x, float& lp, float& bp)
    {
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2; lp = v2; bp = v1;
    }
    inline float lp (float x) { float l, b; tick (x, l, b); return l; }
    inline float bp (float x) { float l, b; tick (x, l, b); return b; }
    void reset() { ic1 = ic2 = 0; }
};

// Where a source sits in the space: level, air absorption, stereo position, dry/reverb split.
struct Placement
{
    Glide gain, cutoff, pan, dry, send;
    Svf lpL, lpR;
    int counter = 0;
    void prepare (double sr)
    {
        for (auto* g : { &gain, &pan, &dry, &send }) g->setTime (0.06, sr);
        cutoff.setTime (0.12, sr); cutoff.snap (2000.0f);
        lpL.reset(); lpR.reset();
    }
    void setTargets (const Spatial& s, float level)
    {
        gain.target = s.gain * level; cutoff.target = s.cutoff; pan.target = s.pan; dry.target = s.dry; send.target = s.send;
    }
    // stereo in -> adds to dry and reverb buses
    inline void place (float l, float r, bool keepStereo, double sr, float& dL, float& dR, float& sL, float& sR)
    {
        const float c = cutoff.next();
        if ((counter++ & 15) == 0) { lpL.set (c, 0.707f, sr); lpR.a1 = lpL.a1; lpR.a2 = lpL.a2; lpR.a3 = lpL.a3; lpR.k = lpL.k; }
        const float g = gain.next(), p = pan.next(), dg = dry.next(), sg = send.next();
        l = lpL.lp (l) * g; r = lpR.lp (r) * g;
        float gl, gr;
        if (keepStereo) { gl = std::min (1.0f, 1.0f - p); gr = std::min (1.0f, 1.0f + p); }        // balance: never mixes L into R
        else { const float a = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi; gl = std::cos (a) * 1.4142f; gr = std::sin (a) * 1.4142f; }
        l *= gl; r *= gr;
        dL += l * dg; dR += r * dg; sL += l * sg; sR += r * sg;
    }
};

// ---------- the scale ----------
struct Scale
{
    int pc = 2, mode = 3;
    float root = 73.42f;
    std::array<float, 6> voicing {};
    std::array<float, 7> notes {};
};

inline const int kModes[7][7] = {
    { 0, 2, 4, 5, 7, 9, 11 },   // major
    { 0, 2, 4, 6, 7, 9, 11 },   // lydian
    { 0, 2, 4, 5, 7, 9, 10 },   // mixolydian
    { 0, 2, 3, 5, 7, 8, 10 },   // minor
    { 0, 2, 3, 5, 7, 9, 10 },   // dorian
    { 0, 1, 3, 5, 7, 8, 10 },   // phrygian
    { 0, 2, 3, 5, 7, 8, 11 },   // harmonic minor
};

inline Scale buildScale (int pc, int mode)
{
    Scale s; s.pc = pc; s.mode = mode;
    const int* d = kModes[mode];
    int m = pc; while (m < 31) m += 12; while (m > 42) m -= 12;                       // root between G1 and F#2
    const int colour = mode == 1 ? d[3] : (mode == 5 || mode == 6) ? d[5] : d[4];
    const int offs[6] = { 0, d[4], 12 + d[2], 12 + d[6], 24 + d[1], 24 + colour };
    auto mtof = [] (float midi) { return 440.0f * std::pow (2.0f, (midi - 69.0f) / 12.0f); };
    for (int i = 0; i < 6; ++i) s.voicing[(size_t) i] = mtof ((float) (m + offs[i]));
    s.root = s.voicing[0];
    for (int i = 0; i < 7; ++i) s.notes[(size_t) i] = s.root * std::pow (2.0f, (float) d[i] / 12.0f);
    return s;
}

// ---------- fossils: a recorded or dropped sound, looping as a soft grain cloud at its original pitch ----------
struct Fossil
{
    juce::AudioBuffer<float> audio;     // stereo, at sourceRate
    double sourceRate = 44100.0;
    juce::String name, path;
    std::atomic<float> x { 0 }, y { 0 }, level { 0.75f };
    int uid = 0;

    // audio-thread state
    struct Grain { double pos = 0; int len = 0, age = 0; bool on = false; float pan = 0; };
    std::array<Grain, 6> grains;
    int untilNext = 0;
    Noise rng;
    Placement place;
    float meter = 0;
};

// ---------- the engine ----------
class Engine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        sendL.assign ((size_t) maxBlock, 0.0f); sendR.assign ((size_t) maxBlock, 0.0f);
        for (auto& p : stonePlace) p.prepare (sr);
        for (auto& g : sineFreq) g.setTime (0.3, sr);
        for (auto& g : binFreq) g.setTime (0.3, sr);
        for (auto& g : resFreq) g.setTime (0.3, sr);
        earthCents.setTime (30.0, sr); storm.setTime (20.0, sr); cloud.setTime (0.2, sr); inputDry.setTime (0.08, sr);
        reverbAmt.setTime (0.2, sr); outGain.setTime (0.05, sr);
        inputDry.snap (1.0f); outGain.snap (1.0f); reverbAmt.snap (0.7f);
        juce::Reverb::Parameters rp; rp.roomSize = 0.92f; rp.damping = 0.55f; rp.wetLevel = 1.0f; rp.dryLevel = 0.0f; rp.width = 1.0f;
        reverb.setParameters (rp); reverb.setSampleRate (sr); reverb.reset();
        revHpL.set (140, 0.707f, sr); revHpR = revHpL; revLpL.set (6500, 0.707f, sr); revLpR = revLpL;
        bedLp.set (1800, 0.707f, sr);
        setScale (scale.pc, scale.mode, true);
        limEnv = 0;
    }

    void setScale (int pc, int mode, bool snap = false)
    {
        scale = buildScale (pc, mode);
        for (size_t i = 0; i < 6; ++i) { sineFreq[i].target = scale.voicing[i]; if (snap) sineFreq[i].snap (scale.voicing[i]); }
        const float b[3] = { scale.root * 2.0f, scale.notes[4] * 2.0f, scale.notes[2] * 4.0f };
        for (size_t i = 0; i < 3; ++i) { binFreq[i].target = b[i]; if (snap) binFreq[i].snap (b[i]); }
        const float r[4] = { scale.voicing[0] * 2.0f, scale.voicing[2], scale.voicing[1] * 2.0f, scale.voicing[0] };
        for (size_t i = 0; i < 4; ++i) { resFreq[i].target = r[i]; if (snap) resFreq[i].snap (r[i]); }
    }

    // control (called once per block, audio thread)
    void setStone (int id, const Spatial& s, float level) { stonePlace[(size_t) id].setTargets (s, level); stoneNear[(size_t) id] = s.near; }
    void setCloud (float c)          { cloud.target = c; }
    void setInputDry (float v)       { inputDry.target = v; }
    void setEarthCents (float c)     { earthCents.target = c; }
    void setStorm (float v)          { storm.target = v; }
    void setReverb (float v)         { reverbAmt.target = v; }
    void setOutput (float g)         { outGain.target = g; }

    // inL/inR may alias outL/outR
    void process (const float* inL, const float* inR, float* outL, float* outR, int n,
                  std::vector<std::shared_ptr<Fossil>>* fossils)
    {
        int done = 0;
        while (done < n)
        {
            const int chunk = std::min ((int) sendL.size(), n - done);
            processChunk (inL + done, inR + done, outL + done, outR + done, chunk, fossils);
            done += chunk;
        }
    }

    std::array<float, numStones> stoneNear {};
    std::atomic<float> inputMeter { 0 }, outputMeter { 0 };

private:
    double sr = 48000.0;
    Scale scale = buildScale (2, 3);
    std::array<Placement, numStones> stonePlace;

    // Earth
    std::array<Osc, 6> earthOsc; Osc earthPulse; Glide earthCents, storm;
    // Sine bank
    std::array<Osc, 6> sineOsc, sineTrem, sineDrift; std::array<Glide, 6> sineFreq;
    // Binaural
    std::array<Osc, 3> binL, binR; std::array<Glide, 3> binFreq; Osc binDrift, bedSway; Svf bedLp; Noise noise;
    // Granular cloud
    std::array<Svf, 4> res; std::array<Glide, 4> resFreq; std::array<double, 4> winPh {}; int resCounter = 0; Glide cloud;
    // buses
    std::vector<float> sendL, sendR;
    juce::Reverb reverb; Svf revHpL, revHpR, revLpL, revLpR; Glide reverbAmt, inputDry, outGain;
    float limEnv = 0;

    void processChunk (const float* inL, const float* inR, float* outL, float* outR, int n,
                       std::vector<std::shared_ptr<Fossil>>* fossils)
    {
        static constexpr float sineAmp[6] = { 0.20f, 0.17f, 0.14f, 0.11f, 0.08f, 0.06f };
        static constexpr double sineTremHz[6] = { 0.031, 0.047, 0.059, 0.071, 0.083, 0.097 };
        static constexpr double sineDriftHz[6] = { 0.013, 0.017, 0.011, 0.021, 0.019, 0.023 };
        static constexpr float binAmp[3] = { 0.12f, 0.08f, 0.05f };
        static constexpr float binBeat[3] = { 6.0f, 4.8f, 7.5f };
        static constexpr double winHz[4] = { 0.23, 0.17, 0.29, 0.13 };
        static constexpr float resQ[4] = { 70, 70, 70, 50 };
        const double ef0 = 7.83 * 16.0;                                 // Schumann x 16 = 125.28 Hz
        static constexpr double earthMul[6] = { 1.0, 0.5, 1.5, 3.0, 4.0, 5.0 };
        static constexpr float earthAmp[6] = { 0.20f, 0.26f, 0.05f, 0.08f, 0.05f, 0.03f };

        float inPeak = 0, outPeak = 0;
        for (int i = 0; i < n; ++i)
        {
            const float inl = inL[i], inr = inR[i], mic = 0.5f * (inl + inr);
            inPeak = std::max (inPeak, std::abs (mic));
            float dL = 0, dR = 0, sL = 0, sR = 0;

            // EARTH: Schumann x 16, sub-octave and fifth; storms open its upper partials and deepen the 7.83 Hz pulse
            {
                const double ratio = std::pow (2.0, earthCents.next() / 1200.0);
                const float st = storm.next();
                float e = 0;
                for (size_t k = 0; k < 6; ++k)
                {
                    const float a = k < 3 ? earthAmp[k] : earthAmp[k] * std::pow (st, 0.8f);
                    e += earthOsc[k].next (ef0 * earthMul[k] * ratio, sr) * a;
                }
                const float depth = 0.15f + 0.35f * st;
                e *= (1.0f - depth) + depth * (0.5f + 0.5f * earthPulse.next (7.83 * ratio, sr));
                stonePlace[earth].place (e, e, false, sr, dL, dR, sL, sR);
            }
            // PURE SINE BANK: each voice breathes slowly and drifts a few cents, like analogue oscillators
            {
                float s = 0;
                for (size_t k = 0; k < 6; ++k)
                {
                    const double drift = 1.0 + 0.003 * sineDrift[k].next (sineDriftHz[k], sr);
                    s += sineOsc[k].next (sineFreq[k].next() * drift, sr) * (0.55f + 0.45f * sineTrem[k].next (sineTremHz[k], sr)) * sineAmp[k];
                }
                stonePlace[sine].place (s, s, false, sr, dL, dR, sL, sR);
            }
            // WHITE + BINAURAL: strict left / right pairs a few Hz apart (never summed), plus a soft swaying noise bed
            {
                const double drift = 1.0 + 0.0035 * binDrift.next (0.015, sr);
                float l = 0, r = 0;
                for (size_t k = 0; k < 3; ++k)
                {
                    const double f = binFreq[k].next() * drift;
                    l += binL[k].next (f, sr) * binAmp[k];
                    r += binR[k].next (f + binBeat[k], sr) * binAmp[k];
                }
                const float bed = bedLp.lp (noise.next()) * 0.06f, sway = bedSway.next (0.07, sr);
                l += bed * (0.5f + 0.5f * sway); r += bed * (0.5f - 0.5f * sway);
                stonePlace[binaural].place (l, r, true, sr, dL, dR, sL, sR);
            }
            // GRANULAR CLOUD: resonators on the scale, excited by soft noise and by the incoming signal
            {
                const float ex = noise.next() * 0.18f + mic * cloud.next() * 1.6f;
                if ((resCounter++ & 31) == 0)
                    for (size_t k = 0; k < 4; ++k) res[k].set (resFreq[k].next(), resQ[k], sr);
                float g = 0;
                for (size_t k = 0; k < 4; ++k)
                {
                    winPh[k] += winHz[k] / sr; winPh[k] -= std::floor (winPh[k]);
                    const float w = 0.5f - 0.5f * (float) std::cos (kTwoPi * winPh[k]);
                    g += res[k].bp (ex) * w;
                }
                g *= 0.5f;
                stonePlace[granular].place (g, g, false, sr, dL, dR, sL, sR);
            }
            // FOSSILS
            if (fossils != nullptr)
                for (auto& fp : *fossils) renderFossil (*fp, dL, dR, sL, sR);

            // the incoming signal, receding as you move into the cloud
            const float id = inputDry.next();
            dL += inl * id; dR += inr * id;

            outL[i] = dL; outR[i] = dR;
            sendL[(size_t) i] = revLpL.lp (revHpL.lp (sL)) * 0.35f;
            sendR[(size_t) i] = revLpR.lp (revHpR.lp (sR)) * 0.35f;
        }

        reverb.processStereo (sendL.data(), sendR.data(), n);

        // master: reverb in, soft tape saturation, transparent -1 dBFS brickwall
        const float ceiling = 0.891f, rel = (float) std::exp (-1.0 / (0.12 * sr)), norm = 1.0f / std::tanh (1.2f);
        for (int i = 0; i < n; ++i)
        {
            const float ra = reverbAmt.next(), og = outGain.next();
            float l = (outL[i] + sendL[(size_t) i] * ra * 2.0f) * og * 0.6f;   // headroom: the limiter should only catch rare peaks
            float r = (outR[i] + sendR[(size_t) i] * ra * 2.0f) * og * 0.6f;
            l = std::tanh (1.2f * l) * norm * 0.9f;
            r = std::tanh (1.2f * r) * norm * 0.9f;
            const float pk = std::max (std::abs (l), std::abs (r));
            limEnv = std::max (pk, limEnv * rel);
            const float gain = limEnv > ceiling ? ceiling / limEnv : 1.0f;
            outL[i] = l * gain; outR[i] = r * gain;
            outPeak = std::max (outPeak, pk * gain);
        }
        inputMeter.store (inPeak); outputMeter.store (outPeak);
    }

    void renderFossil (Fossil& f, float& dL, float& dR, float& sL, float& sR)
    {
        const int len = f.audio.getNumSamples();
        if (len < 64) return;
        const float* srcL = f.audio.getReadPointer (0);
        const float* srcR = f.audio.getReadPointer (f.audio.getNumChannels() > 1 ? 1 : 0);
        const double step = f.sourceRate / sr;                          // original pitch at any host rate
        const int grainLen = (int) (0.20 * sr);
        if (--f.untilNext <= 0)
        {
            for (auto& g : f.grains)
                if (! g.on)
                {
                    const double span = std::max (1.0, (double) len - grainLen * step - 2.0);
                    g.pos = f.rng.uni() * span; g.len = grainLen; g.age = 0; g.on = true; g.pan = f.rng.next() * 0.3f;
                    break;
                }
            f.untilNext = grainLen / 3;
        }
        float l = 0, r = 0;
        for (auto& g : f.grains)
        {
            if (! g.on) continue;
            const int i0 = (int) g.pos; const float fr = (float) (g.pos - i0);
            if (i0 + 1 >= len) { g.on = false; continue; }
            const float w = 0.5f - 0.5f * (float) std::cos (kTwoPi * g.age / g.len);
            const float a = srcL[i0] + (srcL[i0 + 1] - srcL[i0]) * fr, b = srcR[i0] + (srcR[i0 + 1] - srcR[i0]) * fr;
            l += a * w * (1.0f - g.pan); r += b * w * (1.0f + g.pan);
            g.pos += step;
            if (++g.age >= g.len) g.on = false;
        }
        l *= 0.66f; r *= 0.66f;
        f.meter = std::max (std::abs (l), f.meter * 0.9995f);
        f.place.place (l, r, true, sr, dL, dR, sL, sR);
    }
};
} // namespace pitch
