#pragma once
#include "../DSP/Common.h"

namespace vlab
{
    struct ActiveSound
    {
        std::shared_ptr<const vlab::AudioBuffer> data;
        float volume = 1.0f;
        float speed  = 1.0f;
        bool  loop    = false;
        bool  reverse = false;
        bool  finished = false;
        int   pos = 0; // sample index into data
        int   slotIdx = -1; // -1 means "not tied to a slot" (e.g. TTS)

        ActiveSound() = default;
        ActiveSound (std::shared_ptr<const vlab::AudioBuffer> d, float vol, float spd,
                     bool lp, bool rev, int slot = -1)
            : data (std::move (d)), volume (vol), speed (spd), loop (lp), reverse (rev),
              pos (rev && data ? data->getNumSamples() - 1 : 0), slotIdx (slot)
        {}
    };
}
