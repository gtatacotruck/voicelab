#pragma once
// Autotune: YIN pitch detection, snap to scale, TD-PSOLA resynthesis. No JUCE dependency.
// Mono, fixed latency of kLatencySamples. Unvoiced parts pass through the delayed dry signal. untested...test more later.
#include <array>
#include <atomic>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace vlab::dsp
{
    class AutoTune
    {
    public:
        enum Scale { Chromatic = 0, Major, Minor, HarmonicMinor, PentatonicMajor, PentatonicMinor, Blues, Dorian, Mixolydian, NumScales };

        static const char* scaleName (int s)
        {
            static const char* names[] = { "Chromatic", "Major", "Minor", "Harmonic Minor", "Pentatonic Major",
                                           "Pentatonic Minor", "Blues", "Dorian", "Mixolydian" };
            return names[std::clamp (s, 0, (int) NumScales - 1)];
        }

        static const char* noteName (int pitchClass)
        {
            static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
            return names[((pitchClass % 12) + 12) % 12];
        }

        struct Settings
        {
            int   key = 0;            // 0 = C
            int   scale = Chromatic;
            float retuneMs = 20.0f;   // 0 = instant
            float amount = 1.0f;
            float refHz = 440.0f;
            float maxShiftSemis = 6.0f; // bigger corrections are ignored
        };

        static constexpr int kLatencySamples = 1280;

        void prepare (double sampleRateIn)
        {
            sampleRate = sampleRateIn;
            reset();
        }

        void reset()
        {
            std::fill (inRing.begin(), inRing.end(), 0.0f);
            std::fill (acc.begin(), acc.end(), 0.0f);
            std::fill (cover.begin(), cover.end(), 0.0f);
            std::fill (decRing.begin(), decRing.end(), 0.0f);
            inIdx = 0;
            decIdx = 0;
            decPhase = 0;
            decX1 = decX2 = decX3 = 0.0f;
            sinceHop = 0;
            hopCount = 0;
            for (auto& h : hops) h = Hop {};
            unvoicedRun = 0;
            chainActive = false;
            synthPos = 0.0;
            anaPos = 0.0;
            deltaSmoothed = 0.0f;
            haveNote = false;
            currentNote = 0;
            medianHist = { 0.0f, 0.0f, 0.0f };
            detectedMidi.store (0.0f);
            targetMidi.store (0.0f);
            voicedNow.store (false);
        }

        void process (const float* in, float* out, int numSamples, const Settings& s)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float x = in[i];
                inRing[(size_t) (inIdx & kRingMask)] = x;
                pushDecimated (x);
                ++inIdx;
                if (++sinceHop >= kHop)
                {
                    sinceHop = 0;
                    runDetection();
                }
            }
            const int64_t inEnd = inIdx;

            const int64_t emitStart = inEnd - numSamples - kLatencySamples;
            const int64_t emitEnd   = inEnd - kLatencySamples;

            schedule (inEnd, emitStart, emitEnd, s);

            for (int64_t p = emitStart; p < emitEnd; ++p)
            {
                const size_t k = (size_t) (p & kRingMask);
                const float dry = p >= 0 ? inRing[k] : 0.0f;
                const float wet = acc[k];
                const float cov = cover[k];
                acc[k] = 0.0f;
                cover[k] = 0.0f;

                float y;
                if (cov >= 1.0f)      y = wet / cov;
                else                  y = wet + dry * (1.0f - cov);
                out[p - emitStart] = y;
            }
        }

        std::atomic<float> detectedMidi { 0.0f };
        std::atomic<float> targetMidi { 0.0f };
        std::atomic<bool>  voicedNow { false };

    private:
        static constexpr int kRing = 8192, kRingMask = kRing - 1;
        static constexpr int kHop = 256;
        static constexpr int kDecRing = 2048, kDecMask = kDecRing - 1;
        static constexpr int kTauMin = 22;     // ~1000 Hz at the decimated rate
        static constexpr int kTauMax = 276;    // ~80 Hz
        static constexpr int kWin = 400;
        static constexpr float kMaxPeriod = 2.0f * (float) kTauMax;
        static constexpr int kHopHistory = 128;

        struct Hop
        {
            int64_t center = 0;
            float   period = 0.0f;
            bool    voiced = false;
        };

        double sampleRate = 44100.0;

        std::array<float, kRing> inRing {}, acc {}, cover {};
        int64_t inIdx = 0;

        std::array<float, kDecRing> decRing {};
        int64_t decIdx = 0;
        int decPhase = 0;
        float decX1 = 0.0f, decX2 = 0.0f, decX3 = 0.0f;

        int sinceHop = 0;
        int64_t hopCount = 0;
        std::array<Hop, kHopHistory> hops {};
        int unvoicedRun = 0;
        std::array<float, 3> medianHist { 0.0f, 0.0f, 0.0f };

        bool chainActive = false;
        double synthPos = 0.0, anaPos = 0.0;
        float deltaSmoothed = 0.0f;
        bool haveNote = false;
        int currentNote = 0;

        void pushDecimated (float x)
        {
            const float lp = 0.25f * x + 0.5f * decX1 + 0.25f * decX2;
            decX3 = decX2; decX2 = decX1; decX1 = x;
            if ((decPhase++ & 1) == 0)
                decRing[(size_t) (decIdx++ & kDecMask)] = lp;
        }

        // ── YIN ──────────────────────────────────────────────────────────────
        void runDetection()
        {
            constexpr int needed = kWin + kTauMax + 1;
            if (decIdx < needed)
                return;

            std::array<float, needed> seg;
            const int64_t base = decIdx - needed;
            double energy = 0.0;
            for (int i = 0; i < needed; ++i)
                seg[(size_t) i] = decRing[(size_t) ((base + i) & kDecMask)];
            for (int i = 0; i < kWin; ++i)
                energy += (double) seg[(size_t) (i + kTauMax / 2)] * seg[(size_t) (i + kTauMax / 2)];
            const float rms = (float) std::sqrt (energy / kWin);

            std::array<float, kTauMax + 1> d {};
            for (int tau = 1; tau <= kTauMax; ++tau)
            {
                double sum = 0.0;
                for (int j = 0; j < kWin; ++j)
                {
                    const float diff = seg[(size_t) j] - seg[(size_t) (j + tau)];
                    sum += (double) diff * diff;
                }
                d[(size_t) tau] = (float) sum;
            }

            std::array<float, kTauMax + 1> cmnd {};
            cmnd[0] = 1.0f;
            double running = 0.0;
            for (int tau = 1; tau <= kTauMax; ++tau)
            {
                running += d[(size_t) tau];
                cmnd[(size_t) tau] = running > 1e-12 ? (float) (d[(size_t) tau] * tau / running) : 1.0f;
            }

            // first dip under the threshold, then down to its minimum (avoids octave errors)
            constexpr float kThreshold = 0.20f;
            int best = -1;
            for (int tau = kTauMin; tau < kTauMax; ++tau)
            {
                if (cmnd[(size_t) tau] < kThreshold)
                {
                    while (tau + 1 < kTauMax && cmnd[(size_t) (tau + 1)] < cmnd[(size_t) tau])
                        ++tau;
                    best = tau;
                    break;
                }
            }

            Hop h;
            h.center = inIdx - (int64_t) needed;
            h.voiced = false;

            if (best > 0 && rms > 0.0015f)
            {
                float tauF = (float) best;
                if (best > 1 && best < kTauMax)
                {
                    const float a = cmnd[(size_t) (best - 1)], b = cmnd[(size_t) best], c = cmnd[(size_t) (best + 1)];
                    const float denom = a - 2.0f * b + c;
                    if (std::abs (denom) > 1e-9f)
                        tauF += juce_clamp (0.5f * (a - c) / denom, -1.0f, 1.0f);
                }
                float period = tauF * 2.0f;

                medianHist = { medianHist[1], medianHist[2], period };
                if (medianHist[0] > 0.0f)
                {
                    auto m = medianHist;
                    std::sort (m.begin(), m.end());
                    period = m[1];
                }
                h.period = period;
                h.voiced = true;
                unvoicedRun = 0;
            }
            else
            {
                medianHist = { 0.0f, 0.0f, 0.0f };
                // hold the last pitch for a couple of failed frames
                if (++unvoicedRun <= 2)
                {
                    const Hop& prev = hops[(size_t) ((hopCount + kHopHistory - 1) % kHopHistory)];
                    if (prev.voiced) { h.voiced = true; h.period = prev.period; }
                }
            }

            hops[(size_t) (hopCount % kHopHistory)] = h;
            ++hopCount;
        }

        static float juce_clamp (float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

        Hop lookup (double t) const
        {
            if (hopCount == 0)
                return Hop {};
            int64_t i = hopCount - 1;
            const int64_t oldest = std::max<int64_t> (0, hopCount - kHopHistory + 1);
            while (i > oldest && (double) hops[(size_t) (i % kHopHistory)].center > t)
                --i;
            return hops[(size_t) (i % kHopHistory)];
        }

        // ── musical logic ────────────────────────────────────────────────────
        static unsigned scaleMask (int scale)
        {
            auto m = [] (std::initializer_list<int> notes) { unsigned r = 0; for (int n : notes) r |= 1u << n; return r; };
            switch (scale)
            {
                case Major:           return m ({ 0, 2, 4, 5, 7, 9, 11 });
                case Minor:           return m ({ 0, 2, 3, 5, 7, 8, 10 });
                case HarmonicMinor:   return m ({ 0, 2, 3, 5, 7, 8, 11 });
                case PentatonicMajor: return m ({ 0, 2, 4, 7, 9 });
                case PentatonicMinor: return m ({ 0, 3, 5, 7, 10 });
                case Blues:           return m ({ 0, 3, 5, 6, 7, 10 });
                case Dorian:          return m ({ 0, 2, 3, 5, 7, 9, 10 });
                case Mixolydian:      return m ({ 0, 2, 4, 5, 7, 9, 10 });
                default:              return 0xFFFu;
            }
        }

        int nearestAllowed (float midi, unsigned mask, int key) const
        {
            int best = (int) std::lround (midi);
            float bestDist = 1e9f;
            for (int k = (int) std::floor (midi) - 3; k <= (int) std::ceil (midi) + 3; ++k)
            {
                const int pc = (((k - key) % 12) + 12) % 12;
                if (! (mask & (1u << pc)))
                    continue;
                const float dist = std::abs ((float) k - midi);
                if (dist < bestDist) { bestDist = dist; best = k; }
            }
            return best;
        }

        float computeRatio (float period, float outputSpacingGuess, const Settings& s)
        {
            const float f = (float) sampleRate / period;
            const float midi = 69.0f + 12.0f * std::log2 (f / std::max (300.0f, s.refHz));
            const unsigned mask = scaleMask (s.scale);

            int cand = nearestAllowed (midi, mask, s.key);
            if (! haveNote) { currentNote = cand; haveNote = true; }
            else if (cand != currentNote)
            {
                const float dCur = std::abs (midi - (float) currentNote);
                const float dNew = std::abs (midi - (float) cand);
                if (dNew + 0.12f < dCur)
                    currentNote = cand;
            }

            float delta = (float) currentNote - midi;
            if (std::abs (delta) > s.maxShiftSemis)
                delta = 0.0f;

            const float dtMs = outputSpacingGuess / (float) sampleRate * 1000.0f;
            const float alpha = s.retuneMs <= 0.5f ? 1.0f : 1.0f - std::exp (-dtMs / s.retuneMs);
            deltaSmoothed += (delta - deltaSmoothed) * alpha;

            const float shift = deltaSmoothed * juce_clamp (s.amount, 0.0f, 1.0f);
            detectedMidi.store (midi);
            targetMidi.store (midi + shift);
            return std::exp2 (shift / 12.0f);
        }

        // ── PSOLA ────────────────────────────────────────────────────────────
        float readInput (double pos) const
        {
            const int64_t i0 = (int64_t) std::floor (pos);
            const float frac = (float) (pos - (double) i0);
            const float a = inRing[(size_t) (i0 & kRingMask)];
            const float b = inRing[(size_t) ((i0 + 1) & kRingMask)];
            return a + (b - a) * frac;
        }

        void placeGrain (double centerOut, double centerIn, float halfLen, float gain)
        {
            const int64_t first = (int64_t) std::ceil (centerOut - (double) halfLen);
            const int64_t last  = (int64_t) std::floor (centerOut + (double) halfLen);
            constexpr float kPi = 3.14159265358979f;
            for (int64_t p = first; p <= last; ++p)
            {
                const float off = (float) ((double) p - centerOut);
                const float w = 0.5f + 0.5f * std::cos (kPi * off / halfLen);
                const float x = readInput (centerIn + (double) off);
                const size_t k = (size_t) (p & kRingMask);
                acc[k]   += gain * w * x;
                cover[k] += gain * w;
            }
        }

        void schedule (int64_t inEnd, int64_t emitStart, int64_t emitEnd, const Settings& s)
        {
            bool startedThisCall = false;
            for (int guard = 0; guard < 64; ++guard)
            {
                if (! chainActive)
                {
                    if (startedThisCall)
                        return;
                    const Hop h = lookup ((double) inEnd);
                    if (! h.voiced || h.period < 40.0f)
                    {
                        voicedNow.store (false);
                        detectedMidi.store (0.0f);
                        return;
                    }
                    chainActive = true;
                    startedThisCall = true;
                    haveNote = false;
                    deltaSmoothed = 0.0f;
                    synthPos = std::max (synthPos, (double) emitStart + (double) h.period);
                    anaPos = synthPos;
                }

                const Hop h = lookup (synthPos);
                if (! h.voiced || h.period < 40.0f)
                {
                    chainActive = false;
                    voicedNow.store (false);
                    detectedMidi.store (0.0f);
                    continue;
                }

                const float period = juce_clamp (h.period, 40.0f, kMaxPeriod);

                const float guessHalf = period * 1.45f;
                if (synthPos - (double) guessHalf >= (double) emitEnd)
                    return;

                const float ratio = juce_clamp (computeRatio (period, period, s), 0.5f, 2.0f);
                const float outPeriod = period / ratio;
                const float half = std::max (period, outPeriod);
                if (synthPos - (double) half >= (double) emitEnd)
                {
                    return;
                }

                const double steps = std::floor ((synthPos - anaPos) / (double) period);
                if (steps > 0.0)
                    anaPos += steps * (double) period;
                const double overshoot = anaPos + (double) half - (double) (inEnd - 2);
                if (overshoot > 0.0)
                    anaPos -= std::ceil (overshoot / (double) period) * (double) period;

                if (synthPos - (double) half < (double) emitStart)
                {
                    synthPos += (double) outPeriod;
                    continue;
                }

                placeGrain (synthPos, anaPos, half, outPeriod / half);
                voicedNow.store (true);
                synthPos += (double) outPeriod;
            }
        }
    };
}
