#include "pch.h"
#include "InstancedLODModels.h"
#include "CommandContext.h"
#include "ModelInstance.h"
#include "BufferManager.h"

#include "CompiledShaders/InstanceMeshPrepassCS.h"
#include "CompiledShaders/InstanceMeshVS.h"
#include "CompiledShaders/InstanceMeshPS.h"

namespace Graphics
{
    InstancedLODModelManager g_LODModelManager;

    bool InstancedLODModel::Load(const CHAR* strFilename)
    {
        m_pModel = new Model();
        bool Success = m_pModel->Load(strFilename);
        if (!Success)
        {
            delete m_pModel;
            m_pModel = nullptr;
            return false;
        }

        const UINT32 SubsetCount = m_pModel->m_Header.meshCount;
        D3D12_DRAW_INDEXED_ARGUMENTS* pIndirectArgs = new D3D12_DRAW_INDEXED_ARGUMENTS[SubsetCount];
        ZeroMemory(pIndirectArgs, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) * SubsetCount);

        UINT32 MaxParentID = 0;
        for (UINT32 i = 0; i < SubsetCount; ++i)
        {
            const Model::Mesh& m = m_pModel->m_pMesh[i];
            MaxParentID = std::max(MaxParentID, m.ParentMeshID);
            pIndirectArgs[i].IndexCountPerInstance = m.indexCount;
            pIndirectArgs[i].BaseVertexLocation = m.vertexDataByteOffset / m.vertexStride;
            pIndirectArgs[i].StartIndexLocation = m.indexDataByteOffset / 2;
        }

        m_LODCount = MaxParentID + 1;
        m_pLODs = new LODRender[m_LODCount];

        WCHAR strBufferName[32];
        UINT32 CurrentLODIndex = -1;
        for (UINT32 i = 0; i < SubsetCount; ++i)
        {
            const Model::Mesh& m = m_pModel->m_pMesh[i];
            const UINT32 LODIndex = MaxParentID - m.ParentMeshID;
            ASSERT(LODIndex < m_LODCount);
            LODRender& LR = m_pLODs[LODIndex];
            if (LODIndex != CurrentLODIndex)
            {
                CurrentLODIndex = LODIndex;
                LR.SubsetStartIndex = i;
                LR.SubsetCount = 1;
                swprintf_s(strBufferName, L"LOD %u Instance Placements", LODIndex);
                LR.InstancePlacements.Create(strBufferName, m_MaxInstanceCountPerFrame, sizeof(MeshPlacementVertex), nullptr);
                m_hLODPlacementUAVs[LODIndex] = LR.InstancePlacements.GetUAV();
            }
            else
            {
                LR.SubsetCount++;
            }
        }

        m_DrawInstancedArguments.Create(L"LOD DrawIndexed Args", SubsetCount, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS), pIndirectArgs);
        delete[] pIndirectArgs;

        return true;
    }

    void InstancedLODModel::Unload()
    {
        for (UINT32 i = 0; i < m_LODCount; ++i)
        {
            m_pLODs[i].InstancePlacements.Destroy();
        }
        delete[] m_pLODs;
        m_pLODs = nullptr;
        m_LODCount = 0;

        delete m_pModel;
        m_pModel = nullptr;

        m_DrawInstancedArguments.Destroy();

        auto iter = m_SourcePlacementBuffers.begin();
        auto end = m_SourcePlacementBuffers.end();
        while (iter != end)
        {
            StructuredBuffer* pBuffer = *iter++;
            pBuffer->Destroy();
            delete pBuffer;
        }
        m_SourcePlacementBuffers.clear();
    }

    void InstancedLODModel::ResetCounters(CommandContext& Context)
    {
        for (UINT32 i = 0; i < m_LODCount; ++i)
        {
            Context.ResetCounter(m_pLODs[i].InstancePlacements, 0);
        }
    }

    void InstancedLODModel::CullAndSort(ComputeContext& Context, const CBInstanceMeshCulling* pCameraParams)
    {
        CBInstanceMeshCulling CBInstance = *pCameraParams;
        CBInstance.g_LOD0Params.x = FLT_MAX;
        CBInstance.g_LOD1Params.x = FLT_MAX;
        CBInstance.g_LOD2Params.x = FLT_MAX;

        // Set UAVs
        Context.SetDynamicDescriptors(2, 0, m_LODCount, m_hLODPlacementUAVs);

        auto iter = m_SourcePlacementBuffers.begin();
        auto end = m_SourcePlacementBuffers.end();
        while (iter != end)
        {
            StructuredBuffer* pSourcePlacementBuffer = *iter++;

            // Set buffer SRV from placement buffer
            Context.SetBufferSRV(1, *pSourcePlacementBuffer);

            const UINT32 PlacementCount = pSourcePlacementBuffer->GetElementCount();

            // Set constant buffer with camera, LOD, and buffer size:
            CBInstance.g_MaxVertexCount.x = PlacementCount;
            Context.SetDynamicConstantBufferView(0, sizeof(CBInstance), &CBInstance);

            // Dispatch with placement buffer count
            Context.Dispatch2D(8, (PlacementCount + 7) >> 3);
        }

        CopyInstanceCounts(Context);
    }

    void InstancedLODModel::CopyInstanceCounts(ComputeContext& Context)
    {
        Context.TransitionResource(m_DrawInstancedArguments, D3D12_RESOURCE_STATE_COPY_DEST);
        for (UINT32 i = 0; i < m_LODCount; ++i)
        {
            LODRender& LR = m_pLODs[i];
            UINT32 EndSubsetIndex = LR.SubsetStartIndex + LR.SubsetCount;
            for (UINT32 SubsetIndex = LR.SubsetStartIndex; SubsetIndex < EndSubsetIndex; ++SubsetIndex)
            {
                // Copy from LR.InstancePlacements count buffer to m_DrawInstancedArguments[SubsetIndex] instance count
                const UINT32 InstanceCountOffset = SubsetIndex * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS) + offsetof(D3D12_DRAW_INDEXED_ARGUMENTS, InstanceCount);
                Context.CopyBufferRegion(m_DrawInstancedArguments, InstanceCountOffset, LR.InstancePlacements.GetCounterBuffer(), 0, sizeof(UINT32));
            }
        }
        Context.TransitionResource(m_DrawInstancedArguments, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
    }

    void InstancedLODModel::Render(GraphicsContext& Context, const ModelRenderContext* pMRC)
    {
        Context.SetIndexBuffer(m_pModel->m_IndexBuffer.IndexBufferView());
        Context.SetVertexBuffer(0, m_pModel->m_VertexBuffer.VertexBufferView());

        // Set VS constant buffer with camera and shadow constants
        VSModelConstants vsConstants;
        vsConstants.modelToProjection = pMRC->ViewProjection;
        vsConstants.modelToShadow = pMRC->ModelToShadow;
        vsConstants.modelToShadowOuter = pMRC->ModelToShadowOuter;
        XMStoreFloat3(&vsConstants.viewerPos, pMRC->CameraPosition);
        Context.SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);

        for (UINT32 i = 0; i < m_LODCount; ++i)
        {
            LODRender& LR = m_pLODs[i];

            // Set instance VB for placements
            Context.SetVertexBuffer(1, LR.InstancePlacements.VertexBufferView());

            UINT32 EndSubsetIndex = LR.SubsetStartIndex + LR.SubsetCount;
            for (UINT32 SubsetIndex = LR.SubsetStartIndex; SubsetIndex < EndSubsetIndex; ++SubsetIndex)
            {
                // Set material state for SubsetIndex
                const Model::Mesh& m = m_pModel->m_pMesh[SubsetIndex];
                Context.SetDynamicDescriptors(3, 0, 6, m_pModel->GetSRVs(m.materialIndex));

                UINT32 ArgumentOffset = SubsetIndex * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
                Context.DrawIndexedIndirect(m_DrawInstancedArguments, ArgumentOffset);
            }
        }
    }

    StructuredBuffer* InstancedLODModel::CreateSourcePlacementBuffer(UINT32 PlacementCount, const MeshPlacementVertex* pPlacements)
    {
        StructuredBuffer* pNewSB = new StructuredBuffer();
        if (pNewSB == nullptr)
        {
            return nullptr;
        }
        pNewSB->Create(L"Instanced LOD Model Source Placement Buffer", PlacementCount, sizeof(MeshPlacementVertex), pPlacements);
        m_SourcePlacementBuffers.insert(pNewSB);
        return pNewSB;
    }

    bool InstancedLODModel::DestroySourcePlacementBuffer(StructuredBuffer* pBuffer)
    {
        bool Found = false;
        auto iter = m_SourcePlacementBuffers.find(pBuffer);
        if (iter != m_SourcePlacementBuffers.end())
        {
            Found = true;
            m_SourcePlacementBuffers.erase(iter);
        }
        pBuffer->Destroy();
        delete pBuffer;
        return Found;
    }

    void InstancedLODModelManager::Initialize()
    {
        m_CullingRootSig.Reset(4, 2);
        m_CullingRootSig.InitStaticSampler(0, Graphics::SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig.InitStaticSampler(1, Graphics::SamplerLinearWrapDesc, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig[1].InitAsBufferSRV(0, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig[2].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 4, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 16, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig.Finalize(L"Instance Mesh Culling");

        m_CullingPSO.SetRootSignature(m_CullingRootSig);
        m_CullingPSO.SetComputeShader(g_pInstanceMeshPrepassCS, sizeof(g_pInstanceMeshPrepassCS));
        m_CullingPSO.Finalize();

        m_RenderRootSig.Reset(6, 2);
        m_RenderRootSig.InitStaticSampler(0, SamplerAnisoWrapDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RenderRootSig.InitStaticSampler(15, SamplerShadowDesc, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RenderRootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_VERTEX);
        m_RenderRootSig[1].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RenderRootSig[2].InitAsBufferSRV(0, D3D12_SHADER_VISIBILITY_VERTEX);
        m_RenderRootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 0, 6, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RenderRootSig[4].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 64, 3, D3D12_SHADER_VISIBILITY_PIXEL);
        m_RenderRootSig[5].InitAsConstants(1, 1, D3D12_SHADER_VISIBILITY_VERTEX);
        m_RenderRootSig.Finalize(L"Instance Mesh Render", D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

        D3D12_INPUT_ELEMENT_DESC vertElem[] =
        {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "TANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
            { "BITANGENT", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },

            // Per instance stream 1: MeshPlacementVertex
            { "INSTANCEPOSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    1, 0                           , D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "PARAM",            0, DXGI_FORMAT_R32G32B32A32_FLOAT, 1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
            { "PARAM",            1, DXGI_FORMAT_R32_FLOAT,          1, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_INSTANCE_DATA, 1 },
        };

        DXGI_FORMAT ColorFormat = g_SceneColorBuffer.GetFormat();
        DXGI_FORMAT DepthFormat = g_SceneDepthBuffer.GetFormat();
        DXGI_FORMAT ShadowFormat = g_ShadowBuffer.GetFormat();
        ASSERT(g_OuterShadowBuffer.GetFormat() == ShadowFormat);

        g_InputLayoutCache.FindOrAddLayout(vertElem, _countof(vertElem));

        m_DepthPSO.SetRootSignature(m_RenderRootSig);
        m_DepthPSO.SetRasterizerState(RasterizerDefault);
        m_DepthPSO.SetBlendState(BlendNoColorWrite);
        m_DepthPSO.SetDepthStencilState(DepthStateReadWrite);
        m_DepthPSO.SetInputLayout(_countof(vertElem), vertElem);
        m_DepthPSO.SetPrimitiveTopologyType(D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE);
        m_DepthPSO.SetRenderTargetFormats(0, nullptr, DepthFormat);
        m_DepthPSO.SetVertexShader(g_pDepthViewerVS, sizeof(g_pDepthViewerVS));
        m_DepthPSO.SetPixelShader(g_pDepthViewerPS, sizeof(g_pDepthViewerPS));
        m_DepthPSO.Finalize();
        m_DepthPSOCache.Initialize(&m_DepthPSO);

        m_ShadowPSO = m_DepthPSO;
        m_ShadowPSO.SetRasterizerState(RasterizerShadow);
        m_ShadowPSO.SetRenderTargetFormats(0, nullptr, g_ShadowBuffer.GetFormat());
        m_ShadowPSO.Finalize();
        m_ShadowPSOCache.Initialize(&m_ShadowPSO);

        m_RenderPSO = m_DepthPSO;
        m_RenderPSO.SetBlendState(BlendDisable);
        m_RenderPSO.SetDepthStencilState(DepthStateTestEqual);
        m_RenderPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
        m_RenderPSO.SetVertexShader(g_pGameClientVS, sizeof(g_pGameClientVS));
        m_RenderPSO.SetPixelShader(g_pGameClientPS, sizeof(g_pGameClientPS));
        m_RenderPSO.Finalize();
        m_ModelPSOCache.Initialize(&m_ModelPSO);
    }

    void InstancedLODModelManager::Terminate()
    {
        UnloadAllModels();
    }

    void InstancedLODModelManager::UnloadAllModels()
    {
        auto iter = m_Models.begin();
        auto end = m_Models.end();
        while (iter != end)
        {
            InstancedLODModel* pModel = iter->second;
            pModel->Unload();
            delete pModel;
            ++iter;
        }
        m_Models.clear();
    }

    InstancedLODModel* InstancedLODModelManager::FindOrLoadModel(const CHAR* strFileName)
    {
        StringID Key;
        Key.SetAnsi(strFileName);
        const WCHAR* pKey = (const WCHAR*)Key;
        auto iter = m_Models.find(pKey);
        if (iter != m_Models.end())
        {
            return iter->second;
        }

        InstancedLODModel* pNewModel = new InstancedLODModel();
        bool Success = pNewModel->Load(strFileName);
        if (Success)
        {
            m_Models[pKey] = pNewModel;
            return pNewModel;
        }

        delete pNewModel;
        return nullptr;
    }

    void InstancedLODModelManager::CullAndSort(ComputeContext& Context, const CBInstanceMeshCulling* pCameraParams)
    {
        auto iter = m_Models.begin();
        auto end = m_Models.end();
        while (iter != end)
        {
            InstancedLODModel* pModel = iter->second;
            pModel->ResetCounters(Context);
            ++iter;
        }

        Context.SetRootSignature(m_CullingRootSig);
        Context.SetPipelineState(m_CullingPSO);

        iter = m_Models.begin();
        end = m_Models.end();
        while (iter != end)
        {
            InstancedLODModel* pModel = iter->second;
            pModel->CullAndSort(Context, pCameraParams);
            ++iter;
        }
    }

    void InstancedLODModelManager::Render(GraphicsContext& Context, const ModelRenderContext* pMRC)
    {
        Context.SetRootSignature(m_RenderRootSig);
        Context.SetPipelineState(m_RenderPSO);

        auto iter = m_Models.begin();
        auto end = m_Models.end();
        while (iter != end)
        {
            InstancedLODModel* pModel = iter->second;
            pModel->Render(Context, pMRC);
            ++iter;
        }
    }
};
