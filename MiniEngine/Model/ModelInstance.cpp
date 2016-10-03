#include "ModelInstance.h"
#include "Model.h"
#include "LineRender.h"

using namespace Math;
using namespace Graphics;

ModelInstance::~ModelInstance()
{
    if (m_pRigidBody != nullptr)
    {
        m_pRigidBody->GetPhysicsWorld()->RemoveRigidBody(m_pRigidBody);
        delete m_pRigidBody;
        m_pRigidBody = nullptr;
    }
    if (m_pCollisionShape != nullptr)
    {
        delete m_pCollisionShape;
        m_pCollisionShape = nullptr;
    }
}

bool ModelInstance::Initialize(World* pWorld, ModelTemplate* pTemplate, bool GraphicsEnabled, bool IsRemote)
{
    m_pTemplate = pTemplate;

    if (GraphicsEnabled)
    {
        m_pModel = pTemplate->GetModel();
    }
    else
    {
        m_pModel = nullptr;
    }

    // TODO: get initial scale
    const FLOAT InitialScale = 1.0f;
    m_pCollisionShape = pTemplate->GetCollisionShape(InitialScale);

    if (m_pCollisionShape != nullptr)
    {
        FLOAT Mass = pTemplate->GetMass();
        if (IsRemote)
        {
            Mass = 0;
        }
        m_pRigidBody = new RigidBody(m_pCollisionShape, Mass, GetWorldTransform());
        pWorld->GetPhysicsWorld()->AddRigidBody(m_pRigidBody);
    }

    return true;
}

bool ModelInstance::IsLocalNetworkObject() const
{
    return GetNodeID() != 0 && !m_IsRemote;
}

bool ModelInstance::IsRemoteNetworkObject() const
{
    return GetNodeID() != 0 && m_IsRemote;
}

void ModelInstance::SetWorldTransform(const Math::Matrix4& Transform)
{
    XMStoreFloat4x4(&m_WorldTransform, Transform);
    if (IsLocalNetworkObject())
    {
        SetNetworkMatrix(Transform);
    }
    if (m_pRigidBody != nullptr)
    {
        m_pRigidBody->SetWorldTransform(Transform);
    }
}

void ModelInstance::PrePhysicsUpdate(float deltaT)
{
    // Get transform from network if we're a remote network object
    if (IsRemoteNetworkObject())
    {
        const Matrix4 matWorld = GetNetworkMatrix();
        SetWorldTransform(matWorld);
    }

    // Copy world transform to rigid body if the rigid body is kinematic
    if (m_pRigidBody != nullptr && !m_pRigidBody->IsDynamic())
    {
        m_pRigidBody->SetWorldTransform(GetWorldTransform());
    }
}

void ModelInstance::PostPhysicsUpdate(float deltaT)
{
    // Copy world transform from rigid body if the rigid body is dynamic
    if (m_pRigidBody != nullptr && m_pRigidBody->IsDynamic())
    {
        const Matrix4 matWorld(m_pRigidBody->GetWorldTransform());
        XMStoreFloat4x4(&m_WorldTransform, matWorld);
    }

    // Send world transform to network transform if we're a local network object
    if (IsLocalNetworkObject())
    {
        SetNetworkMatrix(GetWorldTransform());
    }
}

void ModelInstance::Render(ModelRenderContext& MRC) const
{
    if (m_pModel == nullptr)
    {
        LineRender::DrawAxis(GetWorldTransform());
        return;
    }

    if (MRC.CurrentPassType == RenderPass_Shadow && !m_RenderInShadowPass)
    {
        return;
    }

    GraphicsContext* pContext = MRC.pContext;

    assert(m_pModel->m_InputLayoutIndex != -1);
    if (MRC.LastInputLayoutIndex != m_pModel->m_InputLayoutIndex)
    {
        GraphicsPSO* pPso = MRC.pPsoCache->SpecializePso(m_pModel->m_InputLayoutIndex);
        pContext->SetPipelineState(*pPso);
        MRC.LastInputLayoutIndex = m_pModel->m_InputLayoutIndex;
    }
    UINT32 LastInputLayoutIndex = -1;

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

void World::Initialize(bool GraphicsEnabled)
{
    m_GraphicsEnabled = GraphicsEnabled;
    m_PhysicsWorld.Initialize(0, XMVectorSet(0, -98.0f, 0, 0));
}

void World::Tick(float deltaT)
{
    auto iter = m_ModelInstances.begin();
    auto end = m_ModelInstances.end();
    while (iter != end)
    {
        ModelInstance* pMI = *iter++;
        pMI->PrePhysicsUpdate(deltaT);
    }

    m_PhysicsWorld.Update(deltaT);

    iter = m_ModelInstances.begin();
    end = m_ModelInstances.end();
    while (iter != end)
    {
        ModelInstance* pMI = *iter++;
        pMI->PostPhysicsUpdate(deltaT);
    }
}

void World::Render(ModelRenderContext& MRC)
{
    assert(m_GraphicsEnabled);

    auto iter = m_ModelInstances.begin();
    auto end = m_ModelInstances.end();
    while (iter != end)
    {
        ModelInstance* pMI = *iter++;
        pMI->Render(MRC);
    }
}

ModelInstance* World::SpawnModelInstance(const CHAR* strTemplateName, const CHAR* strInstanceName, const DecomposedTransform& InitialTransform, bool IsRemote)
{
    ModelTemplate* pMT = FindOrCreateModelTemplate(strTemplateName);
    if (pMT != nullptr)
    {
        return SpawnModelInstance(pMT, strInstanceName, InitialTransform, IsRemote);
    }
    return nullptr;
}

ModelTemplate* World::FindOrCreateModelTemplate(const CHAR* strTemplateName)
{
    StringID s;
    s.SetAnsi(strTemplateName);
    auto iter = m_ModelTemplates.find(s);
    if (iter != m_ModelTemplates.end())
    {
        return iter->second;
    }
    ModelTemplate* pMT = ModelTemplate::Load(strTemplateName, m_GraphicsEnabled);
    if (pMT != nullptr)
    {
        m_ModelTemplates[pMT->GetName()] = pMT;
        return pMT;
    }
    return nullptr;
}

ModelInstance* World::SpawnModelInstance(ModelTemplate* pTemplate, const CHAR* strInstanceName, const DecomposedTransform& InitialTransform, bool IsRemote)
{
    ModelInstance* pMI = new ModelInstance();
    if (pMI == nullptr)
    {
        return nullptr;
    }

    pMI->SetWorldTransform(InitialTransform.GetMatrix());
    pMI->SetRemote((BOOL)IsRemote);

    bool Success = pMI->Initialize(this, pTemplate, m_GraphicsEnabled, IsRemote);

    if (Success)
    {
        m_ModelInstances.insert(pMI);
    }
    else
    {
        delete pMI;
        pMI = nullptr;
    }

    return pMI;
}

World::~World()
{
    {
        auto iter = m_ModelInstances.begin();
        auto end = m_ModelInstances.end();
        while (iter != end)
        {
            ModelInstance* pMI = *iter++;
            delete pMI;
        }
        m_ModelInstances.clear();
    }
    {
        auto iter = m_ModelTemplates.begin();
        auto end = m_ModelTemplates.end();
        while (iter != end)
        {
            ModelTemplate* pMT = iter->second;
            delete pMT;
            ++iter;
        }
        m_ModelTemplates.clear();
    }
}
