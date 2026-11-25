// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <pffft.h>
#include <cstring>
#include <utility>

namespace meq::dsp
{

/** 64-byte aligned float buffer for PFFFT. */
class AlignedBuffer
{
public:
    AlignedBuffer() = default;
    explicit AlignedBuffer (size_t n) { allocate (n); }
    ~AlignedBuffer() { release(); }

    AlignedBuffer (const AlignedBuffer&) = delete;
    AlignedBuffer& operator= (const AlignedBuffer&) = delete;
    AlignedBuffer (AlignedBuffer&& o) noexcept : ptr (std::exchange (o.ptr, nullptr)), count (std::exchange (o.count, 0)) {}
    AlignedBuffer& operator= (AlignedBuffer&& o) noexcept
    {
        if (this != &o) { release(); ptr = std::exchange (o.ptr, nullptr); count = std::exchange (o.count, 0); }
        return *this;
    }

    void allocate (size_t n)
    {
        release();
        if (n == 0) return;
        ptr = static_cast<float*> (pffft_aligned_malloc (n * sizeof (float)));
        count = n;
        clear();
    }

    void clear() noexcept { if (ptr != nullptr) std::memset (ptr, 0, count * sizeof (float)); }
    void clear (size_t n) noexcept { if (ptr != nullptr) std::memset (ptr, 0, (n < count ? n : count) * sizeof (float)); }

    float* data() noexcept { return ptr; }
    const float* data() const noexcept { return ptr; }
    size_t size() const noexcept { return count; }
    float& operator[] (size_t i) noexcept { return ptr[i]; }
    const float& operator[] (size_t i) const noexcept { return ptr[i]; }

private:
    void release() noexcept
    {
        if (ptr != nullptr) pffft_aligned_free (ptr);
        ptr = nullptr;
        count = 0;
    }

    float* ptr = nullptr;
    size_t count = 0;
};

/** Thin RAII wrapper around a real-valued PFFFT setup. */
class RealFFT
{
public:
    RealFFT() = default;
    explicit RealFFT (int n) { init (n); }
    ~RealFFT() { release(); }

    RealFFT (const RealFFT&) = delete;
    RealFFT& operator= (const RealFFT&) = delete;

    void init (int n)
    {
        release();
        setup = pffft_new_setup (n, PFFFT_REAL);
        work.allocate ((size_t) n);
        length = n;
    }

    int size() const noexcept { return length; }
    bool isValid() const noexcept { return setup != nullptr; }

    /** Unordered (internal-layout) transforms, for convolution. Not scaled. */
    void forward (const float* in, float* out) noexcept { pffft_transform (setup, in, out, work.data(), PFFFT_FORWARD); }
    void inverse (const float* in, float* out) noexcept { pffft_transform (setup, in, out, work.data(), PFFFT_BACKWARD); }

    /** Ordered layout: [DC, Nyquist, re1, im1, re2, im2, ...]. Not scaled. */
    void forwardOrdered (const float* in, float* out) noexcept { pffft_transform_ordered (setup, in, out, work.data(), PFFFT_FORWARD); }
    void inverseOrdered (const float* in, float* out) noexcept { pffft_transform_ordered (setup, in, out, work.data(), PFFFT_BACKWARD); }

    /** ab += a * b * scale (unordered spectra). */
    void multiplyAccumulate (const float* a, const float* b, float* ab, float scale) noexcept
    {
        pffft_zconvolve_accumulate (setup, a, b, ab, scale);
    }

private:
    void release() noexcept
    {
        if (setup != nullptr) pffft_destroy_setup (setup);
        setup = nullptr;
    }

    PFFFT_Setup* setup = nullptr;
    AlignedBuffer work;
    int length = 0;
};

} // namespace meq::dsp
