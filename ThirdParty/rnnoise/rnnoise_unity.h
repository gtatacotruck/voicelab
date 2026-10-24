#pragma once
// RNNoise v0.1.1 (github.com/xiph/rnnoise, BSD-3, see COPYING) included as one block so no extra .c files are
// needed in the project. the only local change is a few casts in denoise.c so it builds as C++.
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <assert.h>
#include <float.h>
#include <limits.h>

#ifndef M_PI
 #define M_PI 3.14159265358979323846
#endif

namespace vlab_rnnoise
{
    #include "rnnoise.h"
    #include "kiss_fft.c"
    #include "celt_lpc.c"
    #include "pitch.c"
    #include "rnn.c"
    #include "rnn_data.c"
    #include "denoise.c"
}
