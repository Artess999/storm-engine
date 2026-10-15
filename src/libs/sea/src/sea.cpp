#include "sea.h"

#include <algorithm>
#include <execution>
#include <thread>

#include "core.h"
#include "math3d.h"
#include "sse.h"
#include "math_inlines.h"
#include "shared/sea_ai/script_defines.h"
#include "tga.h"
#include "v_file_service.h"

CREATE_CLASS(SEA)

//#define OLD_WORLD_POS

#define NUM_VERTEXS 131072
#define NUM_INDICES 300000

#define FRAMES 64
#define XWIDTH 128
#define YWIDTH 128

#define MIPSLVLS 4

#define GC_CONSTANT 0 // Global constants = {0.0, 1.0, 0.5, 0.0000152590218967 = (0.5 / 32767.5)}
#define GC_CONSTANT2 1 // Global constants 2 = {2.0, -1.0, 0.00036621652552071 = (12 / 32767.5), fog}
#define GC_ANIMATION 2 // Animation frames(0.0-1.0) for 4 stages = {stage0, stage1, stage2, stage3}
#define GC_SHADOW_CONST1 3
#define GC_SHADOW_CONST2 7
#define GC_LIGHT4 11 //
#define GC_LIGHT3 13
#define GC_LIGHT2 15
#define GC_LIGHT1 17
#define GC_LIGHT0 19
#define GC_MATERIAL 21 //
#define GC_CAMERA_POS 23 // Local Camera position = {x, y, z, 0.0}
#define GC_MTX_WVP 24 // c[0] = mWorld * mView * mProjection

#define GC_FREE 28

SEA *SEA::pSea = nullptr;
IDirect3DVertexDeclaration9 *SEA::vertexDecl_ = nullptr;

SEA::SEA()
{
    aBlocks.reserve(128);
    aSeaTrash.reserve(512);
    aTrashRects.reserve(512);
    aSeaLights.reserve(512);
    aLightsRects.reserve(512);
    aBumpMaps.reserve(FRAMES);
    aNormals.reserve(FRAMES);

    dwMaxDim = 81920; // Blood grid budget (16-bit cap caused trims/holes)
    dwMinDim = 256;

    fMaxSeaHeight = 20.0f;
    fSeaHeight = 0.0f; // Blood engine default Height (Ocean.cpp:2203)
    fSeaFake = 0.0f; // Blood engine default Fake Offset
    fMaxSeaDistance = 2200.0f; // Blood engine default Waves Distance
    fGridStep = 0.0745f; // Blood mesh density
    fLodScale = 0.112f; // Blood LOD curve

    fAmp1 = 1.0f;
    _fAmp1 = 1.0f; // Blood effective amplitude (their temp-1.0 override)
    fFrame1 = 0.0f;
    vMove1 = 0.0f;
    fScale1 = 0.5f; // overwritten by SetupFFTFromSea2Attrs (multiplier*128/len)
    fSeaScale1 = 1.0f; // Blood engine default Waves Scale

    fAmp2 = 0.0f;
    _fAmp2 = 0.0f; // Blood bump frame is texture-only, no geometry term
    fFrame2 = 10.0f;
    vMove2 = 0.0f;
    fScale2 = 2.0f;
    fSeaScale2 = 1.0f;

    // Blood engine defaults (MOP_* in Ocean.cpp:2197). Datum = Height 0.0
    // (fSeaHeight member, settable via Sea2.Height).
    fFFTLen1 = 256.0f;
    fFFTAmp1 = 300.0f;
    fFFTWind1 = 20.0f;
    vFFTDir1 = CVECTOR(1.0f, 0.0f, 0.0f);
    fFFTAlign1 = 0.1f;
    fFFTLen2 = 256.0f;
    fFFTAmp2 = 4000.4f;
    fFFTWind2 = 27.0f;
    vFFTDir2 = CVECTOR(1.0f, 0.0f, 0.0f);
    fFFTAlign2 = 0.0f;

    fAnimSpeed1 = 1.25f; // Blood engine default wave_speed
    fAnimSpeed2 = 2.0f; // Blood engine default wave_speed2

    vMoveSpeed1 = CVECTOR(3.0f, 0.0f, 0.0f); // Blood Wave Move
    vMoveSpeed2 = CVECTOR(0.0f, 0.0f, 0.0f);

    fPosShift = 12.0f / 5.0f * sqrtf(256.0f / 128.0f); // Blood Choppy/sqrt(size): exact 3.3941

    fBumpScaleU = 12.0f;
    fBumpScaleV = 12.0f; // MOP raw engine defaults (handler *0.004)
    fBumpScale = 0.1f; // legacy baked-UV path only (ocean3 uses U/V above)
    fBumpSpeed = 1.0f; // legacy clock (ocean3 scroll uses BumpMoveSpeed)
    fFrenel = 0.75f; // legacy sea.fx only
    fBumpPower = 0.45f; // engine default
    fBumpAngle = 60.0f * 3.14159265f / 180.0f;
    fBumpMoveSpeed = 100.0f; // engine default (scroll = *0.005 m/s)

    v4SeaColor = CVECTOR4(0.1f, 0.2f, 0.2f, 1.0f); // engine default Water color
    v4SkyColor = CVECTOR4(1.0f, 1.0f, 1.0f, 1.0f); // engine default Sky color
    v4SeaParameters = CVECTOR4(0.9f, 0.8f, 0.7f, 0.0f); // legacy sea.fx only (scripts override)

    fFoamPower = 1.0f;
    fFoamBright = 1.0f; // engine default Foam color power
    fFoamAngle = 0.0f;
    fFoamMoveSpeed = 0.0f;
    fFoamMoveAngle = 0.0f;
    v4FoamColor = CVECTOR4(1.0f, 1.0f, 1.0f, 1.0f); // engine default Foam Color

    bTransEnable = true; // engine default: Enable translucence = 1
    bSunRoadEnable = false; // engine default: Enable sunroad = 0
    fTransAngle = 0.6f;
    fTransPower = 6.0f; // engine default
    fTransMultiply = 10.0f; // engine default
    v4TransColor = CVECTOR4(0.0f, 0.1f, 0.0f, 1.0f); // engine default

    fSunRoadAngX = 6.0f;
    fSunRoadAngY = 4.0f;
    fSunAngX = 4.7f;
    fSunAngY = 3.1415f;
    fSunRoadPower = 130.0f; // engine default
    fSunRoadColorPower = 100.0f; // MOP raw (handler *0.01, Ocean.cpp:890)
    fSunBumpScale = 1.0f; // engine default Scale
    fSunMultiply = 1.1f; // engine default
    fSunAmbient = 0.25f; // engine default
    v4SunRoadColor = CVECTOR4(1.0f, 1.0f, 1.0f, 1.0f); // engine default

    fReflMinimum = 0.65f;
    fReflAdd = 2.0f;
    fReflPower = 1.0f;
    fRefrEnvPower = 100.0f;
    fReflEnvBlend = 0.65f;
    fReflMin = 0.1f; // engine default Reflection min
    fReflMax = 0.75f;
    fRefrPower = 100.0f;
    fTransMin = 0.3f; // engine default Transparency min
    fTransMax = 0.0f; // engine default Transparency max

    fFogDistance = 1.0f; // engine default Fog distance

    fFoamV = 0.0f; // engine default Foam start Y
    fFoamK = 0.2f; // foam rate: engine default height 5 -> 1/5 (legacy (y-V)*K, ocean3 (y-V)*K)
    fFoamUV = 0.02f; // Blood Foam UV scale
    fFoamTextureDisturb = 0.01f; // Blood Texture displacement
    iFoamTexture = -1;
    bIniFoamEnable = false;
    bFoamEnable = false; // engine default: Enable foam = 0 (scripts may enable)

    iSeaLightTexture = -1;
    fLastLightTime = 0.0f;

    fLastTrashTime = 0.0f;
    iSeaTrashTexture = -1;
    bUnderSea = false;
    bUnderSeaStarted = false;
    bUnderSeaEnable = false;
    bStarted = false;
    bSimpleSea = false;

    fFogSeaDensity = 0.0f; // engine default Fog multiply (fog off until scripts set it)
    fFogStartDistance = 0.0f; // engine default Fog start
    vFogColor = CVECTOR(1.0f, 1.0f, 1.0f); // engine default Fog Color
    bFogEnable = false;

    pSeaFrame1 = new float[XWIDTH * YWIDTH];
    pSeaFrame2 = new float[XWIDTH * YWIDTH];
    pSeaNormalsFrame1 = new float[2 * XWIDTH * YWIDTH];
    pSeaNormalsFrame2 = new float[2 * XWIDTH * YWIDTH];

    iVSeaBuffer = -1;
    iISeaBuffer = -1;

    fftWavesFrame = nullptr;
    fftBumpFrame = nullptr;
    m_bumpClock = 0.0f;
    m_bumpU = 0.0f;
    m_bumpV = 0.0f;
    m_foamU = 0.0f;
    m_foamV = 0.0f;
    fftWavesLen = -1.0f;
    fftBumpLen = -1.0f;
    bumpScratchA.resize(2 * XWIDTH * YWIDTH, 0.0f);
    bumpScratchB.resize(2 * XWIDTH * YWIDTH, 0.0f);

    bTempFullMode = false;

    pSea = this;

    bStop = false;

    vWorldOffset = 0.f;

    pRenderTargetBumpMap = nullptr;
    pLiveBumpMap = nullptr;
}

SEA::~SEA()
{
    rs->Release(pReflection);
    rs->Release(pReflectionSunroad);
    rs->Release(pRefraction);
    rs->Release(pEnvMap);
    rs->Release(pSunRoadMap);
    rs->Release(pZStencil);
    rs->Release(pReflectionSurfaceDepth);
    rs->Release(pVolumeTexture);
    rs->Release(pRenderTargetBumpMap);
    rs->Release(pLiveBumpMap);

    rs->TextureRelease(iSeaTrashTexture);
    rs->TextureRelease(iSeaLightTexture);

    if (iVSeaBuffer >= 0)
        rs->ReleaseVertexBuffer(iVSeaBuffer);
    iVSeaBuffer = -1;
    if (iISeaBuffer >= 0)
        rs->ReleaseIndexBuffer(iISeaBuffer);
    iISeaBuffer = -1;

    if (iFoamTexture >= 0)
        rs->TextureRelease(iFoamTexture);
    iFoamTexture = -1;

    STORM_DELETE(pIndices);
    STORM_DELETE(pVSea);

    for (int32_t i = 0; i < aBumpMaps.size(); i++)
        rs->Release(aBumpMaps[i]);

    for (int32_t i = 0; i < aBumps.size(); i++)
        STORM_DELETE(aBumps[i]);
    for (int32_t i = 0; i < aNormals.size(); i++)
        STORM_DELETE(aNormals[i]);

    STORM_DELETE(pSeaFrame1);
    STORM_DELETE(pSeaFrame2);
    STORM_DELETE(pSeaNormalsFrame1);
    STORM_DELETE(pSeaNormalsFrame2);
}

void SEA::SFLB_CreateBuffers()
{
    iVSeaBuffer = rs->CreateVertexBuffer(0, NUM_VERTEXS * sizeof(SeaVertex), D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC);
    // 32-bit indices: wide sea views exceed 64k verts (16-bit hard limit).
    iISeaBuffer = rs->CreateIndexBuffer(NUM_INDICES * 3 * sizeof(uint32_t),
                                        D3DUSAGE_WRITEONLY | D3DUSAGE_DYNAMIC, D3DFMT_INDEX32);

    pIndices = new uint32_t[NUM_VERTEXS * 3];
    pVSea = new SeaVertex[NUM_VERTEXS];
}

void SEA::CreateVertexDeclaration()
{
    if (vertexDecl_ != nullptr)
        return;

    const D3DVERTEXELEMENT9 VertexElements[] = {
        {0, 0, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_POSITION, 0},
        {0, 12, D3DDECLTYPE_FLOAT3, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_NORMAL, 0},
        {0, 24, D3DDECLTYPE_FLOAT2, D3DDECLMETHOD_DEFAULT, D3DDECLUSAGE_TEXCOORD, 0},
        D3DDECL_END()};

    rs->CreateVertexDeclaration(VertexElements, &vertexDecl_);
}

bool SEA::Init()
{
    rs = static_cast<VDX9RENDER *>(core.GetService("dx9render"));
    CreateVertexDeclaration();
    {
        auto pEngineIni = fio->OpenIniFile(core.EngineIniFileName());
        bIniFoamEnable = (pEngineIni) ? pEngineIni->GetInt("Sea", "FoamEnable", 1) != 0 : false;
    }

    iFoamTexture = rs->TextureCreate("weather\\sea\\pena\\pena.tga");

    rs->CreateTexture(XWIDTH, YWIDTH, MIPSLVLS, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                      &pRenderTargetBumpMap);
    // Plain DYNAMIC texture: lockable every frame, unlike the RT above.
    // 8 mips like Blood (128 -> 1x1) for a clean distance fade.
    rs->CreateTexture(XWIDTH, YWIDTH, 8, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pLiveBumpMap);

    SFLB_CreateBuffers();

    // Live FFT waves replace baked seaXXXX.tga height interpolation
    // (baked data stays only for the SimpleSea volume-texture path).
    fftWaves.Init(128);
    fftBump.Init(128);
    SetupFFTFromSea2Attrs();

    pVolumeTexture = rs->CreateVolumeTexture(XWIDTH, YWIDTH, FRAMES, 4, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED);
    /*pAnimTexture = rs->CreateAnimationTexture(pVolumeTexture, _FL_);
    pAnimTexture->SetAnimSpeed(20.0f);*/

    rs->CreateCubeTexture(128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pEnvMap);
    rs->CreateCubeTexture(128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pSunRoadMap);
    rs->CreateDepthStencilSurface(128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &pZStencil);

    // Blood uses A8R8G8B8 for reflection targets (no sky-gradient banding).
    rs->CreateTexture(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &pReflection);
    rs->CreateTexture(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &pReflectionSunroad);
    rs->CreateDepthStencilSurface(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, D3DFMT_D24S8, D3DMULTISAMPLE_NONE,
                                  &pReflectionSurfaceDepth);
    // Refraction scene target with alpha (Task 1, shading plan).
    rs->CreateTexture(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &pRefraction);

    iSeaTrashTexture = rs->TextureCreate("seatrash.tga");
    iSeaLightTexture = rs->TextureCreate("sealight.tga");

    uint8_t bMin = 0xFF;
    uint8_t bMax = 0;

    std::vector<uint8_t *> aTmpBumps;

    uint32_t i;

    for (i = 0; i < FRAMES; i++)
    {
        char str[256];
        char *pFBuffer = nullptr;
        uint32_t dwSize;
        sprintf_s(str, "resource\\sea\\sea%.4d.tga", i);
        // sprintf_s(str, "resource\\sea\\sea0000.tga", i);
        fio->LoadFile(str, &pFBuffer, &dwSize);
        if (!pFBuffer)
        {
            core.Trace("Sea: Can't load %s", str);
            return false;
        }

        auto *pFB = pFBuffer + sizeof(TGA_H);

        auto *pBuffer = new uint8_t[XWIDTH * YWIDTH];
        aTmpBumps.push_back(pBuffer);

        for (uint32_t y = 0; y < YWIDTH; y++)
            for (uint32_t x = 0; x < XWIDTH; x++)
            {
                const uint8_t bB = (*pFB);
                // bB = byte(float(bB - 79.0f) * 255.0f / (139.0f - 79.0f));
                if (bB < bMin)
                    bMin = bB;
                if (bB > bMax)
                    bMax = bB;
                pBuffer[x + y * XWIDTH] = bB & 0xFF;
                pFB += sizeof(uint32_t);
            }

        STORM_DELETE(pFBuffer);
    }

    for (i = 0; i < FRAMES; i++)
    {
        auto *pBuffer = new uint8_t[XWIDTH * YWIDTH];
        aBumps.push_back(pBuffer);

        for (uint32_t y = 0; y < YWIDTH; y++)
            for (uint32_t x = 0; x < XWIDTH; x++)
            {
                const auto dwAddress = x + y * YWIDTH;
                float b1, b2, b3, b4, b5; // -2 -1 0 1 2

                b1 = 0.08f * static_cast<float>(aTmpBumps[(i - 2) & (FRAMES - 1)][dwAddress]);
                b2 = 0.17f * static_cast<float>(aTmpBumps[(i - 1) & (FRAMES - 1)][dwAddress]);
                b3 = 0.50f * static_cast<float>(aTmpBumps[(i - 0) & (FRAMES - 1)][dwAddress]);
                b4 = 0.17f * static_cast<float>(aTmpBumps[(i + 1) & (FRAMES - 1)][dwAddress]);
                b5 = 0.08f * static_cast<float>(aTmpBumps[(i + 2) & (FRAMES - 1)][dwAddress]);

                pBuffer[dwAddress] = aTmpBumps[(i - 0) & (FRAMES - 1)][dwAddress];
            }
    }

    for (i = 0; i < aTmpBumps.size(); i++)
        STORM_DELETE(aTmpBumps[i]);

    BuildVolumeTexture();

    // NOTE: legacy EditMode_Update() stub (Amp1=8, test colors) must NOT run
    // here: constructor defaults are the Blood engine defaults. The stub
    // remains available for editor/debug use only.
    return true;
}

void SEA::BuildVolumeTexture()
{
    std::vector<CVECTOR *> aVectors;
    uint32_t i, j;

    for (const auto &normal : aNormals)
        delete normal;
    aNormals.clear();

    D3DLOCKED_BOX box[4];

    if (pVolumeTexture)
        for (i = 0; i < 4; i++)
            pVolumeTexture->LockBox(i, &box[i], nullptr, 0);

    for (i = 0; i < aBumpMaps.size(); i++)
        rs->Release(aBumpMaps[i]);
    aBumpMaps.clear();

    uint32_t dwTexelSize = 4;
    auto *pDst = static_cast<char *>(new char[XWIDTH * YWIDTH * dwTexelSize]);

    // build normals

    aVectors.reserve(FRAMES);
    for (i = 0; i < FRAMES; i++)
    {
        auto pBuffer = new uint32_t[XWIDTH * YWIDTH];
        aNormals.push_back(pBuffer);

        auto *pVectors = new CVECTOR[XWIDTH * YWIDTH];
        aVectors.push_back(pVectors);

        for (uint32_t y = 0; y < YWIDTH; y++)
            for (uint32_t x = 0; x < XWIDTH; x++)
            {
#define GET_MASSIVE(dx, dy)                                                                                            \
    (float(pMassive[((x + dx) & (XWIDTH - 1)) + ((y + dy) & (XWIDTH - 1)) * XWIDTH] & 0xFF) / 255.0f)
                uint8_t *pMassive = aBumps[i];

                float fCenter = GET_MASSIVE(0, 0);

                float fLeft = GET_MASSIVE(-1, 0) - fCenter;
                float fRight = GET_MASSIVE(1, 0) - fCenter;
                float fTop = GET_MASSIVE(0, -1) - fCenter;
                float fBottom = GET_MASSIVE(0, 1) - fCenter;

                CVECTOR vRes, vRes1, d0, d1, d2, d3;

                uint32_t dwNums = 0;
                if (fLeft < 0.0f)
                    dwNums++;
                if (fRight < 0.0f)
                    dwNums++;
                if (fTop < 0.0f)
                    dwNums++;
                if (fBottom < 0.0f)
                    dwNums++;

                float d = 1.0f;
                d0 = CVECTOR(-1.f, d * fLeft, 0.f);
                d1 = CVECTOR(0.f, d * fTop, -1.f);
                d2 = CVECTOR(1.f, d * fRight, 0.f);
                d3 = CVECTOR(0.f, d * fBottom, 1.f);

                // res = !((d0^d1) + (d2^d3));

                CVECTOR v1 = (d1 ^ d0);
                CVECTOR v2 = (d3 ^ d2);
                CVECTOR v3 = (d0 ^ d3);
                CVECTOR v4 = (d2 ^ d1);

                vRes = vRes1 = !((d1 ^ d0) + (d3 ^ d2) + (d0 ^ d3) + (d2 ^ d1));
                if (bSimpleSea)
                {
                    vRes1 = !(vRes * CVECTOR(100.0f, 1.0f, 100.0f));
                }
                // CVECTOR vRes1 = !(vRes * CVECTOR(100.0f, 1.0f, 100.0f));
                uint32_t dwRes = MAKELONG(static_cast<short>(vRes.x * 32767.5f), static_cast<short>(vRes.z * 32767.5f));

                aNormals[i][x + y * XWIDTH] = dwRes;
                aVectors[i][x + y * XWIDTH] = vRes1;

                int32_t red = fftol((vRes1.x * 0.5f + 0.5f) * 255.0f);   // FIX-ME no ftol
                int32_t green = fftol((vRes1.y * 0.5f + 0.5f) * 255.0f); // FIX-ME no ftol
                int32_t blue = fftol((vRes1.z * 0.5f + 0.5f) * 255.0f);  // FIX-ME no ftol

                if (pVolumeTexture)
                {
                    if (bSimpleSea)
                        *(uint32_t *)&(
                            static_cast<char *>(box[0].pBits)[i * box[0].SlicePitch + y * box[0].RowPitch + x * 4]) =
                            ARGB(0x80, blue, blue, red);
                    else
                        *(uint32_t *)&(
                            static_cast<char *>(box[0].pBits)[i * box[0].SlicePitch + y * box[0].RowPitch + x * 4]) =
                            ARGB(0x80, blue, green, red);
                }
            }

        if (!pVolumeTexture)
        {
            D3DSURFACE_DESC d3dsd;
            D3DLOCKED_RECT d3dlr;
            HRESULT hr;

            aBumpMaps.push_back(nullptr);
            // pBumpMap = &aBumpMaps[aBumpMaps.Add()];
            IDirect3DTexture9 **pBumpMap = &aBumpMaps.back();
            hr = rs->CreateTexture(XWIDTH, YWIDTH, MIPSLVLS, 0, D3DFMT_X8R8G8B8, D3DPOOL_MANAGED, pBumpMap);

            // generate mip levels for random bump
            for (uint32_t lev = 0; lev < MIPSLVLS; lev++)
            {
                (*pBumpMap)->GetLevelDesc(lev, &d3dsd);
                (*pBumpMap)->LockRect(lev, &d3dlr, nullptr, 0);

                auto *pDstT = (uint32_t *)pDst;
                for (uint32_t y = 0; y < d3dsd.Height; y++)
                {
                    for (uint32_t x = 0; x < d3dsd.Width; x++)
                    {
                        CVECTOR vTmp = 0.0f;
                        int32_t dwMult = 1 << (lev);
                        for (uint32_t y1 = y * dwMult; y1 < (y + 1) * dwMult; y1++)
                            for (uint32_t x1 = x * dwMult; x1 < (x + 1) * dwMult; x1++)
                                vTmp += aVectors[i][(x1 & (XWIDTH - 1)) + (y1 & (YWIDTH - 1)) * XWIDTH];

                        vTmp *= (1.0f / static_cast<float>(dwMult * dwMult));

                        int red = static_cast<int>((vTmp.x * 0.5f + 0.5f) * 255.0f);   // FIX-ME no ftol
                        int green = static_cast<int>((vTmp.y * 0.5f + 0.5f) * 255.0f); // FIX-ME no ftol
                        int blue = static_cast<int>((vTmp.z * 0.5f + 0.5f) * 255.0f);  // FIX-ME no ftol
                        *pDstT++ = ARGB(255, blue, green, red);
                    }
                }

                // simple copy
                auto pDstTemp = static_cast<uint8_t *>(d3dlr.pBits);
                for (uint32_t y = 0; y < d3dsd.Height; y++)
                {
                    memcpy(pDstTemp, &pDst[y * d3dsd.Width * dwTexelSize], d3dsd.Width * dwTexelSize);
                    pDstTemp += static_cast<uint32_t>(d3dlr.Pitch);
                }

                (*pBumpMap)->UnlockRect(lev);
            }
        }
    }

    for (j = 1; j < 4; j++)
    {
        auto *pVectors = new CVECTOR[(XWIDTH >> j) * (YWIDTH >> j)];

        for (i = 0; i < (FRAMES >> j); i++)
        {
            // calculate current pVectors
            for (uint32_t y = 0; y < (YWIDTH >> j); y++)
                for (uint32_t x = 0; x < (XWIDTH >> j); x++)
                {
                    int32_t iNumVectors = 0;
                    CVECTOR vVec = 0.0f;
                    uint32_t dwW = 1 << j;
                    for (uint32_t k = i * dwW; k < (i + 1) * dwW; k++)
                    {
                        for (uint32_t yy = y * dwW; yy < (y + 1) * dwW; yy++)
                            for (uint32_t xx = x * dwW; xx < (x + 1) * dwW; xx++)
                            {
                                iNumVectors++;
                                vVec += aVectors[k][xx + yy * XWIDTH];
                            }
                    }
                    pVectors[x + y * (XWIDTH >> j)] = vVec / static_cast<float>(iNumVectors);
                }

            //
            if (pVolumeTexture)
                for (uint32_t y = 0; y < (YWIDTH >> j); y++)
                    for (uint32_t x = 0; x < (XWIDTH >> j); x++)
                    {
                        int32_t red = fftol((pVectors[x + y * (XWIDTH >> j)].x * 0.5f + 0.5f) * 255.0f); // FIX-ME no ftol
                        int32_t green = fftol((pVectors[x + y * (XWIDTH >> j)].y * 0.5f + 0.5f) * 255.0f);
                        // FIX-ME no ftol
                        int32_t blue = fftol((pVectors[x + y * (XWIDTH >> j)].z * 0.5f + 0.5f) * 255.0f); // FIX-ME no ftol

                        if (bSimpleSea)
                            *(uint32_t *)&(static_cast<char *>(
                                box[j].pBits)[i * box[j].SlicePitch + y * box[j].RowPitch + x * 4]) =
                                ARGB(0x80, blue, blue, red);
                        else
                            *(uint32_t *)&(static_cast<char *>(
                                box[j].pBits)[i * box[j].SlicePitch + y * box[j].RowPitch + x * 4]) =
                                ARGB(0x80, blue, green, red);
                    }
        }
        STORM_DELETE(pVectors);
    }

    if (pVolumeTexture)
        for (i = 0; i < 4; i++)
            pVolumeTexture->UnlockBox(i);

    for (const auto &vector : aVectors)
        delete vector;
    aVectors.clear();
    // aVectors.DelAllWithPointers();
    STORM_DELETE(pDst);
}

int32_t SEA::VisCode(const CVECTOR &vP)
{
    int32_t vc = 0;

    /*
    CVECTOR v = vP - vCamPos;

    CVECTOR vp1 = CVECTOR(pFrustumPlanes[0].Nx, pFrustumPlanes[0].Ny, pFrustumPlanes[0].Nz);
    CVECTOR vp2 = CVECTOR(pFrustumPlanes[1].Nx, pFrustumPlanes[1].Ny, pFrustumPlanes[1].Nz);
    CVECTOR vp3 = CVECTOR(pFrustumPlanes[2].Nx, pFrustumPlanes[2].Ny, pFrustumPlanes[2].Nz);
    CVECTOR vp4 = CVECTOR(pFrustumPlanes[3].Nx, pFrustumPlanes[3].Ny, pFrustumPlanes[3].Nz);

    //if((v | pFrustumPlanes[0].n) < 0 ) vc |= 0x10;
    if ((v | vp1) < 0 ) vc |= 0x01;
    if ((v | vp2) < 0 ) vc |= 0x02;
    if ((v | vp3) < 0 ) vc |= 0x04;
    if ((v | vp4) < 0 ) vc |= 0x08;

    */

    // Max - so the accuracy is much higher, blocks should not disappear sometimes in the distance
    PLANE *p = &pFrustumPlanes[0];
    if (p->Nx * vP.x + p->Ny * vP.y + p->Nz * vP.z < p->D)
        vc |= 0x01;
    p = &pFrustumPlanes[1];
    if (p->Nx * vP.x + p->Ny * vP.y + p->Nz * vP.z < p->D)
        vc |= 0x02;
    p = &pFrustumPlanes[2];
    if (p->Nx * vP.x + p->Ny * vP.y + p->Nz * vP.z < p->D)
        vc |= 0x04;
    p = &pFrustumPlanes[3];
    if (p->Nx * vP.x + p->Ny * vP.y + p->Nz * vP.z < p->D)
        vc |= 0x08;

    return vc;
}

bool SEA::isVisibleBBox(const CVECTOR &vCenter, const CVECTOR &v1, const CVECTOR &v2)
{
    /*CVECTOR vc = vCenter - vCamPos;
    CVECTOR vp1 = v1 - vCamPos;
    CVECTOR vp2 = v2 - vCamPos;
    float fR2 = sqrtf(Sqr((vp1.x - vp2.x) * 0.5f) + Sqr((vp1.z - vp2.z)) * 0.5f);*/

    // if sphere not visible - return
    //    for (uint32_t i=0; i<dwNumFrustumPlanes; i++)
    //        if ((pFrustumPlanes[i].n | vc) - pFrustumPlanes[i].d < -fR2) return false;

    // check box visible
    int32_t vc = 0xFF;
    vc &= VisCode(CVECTOR(v1.x, v1.y, v1.z));
    if (vc == 0)
        return true;
    vc &= VisCode(CVECTOR(v1.x, v2.y, v1.z));
    vc &= VisCode(CVECTOR(v2.x, v1.y, v1.z));
    vc &= VisCode(CVECTOR(v2.x, v2.y, v1.z));
    if (vc == 0)
        return true;
    vc &= VisCode(CVECTOR(v1.x, v1.y, v2.z));
    vc &= VisCode(CVECTOR(v1.x, v2.y, v2.z));
    vc &= VisCode(CVECTOR(v2.x, v1.y, v2.z));
    vc &= VisCode(CVECTOR(v2.x, v2.y, v2.z));

    return vc == 0;
}

inline float SEA::CalcLod(const float &x, const float &y, const float &z)
{
    return Sqr(x - vCamPos.x) + /*Sqr((y - vCamPos.y)) + */ Sqr(z - vCamPos.z);
}

void SEA::CalculateLOD(const CVECTOR &v1, const CVECTOR &v2, int32_t &iMaxLOD, int32_t &iMinLOD)
{
    float fCur, fMax, fMin;

    fCur = CalcLod(v1.x, vSeaCenterPos.y, v1.z);
    fMax = fCur;
    fCur = CalcLod(v2.x, vSeaCenterPos.y, v1.z);
    if (fCur > fMax)
        fMax = fCur;
    fCur = CalcLod(v1.x, vSeaCenterPos.y, v2.z);
    if (fCur > fMax)
        fMax = fCur;
    fCur = CalcLod(v2.x, vSeaCenterPos.y, v2.z);
    if (fCur > fMax)
        fMax = fCur;

    if (vCamPos.x < v1.x)
    {
        if (vCamPos.z < v1.z)
            fMin = CalcLod(v1.x, vSeaCenterPos.y, v1.z);
        else if (vCamPos.z > v2.z)
            fMin = CalcLod(v1.x, vSeaCenterPos.y, v2.z);
        else
            fMin = CalcLod(v1.x, vSeaCenterPos.y, vCamPos.z);
    }
    else if (vCamPos.x > v2.x)
    {
        if (vCamPos.z < v1.z)
            fMin = CalcLod(v2.x, vSeaCenterPos.y, v1.z);
        else if (vCamPos.z > v2.z)
            fMin = CalcLod(v2.x, vSeaCenterPos.y, v2.z);
        else
            fMin = CalcLod(v2.x, vSeaCenterPos.y, vCamPos.z);
    }
    else
    {
        if (vCamPos.z < v1.z)
            fMin = CalcLod(vCamPos.x, vSeaCenterPos.y, v1.z);
        else if (vCamPos.z > v2.z)
            fMin = CalcLod(vCamPos.x, vSeaCenterPos.y, v2.z);
        else
            fMin = CalcLod(vCamPos.x, vSeaCenterPos.y, vCamPos.z);
    }

    iMaxLOD = static_cast<int>(0.5f * logf(fLodScale * fMax) / logf(2));
    if (iMaxLOD < 1)
        iMaxLOD = 1;
    iMinLOD = static_cast<int>(0.5f * logf(fLodScale * fMin) / logf(2));
    if (iMinLOD < 1)
        iMinLOD = 1;
}

void SEA::AddBlock(int32_t iTX, int32_t iTY, int32_t iSize, int32_t iLOD)
{
    // Blood clamps the finest split (Ocean.cpp:2143, block.iLOD = Max(4,iLOD)):
    // near field stops at LOD 4 (1.19 m step), not LOD 1 (0.15 m, 64x denser).
    iLOD = Max(4, iLOD);
    aBlocks.emplace_back(SeaBlock{0, 0, 0, 0, iSize >> iLOD, iTX, iTY, iSize, iLOD, 0, 0, 0, false, false});
}

void SEA::BuildTree(int32_t iTX, int32_t iTY, int32_t iLev)
{
    int32_t iMaxLOD, iMinLOD;
    const int32_t iSize = static_cast<int32_t>(dwMaxDim >> iLev);
    const float fGSize = fGridStep * iSize;

    const CVECTOR v1 = vSeaCenterPos + CVECTOR(iTX * fGSize, -fMaxSeaHeight / 2.0f, iTY * fGSize);
    const CVECTOR v2 = v1 + CVECTOR(fGSize, fMaxSeaHeight, fGSize);

    if (!isVisibleBBox(vSeaCenterPos, v1, v2))
        return;

    CalculateLOD(v1, v2, iMaxLOD, iMinLOD);

    if (iSize <= static_cast<int32_t>(dwMinDim) || iMaxLOD - iMinLOD <= 1)
    {
        AddBlock(iTX, iTY, iSize, iMinLOD);
        return;
    }

    iTX *= 2;
    iTY *= 2;
    iLev++;

    BuildTree(iTX, iTY, iLev);
    BuildTree(iTX + 1, iTY, iLev);
    BuildTree(iTX, iTY + 1, iLev);
    BuildTree(iTX + 1, iTY + 1, iLev);
}

// INTEL COMMENT:
// This version of the function takes in 4 at a time, to take advantage of SSE.
// In particular I have converted the normalise and square roots over to SSE.
// (Legacy fixed-spectrum notes kept for history: FFT amplitudes now come
// from Sea2.FFTAmp1/FFTAmp2 script attributes, slopes are unboosted Blood
// frame slopes.)

// Bilinear sample of one FFT frame (Ocean3 Ocean_pc.cpp scalar WaveXZ).
static inline void SampleFFTLayer(const fft::dcomplex *frame, int size, float u, float v, float &h, float &nx,
                                  float &nz);

void SEA::SSE_WaveXZ(SeaVertex **pArray)
{
    const bool haveFFT = (fftWavesFrame != nullptr && fftBumpFrame != nullptr);
    const int wavesSize = fftWaves.GetSize();
    const int bumpSize = fftBump.GetSize();

    for (int i = 0; i < 4; i++)
    {
        float fDistance = Sqr(pArray[i]->vPos.x - vCamPos.x) + Sqr(pArray[i]->vPos.z - vCamPos.z);
        if (fDistance > fMaxSeaDistance * fMaxSeaDistance || !haveFFT)
        {
            pArray[i]->vNormal.x = 0.0f;
            pArray[i]->vNormal.y = 1.0f;
            pArray[i]->vNormal.z = 0.0f;
            pArray[i]->vPos.y = 0.0f;
            continue;
        }

        float h1, nX1, nZ1, h2, nX2, nZ2;
        SampleFFTLayer(fftWavesFrame, wavesSize, (pArray[i]->vPos.x + vMove1.x) * fScale1,
                       (pArray[i]->vPos.z + vMove1.z) * fScale1, h1, nX1, nZ1);
        SampleFFTLayer(fftBumpFrame, bumpSize, (pArray[i]->vPos.x + vMove2.x) * fScale2,
                       (pArray[i]->vPos.z + vMove2.z) * fScale2, h2, nX2, nZ2);

        pArray[i]->vPos.y = fAmp1 * h1 + fAmp2 * h2;

        // Blood stores RAW frame slopes in the vertex (Ocean_pc.cpp:239-243,
        // layer 1 only, no scale multiply); y is reconstructed per-frame in
        // the VS from xz (Ocean3.fx:63-76, our sea_ocean3.fx:112-114).
        // No CPU normalize: pre-normalized xz understated crest tilt.
        pArray[i]->vNormal.x = fAmp1 * nX1;
        pArray[i]->vNormal.y = 1.0f; // placeholder, overwritten by the VS
        pArray[i]->vNormal.z = fAmp1 * nZ1;
    }
}

// Sea look comes from Sea2.* / fog.* script attributes (defaults in the
// SEA constructor are the Blood engine defaults). New Ocean3 attributes
// (no legacy analog) are marked NEW in sea.h and AttributeChanged.
namespace {
// Blood sun math: v=(0,0,1) transformed by RotX then RotY via MulVertex,
// which reads result.x = m[0][0]*x + m[1][0]*y + m[2][0]*z (column-style).
// For v0=(0,0,1): RotX gives (0,-sin,cos); RotY then gives (sy*cx, -sx,
// cy*cx). NOTE the minus on Y (review-caught: +sx blacks out the sea,
// MOP sunlight is zenith, not nadir).
static CVECTOR BloodSunDir(float ax, float ay, bool subPI)
{
    if (subPI)
        ax -= 3.14159265f;
    const float sx = sinf(ax), cx = cosf(ax), sy = sinf(ay), cy = cosf(ay);
    return !(CVECTOR(sy * cx, -sx, cy * cx));
}
} // namespace

void SEA::SetupFFTFromSea2Attrs()
{
    // FFT spectra are script-driven (Sea2.FFTLen1/FFTAmp1/...); Setup is
    // cheap (~50ms) and runs only on Init/attribute change.
    fftWaves.Setup(fFFTLen1, fFFTAmp1 * 1e-10f, fFFTWind1, vFFTDir1.x, vFFTDir1.z, fFFTAlign1);
    fftWavesLen = fFFTLen1;
    // Bump chop is ISOTROPIC by default (align 0.0, Blood MOP exact):
    // directional align made streaky jelly + visible tiling (chessboard).
    fftBump.Setup(fFFTLen2, fFFTAmp2 * 1e-10f, fFFTWind2, vFFTDir2.x, vFFTDir2.z, fFFTAlign2);
    fftBumpLen = fFFTLen2;
    // Blood linkage (Ocean.cpp:1179, m_wavesScale = mopScale*size/len):
    // sampling scale = script multiplier * spectrum size/len.
    fScale1 = fSeaScale1 * static_cast<float>(fftWaves.GetSize()) / fFFTLen1;
    fScale2 = fSeaScale2 * static_cast<float>(fftBump.GetSize()) / fFFTLen2;
}

#if SEA_MOP_TEST
// TEMP: mission1 Ocean2 scene values (MOP raw units, Blood transforms apply
// at use, same as attributes). Scene datum Height -2 via members.
void SEA::ApplyMopTestOverrides()
{
    fSeaHeight = 0.0f; // datum 0 (project decision; scene Height -2 not used)
    fSeaFake = 0.0f;
    fFFTLen1 = 256.0f;
    fFFTAmp1 = 2915.405518f;
    fFFTWind1 = 13.49445f;
    vFFTDir1 = CVECTOR(1.0f, 0.0f, 0.0f);
    fFFTAlign1 = 0.1f;
    fFFTLen2 = 338.511841f;
    fFFTAmp2 = 17227.560547f;
    fFFTWind2 = 23.921381f;
    vFFTDir2 = CVECTOR(1.0f, 0.0f, 0.0f);
    fFFTAlign2 = 0.0f;
    fAnimSpeed1 = 1.038077f;
    fAnimSpeed2 = 4.0f;
    fAmp1 = _fAmp1 = 1.0f; // temp-override, Ocean_pc.cpp:273-274
    fAmp2 = _fAmp2 = 0.0f;
    fSeaScale1 = fSeaScale2 = 1.0f; // Waves Scale
    SetupFFTFromSea2Attrs();
    fMaxSeaDistance = 2200.0f;
    vMoveSpeed1 = CVECTOR(3.0f, 0.0f, 0.0f);
    vMoveSpeed2 = CVECTOR(0.0f, 0.0f, 0.0f);
    fPosShift = 12.0f / 5.0f * sqrtf(256.0f / 128.0f); // Choppy 12
    fGridStep = 0.0745f;
    fLodScale = 0.112f;
    dwMinDim = 256;
    dwMaxDim = 81920;
    v4SeaColor = CVECTOR4(12.0f / 255.0f, 64.0f / 255.0f, 100.0f / 255.0f, 1.0f);
    v4SkyColor = CVECTOR4(100.0f / 255.0f, 153.0f / 255.0f, 211.0f / 255.0f, 1.0f);
    fBumpScaleU = 52.815971f; // MOP raw (use: *0.004)
    fBumpScaleV = 92.663956f; // MOP raw (use: *0.004)
    fBumpPower = 0.191265f;
    fBumpAngle = 60.0f * 3.14159265f / 180.0f;
    fBumpMoveSpeed = 1.898017f;
    fFoamV = -1.8f; // scene -3.8 shifted +2 for datum 0 (same foam response)
    fFoamK = 1.0f / 6.975295f; // scene Foam height Y as rate
    fFoamPower = 1.0f;
    fFoamBright = 0.65f;
    fFoamAngle = 0.0f;
    fFoamMoveSpeed = 0.0f;
    fFoamMoveAngle = 0.0f;
    v4FoamColor = CVECTOR4(120.0f / 255.0f, 174.0f / 255.0f, 189.0f / 255.0f, 1.0f);
    bFoamEnable = true;
    bTransEnable = false;
    fTransAngle = 0.6f;
    fTransPower = 9.391157f;
    fTransMultiply = 9.38157f;
    v4TransColor = CVECTOR4(77.0f / 255.0f, 101.0f / 255.0f, 141.0f / 255.0f, 1.0f);
    bSunRoadEnable = true;
    fSunRoadAngX = 6.0f;
    fSunRoadAngY = 4.0f;
    fSunAngX = 4.7f;
    fSunAngY = 3.1415f;
    fSunRoadPower = 441.504303f;
    fSunRoadColorPower = 117.172691f; // MOP raw (use: *0.01)
    fSunBumpScale = 18.235334f;
    fSunMultiply = 0.8f;
    fSunAmbient = 0.15f;
    v4SunRoadColor = CVECTOR4(165.0f / 255.0f, 187.0f / 255.0f, 234.0f / 255.0f, 1.0f);
    fReflMinimum = 0.65f;
    fReflAdd = 2.0f;
    fReflPower = 1.0f;
    fRefrEnvPower = 100.0f;
    fReflEnvBlend = 0.65f;
    fReflMin = 1.0f;
    fReflMax = 0.75f;
    fRefrPower = 100.0f;
    fTransMin = 0.1f;
    fTransMax = 0.000289f;
    fFogSeaDensity = 0.8f;
    fFogStartDistance = 243.333435f;
    fFogDistance = 1339.96521f;
    vFogColor = CVECTOR(1.0f / 255.0f, 8.0f / 255.0f, 14.0f / 255.0f);
    bFogEnable = true;
}
#endif

// Bilinear sample of one FFT frame (Ocean3 Ocean_pc.cpp scalar WaveXZ).
// (Forward-declared before SSE_WaveXZ; defined here.)
static inline void SampleFFTLayer(const fft::dcomplex *frame, int size, float u, float v, float &h, float &nx,
                                  float &nz)
{
    const int mask = size - 1;
    const int x0 = ffloor(u), z0 = ffloor(v);
    const float fx = u - static_cast<float>(x0), fz = v - static_cast<float>(z0);
    const int x1 = x0 + 1, z1 = z0 + 1;

    const fft::dcomplex &a1 = frame[(x0 & mask) + (z0 & mask) * size];
    const fft::dcomplex &a2 = frame[(x1 & mask) + (z0 & mask) * size];
    const fft::dcomplex &a3 = frame[(x0 & mask) + (z1 & mask) * size];
    const fft::dcomplex &a4 = frame[(x1 & mask) + (z1 & mask) * size];

    h = a1.c1.Re + fx * (a2.c1.Re - a1.c1.Re) + fz * (a3.c1.Re - a1.c1.Re) +
        fx * fz * (a4.c1.Re + a1.c1.Re - a2.c1.Re - a3.c1.Re);
    // Blood mesh normals are unboosted frame slopes (no scale multiply).
    nx = a1.c2.Re + fx * (a2.c2.Re - a1.c2.Re) + fz * (a3.c2.Re - a1.c2.Re) +
         fx * fz * (a4.c2.Re + a1.c2.Re - a2.c2.Re - a3.c2.Re);
    nz = a1.c2.Im + fx * (a2.c2.Im - a1.c2.Im) + fz * (a3.c2.Im - a1.c2.Im) +
         fx * fz * (a4.c2.Im + a1.c2.Im - a2.c2.Im - a3.c2.Im);
}

void SEA::GenerateBumpMapFFT()
{
    if (!fftBumpFrame || !pLiveBumpMap || fftBump.GetSize() != XWIDTH)
        return;

    // Blood builds 8 mips (128 -> 1x1); fewer leaves a repeating shimmer at
    // grazing angles where the GPU samples tiny mips (chessboard feel).
    const int bumpMips = 8;

    std::vector<float> *cur = &bumpScratchA, *next = &bumpScratchB;
    int size = XWIDTH;

    for (int lev = 0; lev < bumpMips; lev++, size >>= 1)
    {
        if (lev == 0)
        {
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    // Raw frame slopes, no clamp (Blood wraps at pack time).
                    (*cur)[2 * (x + y * size) + 0] = fftBumpFrame[x + y * XWIDTH].c2.Re;
                    (*cur)[2 * (x + y * size) + 1] = fftBumpFrame[x + y * XWIDTH].c2.Im;
                }
        }
        else
        {
            const int prev = size * 2;
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    const float nx = 0.25f * ((*cur)[2 * ((2 * x + 0) + (2 * y + 0) * prev) + 0] +
                                              (*cur)[2 * ((2 * x + 1) + (2 * y + 0) * prev) + 0] +
                                              (*cur)[2 * ((2 * x + 0) + (2 * y + 1) * prev) + 0] +
                                              (*cur)[2 * ((2 * x + 1) + (2 * y + 1) * prev) + 0]);
                    const float nz = 0.25f * ((*cur)[2 * ((2 * x + 0) + (2 * y + 0) * prev) + 1] +
                                              (*cur)[2 * ((2 * x + 1) + (2 * y + 0) * prev) + 1] +
                                              (*cur)[2 * ((2 * x + 0) + (2 * y + 1) * prev) + 1] +
                                              (*cur)[2 * ((2 * x + 1) + (2 * y + 1) * prev) + 1]);
                    (*next)[2 * (x + y * size) + 0] = nx;
                    (*next)[2 * (x + y * size) + 1] = nz;
                }
            std::swap(cur, next);
        }

        D3DLOCKED_RECT lr;
        // Never silently swallow a lock failure (used to hide C1-class bugs).
        // Packing replicates Blood ARGB(fz,0,0,fx) exactly: A=nz, B=nx, R=G=0,
        // channels wrap mod 256 (x64 DWORD(float) semantics), no clamp.
        auto packB = [](float v) -> uint32_t {
            const int q = (int)(v * 127.5f + 127.5f);
            return (uint32_t)(q & 0xFF);
        };
        if (SUCCEEDED(pLiveBumpMap->LockRect(lev, &lr, nullptr, (lev == 0) ? D3DLOCK_DISCARD : 0)) && lr.pBits)
        {
            auto *dst = static_cast<uint8_t *>(lr.pBits);
            for (int y = 0; y < size; y++)
                for (int x = 0; x < size; x++)
                {
                    const float nx = (*cur)[2 * (x + y * size) + 0];
                    const float nz = (*cur)[2 * (x + y * size) + 1];
                    *reinterpret_cast<uint32_t *>(&dst[y * lr.Pitch + x * 4]) = packB(nx) | (packB(nz) << 24);
                }
            pLiveBumpMap->UnlockRect(lev);
        }
        else
        {
            static bool warned = false;
            if (!warned)
            {
                warned = true;
                core.Trace("Sea: GenerateBumpMapFFT LockRect failed, live bump disabled");
            }
            return;
        }
    }
}

bool SEA::SetupOcean3Effect(const CMatrix &mWorldViewProj, const CVECTOR4 &camPos)
{
    // Effect-owned uniforms MUST go through ID3DXEffect: anything declared in
    // the .fx but left unassigned commits as zeros on BeginPass, wiping
    // device-set registers (this exact bug hid the sea completely).
    ID3DXEffect *fx = rs->GetEffectPointer("newseaocean3");
    if (!fx)
    {
        static bool warned = false;
        if (!warned)
        {
            warned = true;
            core.Trace("Sea: newseaocean3 effect missing, sea will be invisible");
        }
        return false;
    }

    // Missing handle = uniform optimized out by the current VS/PS (e.g. PS
    // uniforms while the flat PS is bound): skip, still success. Technique
    // presence (fx != null) is the only hard requirement.
    auto setVec = [fx](const char *name, const CVECTOR4 &v) {
        D3DXHANDLE h = fx->GetParameterByName(nullptr, name);
        if (h)
            fx->SetVector(h, reinterpret_cast<const D3DXVECTOR4 *>(&v));
    };
    auto setMat = [fx](const char *name, const CMatrix &m) {
        D3DXHANDLE h = fx->GetParameterByName(nullptr, name);
        if (h)
            fx->SetMatrix(h, reinterpret_cast<const D3DXMATRIX *>(&m));
    };

    // Reflection/transparency mix (Blood ReflTrans = min/max pairs).
    CVECTOR4 reflTrans = CVECTOR4(fReflMin, fReflMax, fTransMin, fTransMax);
    // Refraction params: power, env power, multiply, color multiply (x1000 in MOP).
    CVECTOR4 refr = CVECTOR4(fRefrPower * 0.001f, fRefrEnvPower * 0.001f, fReflAdd, fReflPower);
    CVECTOR4 refr2 = CVECTOR4(fReflMinimum, fReflEnvBlend, 0.0f, 0.0f);
    // MOP sun (Blood look is scene-independent: glitter/diffuse always from
    // these dirs; scene-driven sun killed the night glitter). Scene variant
    // lives under the #if block below for future weather sea states.
    CVECTOR sunDir = BloodSunDir(fSunRoadAngX, fSunRoadAngY, true);
    CVECTOR sunLDir = BloodSunDir(fSunAngX, fSunAngY, false);
    // SunRoadColorPower is MOP raw here (Color power 100): Blood pre-
    // multiplies *0.01 at load (Ocean.cpp:890).
    CVECTOR4 sunRoadColor = CVECTOR4(v4SunRoadColor.x * fSunRoadColorPower * 0.01f,
                                     v4SunRoadColor.y * fSunRoadColorPower * 0.01f,
                                     v4SunRoadColor.z * fSunRoadColorPower * 0.01f, 1.0f);
#if 0 // MOP sun for reference match (scene sun under test separately)
    D3DLIGHT9 sceneSun{};
    if (rs->GetLight(0, &sceneSun))
    {
        CVECTOR toSun = -CVECTOR(sceneSun.Direction.x, sceneSun.Direction.y, sceneSun.Direction.z);
        const float l = sqrtf(toSun.x * toSun.x + toSun.y * toSun.y + toSun.z * toSun.z);
        if (l > 1e-6f && (sceneSun.Diffuse.r + sceneSun.Diffuse.g + sceneSun.Diffuse.b) > 1e-4f)
        {
            toSun *= 1.0f / l;
            sunDir = toSun;
            sunLDir = toSun;
            sunRoadColor = CVECTOR4(sceneSun.Diffuse.r * fSunRoadColorPower * 0.01f,
                                    sceneSun.Diffuse.g * fSunRoadColorPower * 0.01f,
                                    sceneSun.Diffuse.b * fSunRoadColorPower * 0.01f, 1.0f);
        }
    }
#endif
    CVECTOR4 sunLight = CVECTOR4(sunLDir.x, sunLDir.y, sunLDir.z, 0.0f);
    CVECTOR4 sunLightParams = CVECTOR4(fSunMultiply, fSunAmbient, 0.0f, 0.0f);
    CVECTOR4 sunRoad = CVECTOR4(sunDir.x, sunDir.y, sunDir.z, fSunRoadPower);
    // Bump UV scales + directional scroll. fBumpScaleU/V are MOP raw
    // (Ocean.cpp:961-962 pre-multiplies *0.004 at load).
    CVECTOR4 bumpSM =
        CVECTOR4(fBumpScaleU * 0.004f, fBumpScaleV * 0.004f, m_bumpU, m_bumpV);
    CVECTOR4 bumpPower = CVECTOR4(fBumpPower, 0.0f, 1.4f, 0.0f); // Blood exact
    // Blood Shifts = (posShift*3, posShift, 0, 0); skew lives in the VS only.
    CVECTOR4 shifts = CVECTOR4(fPosShift * 3.0f, fPosShift, 0.0f, 0.0f);
    // Foam (engine defaults, attribute-driven).
    CVECTOR4 foam1 = CVECTOR4(fFoamV, fFoamK, fFoamPower, 0.0f);
    CVECTOR4 foam2 = CVECTOR4(fFoamUV, fFoamTextureDisturb, m_foamU, m_foamV);
    CVECTOR4 foamColor = CVECTOR4(v4FoamColor.x * fFoamBright, v4FoamColor.y * fFoamBright,
                                  v4FoamColor.z * fFoamBright, 1.0f);
    CVECTOR4 foamRotate = CVECTOR4(fFoamUV * cosf(fFoamAngle), fFoamUV * sinf(fFoamAngle),
                                   -fFoamUV * sinf(fFoamAngle), fFoamUV * cosf(fFoamAngle)); // Blood exact
    CVECTOR4 transAngle = CVECTOR4(fTransAngle, 0.0f, 0.0f, 0.0f);
    CVECTOR4 transParams = CVECTOR4(fTransPower, fTransMultiply, 0.0f, 0.0f);
    CVECTOR4 transColor = CVECTOR4(v4TransColor.x, v4TransColor.y, v4TransColor.z, 1.0f);
    // PS fog uses exp falloff with this density (0 = off, as in Sea2).
    // fog/SeaDensity doubles as the Ocean3 fog multiplier (Blood 0.8).
    CVECTOR4 fogParams =
        CVECTOR4(bFogEnable ? fFogSeaDensity : 0.0f, 1.0f / fFogDistance, fFogStartDistance, 0.0f);
    CVECTOR4 fogColor = CVECTOR4(vFogColor.x, vFogColor.y, vFogColor.z, 1.0f);
    CVECTOR4 distFade = CVECTOR4(1.0f / fMaxSeaDistance, 0.0f, 0.0f, 0.0f); // Blood 1/Waves Distance

    setMat("ocWVP", mWorldViewProj);
    setVec("ocCamPos", camPos);
    setVec("ocReflTrans", reflTrans);
    setVec("ocWater", v4SeaColor);
    setVec("ocSky", v4SkyColor);
    setVec("ocRefr", refr);
    setVec("ocRefr2", refr2);
    setVec("ocSunLight", sunLight);
    setVec("ocSunLightParams", sunLightParams);
    setVec("ocSunRoad", sunRoad);
    setVec("ocSunRoadColor", sunRoadColor);
    setVec("ocBumpSM", bumpSM);
    setVec("ocBumpPower", bumpPower);
    setVec("ocShifts", shifts);
    setVec("ocFoam1", foam1);
    setVec("ocFoam2", foam2);
    setVec("ocFoamColor", foamColor);
    setVec("ocFoamRotate", foamRotate);
    setVec("ocTransAngle", transAngle);
    setVec("ocTransParams", transParams);
    setVec("ocTransColor", transColor);
    setVec("ocFogColor", fogColor);
    setVec("ocDistFade", distFade);
    CVECTOR4 fakeHeight = CVECTOR4(fSeaFake, fSeaHeight + fSeaFake, 0.0f, 0.0f); // Blood FakeHeight
    setVec("ocFakeHeight", fakeHeight);

    // Pixel-shader copies (separate register file).
    // Enables are live: scripts toggle foam/sunroad per weather.
    CVECTOR4 misc =
        CVECTOR4(bFoamEnable ? 1.0f : 0.0f, bTransEnable ? 1.0f : 0.0f, bSunRoadEnable ? 1.0f : 0.0f, 0.0f);
    CVECTOR4 sunBump = CVECTOR4(fSunBumpScale, 1.0f, fSunBumpScale, 1.0f); // Blood exact
    setVec("psSunRoadColor", sunRoadColor);
    setVec("psWater", v4SeaColor);
    setVec("psSky", v4SkyColor);
    setVec("psReflTrans", reflTrans);
    setVec("psRefr", refr);
    setVec("psRefr2", refr2);
    setVec("psFoamColor", foamColor);
    setVec("psTransColor", transColor);
    setVec("psFogParams", fogParams);
    setVec("psFogColor", fogColor);
    setVec("psBumpPower", bumpPower);
    setVec("psCamPos", camPos);
    setVec("psSunLight", sunLight);
    setVec("psSunLightParams", sunLightParams);
    setVec("psMisc", misc);
    setVec("psSunRoad", sunRoad);
    setVec("psFoam2", foam2);
    setVec("psFoamRotate", foamRotate);
    setVec("psTransParams", transParams);
    setVec("psSunBumpScale", sunBump);

    return true;
}

float SEA::WaveXZ(float x, float z, CVECTOR *pNormal)
{
    const float fDistance = Sqr(x - vCamPos.x) + Sqr(z - vCamPos.z);
    if (fDistance > fMaxSeaDistance * fMaxSeaDistance || !fftWavesFrame || !fftBumpFrame)
    {
        if (pNormal)
            *pNormal = CVECTOR(0.0f, 1.0f, 0.0f);
        return fSeaHeight + fSeaFake;
    }

    float h1, nX1, nZ1, h2, nX2, nZ2;
    SampleFFTLayer(fftWavesFrame, fftWaves.GetSize(), (x + vMove1.x) * fScale1, (z + vMove1.z) * fScale1, h1, nX1,
                   nZ1);
    SampleFFTLayer(fftBumpFrame, fftBump.GetSize(), (x + vMove2.x) * fScale2, (z + vMove2.z) * fScale2, h2, nX2,
                   nZ2);

    // Gameplay height matches the rendered surface (VS: h*fade+sea+fake;
    // at fade=1 this equals h+sea+fake).
    const float fRes = fAmp1 * h1 + fAmp2 * h2 + fSeaHeight + fSeaFake;

    if (pNormal)
    {
        // Blood packs raw frame slopes x1 (no scale multiply; scale lives in
        // UV only) and reconstructs y from the single layer (Ocean3.fx:63).
        // Layer 2 has no geometry term (amp2=0): ignore it entirely.
        float s1 = nX1 * nX1 + nZ1 * nZ1;
        if (s1 > 1.0f)
        {
            const float k = 1.0f / sqrtf(s1);
            nX1 *= k;
            nZ1 *= k;
            s1 = 1.0f;
        }

        CVECTOR vNormal;

        vNormal.x = fAmp1 * nX1;
        vNormal.z = fAmp1 * nZ1;
        vNormal.y = sqrtf(Max(0.0f, 1.0f - s1));

        vNormal = !vNormal;

        *pNormal = vNormal;
    }

    return fRes;
}

void SEA::PrepareIndicesForBlock(uint32_t dwBlockIndex)
{
    SeaBlock *pB = &aBlocks[dwBlockIndex];

    const float fStep = fGridStep * static_cast<float>(1 << pB->iLOD);
    float fSize = fGridStep * pB->iSize;
    int32_t x, y, size0 = pB->iSize >> pB->iLOD;

    pB->iIStart = iIStart;

    const float x1 = static_cast<float>(pB->iTX * pB->iSize) * fGridStep;
    const float y1 = static_cast<float>(pB->iTY * pB->iSize) * fGridStep;
    const float x2 = x1 + static_cast<float>(size0) * fStep;
    const float y2 = y1 + static_cast<float>(size0) * fStep;

    pB->iX1 = fftoi(x1 / fGridStep);
    pB->iX2 = fftoi(x2 / fGridStep);
    pB->iY1 = fftoi(y1 / fGridStep);
    pB->iY2 = fftoi(y2 / fGridStep);

    // analysis
    int32_t i, j;
    for (i = 0; i < static_cast<int32_t>(dwBlockIndex); i++) // if (i == 6)
    {
        SeaBlock *pB2 = &aBlocks[i];

        // Test Up & Down
        const bool bTestedUp = pB->iY1 == pB2->iY2;
        const bool bTestedDown = pB->iY2 == pB2->iY1;

        // if (!(core.Controls->GetAsyncKeyState('5')<0))
        if (bTestedUp || bTestedDown)
        {
            const int32_t iAddSrc = pB2->iIStart + ((bTestedUp) ? (pB2->iSize0 + 1) * pB2->iSize0 : 0);
            const int32_t iAddDst = pB->iIStart + ((bTestedUp) ? 0 : (pB->iSize0 + 1) * pB->iSize0);

            if ((pB->iX1 >= pB2->iX1 && pB->iX2 <= pB2->iX2) || (pB->iX1 <= pB2->iX1 && pB->iX2 >= pB2->iX2))
            {
                const int32_t iMinX = Max(pB->iX1, pB2->iX1);
                const int32_t iMaxX = Min(pB->iX2, pB2->iX2);

                const int32_t iStartDstX = pB->iSize0 * (iMinX - pB->iX1) / (pB->iX2 - pB->iX1);
                const int32_t iStartSrcX = pB2->iSize0 * (iMinX - pB2->iX1) / (pB2->iX2 - pB2->iX1);

                const int32_t iEndDstX = pB->iSize0 * (iMaxX - pB->iX1) / (pB->iX2 - pB->iX1);
                int32_t iEndSrcX = pB2->iSize0 * (iMaxX - pB2->iX1) / (pB2->iX2 - pB2->iX1);

                if (pB->iLOD == pB2->iLOD)
                    for (j = iStartDstX; j <= iEndDstX; j++)
                        pIndices[iAddDst + j] = pIndices[iAddSrc + iStartSrcX + (j - iStartDstX)];
                else
                    for (j = iStartDstX; j <= iEndDstX; j++)
                        pIndices[iAddDst + j] = pIndices[iAddSrc + iStartSrcX + (j - iStartDstX) / 2];

                continue;
            }
        }

        // Test Left & Right
        const bool bTestedLeft = pB->iX1 == pB2->iX2;
        const bool bTestedRight = pB->iX2 == pB2->iX1;
        // if ((core.Controls->GetAsyncKeyState('6')<0))
        if (bTestedLeft || bTestedRight)
        {
            const int32_t iAddSrc = pB2->iIStart + ((bTestedLeft) ? (pB2->iSize0) : 0);
            const int32_t iAddDst = pB->iIStart + ((bTestedLeft) ? 0 : (pB->iSize0));

            if ((pB->iY1 >= pB2->iY1 && pB->iY2 <= pB2->iY2) || (pB->iY1 <= pB2->iY1 && pB->iY2 >= pB2->iY2))
            {
                const int32_t iMinY = Max(pB->iY1, pB2->iY1);
                const int32_t iMaxY = Min(pB->iY2, pB2->iY2);

                const int32_t iStartDstY = pB->iSize0 * (iMinY - pB->iY1) / (pB->iY2 - pB->iY1);
                const int32_t iStartSrcY = pB2->iSize0 * (iMinY - pB2->iY1) / (pB2->iY2 - pB2->iY1);

                const int32_t iEndDstY = pB->iSize0 * (iMaxY - pB->iY1) / (pB->iY2 - pB->iY1);
                int32_t iEndSrcY = pB2->iSize0 * (iMaxY - pB2->iY1) / (pB2->iY2 - pB2->iY1);

                if (pB->iLOD == pB2->iLOD)
                    for (j = iStartDstY; j <= iEndDstY; j++)
                        pIndices[iAddDst + j * (pB->iSize0 + 1)] =
                            pIndices[iAddSrc + (iStartSrcY + j - iStartDstY) * (pB2->iSize0 + 1)];
                else
                    for (j = iStartDstY; j <= iEndDstY; j++)
                        pIndices[iAddDst + j * (pB->iSize0 + 1)] =
                            pIndices[iAddSrc + (iStartSrcY + (j - iStartDstY) / 2) * (pB2->iSize0 + 1)];
            }
        }
    }

    int32_t iIFirst = 1000000, iILast = -1000000;

    for (y = 0; y <= size0; y++)
        for (x = 0; x <= size0; x++)
        {
            if (pIndices[iIStart] != static_cast<uint32_t>(-1))
            {
                iIStart++;
                continue;
            }

            if (iVStart < iIFirst)
                iIFirst = iVStart;
            if (iVStart > iILast)
                iILast = iVStart;

            pIndices[iIStart++] = static_cast<uint32_t>(iVStart);
            iVStart++;
        }

    pB->iIFirst = iIFirst;
    pB->iILast = iILast;

    // setup triangles
    int32_t yy, dyy = size0 + 1;
    for (y = 0, yy = 0; y < size0; y++, yy += dyy)
        for (x = 0; x < size0; x++)
        {
            // first triangle
            *pTriangles++ = pIndices[pB->iIStart + static_cast<uint32_t>(x + yy + dyy + 1)];
            *pTriangles++ = pIndices[pB->iIStart + static_cast<uint32_t>(x + yy + 1)];
            *pTriangles++ = pIndices[pB->iIStart + static_cast<uint32_t>(x + yy)];
            // second triangle
            *pTriangles++ = pIndices[pB->iIStart + static_cast<uint32_t>(x + yy + dyy)];
            *pTriangles++ = pIndices[pB->iIStart + static_cast<uint32_t>(x + yy + dyy + 1)];
            *pTriangles++ = pIndices[pB->iIStart + static_cast<uint32_t>(x + yy)];

            iTStart += 2;
        }
}

void SEA::SSE_WaveXZBlock(SeaBlock &pB)
{
    SeaVertex *vTmp[4];
    SeaVertex vFake;

    vFake.vPos.x = vCamPos.x + 1e+5f;
    vFake.vPos.z = vCamPos.z + 1e+5f;

    float cx, cz, fStep = fGridStep * static_cast<float>(1 << pB.iLOD);
    float fSize = fGridStep * pB.iSize;
    int32_t x, y, size0 = pB.iSize >> pB.iLOD;

    float x1 = static_cast<float>(pB.iTX * pB.iSize) * fGridStep;
    float y1 = static_cast<float>(pB.iTY * pB.iSize) * fGridStep;
    float x2 = x1 + static_cast<float>(size0) * fStep;
    float y2 = y1 + static_cast<float>(size0) * fStep;

    pB.iX1 = fftoi(x1 / fGridStep);
    pB.iX2 = fftoi(x2 / fGridStep);
    pB.iY1 = fftoi(y1 / fGridStep);
    pB.iY2 = fftoi(y2 / fGridStep);

    x1 += vSeaCenterPos.x;
    x2 += vSeaCenterPos.x;
    y1 += vSeaCenterPos.z;
    y2 += vSeaCenterPos.z;

    // CVECTOR vNormal, vTmp;
    int32_t iCurrentV = 0;

    int32_t iIStart1 = pB.iIStart;
    const int32_t iIFirst = pB.iIFirst;
    const int32_t iILast = pB.iILast;

    // calculate
    for (cz = y1, y = 0; y <= size0; y++, cz += fStep)
    {
        for (cx = x1, x = 0; x <= size0; x++, cx += fStep)
        {
            const uint32_t dwVIndex = pIndices[iIStart1];

            if (static_cast<int32_t>(dwVIndex) < iIFirst || static_cast<int32_t>(dwVIndex) > iILast)
            {
                iIStart1++;
                continue;
            }

            vTmp[iCurrentV] = &pVSea[dwVIndex];
            vTmp[iCurrentV]->vPos.x = cx;
            vTmp[iCurrentV]->vPos.z = cz;

            iCurrentV++;

            if (iCurrentV == 4)
            {
                SSE_WaveXZ(&vTmp[0]);
                iCurrentV = 0;

                for (int32_t i = 0; i < 4; i++)
                {
                    vTmp[i]->tu = (vTmp[i]->vPos.x - vTmp[i]->vNormal.x * 5.3f) * fBumpScale;
                    vTmp[i]->tv = (vTmp[i]->vPos.z - vTmp[i]->vNormal.z * 5.3f) * fBumpScale;
                    // No CPU skew (Blood leans verts in the VS via Shifts only).
                    vTmp[i]->vPos.x -= vWorldOffset.x;
                    vTmp[i]->vPos.z -= vWorldOffset.z;
                }
            }

            /*vTmp.y = WaveXZ(cx, cz, &vNormal);

            vTmp.x = cx - vNormal.x * fPosShift * 3.0f;
            vTmp.z = cz - vNormal.z * fPosShift * 3.0f;

            pVSea[dwVIndex].vPos = vTmp;
            pVSea[dwVIndex].vNormal = vNormal;
            pVSea[dwVIndex].tu = (cx - vNormal.x * 5.3f) * fBumpScale;
            pVSea[dwVIndex].tv = (cz - vNormal.z * 5.3f) * fBumpScale;*/

            iIStart1++;
        }
    }

    if (iCurrentV != 0)
    {
        for (int32_t i = iCurrentV; i < 4; i++)
            vTmp[i] = &vFake;

        SSE_WaveXZ(&vTmp[0]);
        for (int32_t i = 0; i < iCurrentV; i++)
        {
            vTmp[i]->tu = (vTmp[i]->vPos.x - vTmp[i]->vNormal.x * 5.3f) * fBumpScale;
            vTmp[i]->tv = (vTmp[i]->vPos.z - vTmp[i]->vNormal.z * 5.3f) * fBumpScale;

            vTmp[i]->vPos.x -= vWorldOffset.x;
            vTmp[i]->vPos.z -= vWorldOffset.z;
        }
    }

    pB.bDone = true;
}

SEA::SeaBlock *SEA::GetUndoneBlock()
{
    SeaBlock *pB = nullptr;
    for (int32_t i = 0; i < aBlocks.size(); i++)
        if (!aBlocks[i].bInProgress)
        {
            pB = &aBlocks[i];
            pB->bInProgress = true;
            return pB;
        }
    return pB;
}

void SEA::CalculateNormalMap(float fFrame, float fAmplitude, float *pfOut, std::vector<uint32_t *> &aFrames)
{
    const int32_t iFrame1 = fftol(fFrame) % aFrames.size();
    const int32_t iFrame2 = (iFrame1 + 1) % aFrames.size();

    const float fDelta = fFrame - iFrame1;

    uint32_t *pB1 = aFrames[iFrame1];
    uint32_t *pB2 = aFrames[iFrame2];

    for (int32_t y = 0; y < YWIDTH; y++)
        for (int32_t x = 0; x < XWIDTH; x++)
        {
            const uint32_t dw1 = pB1[x + y * XWIDTH];
            const uint32_t dw2 = pB2[x + y * XWIDTH];
            const float nx1 = static_cast<float>(static_cast<short>(dw1)) / 32767.5f;
            const float nx2 = static_cast<float>(static_cast<short>(dw2)) / 32767.5f;
            const float nz1 = static_cast<float>(static_cast<short>(dw1 >> 0x10)) / 32767.5f;
            const float nz2 = static_cast<float>(static_cast<short>(dw2 >> 0x10)) / 32767.5f;

            pfOut[2 * (x + y * XWIDTH) + 0] = (nx1 + (nx2 - nx1) * fDelta);
            pfOut[2 * (x + y * XWIDTH) + 1] = (nz1 + (nz2 - nz1) * fDelta);
        }
}

void SEA::CalculateHeightMap(float fFrame, float fAmplitude, float *pfOut, std::vector<uint8_t *> &aFrames)
{
    const int32_t iFrame1 = fftol(fFrame) % aFrames.size();
    const int32_t iFrame2 = (iFrame1 + 1) % aFrames.size();

    const float fDelta = fFrame - iFrame1;

    uint8_t *pB1 = aFrames[iFrame1];
    uint8_t *pB2 = aFrames[iFrame2];

    for (int32_t y = 0; y < YWIDTH; y++)
        for (int32_t x = 0; x < XWIDTH; x++)
        {
            const float f1 = pB1[x + y * XWIDTH];
            const float f2 = pB2[x + y * XWIDTH];

            pfOut[x + y * XWIDTH] = fAmplitude * (f1 + (f2 - f1) * fDelta);
        }
}

void SEA::Realize(uint32_t dwDeltaTime)
{
    static float fTmp = 0.0f;

    if (core.Controls->GetDebugAsyncKeyState(VK_SHIFT) < 0 && core.Controls->GetDebugAsyncKeyState('S') < 0)
    {
        if (bTempFullMode)
        {
            bTempFullMode = false;
            fGridStep = fTempGridStep;
            fLodScale = fTempLodScale;
        }
        else
        {
            bTempFullMode = true;
            fTempGridStep = fGridStep;
            fTempLodScale = fLodScale;

            fGridStep = 0.07f;
            fLodScale = 0.5f;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    float fDeltaTime = static_cast<float>(dwDeltaTime) * 0.001f;

#if SEA_MOP_TEST
    ApplyMopTestOverrides(); // TEMP: MOP scene values, scripts bypassed
#endif

    // Blood Stop freezes animation but keeps drawing (m_oceanAnimate=0):
    // zero the clock, still render the frame.
    if (bStop)
        fDeltaTime = 0.0f;

    vMove1 += fDeltaTime * vMoveSpeed1;
    vMove2 += fDeltaTime * vMoveSpeed2;

    fFrame1 += fDeltaTime * fAnimSpeed1;
    fFrame2 += fDeltaTime * fAnimSpeed2;
    while (fFrame1 >= FRAMES)
        fFrame1 -= FRAMES;
    while (fFrame2 >= FRAMES)
        fFrame2 -= FRAMES;

    // pSeaParameters->SetTexture(0, pAnimTexture);

    uint64_t dwTotalRDTSC;
    RDTSC_B(dwTotalRDTSC);

    CMatrix mView, mIView;
    rs->GetTransform(D3DTS_VIEW, mView);
    mIView = mView;
    mIView.Transposition();

    vCamPos = mIView.Pos();
#ifdef OLD_WORLD_POS
    vWorldOffset = 0.0f;
#else
    // World-lock bump/foam UVs: vertices are stored camera-relative, so fold
    // camera motion into the scroll accumulators (Blood has no offset; its
    // UVs are world-based). Bump scroll is in meters (scaled in-shader):
    // wrap mod tile (1/scale). Foam fuv is post-scale: wrap mod 1
    // (both samplers Repeat, so wrapping is sampling-exact).
    {
        const float fOffDX = vCamPos.x - vWorldOffset.x;
        const float fOffDZ = vCamPos.z - vWorldOffset.z;
        if (fOffDX != 0.0f || fOffDZ != 0.0f)
        {
            m_bumpU += fOffDX;
            m_bumpV += fOffDZ;
            const float fCos = cosf(fFoamAngle) * fFoamUV;
            const float fSin = sinf(fFoamAngle) * fFoamUV;
            m_foamU += fOffDX * fCos + fOffDZ * fSin;
            m_foamV += fOffDX * -fSin + fOffDZ * fCos;
            const float fTileU = 1.0f / Max(1e-6f, fBumpScaleU * 0.004f);
            const float fTileV = 1.0f / Max(1e-6f, fBumpScaleV * 0.004f);
            m_bumpU -= floorf(m_bumpU / fTileU) * fTileU;
            m_bumpV -= floorf(m_bumpV / fTileV) * fTileV;
            m_foamU -= floorf(m_foamU);
            m_foamV -= floorf(m_foamV);
        }
    }
    vWorldOffset = vCamPos;
    vWorldOffset.y = 0.f;
#endif

    bUnderSea = false;

    if (vCamPos.y < fSeaHeight)
        bUnderSea = true;

    if (vCamPos.y < WaveXZ(vCamPos.x, vCamPos.z))
        bUnderSea = true;

    if (!bUnderSea)
        bUnderSeaStarted = false;

    if (bSimpleSea)
    {
        EnvMap_Render2();
        SunRoad_Render2();
    }
    else
    {
        EnvMap_Render();
        // SunRoad_Render() (pSunRoadMap cube) intentionally skipped: its
        // output is bound nowhere (only legacy Sea3 uses pReflectionSunroad).
        Refraction_Render();
        // Fresh planar reflection for the NewOcean3 path (s2).
        EnvMap_Render2();
    }

    pFrustumPlanes = rs->GetPlanes();

    float fBlockSize = 256.0f * fGridStep;
    int32_t iNumBlocks = static_cast<int32_t>(dwMaxDim) / (256 * 2);
    // Blood center rides Height+Fake (Ocean.cpp:1477).
    vSeaCenterPos = CVECTOR(fBlockSize * (static_cast<int32_t>(vCamPos.x / fBlockSize) - iNumBlocks),
                            fSeaHeight + fSeaFake,
                            fBlockSize * (static_cast<int32_t>(vCamPos.z / fBlockSize) - iNumBlocks));

    iVStart = 0;
    iTStart = 0;
    iIStart = 0;

    uint64_t dwX;
    RDTSC_B(dwX);
    // Live FFT waves: advance both spectra and publish new frames for
    // WaveXZ/SSE_WaveXZ/GenerateBumpMapFFT below (Ocean3 Executor equivalent,
    // single-threaded; SSE_WaveXZBlock still runs on par_unseq).
    // Bump (chop) time runs at true wave_speed2 (Blood engine default 2.0).
    fftWaves.Execute(fDeltaTime * fAnimSpeed1);
    fftBump.Execute(fDeltaTime * fAnimSpeed2);
    fftWaves.ChangeCurFrame();
    fftBump.ChangeCurFrame();
    fftWavesFrame = fftWaves.GetCurFrame();
    fftBumpFrame = fftBump.GetCurFrame();
    const bool useLiveBump = !bSimpleSea && fftBumpFrame != nullptr;
    RDTSC_E(dwX);

    if (useLiveBump)
    {
        // Live FFT normals straight into the bump render target; geometry
        // detail (fftBump layer) then matches bump detail by construction.
        GenerateBumpMapFFT();
    }
    else if (!pVolumeTexture && aBumpMaps.size())
    {
        IDirect3DSurface9 *pFace;

        static float fBumpMapFrame = 0.0f;
        fBumpMapFrame += static_cast<float>(fDeltaTime) * fBumpSpeed * 48.0f;

        uint32_t dw1 = static_cast<int32_t>(fBumpMapFrame) % aBumpMaps.size();
        uint32_t dw2 = static_cast<int32_t>(fBumpMapFrame + 1.0f) % aBumpMaps.size();

        float fAlpha = 255.0f * (fBumpMapFrame - static_cast<float>(static_cast<int32_t>(fBumpMapFrame)));
        rs->SetRenderState(D3DRS_TEXTUREFACTOR, ARGB(fAlpha, 0, 0, 0));

        rs->SetTexture(0, aBumpMaps[dw1]);
        rs->SetTexture(1, aBumpMaps[dw2]);

        rs->EndScene();
        rs->PushRenderTarget();
        for (uint32_t i = 0; i < MIPSLVLS; i++)
        {
            HRESULT hr = pRenderTargetBumpMap->GetSurfaceLevel(i, &pFace);
            rs->SetRenderTarget(pFace, nullptr);
            pFace->Release();
            float w, h;
            w = h = static_cast<float>(XWIDTH >> i);

            rs->BeginScene();

            RS_SPRITE spr[4];
            FillSpriteVertex(spr[0], 0, 0, 0.1f, 0xFFFFFFFF, 0.0f, 0.0f);
            FillSpriteVertex(spr[1], 0, h, 0.1f, 0xFFFFFFFF, 0.0f, 1.0f);
            FillSpriteVertex(spr[2], w, h, 0.1f, 0xFFFFFFFF, 1.0f, 1.0f);
            FillSpriteVertex(spr[3], w, 0, 0.1f, 0xFFFFFFFF, 1.0f, 0.0f);

            rs->DrawSprites(spr, 1, "bump_interpolate");

            rs->EndScene();
        }
        rs->PopRenderTarget();
        rs->BeginScene();
    }

    memset(pIndices, 0xFF, NUM_VERTEXS * sizeof(pIndices[0]) * 3);

    aBlocks.clear();
    BuildTree(0, 0, 0);
    // aBlocks.QSort(SeaBlock::QSort);
    std::sort(aBlocks.begin(), aBlocks.end(), SeaBlock::QSort);

    // Split fully-distant blocks into flat far-field quads (Ocean3
    // WorkThreadPreFFT): saves (size0+1)^2 verts per block, 4 verts each.
    // Trim-dropped coarse blocks join them too (else the horizon holes).
    flatBlocks.clear();
    auto blockRect = [this](const SeaBlock &blk, float &gx1, float &gz1, float &gx2, float &gz2) {
        const float fStep = fGridStep * static_cast<float>(1 << blk.iLOD);
        const int32_t size0 = blk.iSize >> blk.iLOD;
        gx1 = static_cast<float>(blk.iTX * blk.iSize) * fGridStep;
        gz1 = static_cast<float>(blk.iTY * blk.iSize) * fGridStep;
        gx2 = gx1 + static_cast<float>(size0) * fStep;
        gz2 = gz1 + static_cast<float>(size0) * fStep;
    };
    {
        const CVECTOR cam2D = CVECTOR(vCamPos.x, 0.0f, vCamPos.z);
        for (int32_t bi = 0; bi < static_cast<int32_t>(aBlocks.size()); bi++)
        {
            const SeaBlock &blk = aBlocks[bi];
            const float fStep = fGridStep * static_cast<float>(1 << blk.iLOD);
            const int32_t size0 = blk.iSize >> blk.iLOD;
            const float gx1 = static_cast<float>(blk.iTX * blk.iSize) * fGridStep;
            const float gz1 = static_cast<float>(blk.iTY * blk.iSize) * fGridStep;
            const float gx2 = gx1 + static_cast<float>(size0) * fStep;
            const float gz2 = gz1 + static_cast<float>(size0) * fStep;
            const float x1 = gx1 + vSeaCenterPos.x, z1 = gz1 + vSeaCenterPos.z;
            const float x2 = gx2 + vSeaCenterPos.x, z2 = gz2 + vSeaCenterPos.z;
            const float r2 = fMaxSeaDistance * fMaxSeaDistance;
            if ((Sqr(x1 - cam2D.x) + Sqr(z1 - cam2D.z)) > r2 && (Sqr(x2 - cam2D.x) + Sqr(z1 - cam2D.z)) > r2 &&
                (Sqr(x1 - cam2D.x) + Sqr(z2 - cam2D.z)) > r2 && (Sqr(x2 - cam2D.x) + Sqr(z2 - cam2D.z)) > r2)
            {
                flatBlocks.emplace_back(gx1, gz1, gx2, gz2);
                aBlocks.erase(aBlocks.begin() + bi);
                bi--;
            }
        }
    }

    // 32-bit index budget (131k verts) covers wide views outright; no trim
    // needed (the old delete/coarsen trims caused holes, pancakes and the
    // saucer). SeaGuard below stays as a last-resort fuse.

    auto pVSea2 = static_cast<SeaVertex *>(rs->LockVertexBuffer(iVSeaBuffer, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK));
    pTriangles = static_cast<uint32_t *>(rs->LockIndexBuffer(iISeaBuffer, D3DLOCK_DISCARD | D3DLOCK_NOSYSLOCK));

    uint32_t i = 0;
    for (i = 0; i < aBlocks.size(); i++) {
        PrepareIndicesForBlock(i);
    }

    std::for_each(std::execution::par_unseq, std::begin(aBlocks), std::end(aBlocks),
                  [this](auto &i) { SSE_WaveXZBlock(i); });

    // Emit far-field flat quads (y=0, up normals), same VB/IB.
    for (const CVECTOR4 &fr : flatBlocks)
    {
        if (iVStart + 4 >= NUM_VERTEXS - 8 || iTStart + 2 >= NUM_INDICES - 4)
            break;
        const float x1 = fr.x + vSeaCenterPos.x, z1 = fr.y + vSeaCenterPos.z;
        const float x2 = fr.z + vSeaCenterPos.x, z2 = fr.w + vSeaCenterPos.z;
        const int32_t v0 = iVStart;
        SeaVertex *v = &pVSea[v0];
        v[0].vPos = CVECTOR(x1 - vWorldOffset.x, 0.0f, z1 - vWorldOffset.z);
        v[1].vPos = CVECTOR(x2 - vWorldOffset.x, 0.0f, z1 - vWorldOffset.z);
        v[2].vPos = CVECTOR(x2 - vWorldOffset.x, 0.0f, z2 - vWorldOffset.z);
        v[3].vPos = CVECTOR(x1 - vWorldOffset.x, 0.0f, z2 - vWorldOffset.z);
        for (int k = 0; k < 4; k++)
        {
            v[k].vNormal = CVECTOR(0.0f, 1.0f, 0.0f);
            v[k].tu = (v[k].vPos.x + vWorldOffset.x) * fBumpScale;
            v[k].tv = (v[k].vPos.z + vWorldOffset.z) * fBumpScale;
        }
        *pTriangles++ = static_cast<uint32_t>(v0 + 0);
        *pTriangles++ = static_cast<uint32_t>(v0 + 1);
        *pTriangles++ = static_cast<uint32_t>(v0 + 2);
        *pTriangles++ = static_cast<uint32_t>(v0 + 0);
        *pTriangles++ = static_cast<uint32_t>(v0 + 2);
        *pTriangles++ = static_cast<uint32_t>(v0 + 3);
        iVStart += 4;
        iTStart += 2;
    }

    if (iVStart && iTStart)
        memcpy(pVSea2, pVSea, iVStart * sizeof(SeaVertex));

    // Hard guards: never overflow VB/IB (heap smash surfaced as 1e11 frames).
    if (iVStart >= NUM_VERTEXS - 8 || iTStart >= NUM_INDICES - 8)
    {
        static int guardTick = 0;
        if (++guardTick >= 60)
        {
            guardTick = 0;
            core.Trace("SeaGuard: OVERFLOW DROPPED FRAME verts=%d tris=%d blocks=%d", (int)iVStart, (int)iTStart,
                       (int)aBlocks.size());
        }
        rs->UnLockVertexBuffer(iVSeaBuffer);
        rs->UnLockIndexBuffer(iISeaBuffer);
        return;
    }

    rs->UnLockVertexBuffer(iVSeaBuffer);
    rs->UnLockIndexBuffer(iISeaBuffer);

    if (iVStart && iTStart)
    {
        CMatrix mWorldView, mWorldViewProj;

        CMatrix mView = rs->GetView();
        CMatrix mWorld;
        mWorld.SetIdentity(); // = rs->GetWorld();
        CMatrix mProjection = rs->GetProjection();

#ifndef OLD_WORLD_POS
        mView.MoveInversePosition(-vWorldOffset.x, 0.f, -vWorldOffset.z);
#endif

        mWorldView.EqMultiply(mWorld, mView);
        mWorldViewProj.EqMultiply(mWorldView, mProjection);

        mWorldViewProj.Transposition4x4();

        fTmp += fDeltaTime * fBumpSpeed;
        while (fTmp >= 1.0f)
            fTmp -= 1.0f;
        m_bumpClock = fTmp;
        // Blood bump scroll (Ocean.cpp:1333): U += sin(ang)*speed*0.005*dt,
        // V += cos(ang)*speed*0.005*dt. Blood foam scroll (Ocean.cpp:1921):
        // U += speed*cos(foamAng+moveAng)*dt, V += speed*sin(...)*dt.
        m_bumpU += sinf(fBumpAngle) * fBumpMoveSpeed * 0.005f * fDeltaTime;
        m_bumpV += cosf(fBumpAngle) * fBumpMoveSpeed * 0.005f * fDeltaTime;
        m_foamU += fDeltaTime * fFoamMoveSpeed * cosf(fFoamAngle + fFoamMoveAngle);
        m_foamV += fDeltaTime * fFoamMoveSpeed * sinf(fFoamAngle + fFoamMoveAngle);
        // Wrap every frame (not just on camera move): exact under Repeat,
        // stops float drift over long sessions.
        {
            const float fTileU = 1.0f / Max(1e-6f, fBumpScaleU * 0.004f);
            const float fTileV = 1.0f / Max(1e-6f, fBumpScaleV * 0.004f);
            m_bumpU -= floorf(m_bumpU / fTileU) * fTileU;
            m_bumpV -= floorf(m_bumpV / fTileV) * fTileV;
            m_foamU -= floorf(m_foamU);
            m_foamV -= floorf(m_foamV);
        }

        rs->SetVertexDeclaration(vertexDecl_);

        // Live FFT bump when active, else baked volume (or its RT fallback).
        IDirect3DBaseTexture9 *bumpTex = useLiveBump ? static_cast<IDirect3DBaseTexture9 *>(pLiveBumpMap)
                                          : (pVolumeTexture ? static_cast<IDirect3DBaseTexture9 *>(pVolumeTexture)
                                                            : static_cast<IDirect3DBaseTexture9 *>(pRenderTargetBumpMap));

        const auto vec1 = CVECTOR4(0.0f, 1.0f, 0.5f, -0.04f);
        const auto vec2 = CVECTOR4(2.0f, -1.0f, 0.00036621652552071f, (bFogEnable) ? fFogSeaDensity : 0.0f);
        const auto vec3 = CVECTOR4(fFoamV, fFoamK, fFoamUV, 6.0f);
        const auto vec4 = CVECTOR4(fTmp, fTmp, fTmp, fTmp);
        const auto vec5 = CVECTOR4(vCamPos.x - vWorldOffset.x, vCamPos.y, vCamPos.z - vWorldOffset.z, 1.0f);
        const auto vec6 = CVECTOR4(1.0f, 0.0f, 0.0f, 1.0f);
        const auto vec7 = CVECTOR4(fFrenel, 1.0f, 0.5f, 1.0f);
        const auto vec8 = CVECTOR4(1.0f, 0.0f, 0.0f, 1.0f);
        CVECTOR vTmp = !CVECTOR(0.0f, 1.0f, 0.0f);
        const auto vec9 = CVECTOR4(vTmp.x, vTmp.y, vTmp.z, 1.0f);
        rs->SetVertexShaderConstantF(GC_CONSTANT, (const float *)&vec1, 1);
        rs->SetVertexShaderConstantF(GC_CONSTANT2, (const float *)&vec2, 1);
        rs->SetVertexShaderConstantF(GC_SHADOW_CONST1, (const float *)&vec3, 1);
        rs->SetVertexShaderConstantF(GC_ANIMATION, (const float *)&vec4, 1);
        rs->SetVertexShaderConstantF(GC_CAMERA_POS, (const float *)&vec5, 1);
        rs->SetVertexShaderConstantF(GC_MTX_WVP, (const float *)&mWorldViewProj, 4);

        rs->SetVertexShaderConstantF(GC_FREE, (const float *)&v4SeaParameters, 1);
        rs->SetVertexShaderConstantF(GC_FREE + 1, (const float *)&v4SeaColor, 1);
        rs->SetVertexShaderConstantF(GC_FREE + 2, (const float *)&v4SkyColor, 1);

        rs->SetVertexShaderConstantF(GC_FREE + 5, (const float *)&vec6, 1);
        rs->SetVertexShaderConstantF(GC_FREE + 6, (const float *)&vec7, 1);
        // Frenel K, Frenel Max
        rs->SetVertexShaderConstantF(GC_FREE + 7, (const float *)&vec8, 1);
        rs->SetVertexShaderConstantF(GC_FREE + 30, (const float *)&vec9, 1);

        if (bSimpleSea)
        {
            rs->SetVertexShaderConstantF(GC_FREE + 8, (const float *)&mTexProjection, 4); // Matrix!!

            // rs->SetTexture(0, pVolumeTexture);
            rs->SetTexture(0, (pVolumeTexture) ? static_cast<IDirect3DBaseTexture9 *>(pVolumeTexture)
                                               : static_cast<IDirect3DBaseTexture9 *>(pRenderTargetBumpMap));
            rs->SetTexture(1, pReflection);
            // rs->SetTexture(2, pVolumeTexture);
            rs->SetTexture(2, (pVolumeTexture) ? static_cast<IDirect3DBaseTexture9 *>(pVolumeTexture)
                                               : static_cast<IDirect3DBaseTexture9 *>(pRenderTargetBumpMap));
            rs->SetTexture(3, pReflectionSunroad);

            rs->SetTextureStageState(1, D3DTSS_BUMPENVMAT00, F2DW(0.08f));
            rs->SetTextureStageState(1, D3DTSS_BUMPENVMAT10, F2DW(0.0f));
            rs->SetTextureStageState(1, D3DTSS_BUMPENVMAT01, F2DW(0.0f));
            rs->SetTextureStageState(1, D3DTSS_BUMPENVMAT11, F2DW(0.08f));

            rs->SetTextureStageState(3, D3DTSS_BUMPENVMAT00, F2DW(0.05f));
            rs->SetTextureStageState(3, D3DTSS_BUMPENVMAT10, F2DW(0.0f));
            rs->SetTextureStageState(3, D3DTSS_BUMPENVMAT01, F2DW(0.0f));
            rs->SetTextureStageState(3, D3DTSS_BUMPENVMAT11, F2DW(0.05f));

            rs->DrawIndexedPrimitiveNoVShader(D3DPT_TRIANGLELIST, iVSeaBuffer, sizeof(SeaVertex), iISeaBuffer, 0,
                                              iVStart, 0, iTStart, "Sea3");
        }
        else
        {
            // NewOcean3 look (no fallback): foam/sunroad/translucence live
            // inside the PS. A missing technique shows up in the game Logs.
            SetupOcean3Effect(mWorldViewProj, vec5);
            rs->SetTexture(0, bumpTex);
            rs->SetTexture(1, static_cast<IDirect3DBaseTexture9 *>(pEnvMap));
            rs->SetTexture(2, static_cast<IDirect3DBaseTexture9 *>(pReflection));
            rs->SetTexture(3, static_cast<IDirect3DBaseTexture9 *>(pRefraction));
            rs->SetTexture(4, rs->GetTextureFromID(iFoamTexture));
            rs->DrawIndexedPrimitiveNoVShader(D3DPT_TRIANGLELIST, iVSeaBuffer, sizeof(SeaVertex), iISeaBuffer, 0,
                                               iVStart, 0, iTStart, "newseaocean3");
        }
    }

    RDTSC_E(dwTotalRDTSC);
    // rs->Print(50, 300, "Total ticks with rendering %d", /*iVStart, iTStart, */dwTotalRDTSC);
    /*rs->Print(50, 320, "calc blk%s: %d", (bHT) ? " (HT)" : "", dwBlockRDTSC);
    rs->Print(50, 340, "Blocks in 1st thread: %d", iB1);
    for (int32_t i=0; i<aThreadsTest; i++)
      rs->Print(50, 360 + 20 * i, "Blocks in thread %d: %d", i + 1, aThreadsTest[i]);
    */
    // rs->Print(30, 140, "rdtsc = %d", dwBlockRDTSC);
    // rs->Print(30, 160, "Intel CPU: %s, SSE: %s, HyperThreading: %s", (bIntel) ? "Yes" : "No", (bSSE) ? "On" : "Off",
    // (bHyperThreading) ? "On" : "Off");

    /*D3DVIEWPORT9 vp; rs->GetViewport(&vp);
    float w = 256;
    float h = 256;
    RS_SPRITE spr[4];
    FillSpriteVertex(spr[0], 0, 0, 0.1f, 0xFFFFFFFF, 0.0f, 0.0f);
    FillSpriteVertex(spr[1], 0, h, 0.1f, 0xFFFFFFFF, 0.0f, 1.0f);
    FillSpriteVertex(spr[2], w, h, 0.1f, 0xFFFFFFFF, 1.0f, 1.0f);
    FillSpriteVertex(spr[3], w, 0, 0.1f, 0xFFFFFFFF, 1.0f, 0.0f);
    //rs->SetTexture(0, pReflection);
    rs->SetTexture(0, pReflectionSunroad);
    rs->DrawSprites(spr, 1, "Telescope");*/

    if (bUnderSea && bUnderSeaEnable)
    {
        CVECTOR d(20.0f, 5.0f, 20.0f);
        CVECTOR d2(54.0f, 10.0f, 54.0f);
        if (!bUnderSeaStarted)
        {
            // doing a bunch of sea trash for the first time
            aSeaTrash.clear();
            for (int32_t i = 0; i < 1000; i++)
            {
                aSeaTrash.push_back(SeaTrash{});
                // SeaTrash & st = aSeaTrash[aSeaTrash.Add()];
                SeaTrash &st = aSeaTrash.back();
                st.vPos = vCamPos + CVECTOR(RRnd(-d.x, d.x), RRnd(-d.y, d.y), RRnd(-d.z, d.z));
                st.vSpeed = CVECTOR(0.1f, 0.0f, 0.0f);
                st.dwSubTexture = rand() % 4;
                st.fSize = RRnd(0.01f, 0.05f);
                st.fTime = static_cast<float>(i) * 0.02f;
            }

            // making a bunch of light poles for the first time
            aSeaLights.clear();
            for (int32_t i = 0; i < 50; i++)
            {
                aSeaLights.push_back(SeaLight{});
                // SeaLight & sl = aSeaLights[aSeaLights.Add()];
                SeaLight &sl = aSeaLights.back();
                sl.vPos = vCamPos + CVECTOR(RRnd(-d2.x, d2.x), RRnd(-d2.y, d2.y), RRnd(-d2.z, d2.z));
                sl.vSpeed = CVECTOR(0.1f, 0.0f, 0.0f);
                sl.dwSubTexture = rand() % 4;
                sl.fTime = static_cast<float>(i);
            }
        }

        // add 1 item of sea trash every second
        fLastTrashTime += fDeltaTime;
        while (fLastTrashTime >= 0.03f)
        {
            fLastTrashTime -= 0.03f;

            aSeaTrash.push_back(SeaTrash{});
            // SeaTrash & st = aSeaTrash[aSeaTrash.Add()];
            SeaTrash &st = aSeaTrash.back();
            st.vPos = vCamPos + CVECTOR(RRnd(-d.x, d.x), RRnd(-d.y, d.y), RRnd(-d.z, d.z));
            st.vSpeed = CVECTOR(0.1f, 0.0f, 0.0f);
            st.dwSubTexture = rand() % 4;
            st.fTime = 20.0f;
            st.fSize = RRnd(0.01f, 0.05f);
        }

        // Remove the old sea trash
        for (int32_t i = 0; i < aSeaTrash.size(); i++)
        {
            aSeaTrash[i].fTime -= fDeltaTime;
            if (aSeaTrash[i].fTime <= 0.0f)
            {
                // aSeaTrash.ExtractNoShift(i);
                aSeaTrash[i] = aSeaTrash.back();
                aSeaTrash.pop_back();
                i--;
                continue;
            }
            aSeaTrash[i].vPos += aSeaTrash[i].vSpeed * fDeltaTime;
        }
        // Render sea trash
        aTrashRects.clear();
        for (int32_t i = 0; i < aSeaTrash.size(); i++)
        {
            //    RS_RECT & r = aTrashRects[aTrashRects.Add()];
            aTrashRects.push_back(RS_RECT{});
            RS_RECT &r = aTrashRects.back();

            float fAlpha = 1.0f;
            if (aSeaTrash[i].fTime >= 45.0f)
                fAlpha = 1.0f - (aSeaTrash[i].fTime - 45.0f) / 5.0f;
            if (aSeaTrash[i].fTime <= 5.0f)
                fAlpha = 1.0f - (5.0f - aSeaTrash[i].fTime) / 5.0f;

            r.vPos = aSeaTrash[i].vPos;
            r.fSize = aSeaTrash[i].fSize;
            r.fAngle = 0.0f;
            r.dwColor = ARGB(fAlpha * 255.0f, 255, 255, 255);
            r.dwSubTexture = aSeaTrash[i].dwSubTexture;
        }

        if (aTrashRects.size())
        {
            rs->TextureSet(0, iSeaTrashTexture);
            rs->DrawRects(&aTrashRects[0], aTrashRects.size(), "seatrash", 2, 2);
        }

        // Render schools of fish

        // draw light poles
        // add 1 light pole every second
        fLastLightTime += fDeltaTime;
        while (fLastLightTime >= 1.0f)
        {
            fLastLightTime -= 1.0f;

            aSeaLights.push_back(SeaLight{});
            // SeaLight & sl = aSeaLights[aSeaLights.Add()];
            SeaLight &sl = aSeaLights.back();
            sl.vPos = vCamPos + CVECTOR(RRnd(-d2.x, d2.x), RRnd(-d2.y, d2.y), RRnd(-d2.z, d2.z));
            sl.vSpeed = CVECTOR(0.1f, 0.0f, 0.0f);
            sl.dwSubTexture = rand() % 4;
            sl.fTime = 50.0f;
        }

        // Remove the old light poles
        for (int32_t i = 0; i < aSeaLights.size(); i++)
        {
            aSeaLights[i].fTime -= fDeltaTime;
            if (aSeaLights[i].fTime <= 0.0f)
            {
                aSeaLights[i] = aSeaLights.back();
                aSeaLights.pop_back();
                i--;
                continue;
            }
            aSeaLights[i].vPos += aSeaLights[i].vSpeed * fDeltaTime;
        }

        float fAlphaK =
            (vCamPos.y < fSeaHeight) ? Max(0.0f, 1.0f + (vCamPos.y - fSeaHeight) / 30.0f) : 1.0f;

        // draw light poles
        aLightsRects.clear();
        for (int32_t i = 0; i < aSeaLights.size(); i++)
        {
            aLightsRects.push_back(RS_RECT{});
            // RS_RECT & r = aLightsRects[aLightsRects.Add()];
            RS_RECT &r = aLightsRects.back();
            float fAlpha = 1.0f;
            if (aSeaLights[i].fTime >= 45.0f)
                fAlpha = 1.0f - (aSeaLights[i].fTime - 45.0f) / 5.0f;
            if (aSeaLights[i].fTime <= 5.0f)
                fAlpha = 1.0f - (5.0f - aSeaLights[i].fTime) / 5.0f;

            CVECTOR v = aSeaLights[i].vPos;
            r.vPos = CVECTOR(v.x, WaveXZ(v.x, v.z) - 5.0f, v.z);
            r.fSize = 20.0f;
            r.fAngle = 0.0f;
            r.dwColor = ARGB(0.07f * fAlphaK * fAlpha * 255.0f, 227, 245, 153);
            r.dwSubTexture = aSeaLights[i].dwSubTexture;
        }

        if (aLightsRects.size())
        {
            rs->TextureSet(0, iSeaLightTexture);
            rs->DrawRects(&aLightsRects[0], aLightsRects.size(), "seatrash", 2, 2, 0.5f);
        }

        bUnderSeaStarted = true;
    }

    bStarted = true;
}

float SEA::Trace(const CVECTOR &vSrc, const CVECTOR &vDst)
{
    const int32_t iNumTests = 5;
    float fRes = 2.0f;
    const float fDV = 1.0f / static_cast<float>(iNumTests - 1);

    if (vSrc.y > fMaxSeaHeight && vDst.y > fMaxSeaHeight)
        return 2.0f;

    for (int32_t i = 0; i < iNumTests; i++)
    {
        const CVECTOR vTemp = vSrc + static_cast<float>(i) * fDV * (vDst - vSrc);
        const float fWaveY = WaveXZ(vTemp.x, vTemp.z, nullptr);

        if (fWaveY > vTemp.y)
            return static_cast<float>(i) * fDV;
    }

    return 2.0f;
}

float SEA::Cannon_Trace(int32_t iBallOwner, const CVECTOR &vSrc, const CVECTOR &vDst)
{
    const float fRes = Trace(vSrc, vDst);

    if (fRes <= 1.0f)
    {
        const CVECTOR vTemp = vSrc + fRes * (vDst - vSrc);
        const float fTmpY = WaveXZ(vTemp.x, vTemp.z, nullptr);
        core.Event(BALL_WATER_HIT, "lfff", iBallOwner, vTemp.x, fTmpY, vTemp.z);
    }

    return fRes;
}

uint32_t SEA::AttributeChanged(ATTRIBUTES *pAttribute)
{
    ATTRIBUTES *pParent = pAttribute->GetParent();
    ATTRIBUTES *pParent2 = (pParent) ? pParent->GetParent() : nullptr;

    if (*pParent == "isDone")
    {
        Realize(0);
    }

    if (*pParent == "Sea2")
    {
        if (*pAttribute == "WaterColor")
        {
            v4SeaColor = COLOR2VECTOR4(pAttribute->GetAttributeAsDword());
            return 0;
        }
        if (*pAttribute == "SkyColor")
        {
            v4SkyColor = COLOR2VECTOR4(pAttribute->GetAttributeAsDword());
            return 0;
        }

        if (*pAttribute == "Amp1")
        {
            _fAmp1 = pAttribute->GetAttributeAsFloat();
            // fAmp1 stays 1.0 (Blood temp-override, Ocean_pc.cpp:273-274):
            // the ocean mesh ignores amplitude in Blood. NOTE this is NOT
            // a stub: wiring script Amp (legacy storm values up to 8) into
            // the hotter FFT frame (+-1.9) gives +-15 m mountains that bury
            // ships (verified on-screen). Blood seas are always +-2 m.
            // Transcription: do NOT copy "Waves Amplitude" here at all.
            return 0;
        }
        if (*pAttribute == "AnimSpeed1")
        {
            fAnimSpeed1 = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Scale1")
        {
            // Blood Waves Scale multiplier (Ocean.cpp:1179: scale = mop*size/len).
            fSeaScale1 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "MoveSpeed1")
        {
            sscanf(pAttribute->GetThisAttr(), "%f, %f, %f", &vMoveSpeed1.x, &vMoveSpeed1.y, &vMoveSpeed1.z);
            return 0;
        }

        if (*pAttribute == "Amp2")
        {
            // Transcription: keep 0. Blood has NO bump geometry term
            // (WaveXZ samples the waves layer only, Ocean_pc.cpp:208-268).
            // fAmp2 stays 0.0 for the same mountains reason as Amp1 above.
            _fAmp2 = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "AnimSpeed2")
        {
            fAnimSpeed2 = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Scale2")
        {
            fSeaScale2 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "MoveSpeed2")
        {
            sscanf(pAttribute->GetThisAttr(), "%f, %f, %f", &vMoveSpeed2.x, &vMoveSpeed2.y, &vMoveSpeed2.z);
            return 0;
        }

        if (*pAttribute == "PosShift")
        {
            // Storm-final units (legacy scripts send ready numbers).
            // Blood Choppy transcription: raw/5*sqrt(256/fftSize).
            fPosShift = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "BumpScale")
        {
            fBumpScale = pAttribute->GetAttributeAsFloat();
            // Legacy single value is storm-final; convert to MOP raw
            // (scripts without the new U/V split keep working).
            fBumpScaleU = fBumpScaleV = fBumpScale / 0.004f;
            return 0;
        }
        // Ocean3 bump tiling (no legacy analog). MOP raw units
        // (Ocean.cpp:961-962 pre-multiplies *0.004 at use).
        if (*pAttribute == "BumpScaleU")
        {
            fBumpScaleU = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BumpScaleV")
        {
            fBumpScaleV = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BumpPower")
        {
            fBumpPower = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BumpAngle")
        {
            // MOP degrees (MOP_FLOAT, Ocean.cpp:958 does Deg2Rad at load).
            fBumpAngle = pAttribute->GetAttributeAsFloat() * 3.14159265f / 180.0f;
            return 0;
        }
        if (*pAttribute == "BumpMoveSpeed")
        {
            fBumpMoveSpeed = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "BumpSpeed")
        {
            fBumpSpeed = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "LodScale")
        {
            fLodScale = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "GridStep")
        {
            fGridStep = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "Reflection")
        {
            v4SeaParameters.y = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Transparency")
        {
            v4SeaParameters.z = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Attenuation")
        {
            v4SeaParameters.x = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "Frenel")
        {
            fFrenel = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "FoamV")
        {
            fFoamV = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamK")
        {
            fFoamK = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamUV")
        {
            fFoamUV = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamTexDisturb")
        {
            fFoamTextureDisturb = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamEnable")
        {
            bFoamEnable = pAttribute->GetAttributeAsDword() != 0;
            return 0;
        }
        // Ocean3 foam extras (no legacy analog).
        if (*pAttribute == "FoamPower")
        {
            fFoamPower = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamBright")
        {
            fFoamBright = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamAngle")
        {
            // MOP raw: radians (MOP_ANGLES, Ocean.cpp:912), no conversion.
            fFoamAngle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamColor")
        {
            v4FoamColor = COLOR2VECTOR4(pAttribute->GetAttributeAsDword());
            return 0;
        }
        if (*pAttribute == "FoamMoveSpeed")
        {
            fFoamMoveSpeed = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FoamMoveAngle")
        {
            // MOP raw: radians (MOP_ANGLES, Ocean.cpp:918), no conversion.
            fFoamMoveAngle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        // Ocean3 sun (no legacy analog).
        if (*pAttribute == "SunRoadAngle")
        {
            sscanf(pAttribute->GetThisAttr(), "%f, %f", &fSunRoadAngX, &fSunRoadAngY);
            return 0;
        }
        if (*pAttribute == "SunAngle")
        {
            sscanf(pAttribute->GetThisAttr(), "%f, %f", &fSunAngX, &fSunAngY);
            return 0;
        }
        if (*pAttribute == "SunRoadPower")
        {
            fSunRoadPower = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "SunRoadColor")
        {
            v4SunRoadColor = COLOR2VECTOR4(pAttribute->GetAttributeAsDword());
            return 0;
        }
        if (*pAttribute == "SunRoadColorPower")
        {
            // MOP raw "Color power" (Ocean.cpp:890 pre-multiplies *0.01
            // at use, sea.cpp sunRoadColor). Transcription contract: RAW.
            fSunRoadColorPower = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "SunBumpScale")
        {
            fSunBumpScale = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "SunMultiply")
        {
            fSunMultiply = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "SunAmbient")
        {
            fSunAmbient = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        // Ocean3 translucence (no legacy analog).
        if (*pAttribute == "TransEnable")
        {
            bTransEnable = pAttribute->GetAttributeAsDword() != 0;
            return 0;
        }
        // Ocean3 sunroad toggle (no legacy analog).
        if (*pAttribute == "SunRoadEnable")
        {
            bSunRoadEnable = pAttribute->GetAttributeAsDword() != 0;
            return 0;
        }
        if (*pAttribute == "TransAngle")
        {
            fTransAngle = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "TransPower")
        {
            fTransPower = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "TransMultiply")
        {
            fTransMultiply = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "TransColor")
        {
            v4TransColor = COLOR2VECTOR4(pAttribute->GetAttributeAsDword());
            return 0;
        }
        // Ocean3 reflection/refraction mix (no legacy analog).
        if (*pAttribute == "ReflMinimum")
        {
            fReflMinimum = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "ReflAdd")
        {
            fReflAdd = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "ReflPower")
        {
            fReflPower = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "RefrEnvPower")
        {
            fRefrEnvPower = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "ReflEnvBlend")
        {
            fReflEnvBlend = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "ReflMin")
        {
            fReflMin = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "ReflMax")
        {
            fReflMax = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "RefrPower")
        {
            fRefrPower = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "TransMin")
        {
            fTransMin = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "TransMax")
        {
            fTransMax = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        // Ocean3 FFT spectra (no legacy analog; re-Setup on change).
        // MOP raw units, Blood transforms at load (Ocean.cpp:869-876).
        if (*pAttribute == "FFTLen1")
        {
            fFFTLen1 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTAmp1")
        {
            fFFTAmp1 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTWind1")
        {
            fFFTWind1 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTDir1")
        {
            // MOP raw: wave propagation angle in radians (Ocean.cpp:872-874:
            // drx=cos(y), drz=sin(y)).
            const float fAng = pAttribute->GetAttributeAsFloat();
            vFFTDir1.x = cosf(fAng);
            vFFTDir1.z = sinf(fAng);
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTAlign1")
        {
            fFFTAlign1 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTLen2")
        {
            fFFTLen2 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTAmp2")
        {
            fFFTAmp2 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTWind2")
        {
            fFFTWind2 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTDir2")
        {
            // MOP raw: radians angle (Ocean.cpp:952-954: drx=cos, drz=sin).
            const float fAng = pAttribute->GetAttributeAsFloat();
            vFFTDir2.x = cosf(fAng);
            vFFTDir2.z = sinf(fAng);
            SetupFFTFromSea2Attrs();
            return 0;
        }
        if (*pAttribute == "FFTAlign2")
        {
            fFFTAlign2 = pAttribute->GetAttributeAsFloat();
            SetupFFTFromSea2Attrs();
            return 0;
        }
        // Ocean3 fog distance (no legacy analog; fog/SeaDensity = multiplier).
        if (*pAttribute == "FogDistance")
        {
            fFogDistance = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        // Blood Height / Fake Offset (Ocean.cpp:2203,2207,1931): sea datum.
        // Engine defaults 0/0; mesh, WaveXZ, mirror plane and foam follow.
        if (*pAttribute == "Height")
        {
            fSeaHeight = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "FakeOffset")
        {
            fSeaFake = pAttribute->GetAttributeAsFloat();
            return 0;
        }

        if (*pAttribute == "SimpleSea")
        {
            bSimpleSea = pAttribute->GetAttributeAsDword() != 0;
            BuildVolumeTexture();
            return 0;
        }
        return 0;
    }

    if (*pParent == "fog")
    {
        if (*pAttribute == "Enable")
        {
            bFogEnable = pAttribute->GetAttributeAsDword() != 0;
            return 0;
        }
        if (*pAttribute == "Start")
        {
            fFogStartDistance = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        if (*pAttribute == "Color")
        {
            vFogColor = (1.0f / 255.0f) * COLOR2VECTOR(pAttribute->GetAttributeAsDword());
            return 0;
        }
        if (*pAttribute == "SeaDensity")
        {
            fFogSeaDensity = pAttribute->GetAttributeAsFloat();
            return 0;
        }
        return 0;
    }

    if (*pAttribute == "Stop")
    {
        bStop = AttributesPointer->GetAttributeAsDword("Stop") != 0;
    }

    if (*pAttribute == "UnderWater")
    {
        bUnderSeaEnable = AttributesPointer->GetAttributeAsDword("UnderWater") != 0;
    }

    if (*pAttribute == "MaxSeaHeight")
    {
        // Culling/distance box only. Mesh amplitude stays pinned (see Amp1):
        // scaling fAmp here would reintroduce the mountains.
        fMaxSeaHeight = AttributesPointer->GetAttributeAsFloat("MaxSeaHeight", 50.0f);
    }

    if (*pAttribute == "MaxDim")        { dwMaxDim = pAttribute->GetAttributeAsDword(); return 0; }
    if (*pAttribute == "MinDim")        { dwMinDim = pAttribute->GetAttributeAsDword(); return 0; }

    if (*pAttribute == "MaxSeaDistance")
    {
        // Blood clamps Waves Distance to 2800 (Ocean.cpp:880).
        fMaxSeaDistance = Min(2800.0f, AttributesPointer->GetAttributeAsFloat("MaxSeaDistance"));
    }

    return 0;
}

void SEA::LostRender()
{
    rs->Release(pReflection);
    rs->Release(pReflectionSunroad);
    rs->Release(pRefraction);
    rs->Release(pEnvMap);
    rs->Release(pSunRoadMap);
    rs->Release(pRenderTargetBumpMap);
    rs->Release(pLiveBumpMap);

    rs->Release(pZStencil);
    rs->Release(pReflectionSurfaceDepth);
}

void SEA::RestoreRender()
{
    rs->CreateTexture(XWIDTH, YWIDTH, MIPSLVLS, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT,
                      &pRenderTargetBumpMap);
    // Plain DYNAMIC texture: lockable every frame, unlike the RT above.
    // 8 mips like Blood (128 -> 1x1) for a clean distance fade.
    rs->CreateTexture(XWIDTH, YWIDTH, 8, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pLiveBumpMap);

    rs->CreateCubeTexture(128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pEnvMap);
    rs->CreateCubeTexture(128, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &pSunRoadMap);
    rs->CreateDepthStencilSurface(128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &pZStencil);

    // Blood uses A8R8G8B8 for reflection targets (no sky-gradient banding).
    rs->CreateTexture(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &pReflection);
    rs->CreateTexture(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &pReflectionSunroad);
    rs->CreateDepthStencilSurface(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, D3DFMT_D24S8, D3DMULTISAMPLE_NONE,
                                  &pReflectionSurfaceDepth);
    // Refraction scene target with alpha (Task 1, shading plan).
    rs->CreateTexture(SEA_REFLECTION_SIZE, SEA_REFLECTION_SIZE, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8,
                      D3DPOOL_DEFAULT, &pRefraction);
}
