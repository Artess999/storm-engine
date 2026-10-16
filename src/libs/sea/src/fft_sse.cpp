// Ported from CaptainBlood ArcadeSea/Ocean3/fft_sse.cpp (PC path).
// Adaptations vs original:
// - 64-bit safe alignment (uintptr_t instead of dword truncation).
// - ProfileTimer/api traces removed (results were unused).
// - Dead m_original reference branches and unused N2() removed.
// - srand seeded with time() instead of GetTickCount (no windows.h dep).

#include "fft.h"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ctime>
#include <utility>
#include <xmmintrin.h>

#include "math_inlines.h"

void fft::Construct()
{
    m_zeroFrameUnaligned = nullptr;
    m_omegasUnaligned = nullptr;
    m_wReImUnaligned = nullptr;
    m_nxyUnaligned = nullptr;
    m_gausRND = nullptr;
    m_time = 0.0f;

    m_original = false;
}

void fft::Destruct()
{
    delete[] m_zeroFrameUnaligned;
    delete[] m_omegasUnaligned;
    delete[] m_wReImUnaligned;
    delete[] m_nxyUnaligned;
    delete[] m_gausRND;
}

static inline void *Align16(char *ptr)
{
    return reinterpret_cast<void *>((reinterpret_cast<uintptr_t>(ptr) + 15) & ~uintptr_t(15));
}

void fft::Init(int size)
{
    assert(size <= fft_max);
    m_size = size;
    m_time = 0.0f;

    m_curFrameUnaligned[0] = new char[sizeof(dcomplex) * m_size * m_size + 16];
    m_curFrameUnaligned[1] = new char[sizeof(dcomplex) * m_size * m_size + 16];

    m_zeroFrameUnaligned = new char[sizeof(dcomplex) * m_size * m_size + 16];

    m_zeroFrame = static_cast<dcomplex *>(Align16(m_zeroFrameUnaligned));

    m_omegasUnaligned = new char[sizeof(float) * m_size * m_size + 16];
    m_omegas = static_cast<float *>(Align16(m_omegasUnaligned));

    m_wReImUnaligned = new char[sizeof(CVECTOR4) * 2 * size + 16];
    m_wReIm = static_cast<CVECTOR4 *>(Align16(m_wReImUnaligned));

    m_nxyUnaligned = new char[sizeof(float) * m_size + 16];
    m_nxy = static_cast<float *>(Align16(m_nxyUnaligned));

    // Distinct phases per layer (two Init calls in the same second would
    // share one time() seed otherwise) with per-launch variety.
    static uint32_t s_fftSalt = 0;
    srand(static_cast<unsigned>(time(nullptr)) + s_fftSalt++ * 0x9E3779B9u);
    m_gausRND = new complex[m_size * m_size];
    for (int i = 0; i < m_size * m_size; i++)
    {
        m_gausRND[i].Re = N1();
        m_gausRND[i].Im = N1();
    }

    m_curFrame = static_cast<dcomplex *>(Align16(m_curFrameUnaligned[0]));
    m_nextFrame = static_cast<dcomplex *>(Align16(m_curFrameUnaligned[1]));
}

void fft::Setup(float wave_len, float wave_amp, float wave_vel, float wave_x, float wave_y, float align_power)
{
    complex k, wave_dir;
    float m, k2, c2, L;

    m_waveLen = wave_len;
    m_waveAmp = wave_amp;
    m_waveVel = wave_vel;
    m_waveX = wave_x;
    m_waveY = wave_y;

    L = Sqr(m_waveVel) / 9.81f;
    wave_dir = complex(m_waveX, m_waveY);
    wave_dir = wave_dir / Mod(wave_dir);
    float k0 = 6.28318530717959f / m_waveLen;

    for (int y = 0; y < m_size; y++)
        for (int x = 0; x < m_size; x++)
        {
            int nx = x;
            if (nx >= m_size / 2)
                nx -= m_size;
            int ny = y;
            if (ny >= m_size / 2)
                ny -= m_size;

            k = complex(k0 * nx, k0 * ny);
            k2 = Mod2(k);
            c2 = Sqr(k.Re * wave_dir.Re + k.Im * wave_dir.Im) / k2;
            m = sqrtf(0.5f * wave_amp * expf(-1.0f / (k2 * Sqr(L))) * powf(c2, align_power) / Sqr(k2));

            float n1 = m_gausRND[x + y * m_size].Re;
            float n2 = m_gausRND[x + y * m_size].Im;

            m_zeroFrame[x + y * m_size].c1 = complex(m * n1, m * n2);
        }

    m_zeroFrame[0].c1 = complex(0.0f, 0.0f);

    for (int y = 0; y < m_size; y++)
        for (int x = 0; x < m_size; x++)
        {
            int nx = x;
            if (nx >= m_size / 2)
                nx -= m_size;
            int ny = y;
            if (ny >= m_size / 2)
                ny -= m_size;

            k = complex(k0 * nx, k0 * ny);
            m_omegas[x + y * m_size] = sqrtf(Mod(k) * 9.81f);

            int inx = -nx;
            if (inx < 0)
                inx += m_size;
            int iny = -ny;
            if (iny < 0)
                iny += m_size;
            m_zeroFrame[x + y * m_size].c2 = ~m_zeroFrame[inx + iny * m_size].c1;
        }

    for (int i = 0; i < m_size; i++)
    {
        int ni = i;
        if (ni >= m_size / 2)
            ni -= m_size;
        m_nxy[i] = k0 * ni;
    }

    for (int i = 0; i < m_size; i++)
        m_linearSwapper[i] = i * m_size;

    int j = 0;
    m_swapsNum = 0;
    for (int i = 0; i < m_size; i++)
    {
        if (j > i)
        {
            std::swap(m_linearSwapper[i], m_linearSwapper[j]);
            m_swapper[m_swapsNum++] = j + (i << 16L);
        }

        int m = m_size >> 1;
        while (m >= 1 && j >= m)
        {
            j -= m;
            m >>= 1;
        }
        j += m;
    }

    int mmax = 1, idx = 0;
    float theta = -(6.28318530717959f / 2.0f);
    while (m_size > mmax)
    {
        complex wp = complex(cosf(theta), sinf(theta));
        complex w = complex(1.0f, 0.0f);
        for (int m = 0; m < mmax; m++)
        {
            m_wReIm[idx * 2 + 0] = CVECTOR4(w.Re, w.Re, w.Re, w.Re);
            m_wReIm[idx * 2 + 1] = CVECTOR4(-w.Im, w.Im, -w.Im, w.Im);
            idx++;
            w = w * wp;
        }
        mmax *= 2;
        theta = theta * 0.5f;
    }
}

void fft::Animate(float time)
{
    float dtime = time * (4096.0f / PIm2);
    float *py = m_nxy;

    complex k;
    for (int y = 0; y < m_size; y++)
    {
        k.Im = *py++;

        dcomplex *curFrame = m_nextFrame + y * m_size;
        dcomplex *zeroFrame = m_zeroFrame + y * m_size;
        float *omegaFrame = m_omegas + y * m_size;

        float *px = m_nxy;
        for (int x = 0; x < m_size; x++)
        {
            k.Re = *px++;
            float omega = *omegaFrame;

            const dcomplex &pp = m_polars[int(omega * dtime) & 4095L];
            complex temp = zeroFrame->c1 * pp.c1;
            curFrame->c1 = temp + zeroFrame->c2 * pp.c2;
            curFrame->c2 = curFrame->c1 * complex(-k.Im, k.Re);

            curFrame++;
            zeroFrame++;
            omegaFrame++;
        }
    }
}

void fft::FFTHorizontal(dcomplex *data)
{
    for (int k = 0; k < m_swapsNum; k++)
    {
        int i = m_swapper[k] >> 16L;
        int j = m_swapper[k] & 0xFFFF;

        __m128 mi = _mm_load_ps((float *)&data[i].c1);
        __m128 mj = _mm_load_ps((float *)&data[j].c1);
        _mm_store_ps((float *)&data[i].c1, mj);
        _mm_store_ps((float *)&data[j].c1, mi);
    }

    FFT(data);
}

void fft::FFTVertical(dcomplex *data)
{
    __declspec(align(16)) dcomplex linear_data[fft_max];

    for (int i = 0; i < m_size; i++)
        linear_data[i] = data[m_linearSwapper[i]];

    FFT(linear_data);

    for (int i = 0; i < m_size; i++)
        data[i * m_size] = linear_data[i];
}

void fft::FFT(dcomplex *data)
{
    int i, j, m, mmax, istep;
    mmax = 1;

    int idx = 0;
    while (m_size > mmax)
    {
        istep = 2 * mmax;

        for (m = 0; m < mmax; m++)
        {
            __m128 wre = _mm_load_ps(&m_wReIm[idx + 0].v[0]);
            __m128 wim = _mm_load_ps(&m_wReIm[idx + 1].v[0]);
            idx += 2;

            for (i = m; i < m_size; i += istep)
            {
                j = i + mmax;

                __m128 mj = _mm_load_ps((float *)&data[j].c1);
                __m128 mi = _mm_load_ps((float *)&data[i].c1);

                __m128 imj = _mm_shuffle_ps(mj, mj, _MM_SHUFFLE(2, 3, 0, 1));

                __m128 mtemp = _mm_mul_ps(wre, mj);
                mtemp = _mm_add_ps(mtemp, _mm_mul_ps(wim, imj));

                _mm_store_ps((float *)&data[j].c1, _mm_sub_ps(mi, mtemp));
                _mm_store_ps((float *)&data[i].c1, _mm_add_ps(mi, mtemp));
            }
        }

        mmax = istep;
    }
}

void fft::Execute(float deltaTime)
{
    m_time += deltaTime;
    // Float phase precision degrades after ~a day of play (omega*time loses
    // mantissa bits); wrap every 2h. One brief wave pop per wrap, no drift.
    if (m_time > 7200.0f)
        m_time -= 7200.0f;

    Animate(m_time);
    for (int i = 0; i < m_size; i++)
        FFTHorizontal(m_nextFrame + i * m_size);
    for (int i = 0; i < m_size; i++)
        FFTVertical(m_nextFrame + i);
}

float fft::N1()
{
    int i;
    const int n = 200;
    const float S = sqrtf(float(n) / 12.0f);
    const float M = float(n) / 2;
    int ix = 0;

    for (i = 0; i < n; i++)
        ix += rand();
    float x = float(double(ix) * (1.0 / double(RAND_MAX)));
    x = (x - M) / S;
    return x;
}
