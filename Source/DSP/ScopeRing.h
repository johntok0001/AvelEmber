#pragma once

#include <atomic>
#include <vector>
#include <algorithm>

namespace avel
{
    // Lock-free ring of the most recent mono samples for the analyzer.
    // Single writer (audio thread); the UI reads the latest N samples.
    struct ScopeRing
    {
        static constexpr int size = 1 << 15;

        ScopeRing() : data ((size_t) size, 0.0f) {}

        void push (const float* l, const float* r, int n) noexcept
        {
            int w = writePos.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i)
            {
                data[(size_t) w] = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i];
                w = (w + 1) & (size - 1);
            }
            writePos.store (w, std::memory_order_release);
        }

        void readLatest (float* out, int n) const noexcept
        {
            int pos = (writePos.load (std::memory_order_acquire) - n) & (size - 1);
            for (int i = 0; i < n; ++i)
            {
                out[i] = data[(size_t) pos];
                pos = (pos + 1) & (size - 1);
            }
        }

        std::vector<float> data;
        std::atomic<int> writePos { 0 };
    };
}
