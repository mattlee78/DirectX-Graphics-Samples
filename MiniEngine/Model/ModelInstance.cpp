#include "ModelInstance.h"
#include "Model.h"

using namespace Math;
using namespace Graphics;

ModelInstance::~ModelInstance()
{
}

bool ModelInstance::InitializeModel(const CHAR* strFileName)
{
    m_pModel = new Model();
    bool Success = m_pModel->Load(strFileName);
    if (!Success)
    {
        delete m_pModel;
        m_pModel = nullptr;
    }
    return Success;
}

void ModelInstance::SetWorldTransform(const Math::Matrix4& Transform)
{
    XMStoreFloat4x4(&m_WorldTransform, Transform);
}

void ModelInstance::Render(const ModelRenderContext& MRC) const
{
    GraphicsContext* pContext = MRC.pContext;

    struct VSConstants
    {
        Matrix4 modelToProjection;
        Matrix4 modelToShadow;
        XMFLOAT3 viewerPos;
    } vsConstants;

    Matrix4 WorldTransform = GetWorldTransform();

    vsConstants.modelToProjection = MRC.ViewProjection * WorldTransform;
    vsConstants.modelToShadow = MRC.ModelToShadow * WorldTransform;
    XMStoreFloat3(&vsConstants.viewerPos, MRC.CameraPosition);

    pContext->SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);

    uint32_t materialIdx = 0xFFFFFFFFul;

    uint32_t VertexStride = m_pModel->m_VertexStride;

    pContext->SetIndexBuffer(m_pModel->m_IndexBuffer.IndexBufferView());
    pContext->SetVertexBuffer(0, m_pModel->m_VertexBuffer.VertexBufferView());

    for (unsigned int meshIndex = 0; meshIndex < m_pModel->m_Header.meshCount; meshIndex++)
    {
        const Model::Mesh& mesh = m_pModel->m_pMesh[meshIndex];

        uint32_t indexCount = mesh.indexCount;
        uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
        uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

        if (mesh.materialIndex != materialIdx)
        {
            materialIdx = mesh.materialIndex;
            pContext->SetDynamicDescriptors(3, 0, 6, m_pModel->GetSRVs(materialIdx));
        }

        pContext->DrawIndexed(indexCount, startIndex, baseVertex);
    }
}
