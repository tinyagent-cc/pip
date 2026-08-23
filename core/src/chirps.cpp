#include "pip/chirps.hpp"
#include <cmath>
namespace pip {
namespace {
constexpr double TWO_PI = 6.283185307179586;
constexpr size_t ATTACK = AUDIO_RATE / 100;      // 10 ms
constexpr size_t RELEASE = AUDIO_RATE * 3 / 100; // 30 ms

// One piece of a recipe: a linear frequency sweep f0 -> f1 over ms milliseconds, or a
// silent gap. Everything Pip says is a handful of these.
struct Seg { double f0, f1; int ms; bool silent; };

struct Recipe {
    const Seg* segs; size_t nsegs;
    double amp;                 // peak sample value before the envelope
    bool per_seg_env;           // notes separated by gaps need their own edges, or they click
    double am_hz, am_depth;     // amplitude modulation, 0 for none
};

const Seg kRise[]  = {{600, 1200, 200, false}};
const Seg kTrill[] = {{1000, 1000, 60, false}, {1300, 1300, 60, false}, {1000, 1000, 60, false},
                      {1300, 1300, 60, false}, {1000, 1000, 60, false}};
const Seg kDrop[]  = {{1000, 400, 250, false}};
const Seg kPurr[]  = {{300, 300, 400, false}};
const Seg kBoot[]  = {{523, 523, 120, false}, {0, 0, 10, true}, {659, 659, 120, false},
                      {0, 0, 10, true}, {784, 784, 120, false}};
const Seg kSad[]   = {{500, 500, 200, false}, {350, 350, 250, false}};

const Recipe kRecipes[] = {
    {kRise,  1, 12000, false, 0, 0},
    {kTrill, 5, 12000, false, 0, 0},
    {kDrop,  1, 12000, false, 0, 0},
    {kPurr,  1,  8000, false, 40, 0.8},
    {kBoot,  5, 12000, true,  0, 0},
    {kSad,   2, 10000, false, 0, 0},
};
static_assert(sizeof(kRecipes) / sizeof(kRecipes[0]) == (size_t)Chirp::Count,
              "chirp recipes out of step with the enum");

// Linear attack in, linear release out, flat between. idx and len are in samples.
double envelope(size_t idx, size_t len) {
    double g = 1.0;
    if (idx < ATTACK) g = (double)idx / (double)ATTACK;
    if (len > RELEASE && idx >= len - RELEASE) {
        double r = (double)(len - idx) / (double)RELEASE;
        if (r < g) g = r;
    }
    return g;
}
}

size_t render_chirp(Chirp c, int16_t* out, size_t cap) {
    if ((uint8_t)c >= (uint8_t)Chirp::Count || !out || !cap) return 0;
    const Recipe& r = kRecipes[(uint8_t)c];
    size_t total = 0;
    for (size_t s = 0; s < r.nsegs; ++s) total += (size_t)r.segs[s].ms * AUDIO_RATE / 1000;
    if (total > CHIRP_MAX_SAMPLES) total = CHIRP_MAX_SAMPLES;
    const size_t n = total < cap ? total : cap;
    // The phase runs across segment boundaries so a two-note chirp has no click in the
    // middle; only a silent gap breaks it, and there the amplitude is zero anyway.
    double phase = 0.0;
    size_t i = 0;
    for (size_t s = 0; s < r.nsegs && i < n; ++s) {
        const Seg& sg = r.segs[s];
        const size_t len = (size_t)sg.ms * AUDIO_RATE / 1000;
        for (size_t k = 0; k < len && i < n; ++k, ++i) {
            if (sg.silent) { out[i] = 0; continue; }
            const double f = sg.f0 + (sg.f1 - sg.f0) * ((double)k / (double)len);
            phase += TWO_PI * f / AUDIO_RATE;
            if (phase >= TWO_PI) phase -= TWO_PI;
            double g = r.amp;
            if (r.am_hz > 0.0) {
                const double m = 0.5 + 0.5 * std::sin(TWO_PI * r.am_hz * (double)i / AUDIO_RATE);
                g *= (1.0 - r.am_depth) + r.am_depth * m;
            }
            g *= r.per_seg_env ? envelope(k, len) : envelope(i, total);
            out[i] = (int16_t)std::lround(std::sin(phase) * g);
        }
    }
    return n;
}
}
