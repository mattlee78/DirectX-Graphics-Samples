#include "pch.h"
#include "GridTerrainJobs.h"
#include "CompiledShaders/TerrainSpriteVS.h"
#include "CompiledShaders/TerrainSpritePS.h"

static const DXGI_FORMAT g_HeightmapFormat = DXGI_FORMAT_R32_FLOAT;

GridTerrainJobs g_GridTerrainJobs;

DXGI_FORMAT GridTerrainJobs::GetHeightmapFormat() const
{
    return g_HeightmapFormat;
}

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
    const UINT32 PaddedWidthHeight = HeightmapWidthHeight + 8;
    m_HeightmapRT.Create(L"Heightmap RT", PaddedWidthHeight, PaddedWidthHeight, 1, g_HeightmapFormat);
    m_MaterialmapRT.Create(L"Material Map RT", PaddedWidthHeight, PaddedWidthHeight, 1, DXGI_FORMAT_R8G8_UNORM);
    m_HeightmapViewport.TopLeftX = 0;
    m_HeightmapViewport.TopLeftY = 0;
    m_HeightmapViewport.Width = (FLOAT)PaddedWidthHeight;
    m_HeightmapViewport.Height = (FLOAT)PaddedWidthHeight;
    m_HeightmapViewport.MinDepth = 0;
    m_HeightmapViewport.MaxDepth = 1;
    m_HeightmapScissor.left = 0;
    m_HeightmapScissor.top = 0;
    m_HeightmapScissor.right = PaddedWidthHeight;
    m_HeightmapScissor.bottom = PaddedWidthHeight;
    const UINT32 SurfaceWidthHeight = (1 << pConfig->SurfacemapDimensionLog2);
    m_SurfaceDiffuseRT.Create(L"Surface Diffuse RT", SurfaceWidthHeight, SurfaceWidthHeight, 1, DXGI_FORMAT_R10G10B10A2_UNORM, 0, true);
    m_SurfaceNormalRT.Create(L"Surface Normal RT", SurfaceWidthHeight, SurfaceWidthHeight, 1, DXGI_FORMAT_R10G10B10A2_UNORM, 0, true);

    m_RootSig.Reset(3, 1);
    m_RootSig.InitStaticSampler(0, Graphics::SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
    m_RootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 1, D3D12_SHADER_VISIBILITY_PIXEL);
    m_RootSig.Finalize(L"TerrainSprite", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    D3D12_INPUT_ELEMENT_DESC vertElem[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0,  0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 0, DXGI_FORMAT_R8G8B8A8_UNORM,     0, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,       0, 20, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    };

    m_HeightmapParticlePSO.SetRootSignature(m_RootSig);
    m_HeightmapParticlePSO.SetRasterizerState(Graphics::RasterizerTwoSided);
    m_HeightmapParticlePSO.SetBlendState(Graphics::BlendDisable);
    m_HeightmapParticlePSO.SetDepthStencilState(Graphics::DepthStateDisabled);
    m_HeightmapParticlePSO.SetInputLayout(_countof(vertElem), vertElem);
    m_HeightmapParticlePSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
    m_HeightmapParticlePSO.SetVertexShader(g_pTerrainSpriteVS, sizeof(g_pTerrainSpriteVS));
    m_HeightmapParticlePSO.SetPixelShader(g_pTerrainSpritePS, sizeof(g_pTerrainSpritePS));
    const DXGI_FORMAT ParticleRTs[2] = { g_HeightmapFormat, DXGI_FORMAT_R8G8_UNORM };
    //const DXGI_FORMAT ParticleRTs[1] = { DXGI_FORMAT_R11G11B10_FLOAT };
    m_HeightmapParticlePSO.SetRenderTargetFormats(ARRAYSIZE(ParticleRTs), ParticleRTs, DXGI_FORMAT_UNKNOWN);
    m_HeightmapParticlePSO.Finalize();

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

void GridTerrainJobs::DrawTerrainSprites(GraphicsContext* pContext, GridBlockCoord Coord, const TerrainSpriteVertex* pVertices, UINT32 VertexCount)
{
    TerrainSpriteVertex TestSprites[10];
    if (pVertices == nullptr)
    {
        ZeroMemory(TestSprites, sizeof(TestSprites));
        for (UINT32 i = 0; i < ARRAYSIZE(TestSprites); ++i)
        {
            TerrainSpriteVertex& V = TestSprites[i];
            V.PositionXYZRotation.x = (FLOAT)i * 2000.0f;
            V.PositionXYZRotation.y = 10.0f;
            V.PositionXYZRotation.z = (FLOAT)i * 2000.0f;
            V.PositionXYZRotation.w = XM_PIDIV4;

            V.UVRect = XMUBYTEN4(0.0f, 0.0f, 1.0f, 1.0f);
            V.XZScale = XMFLOAT2(500, 500);
        }
        pVertices = TestSprites;
        VertexCount = ARRAYSIZE(TestSprites);
    }

    TerrainSpriteCB SpriteCB = {};
    Coord.GetCenterXZInvScale(SpriteCB.CenterPosInvScale);
    SpriteCB.HeightOffset = 0;

    pContext->SetRootSignature(m_RootSig);
    pContext->SetPipelineState(m_HeightmapParticlePSO);
    pContext->SetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    pContext->SetDynamicConstantBufferView(0, sizeof(SpriteCB), &SpriteCB);
    pContext->SetDynamicConstantBufferView(1, sizeof(SpriteCB), &SpriteCB);
    pContext->SetDynamicVB(0, VertexCount, sizeof(TerrainSpriteVertex), pVertices);
    pContext->DrawInstanced(4, VertexCount);
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
    WCHAR strID[64];
    swprintf_s(strID, L"Heightmap (%I64d, %I64d) scale %u", Params.ViewCoord.X, Params.ViewCoord.Y, 1U << Params.ViewCoord.SizeShift);
    Utility::Printf("Creating %S\n", strID);

    const UINT32 WidthHeight = (1 << Params.pConfig->HeightmapDimensionLog2);
    const UINT32 PaddedWidthHeight = WidthHeight + 8;
    pJob->RenderingSubrectScale = (FLOAT)WidthHeight / (FLOAT)PaddedWidthHeight;
    pJob->OutputResources.TiledTexture[0].Create(strID, PaddedWidthHeight, PaddedWidthHeight, 1, 1, g_HeightmapFormat);

    if (Params.GenerateMaterialMap)
    {
        pJob->OutputResources.TiledTexture[1].Create(strID, PaddedWidthHeight, PaddedWidthHeight, 1, 1, DXGI_FORMAT_R8G8_UNORM);
    }

    pJob->OutputResources.pGraphicsJob = g_GpuJobQueue.CreateGraphicsJob(strID);
    GraphicsContext* pContext = pJob->OutputResources.pGraphicsJob->pContext;

    pContext->ExplicitTransitionResource(m_HeightmapRT, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
    const FLOAT ClearHeight[4] = { 0, 0, 0, 0 };
    pContext->ClearColor(m_HeightmapRT, ClearHeight);
    if (Params.GenerateMaterialMap)
    {
        pContext->ExplicitTransitionResource(m_MaterialmapRT, D3D12_RESOURCE_STATE_GENERIC_READ, D3D12_RESOURCE_STATE_RENDER_TARGET, true);
        pContext->ClearColor(m_MaterialmapRT);
    }

    D3D12_CPU_DESCRIPTOR_HANDLE RTHandles[2] = { m_HeightmapRT.GetRTV(), m_MaterialmapRT.GetRTV() };
    pContext->SetRenderTargets(2, RTHandles);
    pContext->SetViewportAndScissor(m_HeightmapViewport, m_HeightmapScissor, 2);

    DrawTerrainSprites(pContext, Params.ViewCoord, nullptr, 0);

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
    pJob->RenderingSubrectScale = 1.0f;
    pJob->pJobMap = &m_TextureSurfacemaps;
    WCHAR strID[64];
    swprintf_s(strID, L"Surfacemap (%I64d, %I64d) scale %u", Params.ViewCoord.X, Params.ViewCoord.Y, 1U << Params.ViewCoord.SizeShift);
    Utility::Printf("Creating %S\n", strID);

    const UINT32 WidthHeight = (1 << Params.pConfig->SurfacemapDimensionLog2);
    pJob->OutputResources.TiledTexture[0].Create(L"Terrain Diffuse Map", WidthHeight, WidthHeight, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM);
    pJob->OutputResources.TiledTexture[1].Create(L"Terrain Normal Map", WidthHeight, WidthHeight, 1, 1, DXGI_FORMAT_R10G10B10A2_UNORM);

    pJob->OutputResources.pGraphicsJob = g_GpuJobQueue.CreateGraphicsJob(strID);
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

