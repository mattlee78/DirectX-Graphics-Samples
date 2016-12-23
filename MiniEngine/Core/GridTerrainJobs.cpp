#include "pch.h"
#include "GridTerrainJobs.h"

GridTerrainJobs g_GridTerrainJobs;

void TerrainGpuJob::MarkAsCurrent()
{
    OutputResources.MostRecentTimestamp = Graphics::GetFrameCount();
}

bool TerrainGpuJob::IsPending() const
{
    GraphicsJob* pJob = OutputResources.pGraphicsJob;
    if (pJob == nullptr)
    {
        return false;
    }
    if (!pJob->IsSubmitted() || pJob->IsComplete())
    {
        return false;
    }
    return true;
}

bool TerrainGpuJob::IsComplete()
{
    MarkAsCurrent();
    GraphicsJob* pJob = OutputResources.pGraphicsJob;
    if (pJob == nullptr)
    {
        return true;
    }
    if (pJob->IsComplete())
    {
        g_GpuJobQueue.CloseGraphicsJob(pJob);
        OutputResources.pGraphicsJob = nullptr;
    }
    UpdateSortKey();
    return false;
}

void TerrainGpuJob::FinalRelease()
{
    g_GpuJobQueue.RemovePagingEntry(&OutputResources);
    if (OutputResources.pGraphicsJob != nullptr)
    {
        g_GpuJobQueue.CloseGraphicsJob(OutputResources.pGraphicsJob);
        OutputResources.pGraphicsJob = nullptr;
    }
    g_TilePool.FreeTiledTextureTiles(&OutputResources.TiledTexture[0]);
    g_TilePool.FreeTiledTextureTiles(&OutputResources.TiledTexture[1]);
    OutputResources.TiledTexture[0].Destroy();
    OutputResources.TiledTexture[1].Destroy();
    g_GridTerrainJobs.DeleteJob(this);
}

void TerrainGpuJob::UpdateSortKey()
{
    FLOAT SortKey = 0;
    if (Graphics::GetFrameCount() - OutputResources.MostRecentTimestamp < 60)
    {
        SortKey = (FLOAT)(1U << ViewCoord.SizeShift);
        SortKey *= std::max(0.0f, MostRecentViewWidth);
    }
    OutputResources.PositiveWeight = SortKey;
}

GridTerrainJobs::GridTerrainJobs()
{
    InitializeCriticalSection(&m_JobCritSec);
}


GridTerrainJobs::~GridTerrainJobs()
{
    DeleteCriticalSection(&m_JobCritSec);
}

void GridTerrainJobs::Initialize(const GridTerrainConfig* pConfig)
{
    const UINT32 HeightmapWidthHeight = (1 << pConfig->HeightmapDimensionLog2);
    m_HeightmapRT.Create(L"Heightmap RT", HeightmapWidthHeight, HeightmapWidthHeight, 1, DXGI_FORMAT_R32_FLOAT);
    m_MaterialmapRT.Create(L"Material Map RT", HeightmapWidthHeight, HeightmapWidthHeight, 1, DXGI_FORMAT_R8G8_UNORM);
    const UINT32 SurfaceWidthHeight = (1 << pConfig->SurfacemapDimensionLog2);
    m_SurfaceDiffuseRT.Create(L"Surface Diffuse RT", SurfaceWidthHeight, SurfaceWidthHeight, 1, DXGI_FORMAT_R10G10B10A2_UNORM, 0, true);
    m_SurfaceNormalRT.Create(L"Surface Normal RT", SurfaceWidthHeight, SurfaceWidthHeight, 1, DXGI_FORMAT_R10G10B10A2_UNORM, 0, true);

    g_TilePool.Initialize(4, 0, 16);
}

void GridTerrainJobs::Terminate()
{
    m_HeightmapRT.Destroy();
    m_MaterialmapRT.Destroy();
    m_SurfaceDiffuseRT.Destroy();
    m_SurfaceNormalRT.Destroy();

    g_TilePool.Terminate();
}

TerrainGpuJob* GridTerrainJobs::FindJob(UINT64 Key, TerrainGpuJobMap& JobMap)
{
    TerrainGpuJob* pResult = nullptr;
    EnterCriticalSection(&m_JobCritSec);
    auto iter = JobMap.find(Key);
    if (iter != JobMap.end())
    {
        pResult = iter->second;
    }
    LeaveCriticalSection(&m_JobCritSec);
    return pResult;
}

void GridTerrainJobs::AddJob(TerrainGpuJob* pJob, TerrainGpuJobMap& JobMap)
{
    EnterCriticalSection(&m_JobCritSec);
    JobMap[pJob->ViewCoord.Value] = pJob;
    LeaveCriticalSection(&m_JobCritSec);
    if (pJob->OutputResources.pGraphicsJob != nullptr)
    {
        pJob->OutputResources.pGraphicsJob->HoldJobOpen = true;
    }
    pJob->OutputResources.MostRecentTimestamp = Graphics::GetFrameCount();
    pJob->UpdateSortKey();
    g_GpuJobQueue.AddPagingMapEntry(&pJob->OutputResources);
}

void GridTerrainJobs::DeleteJob(TerrainGpuJob* pJob)
{
    TerrainGpuJobMap* pMap = pJob->pJobMap;
    assert(pMap == &m_TextureHeightmaps || pMap == &m_PhysicsHeightmaps || pMap == &m_TextureSurfacemaps);
    EnterCriticalSection(&m_JobCritSec);
    auto iter = pMap->find(pJob->ViewCoord.Value);
    assert(iter != pMap->end() && iter->second == pJob);
    pMap->erase(iter);
    LeaveCriticalSection(&m_JobCritSec);
}

TerrainGpuJob* GridTerrainJobs::CreateTextureHeightmapJob(const TerrainGraphicsHeightmapParams& Params)
{
    TerrainGpuJob* pJob = FindJob(Params.ViewCoord.Value, m_TextureHeightmaps);
    if (pJob != nullptr)
    {
        pJob->AddRef();
        return pJob;
    }

    pJob = new TerrainGpuJob();
    pJob->ViewCoord = Params.ViewCoord;
    pJob->pJobMap = &m_TextureHeightmaps;
    Utility::Printf("Creating heightmap textures for (%I64d, %I64d) scale %u\n", Params.ViewCoord.X, Params.ViewCoord.Y, 1U << Params.ViewCoord.SizeShift);

    const UINT32 WidthHeight = (1 << Params.pConfig->HeightmapDimensionLog2);
    const UINT32 PaddedWidthHeight = WidthHeight + 8;
    pJob->OutputResources.TiledTexture[0].Create(L"Terrain Heightmap", PaddedWidthHeight, PaddedWidthHeight, 1, 1, DXGI_FORMAT_R32_FLOAT);

    if (Params.GenerateMaterialMap)
    {
        pJob->OutputResources.TiledTexture[1].Create(L"Terrain MaterialMap", PaddedWidthHeight, PaddedWidthHeight, 1, 1, DXGI_FORMAT_R8G8_UNORM);
    }

    pJob->OutputResources.pGraphicsJob = g_GpuJobQueue.CreateGraphicsJob();
    GraphicsContext* pContext = pJob->OutputResources.pGraphicsJob->pContext;

    pContext->TransitionResource(m_HeightmapRT, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    pContext->ClearColor(m_HeightmapRT);
    if (Params.GenerateMaterialMap)
    {
        pContext->TransitionResource(m_MaterialmapRT, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
        pContext->ClearColor(m_MaterialmapRT);
    }

    pContext->TransitionResource(m_HeightmapRT, D3D12_RESOURCE_STATE_GENERIC_READ);
    pContext->TransitionResource(pJob->OutputResources.TiledTexture[0], D3D12_RESOURCE_STATE_COPY_DEST);
    pContext->CopySubresource(pJob->OutputResources.TiledTexture[0], 0, m_HeightmapRT, 0);
    pContext->TransitionResource(pJob->OutputResources.TiledTexture[0], D3D12_RESOURCE_STATE_GENERIC_READ);
    if (Params.GenerateMaterialMap)
    {
        pContext->TransitionResource(m_MaterialmapRT, D3D12_RESOURCE_STATE_GENERIC_READ);
        pContext->TransitionResource(pJob->OutputResources.TiledTexture[1], D3D12_RESOURCE_STATE_COPY_DEST);
        pContext->CopySubresource(pJob->OutputResources.TiledTexture[1], 0, m_MaterialmapRT, 0);
        pContext->TransitionResource(pJob->OutputResources.TiledTexture[1], D3D12_RESOURCE_STATE_GENERIC_READ);
    }
    pContext->FlushResourceBarriers();

    AddJob(pJob, m_TextureHeightmaps);
    return pJob;
}

TerrainGpuJob* GridTerrainJobs::CreateTextureSurfacemapJob(const TerrainSurfacemapParams& Params)
{
    TerrainGpuJob* pJob = FindJob(Params.ViewCoord.Value, m_TextureSurfacemaps);
    if (pJob != nullptr)
    {
        pJob->AddRef();
        return pJob;
    }

    pJob = new TerrainGpuJob();
    pJob->ViewCoord = Params.ViewCoord;
    pJob->pJobMap = &m_TextureSurfacemaps;
    Utility::Printf("Creating surfacemap textures for (%I64d, %I64d) scale %u\n", Params.ViewCoord.X, Params.ViewCoord.Y, 1U << Params.ViewCoord.SizeShift);

    const UINT32 WidthHeight = (1 << Params.pConfig->SurfacemapDimensionLog2);
    pJob->OutputResources.TiledTexture[0].Create(L"Terrain Diffuse Map", WidthHeight, WidthHeight, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM);
    pJob->OutputResources.TiledTexture[1].Create(L"Terrain Normal Map", WidthHeight, WidthHeight, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM);

    pJob->OutputResources.pGraphicsJob = g_GpuJobQueue.CreateGraphicsJob();
    GraphicsContext* pContext = pJob->OutputResources.pGraphicsJob->pContext;

    pContext->TransitionResource(m_SurfaceDiffuseRT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    pContext->TransitionResource(m_SurfaceNormalRT, D3D12_RESOURCE_STATE_RENDER_TARGET);
    pContext->FlushResourceBarriers();

    const FLOAT SurfaceColorGreen[4] = { 0, 0.5f, 0, 1 };
    const FLOAT SurfaceColorBlue[4] = { 0, 0, 0.5f, 1 };
    const FLOAT* pDiffuseColor = nullptr;
    if (Params.ViewCoord.SizeShift <= Params.pConfig->SmallSurfacemapShift)
    {
        pDiffuseColor = SurfaceColorGreen;
    }
    else
    {
        pDiffuseColor = SurfaceColorBlue;
    }
    pContext->ClearColor(m_SurfaceDiffuseRT, pDiffuseColor);
    const FLOAT SurfaceNormalColor[4] = { 0.5f, 1, 0.5f, 0 };
    pContext->ClearColor(m_SurfaceNormalRT, SurfaceNormalColor);

    pContext->TransitionResource(m_SurfaceDiffuseRT, D3D12_RESOURCE_STATE_GENERIC_READ);
    pContext->TransitionResource(pJob->OutputResources.TiledTexture[0], D3D12_RESOURCE_STATE_COPY_DEST);
    pContext->CopySubresource(pJob->OutputResources.TiledTexture[0], 0, m_SurfaceDiffuseRT, 0);
    pContext->TransitionResource(pJob->OutputResources.TiledTexture[0], D3D12_RESOURCE_STATE_GENERIC_READ);

    pContext->TransitionResource(m_SurfaceNormalRT, D3D12_RESOURCE_STATE_GENERIC_READ);
    pContext->TransitionResource(pJob->OutputResources.TiledTexture[1], D3D12_RESOURCE_STATE_COPY_DEST);
    pContext->CopySubresource(pJob->OutputResources.TiledTexture[1], 0, m_SurfaceNormalRT, 0);
    pContext->TransitionResource(pJob->OutputResources.TiledTexture[1], D3D12_RESOURCE_STATE_GENERIC_READ);

    pContext->FlushResourceBarriers();

    AddJob(pJob, m_TextureSurfacemaps);
    return pJob;
}

TerrainGpuJob* GridTerrainJobs::CreatePhysicsHeightmapJob(const TerrainPhysicsHeightmapParams& Params)
{
    return nullptr;
}

