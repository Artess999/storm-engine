// Ported from CaptainBlood ArcadeSea/Ocean3/fft.cpp.
// Shared polar table + frame swap. Per-platform code lives in fft_sse.cpp.

#include "fft.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

#include "math_inlines.h"

int fft::refCount = 0;
char *fft::m_polarsUnaligned = nullptr;
fft::dcomplex *fft::m_polars = nullptr;

fft::fft()
{
    m_curFrameUnaligned[0] = nullptr;
    m_curFrameUnaligned[1] = nullptr;

    m_curFrame = nullptr;
    m_nextFrame = nullptr;

    Construct();

    if (refCount == 0)
    {
        m_polarsUnaligned = new char[sizeof(dcomplex) * 4096 + 16];
        m_polars =
            reinterpret_cast<dcomplex *>((reinterpret_cast<uintptr_t>(m_polarsUnaligned) + 15) & ~uintptr_t(15));

        for (int n = 0; n < 4096; n++)
        {
            m_polars[n].c1 = Polar(1.0f, float(float(n) / 4096.0f * PIm2));
            m_polars[n].c2 = ~m_polars[n].c1;
        }
    }
    refCount++;
}

fft::~fft()
{
    delete[] m_curFrameUnaligned[0];
    delete[] m_curFrameUnaligned[1];

    Destruct();

    refCount--;
    if (refCount == 0)
    {
        delete[] m_polarsUnaligned;
        m_polars = nullptr;
    }
}

void fft::ChangeCurFrame()
{
    dcomplex *tmp = m_curFrame;
    m_curFrame = m_nextFrame;
    m_nextFrame = tmp;
}
