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
        Model* m_pModel;
        UINT32 m_LODCount;
        static const UINT32 m_MaxInstanceCountPerFrame = 1024;

        struct LODRender
        {
            UINT32 SubsetStartIndex;
            UINT32 SubsetCount;
            StructuredBuffer InstancePlacements;
        };
        LODRender* m_pLODs;
        D3D12_CPU_DESCRIPTOR_HANDLE m_hLODPlacementUAVs[4];

        ByteAddressBuffer m_DrawInstancedArguments;

        typedef std::unordered_set<StructuredBuffer*> SourcePlacementBufferSet;
        SourcePlacementBufferSet m_SourcePlacementBuffers;

    public:
        InstancedLODModel()
            : m_pModel(nullptr),
              m_LODCount(0),
              m_pLODs(nullptr)
        { 
            ZeroMemory(m_hLODPlacementUAVs, sizeof(m_hLODPlacementUAVs));
        }

        bool Load(const CHAR* strFilename);
        void Unload();

        StructuredBuffer* CreateSourcePlacementBuffer(UINT32 PlacementCount, const MeshPlacementVertex* pPlacements);
        bool DestroySourcePlacementBuffer(StructuredBuffer* pBuffer);

    private:
        friend class InstancedLODModelManager;
        void ResetCounters(CommandContext& Context);
        void CullAndSort(ComputeContext& Context, const CBInstanceMeshCulling* pCameraParams);
        void CopyInstanceCounts(ComputeContext& Context);
        void Render(GraphicsContext& Context, const ModelRenderContext* pMRC);
    };

    class InstancedLODModelManager
    {
    private:
        typedef std::unordered_map<const WCHAR*, InstancedLODModel*> ModelMap;
        ModelMap m_Models;

        RootSignature m_CullingRootSig;
        ComputePSO m_CullingPSO;

        RootSignature m_RenderRootSig;
        GraphicsPSO m_RenderPSO;
        GraphicsPSO m_DepthPSO;
        GraphicsPSO m_ShadowPSO;

        PsoLayoutCache m_PsoCache;

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
