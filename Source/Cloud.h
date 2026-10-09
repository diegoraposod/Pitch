#pragma once
// PITCH · the tuned cloud. The same effect the web app put on the microphone, now applied to everything
// PITCH receives AND to every fossil: soft grains scattered in time and transposed by octaves and fifths,
// a bank of resonators that rings only the notes of the key, and (outside this class) a shimmer reverb.
// It is always on: raw audio never leaves PITCH untouched.

// included from Engine.h, after the building blocks (Svf, Glide, Noise, Scale)

// (already inside namespace pitch)
class TunedCloud
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        mem.assign ((size_t) (sr * 2.0) + 8, 0.0f); w = 0;
        for (auto& v : voices) { v.on = false; v.wait = (int) (rng.uni() * 0.08 * sr); }
        inHp.set (100, 0.707f, sr); inLp.set (6000, 0.707f, sr);
        trimAvg = 0.02f; trim.setTime (0.8, sr); trim.snap (1.0f);
        resEnv = 0; levelEnv = 0;
        setScale (buildScale (2, 3), true);
    }

    // the seven notes of the key, folded into 200..400 Hz, in three registers (x1, x2, x4)
    void setScale (const Scale& s, bool snap = false)
    {
        const float fifth = std::pow (2.0f, (float) kModes[s.mode][4] / 12.0f);
        const float pool[6] = { 0.25f, 0.5f, 0.5f, 1.0f, 1.0f, fifth };       // grains: two octaves down .. a fifth up
        for (int i = 0; i < 6; ++i) ratioPool[(size_t) i] = pool[i];
        static constexpr float regs[3] = { 1.0f, 2.0f, 4.0f }, regGain[3] = { 1.0f, 0.7f, 0.45f };
        for (int d = 0; d < 7; ++d)
        {
            float f = s.notes[(size_t) d];
            while (f < 200.0f) f *= 2.0f;
            while (f >= 400.0f) f *= 0.5f;
            for (int k = 0; k < 3; ++k)
            {
                auto& r = res[(size_t) (d * 3 + k)];
                r.freq.target = f * regs[k];
                if (snap) r.freq.snap (f * regs[k]);
                r.freq.setTime (2.0, sr);                                     // a new key glides in over a couple of seconds
                r.gain = regGain[k] * (d % 2 == 0 ? 1.0f : 0.6f);
                r.pan = ((d * 3 + k) % 2 == 0 ? -0.35f : 0.35f);
            }
        }
    }

    // mono in (raw input + fossils), stereo out: the direct cloud and what goes to the reverb
    inline void tick (float x, float& outL, float& outR)
    {
        // 1. level: quiet sources are lifted, loud ones tamed, so the cloud always sits at a similar level
        const float a = std::abs (x);
        trimAvg += (a - trimAvg) * 0.00002f;
        trim.target = clampf (0.035f / std::max (trimAvg, 0.0015f), 0.8f, 6.0f);
        x = inLp.lp (inHp.hp (x)) * trim.next();
        // transients off: a fast and a slow envelope; when the fast one jumps ahead (an attack), the signal is held back
        const float ax = std::abs (x);
        fastEnv += (ax - fastEnv) * (ax > fastEnv ? 0.02f : 0.0015f);
        slowEnv += (ax - slowEnv) * (ax > slowEnv ? 0.0005f : 0.0002f);
        x *= std::min (1.0f, (slowEnv + 1.0e-5f) / (fastEnv + 1.0e-5f));
        levelEnv = std::max (std::abs (x), levelEnv * 0.9997f);
        if (levelEnv > 0.35f) x *= 0.35f / levelEnv;

        // 2. memory for the grains
        const int len = (int) mem.size();
        mem[(size_t) w] = x;

        // 3. grains: each voice plays one grain after another, from 0.12 to 1.6 s back, transposed by the pool
        float gl = 0, gr = 0;
        for (auto& v : voices)
        {
            if (! v.on)
            {
                if (--v.wait > 0) continue;
                v.len = (int) ((0.05 + rng.uni() * 0.05) * sr);
                v.ratio = ratioPool[(size_t) std::min (5, (int) (rng.uni() * 6.0f))];
                const double d0 = (0.12 + rng.uni() * 1.48) * sr;
                v.pos = (double) w - d0; while (v.pos < 0) v.pos += len;
                const float p = rng.next() * 0.85f, ang = (p + 1.0f) * 0.25f * juce::MathConstants<float>::pi;
                v.gl = std::cos (ang) * 1.4142f; v.gr = std::sin (ang) * 1.4142f;
                v.age = 0; v.on = true;
            }
            const int i0 = (int) v.pos, i1 = (i0 + 1) % len; const float fr = (float) (v.pos - i0);
            const float s = mem[(size_t) i0] + (mem[(size_t) i1] - mem[(size_t) i0]) * fr;
            const float ph = (float) v.age / (float) v.len, win = std::sin (juce::MathConstants<float>::pi * ph);
            const float y = s * win * win;
            gl += y * v.gl; gr += y * v.gr;
            v.pos += v.ratio; if (v.pos >= len) v.pos -= len;
            if (++v.age >= v.len) { v.on = false; v.wait = (int) ((0.001 + rng.uni() * 0.036) * sr); }
        }
        if (++w >= len) w = 0;
        gl *= 0.6f / 3.0f; gr *= 0.6f / 3.0f;                                     // 10 voices overlap: keep the sum calm

        // 4. resonators on the key, rung by the grains
        // the excitation is the sound's energy, whitened with noise, so ANY pitch you sing rings the key (never your own pitch)
        const float gm = 0.5f * (gl + gr);
        excEnv += (std::abs (gm) - excEnv) * 0.004f;
        const float rin = gm * whiteMix[0] + rng.next() * excEnv * whiteMix[1];
        if ((resCounter++ & 31) == 0)
            for (auto& r : res) { const float f = r.freq.next(); r.f.set (f, 55.0f, sr); r.f2.a1 = r.f.a1; r.f2.a2 = r.f.a2; r.f2.a3 = r.f.a3; r.f2.k = r.f.k; }
        float rl = 0, rr = 0;
        for (auto& r : res)
        {
            const float y = r.f2.bp (r.f.bp (rin)) * r.gain * 2.7f;              // two stages: steep skirts, only the note survives
            rl += y * (1.0f - r.pan); rr += y * (1.0f + r.pan);
        }
        rl *= 10.0f; rr *= 10.0f;                                                  // tuned gain, then a slow compressor
        const float ra = std::max (std::abs (rl), std::abs (rr));
        resEnv = ra > resEnv ? resEnv + (ra - resEnv) * 0.002f : resEnv * 0.99997f;
        if (resEnv > 0.07f) { const float g = std::pow (0.07f / resEnv, 0.75f); rl *= g; rr *= g; }

        // what you hear is the key ringing; the grains themselves stay a faint, blurred texture underneath
        outL = 0.03f * gl + rl;
        outR = 0.03f * gr + rr;
    }

private:
    struct Voice { double pos = 0; float ratio = 1, gl = 1, gr = 1; int len = 1, age = 0, wait = 0; bool on = false; };
    struct Res { Svf f, f2; Glide freq; float gain = 1, pan = 0; };

    double sr = 48000.0;
    std::vector<float> mem; int w = 0;
    std::array<Voice, 10> voices;
    std::array<float, 6> ratioPool { 0.25f, 0.5f, 0.5f, 1.0f, 1.0f, 1.5f };
    std::array<Res, 21> res;
    int resCounter = 0;
    Svf inHp, inLp;
    Glide trim; float trimAvg = 0.02f, levelEnv = 0, resEnv = 0, fastEnv = 0, slowEnv = 0, excEnv = 0;
    float whiteMix[2] = { 0.2f, 3.0f };
    Noise rng;
};
