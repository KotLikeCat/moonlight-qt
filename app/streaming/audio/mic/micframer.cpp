#include "micframer.h"

#include <algorithm>

MicFramer::MicFramer()
    : m_Buffer(FrameSamples),
      m_Filled(0)
{
}

void MicFramer::push(const float* samples, int count, const FrameCallback& onFrame)
{
    while (count > 0) {
        int take = std::min(count, FrameSamples - m_Filled);
        std::copy(samples, samples + take, m_Buffer.begin() + m_Filled);
        m_Filled += take;
        samples += take;
        count -= take;

        if (m_Filled == FrameSamples) {
            m_Filled = 0;
            onFrame(m_Buffer.data());
        }
    }
}
