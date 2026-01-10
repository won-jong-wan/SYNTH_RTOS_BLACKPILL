// biquad.c
#include "user_rtos.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

void biquad_reset(Biquad *q)
{
    if (!q) return;
    q->x1 = q->x2 = 0.0f;
    q->y1 = q->y2 = 0.0f;
}

void biquad_set_lpf(Biquad *q, float Fs, float Fc, float Q)
{
    if (!q) return;

    // safety clamp
    if (Fs <= 0.0f) Fs = 48000.0f;
    if (Fc < 1.0f) Fc = 1.0f;
    float nyq = Fs * 0.5f;
    if (Fc > nyq * 0.45f) Fc = nyq * 0.45f;
    if (Q < 0.1f) Q = 0.1f;

    float w0   = 2.0f * (float)M_PI * Fc / Fs;
    float cos0 = cosf(w0);
    float sin0 = sinf(w0);
    float alpha = sin0 / (2.0f * Q);

    // RBJ low-pass (unnormalized)
    float b0 = (1.0f - cos0) * 0.5f;
    float b1 =  1.0f - cos0;
    float b2 = (1.0f - cos0) * 0.5f;
    float a0 =  1.0f + alpha;
    float a1 = -2.0f * cos0;
    float a2 =  1.0f - alpha;

    // normalize so that a0 == 1
    q->b0 = b0 / a0;
    q->b1 = b1 / a0;
    q->b2 = b2 / a0;
    q->a1 = a1 / a0;
    q->a2 = a2 / a0;
}

float biquad_process(Biquad *q, float x)
{
    // Direct Form I
    float y = q->b0 * x
            + q->b1 * q->x1
            + q->b2 * q->x2
            - q->a1 * q->y1
            - q->a2 * q->y2;

    q->x2 = q->x1;
    q->x1 = x;
    q->y2 = q->y1;
    q->y1 = y;

    return y;
}
