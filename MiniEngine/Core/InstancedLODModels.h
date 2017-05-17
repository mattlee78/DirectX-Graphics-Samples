#pragma once
#include "Model.h"
#include "RootSignature.h"
#include "PipelineState.h"
#include "StringID.h"
#include <unordered_map>
#include <unordered_set>

struct ModelRenderContext;

namespace Graphics
{
    class InstancedLODModelManager;

    struct MeshPlacementVertex
    {
        XMFLOAT3 WorldPosition;
        XMFLOAT4 Orientation;
        FLOAT UniformScale;
    };

    __declspec(align(16))
    struct CBInstanceMeshCulling
    {
        XMFLOAT4X4 g_CameraWorldViewProj;
        XMFLOAT4   g_CameraWorldPos;
        XMFLOAT4   g_CameraWorldDir;
        XMFLOAT4   g_LOD0Params;
        XMFLOAT4   g_LOD1Params;
        XMFLOAT4   g_LOD2Params;
        XMUINT4    g_MaxVertexCount;
    };

    class InstancedLODModel
    {
    protected:
        UINT32 m_ModelIndex;
        UINT32 m_FirstDrawArgIndex;
        Model* m_pModel;
        UINT32 m_LODCount;
        static const UINT32 m_MaxInstanceCountPerFrame = 1024;

        struct LODRender
        {
            UINT32 SubsetStartIndex;
            UINT32 SubsetCount;
        };
        LODRender m_LODs[4];

        typedef std::unordered_set<StructuredBuffer*> SourcePlacementBufferSet;
        SourcePlacementBufferSet m_SourcePlacementBuffers;

    public:
        InstancedLODModel()
            : m_pModel(nullptr),
              m_LODCount(0),
              m_ModelIndex(-1),
              m_FirstDrawArgIndex(0)
        { }

        bool Load(const CHAR* strFilename, UINT32 ModelIndex, D3D12_DRAW_INDEXED_ARGUMENTS* pDestArgs, UINT32* pDestArgOffset);
        void Unload();

        StructuredBuffer* CreateSourcePlacementBuffer(UINT32 PlacementCount, const MeshPlacementVertex* pPlacements);
        bool DestroySourcePlacementBuffer(StructuredBuffer* pBuffer);

    private:
        friend class InstancedLODModelManager;
        void CullAndSort(ComputeContext& Context, const CBInstanceMeshCulling* pCameraParams);
        void Render(GraphicsContext& Context, const ModelRenderContext* pMRC, StructuredBuffer* pInstancePlacementBuffers, ByteAddressBuffer& DrawInstancedArgs);
    };

    class InstancedLODModelManager
    {
    private:
        static const UINT32 m_MaxModelCount = 256;
        static const UINT32 m_MaxLODCount = 4;
        static const UINT32 m_MaxSubsetCountPerModel = 16;

        typedef std::unordered_map<const WCHAR*, InstancedLODModel*> ModelMap;
        UINT32 m_NextModelIndex;
        ModelMap m_Models;

        RootSignature m_CullingRootSig;
        ComputePSO m_CullingPSO;
        ComputePSO m_CreateIndirectArgsPSO;

        RootSignature m_RenderRootSig;

        GraphicsPSO m_RenderPSO;
        GraphicsPSO m_DepthPSO;
        GraphicsPSO m_ShadowPSO;

        PsoLayoutCache m_RenderPSOCache;
        PsoLayoutCache m_DepthPSOCache;
        PsoLayoutCache m_ShadowPSOCache;

        // Start of each frame:
        // 1. Reset instance placements counters
        // 2. Reset first 4 entries of m_InstanceOffsets to zero 
        // For each model:
        // 1. Cull and sort, appending placements to m_InstancePlacements corresponding to LOD
        // 2. Copy 4 counter values to m_InstanceOffsets[(modelIndex + 1) * 4]
        // Post process:
        // For each model * subset:
        // 1. Create a draw indexed argument struct from subset params and delta between m_InstanceOffsets[n] and m_InstanceOffsets[n - 1]
        // Render:
        // Render each subset of each LOD, with the indirect draw args corresponding to each subset
        StructuredBuffer m_InstancePlacements[m_MaxLODCount];
		D3D12_CPU_DESCRIPTOR_HANDLE m_hInstancePlacementUAV[m_MaxLODCount];
        ByteAddressBuffer m_InstanceOffsets;
        ByteAddressBuffer m_DrawIndirectArguments;

        UINT32 m_CurrentSourceArgumentIndex;
        GpuResource m_SourceDrawIndirectArguments;
        D3D12_DRAW_INDEXED_ARGUMENTS* m_pSourceDrawIndirectBuffer;

    public:
        void Initialize();
        void Terminate();

        InstancedLODModel* FindOrLoadModel(const CHAR* strFileName);

        void CullAndSort(ComputeContext& Context, const CBInstanceMeshCulling* pCameraParams);
        void Render(GraphicsContext& Context, const ModelRenderContext* pMRC);

    private:
        void UnloadAllModels();
    };

    extern InstancedLODModelManager g_LODModelManager;
};
