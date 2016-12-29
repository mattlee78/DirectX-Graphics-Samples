#pragma once

#include <vector>
#include <unordered_map>
#include <map>

#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXCollision.h>
using namespace DirectX;
using namespace DirectX::PackedVector;

#include "RefCount.h"
#include "GpuBuffer.h"
#include "PipelineState.h"
#include "LinearAllocator.h"
#include "CommandContext.h"

class GridTerrain;
class CollisionShape;
class RigidBody;
class PhysicsWorld;
class World;
struct TerrainGpuJob;

struct TerrainFeaturesBlock
{
    UINT32 Unused;
};

struct FixedGridVertex
{
    XMHALF2 PosXZ;
};

struct GridVertex
{
    FLOAT     PosY;
    XMUDECN4  Normal;
    XMUBYTEN4 TextureBlend;
};

static const D3D12_INPUT_ELEMENT_DESC GridVertexDesc[] =
{
    { "POSITION", 0, DXGI_FORMAT_R16G16_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
//    { "POSITION", 1, DXGI_FORMAT_R32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
//    { "NORMAL",   0, DXGI_FORMAT_R10G10B10A2_UNORM, 0, 4, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
//    { "TEXCOORD", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
};

struct DecorationModelVertex
{
    XMHALF4 Position;
    XMHALF4 Normal;
    XMUBYTEN4 TexCoord;
};
typedef std::vector<DecorationModelVertex> DecorationModelVertexList;
typedef std::vector<USHORT> DecorationModelIndexList;

struct DecorationInstanceVertex
{
    XMHALF4 PositionScale;
    XMHALF4 Orientation;
    XMUBYTEN4 BottomColor;
    XMUBYTEN4 TopColor;
    XMUBYTEN4 TexCoordUVWH;
};
typedef std::vector<DecorationInstanceVertex> DecorationInstanceVertexList;

static const D3D12_INPUT_ELEMENT_DESC DecorationVertexDesc[] =
{
    { "POSITION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "NORMAL", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 0, 8, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "TEXCOORD", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 0, 16, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    { "POSITION", 1, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, 0, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "ORIENTATION", 0, DXGI_FORMAT_R16G16B16A16_FLOAT, 1, 8, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "COLOR", 0, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 16, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "COLOR", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 20, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
    { "TEXCOORD", 1, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 24, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
};

struct DecorationModelSubset
{
    UINT32 StartIndex;
    UINT32 IndexCount;
};

struct CBGridBlock
{
    XMFLOAT4X4 matWorld;
    XMFLOAT4X4 matViewProj;
    XMFLOAT4 PositionToTexCoord;
    XMFLOAT4 LODConstants;
    XMFLOAT4 ModulateColor;
    XMFLOAT4 BlockToHeightmap;
    XMFLOAT4 BlockToSurfacemap;
};

struct CBTerrain
{
    XMFLOAT4 TexCoordTransform0;
    XMFLOAT4 TexCoordTransform1;
    XMFLOAT4 AmbientLightColor;
    XMFLOAT4 InverseLightDirection;
    XMFLOAT4 DirectionalLightColor;
    XMFLOAT4 WaterConstants;
    XMFLOAT4 CameraPosWorld;
    XMFLOAT4 ShadowSize0;
    XMFLOAT4X4 matWorldToShadow0;
    XMFLOAT4X4 matWorldToShadow1;
    XMFLOAT4X4 matWorldToShadow2;
};

union GridBlockCoord
{
    UINT64 Value;
    struct 
    {
        INT64 X : 30;
        INT64 Y : 30;
        UINT64 SizeShift : 4;
    };

public:
    void InitializeWithShift(GridBlockCoord& Other, UINT32 NewShift)
    {
        X = (Other.X >> NewShift) << NewShift;
        Y = (Other.Y >> NewShift) << NewShift;
        SizeShift = NewShift;
    }

    XMVECTOR GetMin() const { return XMVectorSet((FLOAT)X, 0, (FLOAT)Y, 0); }
    XMVECTOR GetScale() const { return XMVectorReplicate((FLOAT)(1U << SizeShift)); }
    XMVECTOR GetMax() const { return GetMin() + GetScale() * XMVectorSet(1,0,1,0); }
    XMVECTOR GetCenter() const { return GetMin() + GetScale() * XMVectorSet(0.5f, 0, 0.5f, 0); }
    XMVECTOR GetShaderOffsetScale() const { return XMVectorSet((FLOAT)X, (FLOAT)Y, (FLOAT)(1U << SizeShift), 0); }
    void GetCenterXZInvScale(XMFLOAT4& Result, FLOAT ViewScaleFactor) const
    {
        const FLOAT Scale = (FLOAT)(1U << SizeShift) * ViewScaleFactor;
        const FLOAT HalfScale = Scale * 0.5f;
        const FLOAT InvScale = 1.0f / HalfScale;
        Result.x = (FLOAT)X + HalfScale;
        Result.y = (FLOAT)Y + HalfScale;
        Result.z = InvScale;
    }
    RECT GetRect() const
    {
        RECT r = { (LONG)X, (LONG)Y, (LONG)X + (1 << SizeShift), (LONG)Y + (1 << SizeShift) };
        return r;
    }

    GridBlockCoord Get0() const
    {
        GridBlockCoord C = *this;
        C.SizeShift = SizeShift - 1;
        return C;
    }

    GridBlockCoord Get1() const
    {
        GridBlockCoord C = *this;
        C.SizeShift = SizeShift - 1;
        C.X += 1i64 << C.SizeShift;
        return C;
    }

    GridBlockCoord Get2() const
    {
        GridBlockCoord C = *this;
        C.SizeShift = SizeShift - 1;
        C.Y += 1i64 << C.SizeShift;
        return C;
    }

    GridBlockCoord Get3() const
    {
        GridBlockCoord C = *this;
        C.SizeShift = SizeShift - 1;
        C.X += 1i64 << C.SizeShift;
        C.Y += 1i64 << C.SizeShift;
        return C;
    }

    XMFLOAT4 GetScalingRect(const GridBlockCoord& LargerCoord, FLOAT SubRectScale = 1.0f) const;
};

struct GridTerrainConfig
{
    GridTerrain* pGridTerrain;
    ID3D12Device* pd3dDevice;
    PhysicsWorld* pPhysicsWorld;
    World* pWorld;
    UINT32 LargestBlockShift;
    UINT32 SmallestBlockShift;
    FLOAT BlockViewSpaceWidthThreshold;
    UINT32 BlockVertexShift;
    FLOAT WaterLevelYpos;
    UINT32 FeaturesBlockShift;

    UINT32 HeightmapDimensionLog2;
    UINT32 SmallHeightmapShift;
    UINT32 MedHeightmapShift;
    UINT32 LargeHeightmapShift;

    UINT32 SurfacemapDimensionLog2;
    UINT32 SmallSurfacemapShift;
    UINT32 LargeSurfacemapShift;

    UINT32 GetBlockVertexCount() const { return 1U << BlockVertexShift; }

    void SetDefault()
    {
        ZeroMemory(this, sizeof(*this));
        LargestBlockShift = 10;
        SmallestBlockShift = 0;
        BlockVertexShift = 4;
        BlockViewSpaceWidthThreshold = 0.75f;
        FeaturesBlockShift = LargestBlockShift + 3;
        HeightmapDimensionLog2 = 9;
        SmallHeightmapShift = 5;
        MedHeightmapShift = 10;
        LargeHeightmapShift = 15;
        SurfacemapDimensionLog2 = 11;
        SmallSurfacemapShift = 4;
        LargeSurfacemapShift = 10;
    }

    void SetPhysicsDefault()
    {
        SetDefault();
        LargestBlockShift = 8;
        SmallestBlockShift = LargestBlockShift;
        BlockVertexShift = 6;
        SurfacemapDimensionLog2 = 0;
        SmallSurfacemapShift = 0;
        LargeSurfacemapShift = 0;
    }
};

struct GridTerrainUpdate
{
    DirectX::BoundingFrustum TransformedFrustum;
    XMMATRIX matVP;
    XMMATRIX matCameraWorld;
};

struct GridTerrainRender
{
    XMMATRIX matVP;
    XMVECTOR vCameraOffset;
    GraphicsContext* pContext;
    DOUBLE AbsoluteTime;
    bool Wireframe;
};

enum GridTerrainRenderFlags : UINT32
{
    GridTerrainRender_OpaqueTerrain = 0x1,
    GridTerrainRender_OpaqueDecoration = 0x2,

    GridTerrainRender_Transparent = 0x100,
    GridTerrainRender_TransparentWater = 0x101,
    GridTerrainRender_TransparentDecoration = 0x102,
};

enum DecorationModelType
{
    DecorationModelType_Grass = 0,
    DecorationModelType_SmallGrass = 1,
    DecorationModelType_DistantGrass = 2,
    DecorationModelType_MAX
};

struct DecorationItem;
struct DecorationItemInstances
{
    DecorationItem* pModel;
    std::vector<XMFLOAT4X4> Transforms;
};
typedef std::unordered_map<const WCHAR*, DecorationItemInstances*> DecorationInstanceMap;

struct DecorationItem
{
    //StringID ItemName;
    //Item* pItem;
    std::set<DecorationItemInstances*> Placements;
    bool NeedsUpdate;
    UINT32 InstanceCount;
    UINT32 MaxInstanceCount;
    //ItemInstance* pRenderII;
    ID3D12Resource* pInstanceBuffer;
    D3D12_GPU_DESCRIPTOR_HANDLE InstanceSRV;
};
typedef std::unordered_map<const WCHAR*, DecorationItem*> DecorationItemMap;

struct GridBlock : public RefCountBase
{
public:
    enum BlockState
    {
        Initializing = 0,
        Initialized,
        PendingSubdivision,
        Subdivided
    };

private:
    friend class GridTerrain;

    const GridTerrainConfig* m_pConfig;

    static volatile UINT32 m_BlockCount;

    volatile BlockState m_State;
    UINT32 m_LastSeenTime;
    UINT32 m_LastFrameRendered;
    UINT64 m_LastFenceRendered;
    GridBlockCoord m_Coord;
    UINT32 m_QuadrantOfParent;
    GridBlock* m_pParent;
    GridBlock* m_pChildren[4];
    TerrainGpuJob* m_pHeightmapJob;
    TerrainGpuJob* m_pPhysicsHeightmapJob;
    TerrainGpuJob* m_pSurfacemapJob;
    const TerrainFeaturesBlock* m_pFeaturesBlock;

    //StructuredBuffer m_VB;
    FLOAT m_MinHeight;
    FLOAT m_MaxHeight;
    void* m_pVertexData;
    CollisionShape* m_pCollisionShape;
    RigidBody* m_pRigidBody;
    CollisionShape* m_pWaterCollisionShape;
    RigidBody* m_pWaterRigidBody;

    DecorationInstanceVertexList m_InstanceVerts;
    ID3D12Resource* m_pDecorationInstanceVB;
    struct DecorationSet
    {
        UINT32 InstanceCount;
        DecorationModelType Type;
    };
    DecorationSet m_DecorationSets[4];
    XMFLOAT4 m_LODConstants;

    //DecorationInstanceMap m_Instances;

public:
    GridBlock()
        : m_State(Initializing),
          m_pConfig(nullptr),
          m_LastSeenTime(0),
          m_QuadrantOfParent(0),
          m_LastFenceRendered(0),
          m_pVertexData(nullptr),
          m_pRigidBody(nullptr),
          m_pCollisionShape(nullptr),
          m_pWaterRigidBody(nullptr),
          m_pWaterCollisionShape(nullptr),
          m_pHeightmapJob(nullptr),
          m_pPhysicsHeightmapJob(nullptr),
          m_pSurfacemapJob(nullptr),
          m_pParent(nullptr)
    {
        ZeroMemory(m_pChildren, sizeof(m_pChildren));
        ZeroMemory(m_DecorationSets, sizeof(m_DecorationSets));
        InterlockedIncrement(&m_BlockCount);
        m_LODConstants = XMFLOAT4(0, FLT_MAX, 1, 0);
    }

    virtual ~GridBlock()
    {
        InterlockedDecrement(&m_BlockCount);
    }

    void TerminatePhysics(PhysicsWorld* pWorld);
    void TerminateInstances();

    static UINT32 GetBlockCount() { return m_BlockCount; }

    HRESULT Initialize(const GridTerrainConfig* pConfig, GridBlockCoord Coord, GridBlock* pParent, UINT32 QuadrantOfParent, const TerrainFeaturesBlock* pFeaturesBlock, bool SynchronousGeometry);

    BlockState GetState() const { return m_State; }
    bool IsInitialized()
    { 
        if (m_State == Initializing)
        {
            CheckGpuJobs();
        }
        return m_State != Initializing; 
    }

    void Update(UINT32 Time, bool LastFrameRendered) 
    { 
        const bool IsRedundantUpdate = (Time <= m_LastSeenTime);
        m_LastSeenTime = Time;
        if (LastFrameRendered)
        {
            m_LastFrameRendered = Time;
        }
        if (!IsRedundantUpdate && m_pParent != nullptr)
        {
            m_pParent->Update(Time, false);
        }
    }
    UINT32 GetTime() const { return m_LastSeenTime; }
    UINT32 GetLastRenderedTime() const { return m_LastFrameRendered; }
    bool CheckForExpiration(UINT32 CutoffTickCount, const GridTerrainConfig* pConfig);

    GridBlockCoord GetCoord() const { return m_Coord; }
    FLOAT GetMinHeight() const { return m_MinHeight; }
    FLOAT GetMaxHeight() const { return m_MaxHeight; }

    bool HasDecorations() const { return m_DecorationSets[0].InstanceCount > 0; }

    XMMATRIX GetTransform(FXMVECTOR CameraOffset) const
    {
        XMVECTOR Position = m_Coord.GetMin() + CameraOffset;
        const XMMATRIX matWorld = XMMatrixTranslationFromVector(Position);
        const XMMATRIX matScale = XMMatrixScalingFromVector(XMVectorSelect(g_XMOne, m_Coord.GetScale(), g_XMSelect1011));
        return matScale * matWorld;
    }

    void Render(const GridTerrainRender& GTR, LinearAllocator* pCBAllocator, const D3D12_INDEX_BUFFER_VIEW& IBV, UINT32 IndexCount, const GridTerrainConfig* pConfig);
    void RenderDecorations(const GridTerrainRender& GTR, LinearAllocator* pCBAllocator, const GridTerrainConfig* pConfig, const DecorationModelSubset* pSubsets);
    //void DebugRender(GlyphRenderer* pGR, FXMVECTOR CameraPosWorld, FXMVECTOR RenderScale, CXMVECTOR ScreenOffset);

private:
    bool IsGpuWorkPending(bool CheckChildren) const;
    void CheckGpuJobs();
    void Terminate();
    virtual void FinalRelease()
    {
        Terminate();
    }
    static UINT32 BuildGeometryWorkerFunction(GridBlock* pBlock, const GridTerrainConfig* pConfig)
    {
        pBlock->BuildGeometry(pConfig);
        return 0;
    }
    void BuildGeometry(const GridTerrainConfig* pConfig);
    void AddDecorationSet(const DecorationSet& DSet);
    void BuildDetailGeometry(const GridTerrainConfig* pConfig);
    void SetDecorationLOD(FLOAT MinDistance, FLOAT MaxDistance, FLOAT Blend = 0.25f);

//     void BuildItemInstances(const GridTerrainConfig* pConfig);
//     void FindInstanceList(const StringID& ItemName, DecorationItemInstances** ppInstanceList);
};

struct GridBlockEdge
{
    INT32 Coordinate;
    UINT32 Shift;
    UINT32 LeftTopBlocks[2];
    UINT32 RightBottomBlocks[2];
    bool LeftTopSolo;
    bool RightBottomSolo;
    UINT32 DebugIndex;

    bool IsMismatchedScale() const
    {
        return LeftTopSolo ^ RightBottomSolo;
    }
};

typedef std::multimap<INT32, GridBlockEdge> GridBlockEdgeList;

class GridTerrain
{
private:
    CRITICAL_SECTION m_CritSec;
    GridTerrainConfig m_Config;
    std::unordered_map<UINT64, GridBlock*> m_RootBlocks;
    UINT32 m_LastBlockUpdateTime;
    UINT32 m_ExpirationThresholdMsec;
    RECT m_RootBlockExtents;

    //TerrainFeatures* m_pFeatures;

    enum BlockEdgeFlags
    {
        TopEdge = 0x01,
        RightEdge = 0x02,
        BottomEdge = 0x04,
        LeftEdge = 0x08,
    };

    struct BlockRender
    {
        GridBlock* pBlock;
        UINT32 EdgeMask;
        UINT32 RenderFlags;

        bool operator< (const BlockRender& RHS) const
        {
            return pBlock->m_Coord.SizeShift > RHS.pBlock->m_Coord.SizeShift;
        }
    };
    std::vector<BlockRender> m_RenderBlockList;
    GridBlockEdgeList m_HorizEdges;
    GridBlockEdgeList m_VertEdges;
    UINT32 m_AllRenderFlags;

    struct IBTile
    {
        D3D12_GPU_VIRTUAL_ADDRESS IBAddress;
        UINT32 IndexCount;
        //UINT32* pIndices;
    };
    IBTile m_EdgeIB[16];
    ByteAddressBuffer m_GridIB;
    StructuredBuffer m_GridBlockFixedVB;

    RootSignature m_RootSig;
    GraphicsPSO m_OpaqueTerrainPSO;
    GraphicsPSO m_WaterTerrainPSO;

    LinearAllocator* m_pCBAllocator;

    /*
    ID3D11Buffer* m_pDecorationVB;
    ID3D11Buffer* m_pDecorationIB;
    ID3D11InputLayout* m_pDecorationLayout;
    VertexLayout m_DecorationLayout;
    ID3D11VertexShader* m_pDecorationVS;
    ID3D11PixelShader* m_pDecorationPS;
    ShaderMaterial* m_pDecorationMaterial;

    StreamTexture* m_pDecorationTexture;

    DecorationModelSubset m_DecorationModels[DecorationModelType_MAX];

    DecorationItemMap m_Items;
    */

public:
    GridTerrain()
    {
        ZeroMemory(&m_Config, sizeof(m_Config));
    }

    HRESULT Initialize(const GridTerrainConfig* pConfig);
    void Terminate();

    UINT32 GetRootBlockShift() const { return m_Config.LargestBlockShift; }

    // Graphics terrain update
    void Update(const GridTerrainUpdate& GTU);

    // Physics terrain update
    void Update(RECT XZBoundingRect, UINT32 UpdateTime = -1, bool Synchronous = false);
    void Precache(RECT XZBoundingRect);

    void RenderOpaque(const GridTerrainRender& GTR);
    void RenderTransparent(const GridTerrainRender& GTR);

    //void DebugRender(GlyphRenderer* pGR, const DirectX::BoundingFrustum& TransformedFrustum, FXMVECTOR CameraPosWorld, FLOAT FarZ, FLOAT Scale, UINT32 Flags);

    FLOAT QueryHeight(FXMVECTOR PositionXYZ);
    //const TerrainFeatures* GetFeatures() const { return m_pFeatures; }

    void BuildInstanceMap(DecorationInstanceMap& IM);

    void EnterLock() { EnterCriticalSection(&m_CritSec); }
    void LeaveLock() { LeaveCriticalSection(&m_CritSec); }

private:
    UINT32 TestGridBlock(const GridBlockCoord& Coord, 
                         const TerrainFeaturesBlock* pFeaturesBlock,
                         GridBlock* pParentBlock, 
                         const GridTerrainUpdate& GTU, 
                         GridBlock** ppChildBlock,
                         UINT32 QuadrantOfParent);

    void UpdateGridBlockNoSubdivision(const GridBlockCoord& Coord,
                                      const TerrainFeaturesBlock* pFeaturesBlock,
                                      bool Synchronous);

    void ResolveEdges();

    void CheckRootBlocksForExpiration();

    void UpdateTerrainCB(GraphicsContext* pContext, DOUBLE AbsoluteTime);

//     DecorationItem* CreateItem(const StringID& ItemName);
//     void UpdateItemInstances();
};

struct RectSet
{
private:
    UINT32 m_Shift;
    std::vector<RECT> m_ShiftedRects;

    RECT ShiftRightRect(const RECT& r) const
    {
        RECT Result;
        Result.left = r.left >> m_Shift;
        Result.top = r.top >> m_Shift;
        Result.right = r.right >> m_Shift;
        Result.bottom = r.bottom >> m_Shift;
        return Result;
    }

    RECT ShiftLeftRect(const RECT& r) const
    {
        RECT Result;
        Result.left = r.left << m_Shift;
        Result.top = r.top << m_Shift;
        Result.right = r.right << m_Shift;
        Result.bottom = r.bottom << m_Shift;
        return Result;
    }

    void IncorporateRect(const RECT& r)
    {
        RECT S = ShiftRightRect(r);
        if (m_ShiftedRects.empty())
        {
            m_ShiftedRects.push_back(S);
        }
        else
        {
            RECT& LastRect = m_ShiftedRects.back();
            RECT IR;
            if (IntersectRect(&IR, &LastRect, &S))
            {
                LastRect = UnionRect(LastRect, S);
            }
            else
            {
                m_ShiftedRects.push_back(S);
            }
        }
    }

public:
    RectSet(UINT32 Shift)
        : m_Shift(Shift)
    {
    }

    UINT32 GetRectCount() const { return (UINT32)m_ShiftedRects.size(); }
    RECT GetRect(UINT32 Index) const { return ShiftLeftRect(m_ShiftedRects[Index]); }

    static RECT ExpandRect(const RECT& Original, INT Amount)
    {
        RECT R = Original;
        R.left -= Amount;
        R.top -= Amount;
        R.right += Amount;
        R.bottom += Amount;
        return R;
    }

    static RECT UnionRect(const RECT& A, const RECT& B)
    {
        RECT R;
        R.left = std::min(A.left, B.left);
        R.top = std::min(A.top, B.top);
        R.right = std::max(A.right, B.right);
        R.bottom = std::max(A.bottom, B.bottom);
        return R;
    }

    static RECT PointXZRect(FXMVECTOR v)
    {
        RECT r;
        r.left = r.right = (INT)XMVectorGetX(v);
        r.top = r.bottom = (INT)XMVectorGetZ(v);
        return r;
    }
    void AddPoint(FXMVECTOR v, INT32 ExpandAmount = 5)
    {
        RECT r = PointXZRect(v);
        r = ExpandRect(r, ExpandAmount);
        IncorporateRect(r);
    }
    void AddVector(FXMVECTOR Origin, CXMVECTOR Delta, INT32 ExpandAmount)
    {
        RECT RA = ExpandRect(PointXZRect(Origin), ExpandAmount);
        RECT RB = ExpandRect(PointXZRect(Origin + Delta), ExpandAmount);
        IncorporateRect(UnionRect(RA, RB));
    }
};

/*
class GridTerrainProxy : public SystemNetObject
{
private:
    UINT32 m_Seed;
    RandomPad m_Pad1;
    RandomPad m_Pad2;
    GridTerrainConfig m_Config;
    GridTerrain* m_pGT;
    GridTerrain* m_pRenderGT;

public:
    GridTerrainProxy()
        : m_pGT(nullptr),
          m_pRenderGT(nullptr),
          m_Seed(0)
    {
        ZeroMemory(&m_Config, sizeof(m_Config));
    }

    static const WCHAR* GetClassName() { return L"*GridTerrain"; }
    static SystemNetObject* CreateGridTerrainProxy(UINT UnusedNodeID) { return new GridTerrainProxy(); }

    UINT GetType() const { return ProxyType_GridTerrain; }
    virtual VOID GetMemberDatas(const MemberDataPosition** ppMemberDatas, UINT* pMemberDataCount) const;

    void ServerInitializeTerrain(UINT32 Seed, EngineServer* pServer);

    bool ClientIsSynced() const { return m_Seed != 0; }
    bool ClientInitialize(ID3D11Device* pd3dDevice, ParameterCollection* pRootParams, PhysicsWorld* pPhysicsWorld);
    GridTerrain* GetRenderableTerrain() const { return m_pRenderGT; }
    GridTerrain* GetPhysicsTerrain() const { return m_pGT; }

    virtual BOOL Initialize(EngineServer* pServer);
    virtual VOID Update(FLOAT DeltaTime, DOUBLE AbsoluteTime, EngineServer* pServer);
};
*/
