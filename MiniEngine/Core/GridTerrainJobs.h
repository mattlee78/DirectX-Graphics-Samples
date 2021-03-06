#pragma once

#include "GridTerrain.h"
#include "GpuJobQueue.h"
#include "RefCount.h"
#include "ColorBuffer.h"
#include "TiledResources.h"

struct TerrainGpuJob;
typedef std::unordered_map<UINT64, TerrainGpuJob*> TerrainGpuJobMap;

struct TerrainGpuJob : public RefCountBase
{
    TerrainGpuJobMap* pJobMap;
    GridBlockCoord ViewCoord;
    FLOAT RenderingSubrectScale;
    FLOAT MostRecentViewWidth;
    PagingQueueEntry OutputResources;

    TerrainGpuJob()
        : pJobMap(nullptr),
          MostRecentViewWidth(0)
    { }

    void MarkAsCurrent();

    bool IsPending() const;
    bool IsComplete();
    void FinalRelease();
    void UpdateSortKey();
};

struct TerrainGraphicsHeightmapParams
{
    GridBlockCoord ViewCoord;
    const GridTerrainConfig* pConfig;
    bool GenerateMaterialMap;
};

struct TerrainSurfacemapParams
{
    GridBlockCoord ViewCoord;
    const GridTerrainConfig* pConfig;
    TerrainGpuJob* pGraphicsHeightmapJob;
};

struct TerrainPhysicsHeightmapParams
{
    GridBlockCoord ViewCoord;
    const GridTerrainConfig* pConfig;
    TerrainGpuJob* pGraphicsHeightmapJob;
};

struct TerrainIrregularMaterialParams
{

};

__declspec(align(16)) struct TerrainSpriteVertex
{
    XMFLOAT4 PositionXYZRotation;
    XMUBYTEN4 UVRect;
    XMFLOAT2 XZScale;
};

__declspec(align(16)) struct TerrainSpriteCB
{
    XMFLOAT4 CenterPosInvScale;
    FLOAT HeightOffset;
};

class GridTerrainJobs
{
private:
    RootSignature m_RootSig;

    GraphicsPSO m_HeightmapParticlePSO;
    ComputePSO m_HeightmapExtractRoadPSO;
    GraphicsPSO m_HeightmapSmoothRoadPSO;

    TerrainGpuJobMap m_TextureHeightmaps;
    TerrainGpuJobMap m_TextureSurfacemaps;
    TerrainGpuJobMap m_PhysicsHeightmaps;
    CRITICAL_SECTION m_JobCritSec;

    ColorBuffer m_HeightmapRT;
    ColorBuffer m_MaterialmapRT;
    D3D12_VIEWPORT m_HeightmapViewport;
    D3D12_RECT m_HeightmapScissor;
    FLOAT m_HeightmapViewScaleFactor;

    ColorBuffer m_SurfaceDiffuseRT;
    ColorBuffer m_SurfaceNormalRT;

public:
    GridTerrainJobs();
    ~GridTerrainJobs();

    void Initialize(const GridTerrainConfig* pConfig);
    void Terminate();

    TerrainGpuJob* CreateTextureHeightmapJob(const TerrainGraphicsHeightmapParams& Params);
    TerrainGpuJob* CreateTextureSurfacemapJob(const TerrainSurfacemapParams& Params);
    TerrainGpuJob* CreatePhysicsHeightmapJob(const TerrainPhysicsHeightmapParams& Params);
    TerrainGpuJob* CreateIrregularMaterialJob(const TerrainIrregularMaterialParams& Params);

    DXGI_FORMAT GetHeightmapFormat() const;

    void DrawTerrainSprites(GraphicsContext* pContext, GridBlockCoord Coord, FLOAT ViewScaleFactor, const TerrainSpriteVertex* pVertices, UINT32 VertexCount);

private:
    friend struct TerrainGpuJob;
    TerrainGpuJob* FindJob(UINT64 Key, TerrainGpuJobMap& JobMap);
    void AddJob(TerrainGpuJob* pJob, TerrainGpuJobMap& JobMap);
    void DeleteJob(TerrainGpuJob* pJob);
};

extern GridTerrainJobs g_GridTerrainJobs;
