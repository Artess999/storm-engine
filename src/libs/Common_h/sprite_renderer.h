#include "bgfx_utils.h"
#include "brtshaderc.h"

#include "glm.hpp"

#include <vector>


#ifndef SPRITE_RENDERER_DEF
#define SPRITE_RENDERER_DEF


struct SPRITE_VERTEX
{
    float x;
    float y;

    // uint32_t color;
    float tu, tv;

    static void Init()
    {
        ms_layout.begin()
            .add(bgfx::Attrib::Position, 2, bgfx::AttribType::Float)
            //.add(bgfx::Attrib::Color0, 4, bgfx::AttribType::Uint8, true)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float, true)
            .end();
    };

    static bgfx::VertexLayout ms_layout;
};



static SPRITE_VERTEX s_spriteVertices[] = {
    {20, 20, 1, 0}, 
    {20, -20, 1, 1}, 
    {-20, -20, 0, 1}, 
    {-20, 20, 0, 0}
};

static constexpr uint16_t s_spriteIndices[6] = {0, 1, 2, 1, 3, 2};


struct Rect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

struct SpriteInfo
{
    std::vector<SPRITE_VERTEX> vertices;
    float depth;
    std::shared_ptr<TextureResource> texture;
};

static const uint32_t InitialQueueSize = 256;

class SpriteRenderer
{
    static const uint32_t MaxBatchSize = 2048;

    //bgfx::DynamicVertexBufferHandle m_fvbh;
    //bgfx::DynamicIndexBufferHandle m_fibh;

    bgfx::IndexBufferHandle m_ibh;


    //bgfx::VertexBufferHandle m_sfvbh;
    //bgfx::IndexBufferHandle m_sfibh;

    bgfx::UniformHandle s_texColor;
    //bgfx::UniformHandle u_color;
    bgfx::ProgramHandle m_prog;

    
    void GetVertices(std::shared_ptr<TextureResource> texture, Rect source, SPRITE_VERTEX *vertices);

    long m_width;
    long m_height;

    std::vector<SpriteInfo> m_spriteQueue;

    std::vector<SpriteInfo const *> m_sortedSprites;

    uint32_t m_spriteQueueSize;
    uint32_t m_spriteQueueCount;

    std::shared_ptr<TextureResource> m_backbufferTexture;

  public:
    std::shared_ptr<TextureResource> Texture;

    SpriteRenderer(long m_fbWidth, long m_fbHeight);
    //SpriteRenderer();

    ~SpriteRenderer();
    void SetBackbufferTexture(std::shared_ptr<TextureResource> texture);
    void ReleaseTexture(std::shared_ptr<TextureResource> texture);

    void SetViewProjection();

    void UpdateIndexBuffer(std::vector<uint16_t> indices);
    void UpdateVertexBuffer(std::vector<glm::vec3> &vertices, glm::vec2 &u, glm::vec2 &v, uint32_t &color, float depth = 0.0f);

    void Submit();
};

#endif