// GKEQ - 24-band dynamic EQ plug-in
// Copyright (C) 2026 Roman Gutekunst (GK)
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <atomic>
#include <vector>
#include <cstdint>
#include <algorithm>

namespace meq
{

/** Single-writer, multi-reader ring of the most recent audio samples.
    The audio thread writes; any number of UI threads copy out the latest
    window for spectrum analysis. Reads are lock-free and may tear by a few
    samples at the write boundary, which is irrelevant for visualisation. */
class SnapshotBuffer
{
public:
    static constexpr int kSize = 1 << 16;   // power of two

    SnapshotBuffer() : buf ((size_t) kSize, 0.0f) {}

    void write (const float* x, int n) noexcept
    {
        uint64_t w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            buf[(size_t) ((w + (uint64_t) i) & (kSize - 1))] = x[i];
        writePos.store (w + (uint64_t) n, std::memory_order_release);
    }

    /** Mono mix of two channels. */
    void writeMix (const float* a, const float* b, int n) noexcept
    {
        uint64_t w = writePos.load (std::memory_order_relaxed);
        for (int i = 0; i < n; ++i)
            buf[(size_t) ((w + (uint64_t) i) & (kSize - 1))] = b != nullptr ? 0.5f * (a[i] + b[i]) : a[i];
        writePos.store (w + (uint64_t) n, std::memory_order_release);
    }

    /** Copies the latest n samples (oldest first). Returns the write counter. */
    uint64_t read (float* dest, int n) const noexcept
    {
        n = std::min (n, kSize);
        const uint64_t w = writePos.load (std::memory_order_acquire);
        const uint64_t start = w - (uint64_t) n;
        for (int i = 0; i < n; ++i)
            dest[i] = buf[(size_t) ((start + (uint64_t) i) & (kSize - 1))];
        return w;
    }

    uint64_t position() const noexcept { return writePos.load (std::memory_order_acquire); }

private:
    std::vector<float> buf;
    std::atomic<uint64_t> writePos { 0 };
};

} // namespace meq
