#pragma once

// Adapt this signature to match the target asm block.
// This template covers an 8-output-channel kernel with two spatial modes:
//   mode 0 - 8x8 tile  (writes 8 floats per output pointer)
//   mode 1 - 8x1 tail  (writes 1 float  per output pointer)

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
    int mode);

void kernel_reference(
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
    int mode);