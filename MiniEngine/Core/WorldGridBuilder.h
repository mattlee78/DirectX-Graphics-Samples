#pragma once

#include <unordered_map>
#include <deque>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>

#include "InstancedLODModels.h"
#include "Math\Random.h"

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
    virtual void PostUpdate() { }

protected:
    BlockCoord VectorToCoord(const XMVECTOR& Coord) const;
    TerrainBlock* FindBlock(const BlockCoord& Coord) const;
    void TrackRect(const BlockCoord& MinCoord, const BlockCoord& MaxCoord);
    void TrackBlock(const BlockCoord& Coord);
    void FreeTerrainBlock(TerrainBlock* pTB);
};

class TessellatedTerrain;

class TerrainServerRenderer : public WorldGridBuilder
{
protected:
    TessellatedTerrain* m_pTessTerrain;

    struct BlockData
    {
        FLOAT* pData;
        UINT32 HeightmapIndex;
        const FLOAT* pGpuSamples;
        D3D12_SUBRESOURCE_FOOTPRINT Footprint;
        FLOAT MinValue;
        FLOAT MaxValue;
    };

public:
    TerrainServerRenderer()
        : m_pTessTerrain(nullptr)
    { }

    void Initialize(TessellatedTerrain* pTerrain, FLOAT BlockWorldScale);
    void Terminate();

    void ServerRender(GraphicsContext* pContext);

protected:
    virtual bool IsInitialized() { return m_pTessTerrain != nullptr; }
    virtual void InitializeBlockData(TerrainBlock* pNewBlock);
    virtual bool IsBlockInitialized(TerrainBlock* pNewBlock);
    virtual void CompleteBlockData(TerrainBlock* pBlock, TerrainBlock* pNeighborBlocks[4]);
    virtual void DeleteBlockData(TerrainBlock* pBlock);

    void ConvertHeightmap(TerrainBlock* pBlock, FLOAT HeightScaleFactor);

    virtual void ProcessTerrainHeightfield(TerrainBlock* pBlock) {}
    virtual void CompleteTerrainHeightfield(TerrainBlock* pBlock, TerrainBlock* pNeighborBlocks[4]) {}
};

class PhysicsWorld;
class RigidBody;
class CollisionShape;
struct TerrainConstructionDesc;
struct ObjectPlacementDesc;

class TerrainPhysicsMap : public TerrainServerRenderer
{
protected:
    PhysicsWorld* m_pPhysicsWorld;
    FLOAT m_WaterLevel;

    struct PhysicsBlockData : public BlockData
    {
        RigidBody* pRigidBody;
        CollisionShape* pShape;

        RigidBody* pWaterRigidBody;
        CollisionShape* pWaterShape;
    };

public:
    void Initialize(PhysicsWorld* pPhysicsWorld, TessellatedTerrain* pTessTerrain, FLOAT BlockWorldScale);

protected:
    virtual void InitializeBlockData(TerrainBlock* pNewBlock);
    virtual void DeleteBlockData(TerrainBlock* pBlock);
    virtual void ProcessTerrainHeightfield(TerrainBlock* pBlock);
};

class TerrainObjectMap : public TerrainServerRenderer
{
protected:
    const TerrainConstructionDesc* m_pConstructionDesc;

    struct InstanceModelPlacementBuffer
    {
        Graphics::InstancedLODModel* pModel;
        std::vector<Graphics::MeshPlacementVertex> Placements;
        StructuredBuffer* pSB;
    };

    struct ObjectBlockData : public BlockData
    {
        std::unordered_map<UINT32, InstanceModelPlacementBuffer*> PlacementBuffers;
    };

    struct ObjectPlacementStackEntry
    {
        const ObjectPlacementDesc* pParentPlacementDesc;
        UINT32 PlacementCount;
        XMFLOAT2 ParentCoordXY;
    };
    typedef std::deque<ObjectPlacementStackEntry> ObjectPlacementStack;

public:
    void Initialize(TessellatedTerrain* pTessTerrain, FLOAT BlockWorldScale);

protected:
    virtual void InitializeBlockData(TerrainBlock* pNewBlock);
    virtual void DeleteBlockData(TerrainBlock* pBlock);
    virtual void ProcessTerrainHeightfield(TerrainBlock* pBlock);
    virtual void CompleteTerrainHeightfield(TerrainBlock* pBlock, TerrainBlock* pNeighborBlocks[4]);
    virtual void PostUpdate();

    XMVECTOR LerpCoords(XMVECTOR NormalizedXY, const TerrainBlock* pBlock) const;

    void CreatePlacement(XMVECTOR NormalizedXY, const TerrainBlock* pBlock, ObjectPlacementStack& PlacementStack, Math::RandomNumberGenerator& rng, const ObjectPlacementDesc* pParentDesc);

private:
    InstanceModelPlacementBuffer* FindIMPlacementBuffer(ObjectBlockData* pBlockData, Graphics::InstancedLODModel* pModel) const;
};
