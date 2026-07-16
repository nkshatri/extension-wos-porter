// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include "kernel_compare.h"

void kernel_reference(
    float *outptr0,
    float *outptr1,
    float *outptr2,
    float *outptr3,
    const float *tmpptr,
    const float *kptr,
    const float *biasptr,
    int nn)
{

  asm volatile(
      "ld1    {v0.4s}, [%12]          \n"
      "dup    v8.4s, v0.s[0]          \n"
      "dup    v9.4s, v0.s[0]          \n"
      "dup    v10.4s, v0.s[1]         \n"
      "dup    v11.4s, v0.s[1]         \n"
      "dup    v12.4s, v0.s[2]         \n"
      "dup    v13.4s, v0.s[2]         \n"
      "dup    v14.4s, v0.s[3]         \n"
      "dup    v15.4s, v0.s[3]         \n"
      "lsr    w4, %w13, #2            \n"
      "cmp    w4, #0                  \n"
      "beq    1f                      \n"
      "0:                             \n"
      "prfm   pldl1keep, [%4, #512]   \n"
      "ld1    {v4.4s, v5.4s, v6.4s, v7.4s}, [%4], #64     \n"
      "prfm   pldl1keep, [%5, #512]   \n"
      "ld1    {v0.4s, v1.4s, v2.4s, v3.4s}, [%5], #64     \n"
      "fmla   v8.4s, v4.4s, v0.s[0]   \n"
      "fmla   v10.4s, v4.4s, v0.s[1]  \n"
      "fmla   v12.4s, v4.4s, v0.s[2]  \n"
      "fmla   v14.4s, v4.4s, v0.s[3]  \n"
      "fmla   v9.4s, v5.4s, v0.s[0]   \n"
      "fmla   v11.4s, v5.4s, v0.s[1]  \n"
      "fmla   v13.4s, v5.4s, v0.s[2]  \n"
      "fmla   v15.4s, v5.4s, v0.s[3]  \n"
      "prfm   pldl1keep, [%4, #512]   \n"
      "ld1    {v16.4s, v17.4s, v18.4s, v19.4s}, [%4], #64 \n"
      "fmla   v8.4s, v6.4s, v1.s[0]   \n"
      "fmla   v10.4s, v6.4s, v1.s[1]  \n"
      "fmla   v12.4s, v6.4s, v1.s[2]  \n"
      "fmla   v14.4s, v6.4s, v1.s[3]  \n"
      "fmla   v9.4s, v7.4s, v1.s[0]   \n"
      "fmla   v11.4s, v7.4s, v1.s[1]  \n"
      "fmla   v13.4s, v7.4s, v1.s[2]  \n"
      "fmla   v15.4s, v7.4s, v1.s[3]  \n"
      "subs   w4, w4, #1              \n"
      "fmla   v8.4s, v16.4s, v2.s[0]  \n"
      "fmla   v10.4s, v16.4s, v2.s[1] \n"
      "fmla   v12.4s, v16.4s, v2.s[2] \n"
      "fmla   v14.4s, v16.4s, v2.s[3] \n"
      "fmla   v9.4s, v17.4s, v2.s[0]  \n"
      "fmla   v11.4s, v17.4s, v2.s[1] \n"
      "fmla   v13.4s, v17.4s, v2.s[2] \n"
      "fmla   v15.4s, v17.4s, v2.s[3] \n"
      "fmla   v8.4s, v18.4s, v3.s[0]  \n"
      "fmla   v10.4s, v18.4s, v3.s[1] \n"
      "fmla   v12.4s, v18.4s, v3.s[2] \n"
      "fmla   v14.4s, v18.4s, v3.s[3] \n"
      "fmla   v9.4s, v19.4s, v3.s[0]  \n"
      "fmla   v11.4s, v19.4s, v3.s[1] \n"
      "fmla   v13.4s, v19.4s, v3.s[2] \n"
      "fmla   v15.4s, v19.4s, v3.s[3] \n"
      "bne    0b                      \n"
      "1:                             \n"
      "and    w4, %w13, #3            \n"
      "cmp    w4, #0                  \n"
      "beq    3f                      \n"
      "2:                             \n"
      "prfm   pldl1keep, [%4, #256]   \n"
      "ld1    {v4.4s, v5.4s}, [%4], #32   \n"
      "prfm   pldl1keep, [%5, #128]   \n"
      "ld1    {v0.4s}, [%5], #16      \n"
      "fmla   v8.4s, v4.4s, v0.s[0]   \n"
      "fmla   v10.4s, v4.4s, v0.s[1]  \n"
      "fmla   v12.4s, v4.4s, v0.s[2]  \n"
      "fmla   v14.4s, v4.4s, v0.s[3]  \n"
      "subs   w4, w4, #1              \n"
      "fmla   v9.4s, v5.4s, v0.s[0]   \n"
      "fmla   v11.4s, v5.4s, v0.s[1]  \n"
      "fmla   v13.4s, v5.4s, v0.s[2]  \n"
      "fmla   v15.4s, v5.4s, v0.s[3]  \n"
      "bne    2b                      \n"
      "3:                             \n"
      "st1    {v8.4s, v9.4s}, [%0], #32   \n"
      "st1    {v10.4s, v11.4s}, [%1], #32 \n"
      "st1    {v12.4s, v13.4s}, [%2], #32 \n"
      "st1    {v14.4s, v15.4s}, [%3], #32 \n"
      : "=r"(outptr0),
        "=r"(outptr1),
        "=r"(outptr2),
        "=r"(outptr3),
        "=r"(tmpptr),
        "=r"(kptr)
      : "0"(outptr0),
        "1"(outptr1),
        "2"(outptr2),
        "3"(outptr3),
        "4"(tmpptr),
        "5"(kptr),
        "r"(biasptr),
        "r"(nn)
      : "cc", "memory", "x4", "v0", "v1", "v2", "v3", "v4", "v5", "v6", "v7", "v8", "v9", "v10", "v11", "v12", "v13", "v14", "v15", "v16", "v17", "v18", "v19");
}
