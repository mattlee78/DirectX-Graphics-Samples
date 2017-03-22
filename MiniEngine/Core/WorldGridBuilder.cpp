#include "pch.h"
#include "WorldGridBuilder.h"
#include "GraphicsCore.h"

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
