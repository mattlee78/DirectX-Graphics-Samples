#include "pch.h"
#include "InstancedLODModels.h"
#include "CommandContext.h"
#include "ModelInstance.h"
#include "BufferManager.h"

#include "CompiledShaders/InstanceMeshPrepassCS.h"
#include "CompiledShaders/CreateDrawParamsCS.h"
#include "CompiledShaders/InstanceMeshVS.h"
#include "CompiledShaders/InstanceMeshPS.h"
#include "CompiledShaders/InstanceMeshDepthVS.h"
#include "CompiledShaders/InstanceMeshDepthPS.h"

namespace Graphics
{
    InstancedLODModelManager g_LODModelManager;

    bool InstancedLODModel::Load(const CHAR* strFilename, UINT32 ModelIndex, D3D12_DRAW_INDEXED_ARGUMENTS* pDestArgs, UINT32* pDestArgOffset)
    {
        m_pModel = new Model();
        bool Success = m_pModel->Load(strFilename);
        if (!Success)
        {
            delete m_pModel;
            m_pModel = nullptr;
            return false;
        }

        m_ModelIndex = ModelIndex;
        ASSERT(m_ModelIndex != -1);

		const Model::BoundingBox& bbox = m_pModel->GetBoundingBox();
		Scalar RadiusA = Math::Length(bbox.min);
		Scalar RadiusB = Math::Length(bbox.max);
		m_BoundingRadius = Math::Max(RadiusA, RadiusB);

        UINT32 ArgOffset = *pDestArgOffset;
        m_FirstDrawArgIndex = ArgOffset;

        const UINT32 SubsetCount = m_pModel->m_Header.meshCount;
        D3D12_DRAW_INDEXED_ARGUMENTS* pIndirectArgs = pDestArgs + ArgOffset;

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

        UINT32 CurrentLODIndex = -1;
        for (UINT32 i = 0; i < SubsetCount; ++i)
        {
            const Model::Mesh& m = m_pModel->m_pMesh[i];
            const UINT32 LODIndex = MaxParentID - m.ParentMeshID;
            ASSERT(LODIndex < m_LODCount);
            LODRender& LR = m_LODs[LODIndex];
            if (LODIndex != CurrentLODIndex)
            {
                CurrentLODIndex = LODIndex;
                LR.SubsetStartIndex = i;
                LR.SubsetCount = 1;
            }
            else
            {
                LR.SubsetCount++;
            }

            pIndirectArgs[i].StartInstanceLocation = m_ModelIndex;
            pIndirectArgs[i].InstanceCount = LODIndex;
        }

        *pDestArgOffset = ArgOffset + SubsetCount;

        return true;
    }

    void InstancedLODModel::Unload()
    {
        m_LODCount = 0;

        delete m_pModel;
        m_pModel = nullptr;

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

    void InstancedLODModel::CullAndSort(ComputeContext& Context, const CBInstanceMeshCulling* pCameraParams)
    {
        CBInstanceMeshCulling CBInstance = *pCameraParams;
        CBInstance.g_LOD0Params.x = 30.0f;
        CBInstance.g_LOD1Params.x = 150.0f;
        CBInstance.g_LOD2Params.x = 400.0f;
		//CBInstance.g_LOD0Params.x = FLT_MAX;

		CBInstance.g_LOD0Params.y = m_BoundingRadius;

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

		const UINT32 DynamicPlacementCount = (UINT32)m_DynamicPlacements.size();
		if (DynamicPlacementCount > 0)
		{
			Context.SetDynamicSRV(1, DynamicPlacementCount * sizeof(MeshPlacementVertex), &m_DynamicPlacements.front());

			// Set constant buffer with camera, LOD, and buffer size:
			CBInstance.g_MaxVertexCount.x = DynamicPlacementCount;
			Context.SetDynamicConstantBufferView(0, sizeof(CBInstance), &CBInstance);

			// Dispatch with placement buffer count
			Context.Dispatch2D(8, (DynamicPlacementCount + 7) >> 3);

			//m_DynamicPlacements.clear();
		}
    }

    void InstancedLODModel::Render(GraphicsContext& Context, const ModelRenderContext* pMRC, StructuredBuffer* pInstancePlacementBuffers, ByteAddressBuffer& DrawInstancedArgs)
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
            LODRender& LR = m_LODs[i];

            // Set instance VB for placements
			Context.SetVertexBuffer(1, pInstancePlacementBuffers[i].VertexBufferView());

            UINT32 EndSubsetIndex = LR.SubsetStartIndex + LR.SubsetCount;
            for (UINT32 SubsetIndex = LR.SubsetStartIndex; SubsetIndex < EndSubsetIndex; ++SubsetIndex)
            {
                // Set material state for SubsetIndex
                const Model::Mesh& m = m_pModel->m_pMesh[SubsetIndex];
                Context.SetDynamicDescriptors(3, 0, 6, m_pModel->GetSRVs(m.materialIndex));

                UINT32 ArgumentOffset = (SubsetIndex + m_FirstDrawArgIndex) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
				Context.DrawIndexedIndirect(DrawInstancedArgs, ArgumentOffset);
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
        const UINT32 SourceArgBufferSizeBytes = m_MaxModelCount * m_MaxSubsetCountPerModel * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
        D3D12_RESOURCE_DESC BufferDesc = CD3DX12_RESOURCE_DESC::Buffer(SourceArgBufferSizeBytes);
        CD3DX12_HEAP_PROPERTIES HeapProps(D3D12_HEAP_TYPE_UPLOAD);
        ID3D12Resource* pSourceArgBuffer = nullptr;
        Graphics::g_Device->CreateCommittedResource(&HeapProps, D3D12_HEAP_FLAG_NONE, &BufferDesc, D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, __uuidof(ID3D12Resource), (void**)&pSourceArgBuffer);
        pSourceArgBuffer->SetName(L"ILMM Source Draw Argument Buffer");
        pSourceArgBuffer->Map(0, nullptr, (void**)&m_pSourceDrawIndirectBuffer);
        m_SourceDrawIndirectArguments = GpuResource(pSourceArgBuffer, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		pSourceArgBuffer->Release();
        m_CurrentSourceArgumentIndex = 0;

        m_DrawIndirectArguments.Create(L"ILMM Processed Draw Argument Buffer", m_MaxModelCount * m_MaxSubsetCountPerModel, sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));

        m_InstanceOffsets.Create(L"ILMM Instance Offset Buffer", (m_MaxModelCount + 1) * m_MaxLODCount, sizeof(UINT32));

        UINT32 MaxInstanceCount = 1024;
        for (UINT32 i = 0; i < ARRAYSIZE(m_InstancePlacements); ++i)
        {
            WCHAR strName[64];
            swprintf_s(strName, L"ILMM Instance Placement Buffer LOD %u", i);
            m_InstancePlacements[i].Create(strName, MaxInstanceCount, sizeof(MeshPlacementVertex));
			m_hInstancePlacementUAV[i] = m_InstancePlacements[i].GetUAV();
            MaxInstanceCount *= 4;
        }

        m_CullingRootSig.Reset(4, 2);
        m_CullingRootSig.InitStaticSampler(0, Graphics::SamplerLinearClampDesc, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig.InitStaticSampler(1, Graphics::SamplerLinearWrapDesc, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig[0].InitAsConstantBuffer(0, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig[1].InitAsBufferSRV(0, D3D12_SHADER_VISIBILITY_ALL);
		m_CullingRootSig[2].InitAsBufferSRV(1, D3D12_SHADER_VISIBILITY_ALL);
		m_CullingRootSig[3].InitAsDescriptorRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 0, 4, D3D12_SHADER_VISIBILITY_ALL);
        m_CullingRootSig.Finalize(L"Instance Mesh Culling");

        m_CullingPSO.SetRootSignature(m_CullingRootSig);
        m_CullingPSO.SetComputeShader(g_pInstanceMeshPrepassCS, sizeof(g_pInstanceMeshPrepassCS));
        m_CullingPSO.Finalize();

		m_CreateIndirectArgsPSO.SetRootSignature(m_CullingRootSig);
		m_CreateIndirectArgsPSO.SetComputeShader(g_pCreateDrawParamsCS, sizeof(g_pCreateDrawParamsCS));
		m_CreateIndirectArgsPSO.Finalize();

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
			{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
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
        m_DepthPSO.SetVertexShader(g_pInstanceMeshDepthVS, sizeof(g_pInstanceMeshDepthVS));
        m_DepthPSO.SetPixelShader(g_pInstanceMeshDepthPS, sizeof(g_pInstanceMeshDepthPS));
        m_DepthPSO.Finalize();
        m_DepthPSOCache.Initialize(&m_DepthPSO);

        m_ShadowPSO = m_DepthPSO;
        m_ShadowPSO.SetRasterizerState(RasterizerShadow);
        m_ShadowPSO.SetRenderTargetFormats(0, nullptr, g_ShadowBuffer.GetFormat());
        m_ShadowPSO.Finalize();
        m_ShadowPSOCache.Initialize(&m_ShadowPSO);

        m_RenderPSO = m_DepthPSO;
		D3D12_BLEND_DESC BlendCoverage = BlendTraditional;
		BlendCoverage.AlphaToCoverageEnable = TRUE;
        m_RenderPSO.SetBlendState(BlendCoverage);
        //m_RenderPSO.SetDepthStencilState(DepthStateTestEqual);
        m_RenderPSO.SetRenderTargetFormats(1, &ColorFormat, DepthFormat);
        m_RenderPSO.SetVertexShader(g_pInstanceMeshVS, sizeof(g_pInstanceMeshVS));
        m_RenderPSO.SetPixelShader(g_pInstanceMeshPS, sizeof(g_pInstanceMeshPS));
        m_RenderPSO.Finalize();
        m_RenderPSOCache.Initialize(&m_RenderPSO);
    }

    void InstancedLODModelManager::Terminate()
    {
        UnloadAllModels();
		m_SourceDrawIndirectArguments.Destroy();
		m_DrawIndirectArguments.Destroy();
		m_InstanceOffsets.Destroy();
		for (UINT32 i = 0; i < ARRAYSIZE(m_InstancePlacements); ++i)
		{
			m_InstancePlacements[i].Destroy();
		}
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
        bool Success = pNewModel->Load(strFileName, m_NextModelIndex, m_pSourceDrawIndirectBuffer, &m_CurrentSourceArgumentIndex);
        if (Success)
        {
            ++m_NextModelIndex;
            m_Models[pKey] = pNewModel;
            return pNewModel;
        }

        delete pNewModel;
        return nullptr;
    }

    void InstancedLODModelManager::CullAndSort(ComputeContext& Context, const CBInstanceMeshCulling* pCameraParams)
    {
        for (UINT32 i = 0; i < ARRAYSIZE(m_InstancePlacements); ++i)
        {
            Context.ResetCounter(m_InstancePlacements[i]);
        }

        Context.FillBuffer(m_InstanceOffsets, 0, 0, 4 * sizeof(UINT32));

        Context.SetRootSignature(m_CullingRootSig);
        Context.SetPipelineState(m_CullingPSO);

		Context.SetDynamicDescriptors(3, 0, ARRAYSIZE(m_hInstancePlacementUAV), m_hInstancePlacementUAV);

        UINT32 CopyOffset = 4;
        auto iter = m_Models.begin();
        auto end = m_Models.end();
        while (iter != end)
        {
            InstancedLODModel* pModel = iter->second;
            pModel->CullAndSort(Context, pCameraParams);
            for (UINT32 i = 0; i < ARRAYSIZE(m_InstancePlacements); ++i)
            {
				Context.TransitionResource(m_InstancePlacements[i].GetCounterBuffer(), D3D12_RESOURCE_STATE_COPY_SOURCE);
                Context.CopyBufferRegion(m_InstanceOffsets, (CopyOffset + i) * sizeof(UINT32), m_InstancePlacements[i].GetCounterBuffer(), 0, sizeof(UINT32));
            }
            CopyOffset += ARRAYSIZE(m_InstancePlacements);
            ++iter;
        }

        // Build draw indirect args buffer from source args and instance offsets
		Context.TransitionResource(m_InstanceOffsets, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		Context.TransitionResource(m_DrawIndirectArguments, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

		Context.SetPipelineState(m_CreateIndirectArgsPSO);
		Context.SetBufferSRV(1, m_SourceDrawIndirectArguments->GetGPUVirtualAddress());
		Context.SetBufferSRV(2, m_InstanceOffsets);
		Context.SetDynamicDescriptor(3, 0, m_DrawIndirectArguments.GetUAV());
		Context.SetDynamicConstantBufferView(0, sizeof(m_CurrentSourceArgumentIndex), &m_CurrentSourceArgumentIndex);
		Context.Dispatch2D(8, (m_CurrentSourceArgumentIndex + 7) >> 3);

		Context.TransitionResource(m_DrawIndirectArguments, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT);
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
            pModel->Render(Context, pMRC, m_InstancePlacements, m_DrawIndirectArguments);
            ++iter;
        }
    }
};
