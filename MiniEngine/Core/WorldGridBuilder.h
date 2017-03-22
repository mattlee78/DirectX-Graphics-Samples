#pragma once

#include <unordered_map>

class WorldGridBuilder
{
protected:
    FLOAT m_BlockWorldScale;
    UINT64 m_ExpireFrameCount;
    bool m_RequireNeighborsForCompletion;

    union BlockCoord
    {
        struct
        {
            INT32 X;
            INT32 Z;
        };
        UINT64 Hash;

        XMVECTOR GetWorldPosition(FLOAT WorldScale, FLOAT BlockOffset = 0.0f) const
        {
            XMVECTOR BlockPos = XMVectorSet(((FLOAT)X + BlockOffset) * WorldScale, 0, ((FLOAT)Z + BlockOffset) * WorldScale, 0);
            return BlockPos;
        }

        BlockCoord GetBlockInDirection(UINT32 Direction) const
        {
            BlockCoord BC;
            BC.Hash = Hash;
            switch (Direction & 0x3)
            {
            case 0:
                BC.Z -= 1;
                break;
            case 1:
                BC.X += 1;
                break;
            case 2:
                BC.Z += 1;
                break;
            case 3:
                BC.X -= 1;
                break;
            }
            return BC;
        }
    };

    enum class BlockState
    {
        Created,
        Initialized,
        Completed
    };

    struct TerrainBlock
    {
        BlockState State;
        UINT64 LastFrameUsed;
        BlockCoord Coord;
        UINT64 AvailableFence;
        void* pData;
    };

    typedef std::unordered_map<UINT64, TerrainBlock*> TerrainBlockMap;
    TerrainBlockMap m_BlockMap;

public:
    WorldGridBuilder();
    ~WorldGridBuilder();

    void Initialize(FLOAT BlockWorldScale, UINT64 ExpireFrameCount = -1);
    void Terminate();
    void ClearBlocks();

    void TrackObject(const XMVECTOR& Origin, const XMVECTOR& Velocity, FLOAT Radius);
    void Update();

protected:
    virtual bool IsInitialized() { return true; }
    virtual void InitializeBlockData(TerrainBlock* pNewBlock) { }
    virtual bool IsBlockInitialized(TerrainBlock* pNewBlock) { return true; }
    virtual void CompleteBlockData(TerrainBlock* pBlock, TerrainBlock* pNeighborBlocks[4]) { }
    virtual void DeleteBlockData(TerrainBlock* pBlock) { }

protected:
    BlockCoord VectorToCoord(const XMVECTOR& Coord) const;
    TerrainBlock* FindBlock(const BlockCoord& Coord) const;
    void TrackRect(const BlockCoord& MinCoord, const BlockCoord& MaxCoord);
    void TrackBlock(const BlockCoord& Coord);
    void FreeTerrainBlock(TerrainBlock* pTB);
};
