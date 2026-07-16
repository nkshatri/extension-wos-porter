// Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
// SPDX-License-Identifier: BSD-3-Clause-Clear

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <random>
#include <vector>

#include "kernel_compare.h"

namespace
{
    void expect_all_close(const std::array<float, 8> &a, const std::array<float, 8> &b, float eps)
    {
        for (int i = 0; i < 8; ++i)
        {
            EXPECT_NEAR(a[i], b[i], eps) << "mismatch at index " << i;
        }
    }
}

TEST(KernelCompare, MatchesReferenceForVariousNN)
{
    std::mt19937 rng(123);
    std::uniform_real_distribution<float> dist(-3.0f, 3.0f);

    for (int nn = 0; nn <= 9; ++nn)
    {
        for (int round = 0; round < 50; ++round)
        {
            const int tmpptr_size = (nn / 4) * 16 + (nn % 4) * 8;
            const int kptr_size = (nn / 4) * 16 + (nn % 4) * 4;

            std::vector<float> tmp(tmpptr_size);
            std::vector<float> k(kptr_size);
            std::array<float, 4> bias{};

            for (float &v : tmp)
                v = dist(rng);
            for (float &v : k)
                v = dist(rng);
            for (float &v : bias)
                v = dist(rng);

            std::array<float, 8> out0_intr{};
            std::array<float, 8> out1_intr{};
            std::array<float, 8> out2_intr{};
            std::array<float, 8> out3_intr{};

            std::array<float, 8> out0_ref{};
            std::array<float, 8> out1_ref{};
            std::array<float, 8> out2_ref{};
            std::array<float, 8> out3_ref{};

            kernel_intrinsics(
                out0_intr.data(),
                out1_intr.data(),
                out2_intr.data(),
                out3_intr.data(),
                tmp.data(),
                k.data(),
                bias.data(),
                nn);

            kernel_reference(
                out0_ref.data(),
                out1_ref.data(),
                out2_ref.data(),
                out3_ref.data(),
                tmp.data(),
                k.data(),
                bias.data(),
                nn);

            expect_all_close(out0_intr, out0_ref, 1e-4f);
            expect_all_close(out1_intr, out1_ref, 1e-4f);
            expect_all_close(out2_intr, out2_ref, 1e-4f);
            expect_all_close(out3_intr, out3_ref, 1e-4f);
        }
    }
}
