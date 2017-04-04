#include "pch.h"
#include "WorldGridBuilder.h"
#include "GraphicsCore.h"
#include "TessTerrain.h"
#include "BulletPhysics.h"
#include "LineRender.h"
#include "Math/Random.h"

WorldGridBuilder::WorldGridBuilder()
{
}


WorldGridBuilder::~WorldGridBuilder()
{
}

void WorldGridBuilder::Initialize(FLOAT BlockWorldScale, UINT64 ExpireFrameCount)
{
    m_BlockWorldScale = BlockWorldScale;
    m_ExpireFrameCount = ExpireFrameCount;
    m_RequireNeighborsForCompletion = false;
}

void WorldGridBuilder::Terminate()
{
    ClearBlocks();
}

void WorldGridBuilder::ClearBlocks()
{
    auto iter = m_BlockMap.begin();
    auto end = m_BlockMap.end();
    while (iter != end)
    {
        TerrainBlock* pBlock = iter->second;
        FreeTerrainBlock(pBlock);
        ++iter;
    }
    m_BlockMap.clear();
}

void WorldGridBuilder::FreeTerrainBlock(TerrainBlock* pTB)
{
    DeleteBlockData(pTB);
    pTB->pData = nullptr;
    delete pTB;
}

WorldGridBuilder::BlockCoord WorldGridBuilder::VectorToCoord(const XMVECTOR& Coord) const
{
    FLOAT X = floorf(XMVectorGetX(Coord) / m_BlockWorldScale);
    FLOAT Z = floorf(XMVectorGetZ(Coord) / m_BlockWorldScale);
    BlockCoord TC;
    TC.X = (INT32)X;
    TC.Z = (INT32)Z + 1;
    return TC;
}

void WorldGridBuilder::TrackObject(const XMVECTOR& Origin, const XMVECTOR& Velocity, FLOAT Radius)
{
    if (!IsInitialized())
    {
        return;
    }

    const XMVECTOR ObjectSize = XMVectorReplicate(Radius);

    XMVECTOR MinO = Origin - ObjectSize;
    XMVECTOR MaxO = Origin + ObjectSize;
    XMVECTOR ProjectedPos = Origin + Velocity * 3.0f;
    XMVECTOR MinP = ProjectedPos - ObjectSize;
    XMVECTOR MaxP = ProjectedPos + ObjectSize;

    XMVECTOR RectMin = XMVectorMin(MinO, MinP);
    XMVECTOR RectMax = XMVectorMax(MaxO, MaxP);

    BlockCoord CoordMin = VectorToCoord(RectMin);
    BlockCoord CoordMax = VectorToCoord(RectMax);

    TrackRect(CoordMin, CoordMax);
}

void WorldGridBuilder::TrackRect(const BlockCoord& MinCoord, const BlockCoord& MaxCoord)
{
    BlockCoord TC;

    for (INT32 Z = MinCoord.Z; Z <= MaxCoord.Z; ++Z)
    {
        TC.Z = Z;
        for (INT32 X = MinCoord.X; X <= MaxCoord.X; ++X)
        {
            TC.X = X;
            TrackBlock(TC);
        }
    }
}

void WorldGridBuilder::TrackBlock(const BlockCoord& Coord)
{
    const UINT64 CurrentFrameIndex = Graphics::GetFrameCount();

    auto iter = m_BlockMap.find(Coord.Hash);
    if (iter != m_BlockMap.end())
    {
        TerrainBlock* pTB = iter->second;
        pTB->LastFrameUsed = CurrentFrameIndex;
    }
    else
    {
        TerrainBlock* pTB = new TerrainBlock();
        ZeroMemory(pTB, sizeof(*pTB));
        pTB->State = BlockState::Created;
        pTB->Coord = Coord;
        pTB->LastFrameUsed = CurrentFrameIndex;
        InitializeBlockData(pTB);
        m_BlockMap[Coord.Hash] = pTB;
    }
}

void WorldGridBuilder::Update()
{
    const UINT64 CurrentFrameIndex = Graphics::GetFrameCount();

    auto iter = m_BlockMap.begin();
    auto end = m_BlockMap.end();
    while (iter != end)
    {
        TerrainBlock* pTB = iter->second;
        auto nextiter = iter;
        ++nextiter;

        if (pTB->State == BlockState::Completed)
        {
            UINT64 Delta = CurrentFrameIndex - pTB->LastFrameUsed;
            if (Delta >= m_ExpireFrameCount)
            {
                FreeTerrainBlock(pTB);
                m_BlockMap.erase(iter);
                iter = nextiter;
                continue;
            }
        }
        else if (pTB->State == BlockState::Created)
        {
            if (IsBlockInitialized(pTB))
            {
                pTB->State = BlockState::Initialized;
            }
        }
        if (pTB->State == BlockState::Initialized)
        {
            TerrainBlock* pNeighborBlocks[4] = {};
            bool CompleteNeighbors = true;
            if (m_RequireNeighborsForCompletion)
            {
                BlockCoord ThisCoord = pTB->Coord;
                for (UINT32 i = 0; i < 4; ++i)
                {
                    pNeighborBlocks[i] = FindBlock(ThisCoord.GetBlockInDirection(i));
                    if (pNeighborBlocks[i] == nullptr || pNeighborBlocks[i]->State < BlockState::Initialized)
                    {
                        CompleteNeighbors = false;
                    }
                }
            }
            if (CompleteNeighbors)
            {
                CompleteBlockData(pTB, pNeighborBlocks);
                pTB->State = BlockState::Completed;
            }
        }

        iter = nextiter;
    }

    PostUpdate();
}

WorldGridBuilder::TerrainBlock* WorldGridBuilder::FindBlock(const BlockCoord& Coord) const
{
    auto iter = m_BlockMap.find(Coord.Hash);
    if (iter != m_BlockMap.end())
    {
        return iter->second;
    }
    return nullptr;
}

void TerrainServerRenderer::Initialize(TessellatedTerrain* pTerrain, FLOAT BlockWorldScale)
{
    m_pTessTerrain = pTerrain;
    WorldGridBuilder::Initialize(BlockWorldScale);
}

void TerrainServerRenderer::Terminate()
{
    m_pTessTerrain = nullptr;
    WorldGridBuilder::Terminate();
}

void TerrainServerRenderer::InitializeBlockData(TerrainBlock* pNewBlock)
{
    BlockData* pBD = (BlockData*)pNewBlock->pData;
    if (pBD == nullptr)
    {
        pBD = new BlockData();
        pNewBlock->pData = pBD;
    }
    pBD->pData = nullptr;
    pBD->pGpuSamples = nullptr;
    pBD->HeightmapIndex = -1;
    pBD->MinValue = 0;
    pBD->MaxValue = 0;
}

void TerrainServerRenderer::DeleteBlockData(TerrainBlock* pBlock)
{
    BlockData* pBD = (BlockData*)pBlock->pData;
    if (pBD == nullptr)
    {
        return;
    }

    if (pBD->HeightmapIndex != -1)
    {
        m_pTessTerrain->FreePhysicsHeightmap(pBD->HeightmapIndex);
        pBD->HeightmapIndex = -1;
    }

    if (pBD->pData != nullptr)
    {
        delete[] pBD->pData;
        pBD->pData = nullptr;
    }

    delete pBD;
    pBlock->pData = nullptr;
}

bool TerrainServerRenderer::IsBlockInitialized(TerrainBlock* pNewBlock)
{
    BlockData* pBD = (BlockData*)pNewBlock->pData;

    if (pBD->HeightmapIndex == -1 || pNewBlock->AvailableFence == 0)
    {
        return false;
    }

    CommandQueue& CQ = Graphics::g_CommandManager.GetQueue();

    if (CQ.IsFenceComplete(pNewBlock->AvailableFence))
    {
        assert(pBD->pGpuSamples != nullptr);
        ProcessTerrainHeightfield(pNewBlock);
        m_pTessTerrain->FreePhysicsHeightmap(pBD->HeightmapIndex);
        pBD->HeightmapIndex = -1;
        pBD->pGpuSamples = nullptr;
        pNewBlock->AvailableFence = -1;
        return true;
    }

    return false;
}

void TerrainServerRenderer::CompleteBlockData(TerrainBlock* pBlock, TerrainBlock* pNeighborBlocks[4])
{
    BlockData* pBD = (BlockData*)pBlock->pData;
    assert(pBD->HeightmapIndex == -1);
    assert(pBD->pGpuSamples == nullptr);
    CompleteTerrainHeightfield(pBlock, pNeighborBlocks);
}

void TerrainServerRenderer::ServerRender(GraphicsContext* pContext)
{
    if (!IsInitialized())
    {
        return;
    }

    auto iter = m_BlockMap.begin();
    auto end = m_BlockMap.end();
    while (iter != end)
    {
        TerrainBlock* pTB = iter->second;
        if (pTB->State == WorldGridBuilder::BlockState::Created && pTB->AvailableFence == 0)
        {
            BlockData* pBD = (BlockData*)pTB->pData;

            const XMVECTOR BlockPos = pTB->Coord.GetWorldPosition(m_BlockWorldScale);

            UINT32 HeightmapIndex = m_pTessTerrain->PhysicsRender(pContext, BlockPos, m_BlockWorldScale, &pBD->pGpuSamples, &pBD->Footprint);
            if (HeightmapIndex != -1)
            {
                pBD->HeightmapIndex = HeightmapIndex;
                pTB->AvailableFence = Graphics::g_CommandManager.GetQueue().GetNextFenceValue();
            }
        }

        ++iter;
    }
}

void TerrainServerRenderer::ConvertHeightmap(TerrainBlock* pBlock, FLOAT HeightScaleFactor)
{
    BlockData* pBD = (BlockData*)pBlock->pData;
    const D3D12_SUBRESOURCE_FOOTPRINT& Footprint = pBD->Footprint;

    assert(pBD->HeightmapIndex != -1);
    const FLOAT* pSrc = pBD->pGpuSamples;
    const FLOAT* pSrcRow = pSrc;
    FLOAT* pSamples = new FLOAT[Footprint.Width * Footprint.Height];
    FLOAT* pDestRow = pSamples;

    FLOAT MinValue = FLT_MAX;
    FLOAT MaxValue = -FLT_MAX;

    const FLOAT ValueScale = m_pTessTerrain->GetWorldScale() * HeightScaleFactor;
    for (UINT32 y = 0; y < Footprint.Height; ++y)
    {
        for (UINT32 x = 0; x < Footprint.Width; ++x)
        {
            FLOAT SrcValue = pSrcRow[x];
            FLOAT DestValue = SrcValue * ValueScale;
            pDestRow[x] = DestValue;
            if (DestValue < MinValue) MinValue = DestValue;
            if (DestValue > MaxValue) MaxValue = DestValue;
        }
        pSrcRow = (const FLOAT*)((BYTE*)pSrcRow + Footprint.RowPitch);
        pDestRow += Footprint.Width;
    }

    pBD->pData = pSamples;
    pBD->MinValue = MinValue;
    pBD->MaxValue = MaxValue;
}

void TerrainPhysicsMap::Initialize(PhysicsWorld* pPhysicsWorld, TessellatedTerrain* pTessTerrain, FLOAT BlockWorldScale)
{
    m_pPhysicsWorld = pPhysicsWorld;
    m_WaterLevel = 0;
    TerrainServerRenderer::Initialize(pTessTerrain, BlockWorldScale);
}

void TerrainPhysicsMap::InitializeBlockData(TerrainBlock* pNewBlock)
{
    PhysicsBlockData* pBD = new PhysicsBlockData();
    pBD->pRigidBody = nullptr;
    pBD->pShape = nullptr;
    pNewBlock->pData = pBD;

    TerrainServerRenderer::InitializeBlockData(pNewBlock);
}

void TerrainPhysicsMap::DeleteBlockData(TerrainBlock* pBlock)
{
    PhysicsBlockData* pBD = (PhysicsBlockData*)pBlock->pData;
    if (pBD->pRigidBody != nullptr)
    {
        m_pPhysicsWorld->RemoveRigidBody(pBD->pRigidBody);
        delete pBD->pRigidBody;
        pBD->pRigidBody = nullptr;
    }

    if (pBD->pShape != nullptr)
    {
        delete pBD->pShape;
        pBD->pShape = nullptr;
    }

    if (pBD->pWaterRigidBody != nullptr)
    {
        m_pPhysicsWorld->RemoveRigidBody(pBD->pWaterRigidBody);
        delete pBD->pWaterRigidBody;
        pBD->pWaterRigidBody = nullptr;
    }

    if (pBD->pWaterShape != nullptr)
    {
        delete pBD->pWaterShape;
        pBD->pWaterShape = nullptr;
    }

    TerrainServerRenderer::DeleteBlockData(pBlock);
}

void TerrainPhysicsMap::ProcessTerrainHeightfield(TerrainBlock* pBlock)
{
    PhysicsBlockData* pBD = (PhysicsBlockData*)pBlock->pData;
    const D3D12_SUBRESOURCE_FOOTPRINT& Footprint = pBD->Footprint;
    const FLOAT PhysicsHeightScale = (FLOAT)(Footprint.Width - 1) / m_BlockWorldScale;
    ConvertHeightmap(pBlock, PhysicsHeightScale);

    const FLOAT ShapeScale = 1.0f / PhysicsHeightScale;
    const FLOAT CenterYPos = (pBD->MinValue + pBD->MaxValue) * 0.5f * ShapeScale;

    CollisionShape* pShape = CollisionShape::CreateHeightfield(pBD->pData, Footprint.Width, Footprint.Height, pBD->MinValue, pBD->MaxValue);
    pShape->SetUniformScale(1.0f / PhysicsHeightScale);

    XMVECTOR BlockCenterPos = pBlock->Coord.GetWorldPosition(m_BlockWorldScale, 0.5f);
    BlockCenterPos -= XMVectorSet(0, 0, m_BlockWorldScale, 0);
    BlockCenterPos = XMVectorSetY(BlockCenterPos, CenterYPos);
    XMMATRIX matTransform = XMMatrixTranslationFromVector(BlockCenterPos);
    RigidBody* pRB = new RigidBody(pShape, 0, matTransform);

    m_pPhysicsWorld->AddRigidBody(pRB);

    pBD->pShape = pShape;
    pBD->pRigidBody = pRB;

    if (pBD->MinValue < m_WaterLevel)
    {
        FLOAT WaterHalfHeight = (m_WaterLevel - pBD->MinValue) * 0.5f * ShapeScale;
        XMVECTOR BoxHalfSize = XMVectorSet(m_BlockWorldScale * 0.5f, WaterHalfHeight, m_BlockWorldScale * 0.5f, 0);
        CollisionShape* pWaterShape = CollisionShape::CreateBox(BoxHalfSize);
        FLOAT WaterCenterY = (m_WaterLevel + pBD->MinValue) * 0.5f * ShapeScale;
        BlockCenterPos = XMVectorSetY(BlockCenterPos, WaterCenterY);
        matTransform = XMMatrixTranslationFromVector(BlockCenterPos);
        RigidBody* pWaterRB = new RigidBody(pWaterShape, 0, matTransform);
        m_pPhysicsWorld->AddRigidBody(pWaterRB);
        pWaterRB->SetWaterRigidBody();
        pBD->pWaterShape = pWaterShape;
        pBD->pWaterRigidBody = pWaterRB;
    }
}

void TerrainObjectMap::Initialize(TessellatedTerrain* pTessTerrain, FLOAT BlockWorldScale)
{
    TerrainServerRenderer::Initialize(pTessTerrain, BlockWorldScale);
    m_WaterLevel = 0;
}

void TerrainObjectMap::RenderWater(GraphicsContext* pContext)
{
    const XMVECTOR BlockOffset = XMVectorSet(0, 0, -m_BlockWorldScale, 0);
    auto iter = m_BlockMap.begin();
    auto end = m_BlockMap.end();
    while (iter != end)
    {
        TerrainBlock* pTB = iter->second;
        if (pTB->pData == nullptr || pTB->State == WorldGridBuilder::BlockState::Initialized)
        {
            ++iter;
            continue;
        }
        ObjectBlockData* pBD = (ObjectBlockData*)pTB->pData;
        if (pBD->MinValue >= m_WaterLevel)
        {
            ++iter;
            continue;
        }
        XMVECTOR BlockCoord = pTB->Coord.GetWorldPosition(m_BlockWorldScale, 0.5f) + BlockOffset;
        BlockCoord = XMVectorSetY(BlockCoord, m_WaterLevel);
        LineRender::DrawGridXZ(XMMatrixTranslationFromVector(BlockCoord), m_BlockWorldScale * 0.5f, 100, XMVectorSet(0.5f, 0.5f, 1.0f, 1.0f));
        ++iter;
    }
}

void TerrainObjectMap::InitializeBlockData(TerrainBlock* pNewBlock)
{
    m_RequireNeighborsForCompletion = true;
    ObjectBlockData* pBD = new ObjectBlockData();
    pNewBlock->pData = pBD;
    TerrainServerRenderer::InitializeBlockData(pNewBlock);
}

void TerrainObjectMap::DeleteBlockData(TerrainBlock* pBlock)
{
    TerrainServerRenderer::DeleteBlockData(pBlock);
}

void TerrainObjectMap::ProcessTerrainHeightfield(TerrainBlock* pBlock)
{
    ObjectBlockData* pBD = (ObjectBlockData*)pBlock->pData;
    const D3D12_SUBRESOURCE_FOOTPRINT& Footprint = pBD->Footprint;
    ConvertHeightmap(pBlock, 1.0f);

    // TODO: place objects
    Math::RandomNumberGenerator rng;
    rng.SetSeed((UINT32)pBlock->Coord.Hash ^ (UINT32)(pBlock->Coord.Hash >> 32));

    PlacedObject PO;
    for (UINT32 i = 0; i < 100; ++i)
    {
//         UINT32 Row = i / 10;
//         UINT32 Col = i % 10;
//         XMVECTOR NormXY = XMVectorSet((FLOAT)Row * 0.1f, (FLOAT)Col * 0.1f, 0, 0);
        XMVECTOR NormXY = XMVectorSet(rng.NextFloat(), rng.NextFloat(), 0, 0);
        XMStoreHalf2(&PO.NormCoord, NormXY);
        PO.Radius = rng.NextFloat(0, 5);
        pBD->ObjectCoords.push_back(PO);
    }
}

void TerrainObjectMap::CompleteTerrainHeightfield(TerrainBlock* pBlock, TerrainBlock* pNeighborBlocks[4])
{
    ObjectBlockData* pBD = (ObjectBlockData*)pBlock->pData;
    ObjectBlockData* pNBD[4];
    for (UINT32 i = 0; i < 4; ++i)
    {
        pNBD[i] = (ObjectBlockData*)pNeighborBlocks[i]->pData;
        assert(pNBD[i] != nullptr);
    }

    // TODO: add additional cross-block objects based on objects within pBD and pNBDs

    // TODO: for client, build instance VBs for renderable objects
    // TODO: for server, build physics objects for collidable objects
}

void TerrainObjectMap::PostUpdate()
{
    if (1)
    {
        return;
    }

    const FLOAT HeightScale = 1.0f;
    const XMVECTOR BlockOffset = XMVectorSet(0, 0, -m_BlockWorldScale, 0);

    auto iter = m_BlockMap.begin();
    auto end = m_BlockMap.end();
    while (iter != end)
    {
        const TerrainBlock* pTB = iter->second;
        const ObjectBlockData* pOBD = (const ObjectBlockData*)pTB->pData;
        ++iter;

        XMVECTOR BlockMin = pTB->Coord.GetWorldPosition(m_BlockWorldScale, 0.0f) + BlockOffset;
        XMVECTOR BlockMax = pTB->Coord.GetWorldPosition(m_BlockWorldScale, 1.0f) + BlockOffset;
        BlockMin = XMVectorSetY(BlockMin, pOBD->MinValue * HeightScale);
        BlockMax = XMVectorSetY(BlockMax, pOBD->MaxValue * HeightScale);

        LineRender::DrawAxisAlignedBox(BlockMin, BlockMax, g_XMOne);

        if (pOBD->pData != nullptr)
        {
            const XMVECTOR BoxSize = XMVectorReplicate(2.0f);
            const UINT32 ObjCount = (UINT32)pOBD->ObjectCoords.size();
            for (UINT32 i = 0; i < ObjCount; ++i)
            {
                const PlacedObject& PO = pOBD->ObjectCoords[i];
                XMVECTOR NormXY = XMLoadHalf2(&PO.NormCoord);
                XMVECTOR HeightY = LerpCoords(NormXY, pTB) * XMVectorReplicate(HeightScale);
                XMVECTOR PosXZ = XMVectorLerpV(BlockMin, BlockMax, XMVectorSwizzle<0, 3, 1, 3>(NormXY));
                XMVECTOR PosXYZ = XMVectorSelect(HeightY, PosXZ, g_XMSelect1010);
                LineRender::DrawAxisAlignedBox(PosXYZ - BoxSize, PosXYZ + BoxSize, XMVectorSet(1, 0, 1, 1));
            }
        }
    }
}

XMVECTOR TerrainObjectMap::LerpCoords(XMVECTOR NormalizedXY, const TerrainBlock* pBlock) const
{
    const ObjectBlockData* pOBD = (const ObjectBlockData*)pBlock->pData;
    assert(pOBD->pData != nullptr);

    const UINT32 PhysicsMapDimension = m_pTessTerrain->GetPhysicsMapDimension();
    NormalizedXY *= XMVectorReplicate((FLOAT)(PhysicsMapDimension - 1));
    XMVECTOR Frac = NormalizedXY - XMVectorFloor(NormalizedXY);
    XMVECTOR InvFrac = g_XMOne - Frac;
    XMVECTOR FracXIX = XMVectorPermute<0, 4, 1, 5>(Frac, InvFrac);
    UINT32 Column = (UINT32)XMVectorGetX(NormalizedXY);
    UINT32 Row = (UINT32)XMVectorGetY(NormalizedXY);
    const UINT32 Coord = Row * PhysicsMapDimension + Column;
    const FLOAT* pSampleA = (const FLOAT*)pOBD->pData + Coord;
    const XMVECTOR SamplesAB = XMLoadFloat2((const XMFLOAT2*)pSampleA);
    const XMVECTOR SamplesCD = XMLoadFloat2((const XMFLOAT2*)(pSampleA + PhysicsMapDimension));
    XMVECTOR LerpAB = SamplesAB * FracXIX;
    LerpAB += XMVectorSplatY(LerpAB);
    XMVECTOR LerpCD = SamplesCD * FracXIX;
    LerpCD += XMVectorSplatY(LerpCD);
    XMVECTOR Lerp = LerpAB * XMVectorSplatZ(FracXIX) + LerpCD * XMVectorSplatW(FracXIX);
    return XMVectorSplatX(Lerp);
}
