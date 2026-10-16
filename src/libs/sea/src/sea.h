// ************************************************************************
// *  Author : Vano, 1861; Description : Sea/Ocean/Water/Lake/River/pool  *
// ************************************************************************
#pragma once

#include "sea_base.h"
#include "sea_defines.h"
#include "c_vector4.h"
#include "dx9render.h"
#include "fft.h"
#include "vma.hpp"


class SEA : public SEA_BASE
{
  private:
    uint32_t dwIslandCode = MakeHashValue("island");
    uint32_t dwShipCode = MakeHashValue("ship");
    uint32_t dwSailCode = MakeHashValue("sail");
    uint32_t dwSkyCode = MakeHashValue("sky");

    static IDirect3DVertexDeclaration9 *vertexDecl_;

    struct SeaVertex
    {
        CVECTOR vPos;
        CVECTOR vNormal;
        float tu, tv;
    };

    struct SeaBlock
    {
        int32_t iX1, iX2, iY1, iY2; // result rectangle(in units)
        int32_t iSize0;             //

        int32_t iTX, iTY;
        int32_t iSize;
        int32_t iLOD;
        int32_t iIStart;
        int32_t iIFirst, iILast;

        bool bInProgress, bDone;

        static bool QSort(const SeaBlock &b1, const SeaBlock &b2)
        {
            return (b1.iLOD > b2.iLOD);
        }

        inline bool operator<(const SeaBlock &B) const
        {
            return (B.iLOD < iLOD);
        };
    };

    struct SeaTrash
    {
        CVECTOR vPos;
        CVECTOR vSpeed;
        uint32_t dwSubTexture;
        float fTime;
        float fSize;
    };

    struct SeaLight
    {
        CVECTOR vPos;
        CVECTOR vSpeed;
        uint32_t dwSubTexture;
        float fTime;
    };

    VDX9RENDER *rs;
    static SEA *pSea;

    uint32_t dwMaxDim, dwMinDim;
    float fMaxSeaHeight;
    float fMaxSeaDistance;
    float fGridStep;
    float fLodScale;
    float fBumpScale, fBumpSpeed;
    CVECTOR vMoveSpeed1, vMoveSpeed2;

    CVECTOR vCamPos, vSeaCenterPos, vWorldOffset;

    bool bStarted;
    bool bUnderSea;
    bool bUnderSeaEnable;
    float bUnderSeaStarted;

    int32_t iVSeaBuffer, iISeaBuffer;

    IDirect3DVolumeTexture9 *pVolumeTexture;
    IDirect3DCubeTexture9 *pEnvMap, *pSunRoadMap;
    IDirect3DSurface9 *pZStencil;

    IDirect3DTexture9 *pReflection, *pReflectionSunroad;
    IDirect3DSurface9 *pReflectionSurfaceDepth;
    // Refraction scene target (Task 1, shading plan). Alpha cleared to 1.
    IDirect3DTexture9 *pRefraction;

    bool bSimpleSea;

    SeaVertex *pVSea;
    uint32_t *pTriangles;
    uint32_t *pIndices;
    int32_t iVStart, iTStart, iIStart;

    PLANE *pFrustumPlanes;
    uint32_t dwNumFrustumPlanes;

    int32_t iSeaTrashTexture;
    float fLastTrashTime;
    std::vector<SeaTrash> aSeaTrash;
    std::vector<RS_RECT> aTrashRects;

    int32_t iSeaLightTexture;
    std::vector<SeaLight> aSeaLights;
    float fLastLightTime;
    std::vector<RS_RECT> aLightsRects;

    std::vector<uint32_t *> aNormals;
    std::vector<uint8_t *> aBumps;
    std::vector<SeaBlock> aBlocks;
    // Far-field flat quads (grid coords x1, z1, x2, z2), Ocean3 m_flatBlocks.
    std::vector<CVECTOR4> flatBlocks;

    CVECTOR4 v4SeaColor, v4SkyColor, v4SeaParameters;

    std::vector<IDirect3DTexture9 *> aBumpMaps;
    IDirect3DTexture9 *pRenderTargetBumpMap;
    // CPU-written live FFT bump map (DYNAMIC; the RT above is not lockable).
    IDirect3DTexture9 *pLiveBumpMap;

    float *pSeaFrame1, *pSeaFrame2, *pSeaNormalsFrame1, *pSeaNormalsFrame2;

    // Live Ocean3-style FFT waves (Task 2 of sea-ocean3 migration).
    // Layer 1 (swell) = fftWaves, layer 2 (chop) = fftBump; the same
    // fftBump frame feeds the per-frame bump texture.
    fft fftWaves, fftBump;
    fft::dcomplex *fftWavesFrame, *fftBumpFrame;
    float fftWavesLen, fftBumpLen;
    std::vector<float> bumpScratchA, bumpScratchB;

    void SetupFFTFromSea2Attrs();
#if SEA_MOP_TEST
    void ApplyMopTestOverrides(); // TEMP MOP-scene test, do not commit
#endif
    float m_bumpU, m_bumpV;
    float m_foamU, m_foamV;
    void GenerateBumpMapFFT();
    float m_bumpClock;

    // NewOcean3 shading uniforms (shading plan Task 3). VS base register 40
    // (28-39 and 58 are taken by the legacy Sea2 slots); PS uses c0-c7.
    enum
    {
        OC3_REFLTRANS = 40,
        OC3_WATER = 41,
        OC3_SKY = 42,
        OC3_REFR = 43,
        OC3_REFR2 = 44,
        OC3_SUNLIGHT = 45,
        OC3_SUNLIGHT_PARAMS = 46,
        OC3_SUNROAD = 47,
        OC3_SUNROAD_COLOR = 48,
        OC3_BUMPSCALEMOVE = 49,
        OC3_BUMPPOWER = 50,
        OC3_SHIFTS = 51,
        OC3_FOAM1 = 52,
        OC3_FOAM2 = 53,
        OC3_FOAMCOLOR = 54,
        OC3_FOAMROTATE = 55,
        OC3_TRANSANGLE = 56,
        OC3_TRANSPARAMS = 57,
        OC3_TRANSCOLOR = 59,
        OC3_FOGPARAMS = 60,
        OC3_FOGCOLOR = 61,
        OC3_DISTFADE = 62
    };
    // Sets NewOcean3 uniforms through the D3DX effect (returns false when the
    // technique is missing -> caller falls back to legacy Sea2). Device-set
    // constants do NOT survive effect BeginPass (unassigned uniforms commit
    // as zeros), so the effect path is mandatory, not optional.
    bool SetupOcean3Effect(const CMatrix &mWorldViewProj, const CVECTOR4 &camPos);

    CVECTOR vMove1, vMove2;
    float fFrame1, fFrame2;
    float fScale1, fScale2; // effective sampling scales (128 spectrum texels / FFTLen)
    float fAmpCalib1 = 0.125f, fAmpCalib2 = 0.125f; // legacy->FFT gain (measured per Setup; fallback = 0.4745/3.8)
    bool bFFTDirty = false; // spectra params changed -> re-Setup once in Realize
    bool bSet_FFTLen1 = false, bSet_FFTLen2 = false; // NEW len wins: legacy Scale must not derive it
    bool bSet_FFTDir1 = false, bSet_FFTDir2 = false; // NEW dir wins: legacy MoveSpeed must not derive it
    float fSeaHeight, fSeaFake; // Sea2 Height / FakeOffset (NEW, engine defaults 0)
    float fAmp1, fAmp2;
    float _fAmp1, _fAmp2;
    void UpdateLegacyAmp();
    // NEW-attr wins: once a NEW attribute arrives, legacy fallback no longer
    // overwrites it (attribute order in scripts is arbitrary).
    bool bSet_BumpU = false, bSet_BumpV = false, bSet_BumpMoveSpeed = false;
    bool bSet_ReflMax = false, bSet_TransMin = false;
    bool bSet_ReflEnvBlend = false;
    bool bSet_Frenel = false; // Sea2.Frenel arrived (strength applies); else full curve
    float fAnimSpeed1, fAnimSpeed2; // legacy baked frames/sec (SimpleSea path)
    float fFftSpeed1, fFftSpeed2; // FFT ocean-sec/sec (Execute time base)
    float fPosShift;
    float fFrenel;

    // Ocean3 FFT spectra (NEW Sea2.* attributes, no legacy analog: FFTLen1,
    // FFTAmp1, FFTWind1, FFTDir1, FFTAlign1 + same *2; defaults = Blood engine defaults).
    float fFFTLen1, fFFTAmp1, fFFTWind1, fFFTAlign1;
    CVECTOR vFFTDir1;
    float fFFTLen2, fFFTAmp2, fFFTWind2, fFFTAlign2;
    CVECTOR vFFTDir2;
    // Ocean3 bump tiling/scroll (NEW: BumpScaleU, BumpScaleV, BumpPower,
    // BumpAngle, BumpMoveSpeed; legacy BumpScale sets U+V together).
    float fBumpScaleU, fBumpScaleV, fBumpPower, fBumpAngle, fBumpMoveSpeed;
    // Ocean3 foam (NEW, no legacy analog: FoamPower, FoamBright,
    // FoamAngle, FoamColor, FoamMoveSpeed, FoamMoveAngle. FoamV/FoamUV/
    // FoamTexDisturb/FoamEnable reuse legacy slots; 
    float fFoamPower, fFoamBright, fFoamAngle;
    float fFoamMoveSpeed, fFoamMoveAngle;
    CVECTOR4 v4FoamColor;
    // Ocean3 sun (NEW, no legacy analog: SunRoadAngle, SunAngle,
    // SunRoadPower, SunRoadColor, SunRoadColorPower, SunBumpScale,
    // SunMultiply, SunAmbient; defaults = Blood engine defaults).
    float fSunRoadAngX, fSunRoadAngY, fSunAngX, fSunAngY;
    float fSunRoadPower, fSunRoadColorPower, fSunBumpScale;
    float fSunMultiply, fSunAmbient;
    CVECTOR4 v4SunRoadColor;
    // Ocean3 translucence (NEW, no legacy analog: TransEnable, TransAngle,
    // TransPower, TransMultiply, TransColor; engine default = on).
    bool bTransEnable;
    // Ocean3 sunroad toggle (NEW, no legacy analog: SunRoadEnable).
    bool bSunRoadEnable;
    float fTransAngle, fTransPower, fTransMultiply;
    CVECTOR4 v4TransColor;
    // Ocean3 reflection/refraction mix (NEW: ReflMinimum,
    // ReflAdd, ReflPower, RefrEnvPower, ReflEnvBlend, ReflMin, ReflMax,
    // RefrPower, TransMin, TransMax; legacy Reflection/Transparency/
    // Attenuation/Frenel drive these as fallback unless the NEW name was set).
    float fReflMinimum, fReflAdd, fReflPower, fRefrEnvPower, fReflEnvBlend;
    float fReflMin, fReflMax, fRefrPower, fTransMin, fTransMax;
    float fLegacyAtten = 0.0f; // Sea2.Attenuation (view-depth reflection darkening, 0 = off)
    // Ocean3 fog distance (NEW: FogDistance; fog/Enable/Start/Color reuse
    // the legacy slots, fog/SeaDensity doubles as the fog multiplier).
    float fFogDistance;

    bool bStop;

    float fFogSeaDensity, fFogStartDistance;
    CVECTOR vFogColor;
    bool bFogEnable;

    bool bTempFullMode;
    float fTempGridStep, fTempLodScale;

    bool bIniFoamEnable, bFoamEnable;
    float fFoamV, fFoamK, fFoamUV, fFoamTextureDisturb;
    int32_t iFoamTexture;

    CMatrix mTexProjection;

    void SSE_WaveXZ(SeaVertex **pArray);
    float WaveXZ(float x, float z, CVECTOR *pNormal = nullptr) override;

    void AddBlock(int32_t iTX, int32_t iTY, int32_t iSize, int32_t iLOD);
    void BuildTree(int32_t iTX, int32_t iTY, int32_t iLev);
    void SetBlock(uint32_t dwBlockIndex);

    bool isVisibleBBox(const CVECTOR &vCenter, const CVECTOR &v1, const CVECTOR &v2);
    void CalculateLOD(const CVECTOR &v1, const CVECTOR &v2, int32_t &iMaxLOD, int32_t &iMinLOD);
    inline float CalcLod(const float &x, const float &y, const float &z);
    void SSE_WaveXZBlock(SeaBlock &pB);
    SeaBlock *GetUndoneBlock();
    void PrepareIndicesForBlock(uint32_t dwBlockIndex);

    int32_t VisCode(const CVECTOR &vP);

    void CalculateHeightMap(float fFrame, float fAmplitude, float *pfOut, std::vector<uint8_t *> &aFrames);
    void CalculateNormalMap(float fFrame, float fAmplitude, float *pfOut, std::vector<uint32_t *> &aFrames);

    bool SunRoad_Render2();
    bool EnvMap_Render2();
    bool SunRoad_Render();
    bool EnvMap_Render();
    bool Refraction_Render();
    void EnvMap_GetSideMatrix(D3DCUBEMAP_FACES Face, CMatrix &mView);

    void SFLB_CreateBuffers();
    void CreateVertexDeclaration();

  public:
    SEA();
    ~SEA() override;

    bool Init() override;
    void BuildVolumeTexture();
    void Realize(uint32_t dwDeltaTime);

    float Trace(const CVECTOR &vSrc, const CVECTOR &vDst) override;
    float Cannon_Trace(int32_t iBallOwner, const CVECTOR &src, const CVECTOR &dst) override;

    bool Clip(const PLANE *planes, int32_t nplanes, const CVECTOR &center, float radius, ADD_POLYGON_FUNC addpoly) override
    {
        return false;
    }

    const char *GetCollideMaterialName() override
    {
        return nullptr;
    };

    bool GetCollideTriangle(TRIANGLE &triangle) override
    {
        return false;
    }

    uint32_t AttributeChanged(ATTRIBUTES *pAttribute) override;

    void ProcessStage(Stage stage, uint32_t delta) override
    {
        switch (stage)
        {
            // case Stage::execute:
            //    Execute(delta); break;
        case Stage::realize:
            Realize(delta);
            break;
        case Stage::lost_render:
            LostRender();
            break;
        case Stage::restore_render:
            RestoreRender();
            break;
        }
    }

    void LostRender();
    void RestoreRender();
};
