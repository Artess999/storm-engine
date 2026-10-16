// NewOcean3 sea shading ported from CaptainBlood Ocean3.fx.
// Runtime-loaded D3DX effect (resource/techniques/weather/). Uniforms are
// register-bound globals set from sea.cpp (SetupOcean3Uniforms); the effect
// assigns no uniform values itself so device-set registers persist.
// Stages: s0 bump, s1 env cube, s2 planar reflection, s3 refraction, s4 foam.

uniform float4 ocReflTrans : register(c40);    // refl_min, refl_max, trans_min, trans_max
uniform float4 ocWater : register(c41);
uniform float4 ocSky : register(c42);
uniform float4 ocRefr : register(c43);         // refr_power, env_power, multiply, color_multiply
uniform float4 ocRefr2 : register(c44);        // refl_minus, blendSkyObj, 0, 0
uniform float4 ocSunLight : register(c45);
uniform float4 ocSunLightParams : register(c46); // dot_mul, add
uniform float4 ocSunRoad : register(c47);      // xyz dir, w power
uniform float4 ocSunRoadColor : register(c48);
uniform float4 ocBumpSM : register(c49);        // scaleU, scaleV, moveU, moveV
uniform float4 ocBumpPower : register(c50);     // x bump power, z normal scale
uniform float4 ocShifts : register(c51);        // posShift, ...
uniform float4 ocFoam1 : register(c52);         // startY, 1/heightY, powerY
uniform float4 ocFoam2 : register(c53);         // uvScale, displacement, startUv
uniform float4 ocFoamColor : register(c54);
uniform float4 ocFoamRotate : register(c55);    // 2x2 matrix rows
uniform float4 ocTransAngle : register(c56);
uniform float4 ocTransParams : register(c57);   // pow, mul
uniform float4 ocTransColor : register(c59);
uniform float4 ocFogColor : register(c61);
uniform float4 ocDistFade : register(c62);
uniform float4 ocFakeHeight : register(c63); // (fake, sea+fake, 0): distant mesh sinks to sea level
uniform float4 ocCamPos : register(c23);
uniform float4x4 ocWVP : register(c24);

// Pixel-shader copies (VS/PS register files are separate).
uniform float4 psSunRoadColor : register(c0);
uniform float4 psWater : register(c1);
uniform float4 psSky : register(c2);
uniform float4 psReflTrans : register(c3);
uniform float4 psRefr : register(c4);
uniform float4 psRefr2 : register(c5);
uniform float4 psFoamColor : register(c6);
uniform float4 psTransColor : register(c7);
uniform float4 psFogParams : register(c8); // (mult, rate, start): Blood formula
uniform float4 psFogColor : register(c9);
uniform float4 psBumpPower : register(c10);
uniform float4 psCamPos : register(c11);
uniform float4 psSunLight : register(c12);
uniform float4 psSunLightParams : register(c13);
uniform float4 psMisc : register(c14); // x foam, y translucence, z sunroad enables; w legacy attenuation
uniform float4 psSunRoad : register(c15);
uniform float4 psFoam2 : register(c16);
uniform float4 psFoamRotate : register(c17);
uniform float4 psTransParams : register(c20);
uniform float4 psSunBumpScale : register(c21);

sampler BumpSmp : register(s0) = sampler_state
{
    MipFilter = LINEAR; MinFilter = ANISOTROPIC; MagFilter = LINEAR;
    MaxAnisotropy = 8;
    AddressU = Wrap; AddressV = Wrap;
};
sampler EnvSmp : register(s1) = sampler_state
{
    MipFilter = LINEAR; MinFilter = LINEAR; MagFilter = LINEAR;
    AddressU = Clamp; AddressV = Clamp;
};
sampler ReflSmp : register(s2) = sampler_state
{
    MipFilter = LINEAR; MinFilter = LINEAR; MagFilter = LINEAR;
    AddressU = Clamp; AddressV = Clamp;
};
sampler RefrSmp : register(s3) = sampler_state
{
    MipFilter = LINEAR; MinFilter = LINEAR; MagFilter = LINEAR;
    AddressU = Clamp; AddressV = Clamp;
};
sampler FoamSmp : register(s4) = sampler_state
{
    MipFilter = LINEAR; MinFilter = LINEAR; MagFilter = LINEAR;
    AddressU = Wrap; AddressV = Wrap;
};

struct VSIn
{
    float3 pos : POSITION;
    float3 normal : NORMAL;
    float2 uv : TEXCOORD0;
};

struct VSOut
{
    float4 pos : POSITION;
    float4 t0 : TEXCOORD0; // normal.xyz, fade
    float4 t1 : TEXCOORD1; // bump uv, dist
    float4 t2 : TEXCOORD2; // cam dir, dist-k
    float4 t3 : TEXCOORD3; // clip pos
    float4 t4 : TEXCOORD4; // translucence dir, coeff
    float4 t5 : TEXCOORD5; // world pos, foam
};

VSOut vs_ocean3(VSIn v)
{
    VSOut o;
    float4 pos = float4(v.pos, 1.0f);

    float3 cam2Pos = ocCamPos.xyz - pos.xyz;
    float camDist = length(cam2Pos);
    cam2Pos /= camDist;

    float fade = 1.0f - min(1.0f, pow(camDist * ocDistFade.x, 1.5f));
    float faden = 1.0f - min(1.0f, camDist * ocDistFade.x);

    float3 normal;
    normal.xz = v.normal.xz * faden;
    normal.y = sqrt(1.0f - saturate(v.normal.x * v.normal.x + v.normal.z * v.normal.z));

    // Bump UV from the UNSHIFTED position (Blood Ocean3.fx:68 computes UV
    // before the xz shift at :72).
    o.t1.xy = (pos.xz + ocBumpSM.zw) * ocBumpSM.xy;

    pos.xz -= normal.xz * ocShifts.x * fade;
    normal.xz *= ocBumpPower.z * fade;
    normal = normalize(normal);

    pos.y = pos.y * fade + ocFakeHeight.y;

    // NOTE packing: D3DX effects store float4x4 column-major, so SetMatrix
    // transposes the row-major input on upload; transpose() here restores
    // the Sea2-proven layout (verified on-screen: without it the mesh
    // collapses to a line).
    float4 clip = mul(pos, transpose(ocWVP));
    o.pos = clip;
    o.t3 = clip;
    o.t0 = float4(normal, fade);

    o.t1.z = camDist;
    o.t1.w = 1.0f;

    o.t2.xyz = cam2Pos;
    o.t2.w = max(0.1f, 1.0f - camDist * 0.001f);

    float3 transDir = normalize(float3(cam2Pos.x, 0.0f, cam2Pos.z));
    transDir = normalize(float3(transDir.x, ocTransAngle.x, transDir.z));
    o.t4.xyz = transDir;
    o.t4.w = 0.1f + 0.9f * pow(1.0f - cam2Pos.y * cam2Pos.y, 2.0f);

    o.t5.xyz = pos.xyz;
    o.t5.w = fade * pow(saturate((pos.y - ocFoam1.x) * ocFoam1.y), ocFoam1.z);

    return o;
}

float4 ps_ocean3(VSOut p) : COLOR
{
    // Detail bump from the live FFT texture (Blood packing: B=nx, A=nz).
    float4 tt = tex2D(BumpSmp, p.t1.xy);
    float3 bump;
    bump.xz = float2(tt.b, tt.a) * 2.0f - 1.0f;
    bump.y = sqrt(saturate(1.0f - dot(bump.xz, bump.xz)));

    float3 normal = bump * psBumpPower.x + p.t0.xyz;
    normal = normalize(normal);

    float3 cam2Pos = normalize(psCamPos.xyz - p.t5.xyz);

    // Fresnel (Blood pow6 expanded: fr^2 squared then times fr^2),
    // scaled by Sea2.Frenel strength when sent (sunLightParams.z, else 1).
    float doter = abs(dot(cam2Pos, normal));
    float fr = 1.0f - doter;
    float fr2 = fr * fr;
    float k = fr2 * fr2 * fr2 * psSunLightParams.z;

    // x = reflection, y = alpha.
    float2 ra = saturate(lerp(psReflTrans.xz, psReflTrans.yw, min(1.0f, k)));

    // Environment from the cube map (replaces Blood spheremap lookup).
    float3 vReflect = reflect(cam2Pos, normal);
    float3 reflTex = texCUBE(EnvSmp, vReflect);

    float2 ruv = p.t3.xy / p.t3.w * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

    // Near-field planar reflection add.
    float4 reflEnvTex = tex2D(ReflSmp, normal.xz * psRefr.yy + ruv.xy);
    float tmpdot = dot(reflEnvTex.rgb, float3(0.3f, 0.59f, 0.11f));
    float3 addReflColor =
        reflEnvTex.rgb * saturate((tmpdot - psRefr2.x) * psRefr.z) * reflEnvTex.a * p.t2.w;
    float blendSkyObj = saturate(min(psRefr2.y, reflEnvTex.a) * p.t2.w);
    reflTex = lerp(reflTex, psRefr.w * reflEnvTex.rgb, blendSkyObj);

    float4 color;

    // Sun diffuse (saturate: legacy clamps the unlit side) with legacy
    // view-depth darkening on the REFLECTION term (Attenuation in misc.w):
    // 2^(-Att/|cosV|), 1 when Att = 0.
    float atten = exp2(-psMisc.w / max(cam2Pos.y, 0.05f));
    k = saturate(dot(normal, psSunLight.xyz)) * psSunLightParams.x + psSunLightParams.y;
    color.rgb = k * lerp(psWater.rgb, reflTex * psSky.rgb * atten, ra.x);

    // Refraction tint.
    float4 refrTex = tex2D(RefrSmp, normal.xz * psRefr.xx + ruv.xy);
    float4 refrTex2 = tex2D(RefrSmp, ruv.xy);
    refrTex.rgb = lerp(refrTex2.rgb, refrTex.rgb, refrTex.a);
    color.rgb = lerp(color.rgb, refrTex.rgb, saturate(ra.y));

    // Foam on crests (Blood fuv has no extra UV scale: 1 tile per meter).
    if (psMisc.x > 0.5f)
    {
        float2 fuv = p.t5.xx * psFoamRotate.xy + p.t5.zz * psFoamRotate.zw;
        color.rgb += p.t5.w * psFoamColor.rgb
            * tex2D(FoamSmp, psFoam2.zw + fuv - normal.xz * psFoam2.y).rgb;
    }

    // Translucence toward the sun.
    if (psMisc.y > 0.5f)
    {
        float dok = dot(normal, p.t4.xyz);
        dok = saturate(psTransParams.y * pow(saturate(dok), psTransParams.x));
        color.rgb += psTransColor.rgb * dok * p.t4.w;
    }

    // Sun road glitter (before fog: Blood applies fog last).
    if (psMisc.z > 0.5f)
    {
        float3 rn = normalize(normal * psSunBumpScale.xyz);
        float3 vr = reflect(-cam2Pos, rn);
        float kk = dot(vr, psSunRoad.xyz);
        kk = pow(saturate(kk), psSunRoad.w);
        color.rgb += psSunRoadColor.rgb * kk;
    }

    color.rgb += addReflColor;

    // Fog last. Legacy curve (density as rate, not cap):
    // 1 - 2^(-(dist-start)*Density/FogDistance). FogDistance 1.0 (legacy
    // scripts don't send it) reduces to the exact legacy curve.
    float fogk =
        saturate(1.0f - exp2(-max(0.0f, p.t1.z - psFogParams.z) * psFogParams.x / max(psFogParams.y, 1e-6f)));

    return float4(lerp(color.rgb, psFogColor.rgb, fogk), 1.0f);
}

technique newseaocean3
{
    pass P0
    {
        CullMode = None; // Blood uses cw, but our grid winding faces down
        // under cw (verified: sea vanishes); reversing all stitched index
        // order is risk without visual gain.
        ZEnable = true;
        ZWriteEnable = true;
        AlphaBlendEnable = false;
        AlphaTestEnable = false;
        FogEnable = false;
        VertexShader = compile vs_3_0 vs_ocean3();
        PixelShader = compile ps_3_0 ps_ocean3();
    }
}
