#include "kernel_compare.h"

#include <arm_neon.h>

// intrinsics for <source_file>: lines <start>-<end>
// Adapt this implementation to match the target asm block.
void kernel_intrinsics(
    float *outptr0,
    float *outptr1,
    float *outptr2,
    float *outptr3,
    float *outptr4,
    float *outptr5,
    float *outptr6,
    float *outptr7,
    const float *tmpptr,
    const float *kptr,
    const float *biasptr,
    int nn,
    int mode)
{
    if (mode == 0)
    {
        // 8x8 tile: each output pointer receives 8 floats (2 x float32x4_t)
        float32x4_t _bias0 = vld1q_f32(biasptr);
        float32x4_t _bias1 = vld1q_f32(biasptr + 4);

        float32x4_t _sum0 = vdupq_laneq_f32(_bias0, 0);
        float32x4_t _sum1 = vdupq_laneq_f32(_bias0, 0);
        float32x4_t _sum2 = vdupq_laneq_f32(_bias0, 1);
        float32x4_t _sum3 = vdupq_laneq_f32(_bias0, 1);
        float32x4_t _sum4 = vdupq_laneq_f32(_bias0, 2);
        float32x4_t _sum5 = vdupq_laneq_f32(_bias0, 2);
        float32x4_t _sum6 = vdupq_laneq_f32(_bias0, 3);
        float32x4_t _sum7 = vdupq_laneq_f32(_bias0, 3);
        float32x4_t _sum8 = vdupq_laneq_f32(_bias1, 0);
        float32x4_t _sum9 = vdupq_laneq_f32(_bias1, 0);
        float32x4_t _suma = vdupq_laneq_f32(_bias1, 1);
        float32x4_t _sumb = vdupq_laneq_f32(_bias1, 1);
        float32x4_t _sumc = vdupq_laneq_f32(_bias1, 2);
        float32x4_t _sumd = vdupq_laneq_f32(_bias1, 2);
        float32x4_t _sume = vdupq_laneq_f32(_bias1, 3);
        float32x4_t _sumf = vdupq_laneq_f32(_bias1, 3);

        int nn4 = nn >> 2;
        for (; nn4 > 0; nn4--)
        {
            float32x4_t _p0 = vld1q_f32(tmpptr);
            float32x4_t _p1 = vld1q_f32(tmpptr + 4);
            float32x4_t _p2 = vld1q_f32(tmpptr + 8);
            float32x4_t _p3 = vld1q_f32(tmpptr + 12);
            tmpptr += 16;

            float32x4_t _k0 = vld1q_f32(kptr);
            float32x4_t _k1 = vld1q_f32(kptr + 4);
            float32x4_t _k2 = vld1q_f32(kptr + 8);
            float32x4_t _k3 = vld1q_f32(kptr + 12);
            kptr += 16;

            _sum0 = vfmaq_laneq_f32(_sum0, _p0, _k0, 0);
            _sum2 = vfmaq_laneq_f32(_sum2, _p0, _k0, 1);
            _sum4 = vfmaq_laneq_f32(_sum4, _p0, _k0, 2);
            _sum6 = vfmaq_laneq_f32(_sum6, _p0, _k0, 3);
            _sum1 = vfmaq_laneq_f32(_sum1, _p1, _k0, 0);
            _sum3 = vfmaq_laneq_f32(_sum3, _p1, _k0, 1);
            _sum5 = vfmaq_laneq_f32(_sum5, _p1, _k0, 2);
            _sum7 = vfmaq_laneq_f32(_sum7, _p1, _k0, 3);

            _sum8 = vfmaq_laneq_f32(_sum8, _p0, _k1, 0);
            _suma = vfmaq_laneq_f32(_suma, _p0, _k1, 1);
            _sumc = vfmaq_laneq_f32(_sumc, _p0, _k1, 2);
            _sume = vfmaq_laneq_f32(_sume, _p0, _k1, 3);
            _sum9 = vfmaq_laneq_f32(_sum9, _p1, _k1, 0);
            _sumb = vfmaq_laneq_f32(_sumb, _p1, _k1, 1);
            _sumd = vfmaq_laneq_f32(_sumd, _p1, _k1, 2);
            _sumf = vfmaq_laneq_f32(_sumf, _p1, _k1, 3);

            float32x4_t _p4 = vld1q_f32(tmpptr);
            float32x4_t _p5 = vld1q_f32(tmpptr + 4);
            float32x4_t _p6 = vld1q_f32(tmpptr + 8);
            float32x4_t _p7 = vld1q_f32(tmpptr + 12);
            tmpptr += 16;

            _sum0 = vfmaq_laneq_f32(_sum0, _p2, _k2, 0);
            _sum2 = vfmaq_laneq_f32(_sum2, _p2, _k2, 1);
            _sum4 = vfmaq_laneq_f32(_sum4, _p2, _k2, 2);
            _sum6 = vfmaq_laneq_f32(_sum6, _p2, _k2, 3);
            _sum1 = vfmaq_laneq_f32(_sum1, _p3, _k2, 0);
            _sum3 = vfmaq_laneq_f32(_sum3, _p3, _k2, 1);
            _sum5 = vfmaq_laneq_f32(_sum5, _p3, _k2, 2);
            _sum7 = vfmaq_laneq_f32(_sum7, _p3, _k2, 3);

            _sum8 = vfmaq_laneq_f32(_sum8, _p2, _k3, 0);
            _suma = vfmaq_laneq_f32(_suma, _p2, _k3, 1);
            _sumc = vfmaq_laneq_f32(_sumc, _p2, _k3, 2);
            _sume = vfmaq_laneq_f32(_sume, _p2, _k3, 3);
            _sum9 = vfmaq_laneq_f32(_sum9, _p3, _k3, 0);
            _sumb = vfmaq_laneq_f32(_sumb, _p3, _k3, 1);
            _sumd = vfmaq_laneq_f32(_sumd, _p3, _k3, 2);
            _sumf = vfmaq_laneq_f32(_sumf, _p3, _k3, 3);

            _k0 = vld1q_f32(kptr);
            _k1 = vld1q_f32(kptr + 4);
            _k2 = vld1q_f32(kptr + 8);
            _k3 = vld1q_f32(kptr + 12);
            kptr += 16;

            _sum0 = vfmaq_laneq_f32(_sum0, _p4, _k0, 0);
            _sum2 = vfmaq_laneq_f32(_sum2, _p4, _k0, 1);
            _sum4 = vfmaq_laneq_f32(_sum4, _p4, _k0, 2);
            _sum6 = vfmaq_laneq_f32(_sum6, _p4, _k0, 3);
            _sum1 = vfmaq_laneq_f32(_sum1, _p5, _k0, 0);
            _sum3 = vfmaq_laneq_f32(_sum3, _p5, _k0, 1);
            _sum5 = vfmaq_laneq_f32(_sum5, _p5, _k0, 2);
            _sum7 = vfmaq_laneq_f32(_sum7, _p5, _k0, 3);

            _sum8 = vfmaq_laneq_f32(_sum8, _p4, _k1, 0);
            _suma = vfmaq_laneq_f32(_suma, _p4, _k1, 1);
            _sumc = vfmaq_laneq_f32(_sumc, _p4, _k1, 2);
            _sume = vfmaq_laneq_f32(_sume, _p4, _k1, 3);
            _sum9 = vfmaq_laneq_f32(_sum9, _p5, _k1, 0);
            _sumb = vfmaq_laneq_f32(_sumb, _p5, _k1, 1);
            _sumd = vfmaq_laneq_f32(_sumd, _p5, _k1, 2);
            _sumf = vfmaq_laneq_f32(_sumf, _p5, _k1, 3);

            _sum0 = vfmaq_laneq_f32(_sum0, _p6, _k2, 0);
            _sum2 = vfmaq_laneq_f32(_sum2, _p6, _k2, 1);
            _sum4 = vfmaq_laneq_f32(_sum4, _p6, _k2, 2);
            _sum6 = vfmaq_laneq_f32(_sum6, _p6, _k2, 3);
            _sum1 = vfmaq_laneq_f32(_sum1, _p7, _k2, 0);
            _sum3 = vfmaq_laneq_f32(_sum3, _p7, _k2, 1);
            _sum5 = vfmaq_laneq_f32(_sum5, _p7, _k2, 2);
            _sum7 = vfmaq_laneq_f32(_sum7, _p7, _k2, 3);

            _sum8 = vfmaq_laneq_f32(_sum8, _p6, _k3, 0);
            _suma = vfmaq_laneq_f32(_suma, _p6, _k3, 1);
            _sumc = vfmaq_laneq_f32(_sumc, _p6, _k3, 2);
            _sume = vfmaq_laneq_f32(_sume, _p6, _k3, 3);
            _sum9 = vfmaq_laneq_f32(_sum9, _p7, _k3, 0);
            _sumb = vfmaq_laneq_f32(_sumb, _p7, _k3, 1);
            _sumd = vfmaq_laneq_f32(_sumd, _p7, _k3, 2);
            _sumf = vfmaq_laneq_f32(_sumf, _p7, _k3, 3);
        }

        int remain = nn & 3;
        for (; remain > 0; remain--)
        {
            float32x4_t _p0 = vld1q_f32(tmpptr);
            float32x4_t _p1 = vld1q_f32(tmpptr + 4);
            tmpptr += 8;

            float32x4_t _k0 = vld1q_f32(kptr);
            float32x4_t _k1 = vld1q_f32(kptr + 4);
            kptr += 8;

            _sum0 = vfmaq_laneq_f32(_sum0, _p0, _k0, 0);
            _sum2 = vfmaq_laneq_f32(_sum2, _p0, _k0, 1);
            _sum4 = vfmaq_laneq_f32(_sum4, _p0, _k0, 2);
            _sum6 = vfmaq_laneq_f32(_sum6, _p0, _k0, 3);
            _sum1 = vfmaq_laneq_f32(_sum1, _p1, _k0, 0);
            _sum3 = vfmaq_laneq_f32(_sum3, _p1, _k0, 1);
            _sum5 = vfmaq_laneq_f32(_sum5, _p1, _k0, 2);
            _sum7 = vfmaq_laneq_f32(_sum7, _p1, _k0, 3);

            _sum8 = vfmaq_laneq_f32(_sum8, _p0, _k1, 0);
            _suma = vfmaq_laneq_f32(_suma, _p0, _k1, 1);
            _sumc = vfmaq_laneq_f32(_sumc, _p0, _k1, 2);
            _sume = vfmaq_laneq_f32(_sume, _p0, _k1, 3);
            _sum9 = vfmaq_laneq_f32(_sum9, _p1, _k1, 0);
            _sumb = vfmaq_laneq_f32(_sumb, _p1, _k1, 1);
            _sumd = vfmaq_laneq_f32(_sumd, _p1, _k1, 2);
            _sumf = vfmaq_laneq_f32(_sumf, _p1, _k1, 3);
        }

        vst1q_f32(outptr0, _sum0);
        vst1q_f32(outptr0 + 4, _sum1);
        vst1q_f32(outptr1, _sum2);
        vst1q_f32(outptr1 + 4, _sum3);
        vst1q_f32(outptr2, _sum4);
        vst1q_f32(outptr2 + 4, _sum5);
        vst1q_f32(outptr3, _sum6);
        vst1q_f32(outptr3 + 4, _sum7);
        vst1q_f32(outptr4, _sum8);
        vst1q_f32(outptr4 + 4, _sum9);
        vst1q_f32(outptr5, _suma);
        vst1q_f32(outptr5 + 4, _sumb);
        vst1q_f32(outptr6, _sumc);
        vst1q_f32(outptr6 + 4, _sumd);
        vst1q_f32(outptr7, _sume);
        vst1q_f32(outptr7 + 4, _sumf);
        return;
    }

    // mode 1: 8x1 tail - each output pointer receives 1 float
    float32x4_t _bias0 = vld1q_f32(biasptr);
    float32x4_t _bias1 = vld1q_f32(biasptr + 4);

    float32x4_t _sum0 = vdupq_n_f32(0.f);
    float32x4_t _sum1 = vdupq_n_f32(0.f);
    float32x4_t _sum2 = vdupq_n_f32(0.f);
    float32x4_t _sum3 = vdupq_n_f32(0.f);

    int nn4 = nn >> 2;
    for (; nn4 > 0; nn4--)
    {
        float32x4_t _p = vld1q_f32(tmpptr);
        tmpptr += 4;

        float32x4_t _k0 = vld1q_f32(kptr);
        float32x4_t _k1 = vld1q_f32(kptr + 4);
        float32x4_t _k2 = vld1q_f32(kptr + 8);
        float32x4_t _k3 = vld1q_f32(kptr + 12);
        float32x4_t _k4 = vld1q_f32(kptr + 16);
        float32x4_t _k5 = vld1q_f32(kptr + 20);
        float32x4_t _k6 = vld1q_f32(kptr + 24);
        float32x4_t _k7 = vld1q_f32(kptr + 28);
        kptr += 32;

        _sum0 = vfmaq_laneq_f32(_sum0, _k0, _p, 0);
        _sum1 = vfmaq_laneq_f32(_sum1, _k1, _p, 0);
        _sum0 = vfmaq_laneq_f32(_sum0, _k2, _p, 1);
        _sum1 = vfmaq_laneq_f32(_sum1, _k3, _p, 1);
        _sum2 = vfmaq_laneq_f32(_sum2, _k4, _p, 2);
        _sum3 = vfmaq_laneq_f32(_sum3, _k5, _p, 2);
        _sum2 = vfmaq_laneq_f32(_sum2, _k6, _p, 3);
        _sum3 = vfmaq_laneq_f32(_sum3, _k7, _p, 3);
    }

    float32x4_t _acc0 = vaddq_f32(_sum0, _sum2);
    float32x4_t _acc1 = vaddq_f32(_sum1, _sum3);
    float32x4_t _out0 = vaddq_f32(_bias0, _acc0);
    float32x4_t _out1 = vaddq_f32(_bias1, _acc1);

    int remain = nn & 3;
    for (; remain > 0; remain--)
    {
        float32x4_t _p = vdupq_n_f32(*tmpptr++);
        float32x4_t _k0 = vld1q_f32(kptr);
        float32x4_t _k1 = vld1q_f32(kptr + 4);
        kptr += 8;

        _out0 = vfmaq_f32(_out0, _p, _k0);
        _out1 = vfmaq_f32(_out1, _p, _k1);
    }

    outptr0[0] = vgetq_lane_f32(_out0, 0);
    outptr1[0] = vgetq_lane_f32(_out0, 1);
    outptr2[0] = vgetq_lane_f32(_out0, 2);
    outptr3[0] = vgetq_lane_f32(_out0, 3);
    outptr4[0] = vgetq_lane_f32(_out1, 0);
    outptr5[0] = vgetq_lane_f32(_out1, 1);
    outptr6[0] = vgetq_lane_f32(_out1, 2);
    outptr7[0] = vgetq_lane_f32(_out1, 3);
}