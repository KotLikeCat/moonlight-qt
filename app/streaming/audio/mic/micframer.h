#pragma once

#include <functional>
#include <vector>

// Accumulates float mono samples of arbitrary chunk sizes and emits exact
// 960-sample (20 ms at 48 kHz) frames. Pure, no SDL/Qt dependency.
class MicFramer
{
public:
    static constexpr int FrameSamples = 960;

    using FrameCallback = std::function<void(const float*)>;

    MicFramer();

    // Appends `count` samples; invokes onFrame once per completed frame, in order.
    // The frame pointer is only valid for the duration of the callback.
    void push(const float* samples, int count, const FrameCallback& onFrame);

    // Samples buffered that do not yet make a full frame
    int pending() const { return m_Filled; }

    void reset() { m_Filled = 0; }

private:
    std::vector<float> m_Buffer;
    int m_Filled;
};
