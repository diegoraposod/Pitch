#pragma once
// PITCH · the shared map: where the stones are, and how distance becomes sound.
// Used by the processor (to mix) and by the editor (to draw), so both always agree.

#include <cmath>
#include <algorithm>

namespace pitch
{
inline float clampf (float v, float a, float b) { return std::min (b, std::max (a, v)); }
inline float smooth01 (float t) { t = clampf (t, 0.0f, 1.0f); return t * t * (3.0f - 2.0f * t); }

enum StoneId { earth = 0, binaural, granular, sine, numStones };

struct StoneDef { const char* label; float x, y, r; bool isEarth; };

// A wide universe: the stones are far apart so each drone can be heard on its own.
inline const StoneDef stones[numStones] = {
    { "EARTH",            0.0f,    0.0f,  30.0f, true  },
    { "WHITE + BINAURAL", -520.0f, -260.0f, 72.0f, false },
    { "GRANULAR",          640.0f,  180.0f, 94.0f, false },
    { "PURE SINE",        -120.0f,  600.0f, 64.0f, false },
};

constexpr float kRange    = 900.0f;    // maxDistance: beyond this a stone is silent
constexpr float kWorld    = 2000.0f;   // the core lives inside -2000 .. 2000
constexpr float kFossilR  = 24.0f;

// Everything distance decides for one stone.
struct Spatial
{
    float gain = 0;       // presence (before the user's level)
    float near = 0;       // 0 far .. 1 beside it (visual warmth, dry/wet balance)
    float intensity = 0;  // straight-line closeness used for the air-absorption filter
    float cutoff = 200;   // Hz
    float pan = 0;        // -1 .. 1
    float dry = 0;        // share sent straight to the mix
    float send = 0;       // share sent to the reverb (far = wetter)
};

inline Spatial spatialFor (float sx, float sy, float r, bool isEarth, float coreX, float coreY)
{
    Spatial s;
    const float dx = sx - coreX, dy = sy - coreY, d = std::sqrt (dx * dx + dy * dy);
    const float ref = r * (isEarth ? 2.2f : 1.6f);
    s.gain = clampf (1.0f - std::max (0.0f, d - ref) / (kRange - ref), 0.0f, 1.0f) * 0.85f;   // full inside, then a straight line to silence
    s.near = smooth01 (1.0f - d / kRange);
    s.intensity = clampf (1.0f - d / kRange, 0.0f, 1.0f);
    s.cutoff = 200.0f + s.intensity * 8000.0f;                                                 // air absorption: 200 Hz far, 8.2 kHz beside it
    s.pan = clampf (dx / (kRange * 0.55f), -1.0f, 1.0f) * 0.9f;
    const float depth = clampf (-dy / kRange, -1.0f, 1.0f);                                    // above you = further back in the room
    s.dry = smooth01 (clampf (s.near * 1.15f + 0.2f, 0.0f, 1.0f)) * (1.0f - 0.3f * std::max (0.0f, depth));
    s.send = (0.45f + 1.6f * (1.0f - s.near)) * (1.0f + 0.7f * depth);
    return s;
}
} // namespace pitch
