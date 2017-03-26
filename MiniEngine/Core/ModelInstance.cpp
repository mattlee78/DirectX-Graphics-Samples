#include "pch.h"
#include "ModelInstance.h"
#include "Model.h"
#include "LineRender.h"
#include "TessTerrain.h"

using namespace Math;
using namespace Graphics;

ModelInstance::~ModelInstance()
{
    if (m_pVehicle != nullptr)
    {
        delete m_pVehicle;
        m_pVehicle = nullptr;
    }
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
        if (pTemplate->GetRigidBodyDesc()->IsWater)
        {
            m_pRigidBody->SetWaterRigidBody();
        }

        const ModelRigidBodyDesc* pRBDesc = pTemplate->GetRigidBodyDesc();
        assert(pRBDesc != nullptr);
        if (pRBDesc->pVehicleConfig != nullptr)
        {
            const VehicleConfig* pVC = pRBDesc->pVehicleConfig;
            if (IsRemote)
            {
                m_WheelCount = (UINT32)pVC->Axles.size() * 2;
            }
            else
            {
                m_pVehicle = new Vehicle(pWorld->GetPhysicsWorld(), m_pRigidBody, pVC);
                m_WheelCount = m_pVehicle->GetWheelCount();
                m_pVehicle->SetSteering(0.1f);
                m_pVehicle->SetGasAndBrake(0.2f, 0);
            }
            if (m_WheelCount > 0)
            {
                m_pWheelData = new WheelData[m_WheelCount];
                ZeroMemory(m_pWheelData, m_WheelCount * sizeof(WheelData));
            }
        }
    }

    return true;
}

struct AdditionalBindingDesc
{
    UINT32 Type : 8;
    UINT32 Context : 24;
};

enum AdditionalBindingType
{
    ABT_Invalid = 0,
    ABT_VehicleWheelPosition = 1,
    ABT_VehicleWheelOrientation = 2,
};

UINT ModelInstance::CreateAdditionalBindings(StateInputOutput* pStateIO, UINT ParentID, UINT FirstChildID)
{
    assert(IsLocalNetworkObject());

    UINT32 ChildID = FirstChildID;

    if (m_pWheelData != nullptr)
    {
        AdditionalBindingDesc ABD = {};

        for (UINT32 i = 0; i < m_WheelCount; ++i)
        {
            WheelData& WD = m_pWheelData[i];
            ABD.Context = i;
            ABD.Type = ABT_VehicleWheelPosition;

            pStateIO->CreateNode(
                ParentID,
                ChildID++,
                StateNodeType::Float3Delta,
                &WD.Position,
                sizeof(WD.Position),
                0,
                &ABD,
                sizeof(ABD),
                TRUE);

            ABD.Type = ABT_VehicleWheelOrientation;
            pStateIO->CreateNode(
                ParentID,
                ChildID++,
                StateNodeType::Float4AsHalf4Delta,
                &WD.Orientation,
                sizeof(WD.Orientation),
                0,
                &ABD,
                sizeof(ABD),
                TRUE);
        }
    }

    return ChildID;
}

BOOL ModelInstance::CreateDynamicChildNode(const VOID* pCreationData, const SIZE_T CreationDataSizeBytes, const StateNodeType NodeType, VOID** ppCreatedData, SIZE_T* pCreatedDataSizeBytes)
{
    if (CreationDataSizeBytes == sizeof(AdditionalBindingDesc))
    {
        const AdditionalBindingDesc* pABD = (const AdditionalBindingDesc*)pCreationData;
        switch (pABD->Type)
        {
        case ABT_VehicleWheelPosition:
            if (pABD->Context >= m_WheelCount)
            {
                return FALSE;
            }
            *ppCreatedData = &m_pWheelData[pABD->Context].Position;
            *pCreatedDataSizeBytes = sizeof(m_pWheelData[pABD->Context].Position);
            return TRUE;
        case ABT_VehicleWheelOrientation:
            if (pABD->Context >= m_WheelCount)
            {
                return FALSE;
            }
            *ppCreatedData = &m_pWheelData[pABD->Context].Orientation;
            *pCreatedDataSizeBytes = sizeof(m_pWheelData[pABD->Context].Orientation);
            return TRUE;
        }
    }
    return FALSE;
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
        SetNetworkMatrix(Transform, true);
    }
    if (m_pRigidBody != nullptr)
    {
        m_pRigidBody->SetWorldTransform(Transform);
    }
}

bool ModelInstance::IsDynamic() const
{
    return m_pRigidBody != nullptr && m_pRigidBody->IsDynamic();
}

Math::Vector3 ModelInstance::GetWorldVelocity() const
{
    if (IsDynamic())
    {
        return Vector3(m_pRigidBody->GetLinearVelocity());
    }
    return Vector3(kZero);
}

FLOAT ModelInstance::GetRadius() const
{
    if (m_pRigidBody != nullptr)
    {
        XMVECTOR BoxMin, BoxMax;
        m_pRigidBody->GetWorldAABB(&BoxMin, &BoxMax);
        return XMVectorGetX(XMVector3LengthEst(BoxMax - BoxMin));
    }
    return 0;
}

bool ModelInstance::PrePhysicsUpdate(float deltaT, INT64 ClientTicks)
{
    // Get transform from network if we're a remote network object
    if (IsRemoteNetworkObject())
    {
        assert(ClientTicks != 0);
        const Matrix4 matWorld = GetNetworkMatrix(ClientTicks);
        SetWorldTransform(matWorld);
        XMStoreFloat4x4(&m_ScaledWorldTransform, GetNetworkMatrix(ClientTicks, true));
        for (UINT32 i = 0; i < m_WheelCount; ++i)
        {
            WheelData& WD = m_pWheelData[i];
            XMVECTOR PredictedPosition = WD.Position.Lerp(ClientTicks);
            XMVECTOR PredictedOrientation = WD.Orientation.LerpQuaternion(ClientTicks);
            Matrix4 m;
            m.Compose(Vector3(PredictedPosition), 1, Vector4(PredictedOrientation));

            static const XMMATRIX LeftWheelTransform = { g_XMIdentityR1, -g_XMIdentityR0, g_XMIdentityR2, g_XMIdentityR3 };
            static const XMMATRIX RightWheelTransform = { -g_XMIdentityR1, g_XMIdentityR0, g_XMIdentityR2, g_XMIdentityR3 };

            if (i % 2 == 0)
            {
                m = m * Matrix4(LeftWheelTransform);
            }
            else
            {
                m = m * Matrix4(RightWheelTransform);
            }

            XMStoreFloat4x4(&WD.Transform, m);
        }
    }

    // Copy world transform to rigid body if the rigid body is kinematic
    if (m_pRigidBody != nullptr && !m_pRigidBody->IsDynamic())
    {
        m_pRigidBody->SetWorldTransform(GetWorldTransform());
    }

    bool StillAlive = true;
    if (m_LifetimeRemaining >= 0)
    {
        m_LifetimeRemaining -= deltaT;
        if (m_LifetimeRemaining <= 0)
        {
            m_LifetimeRemaining = 0;
            StillAlive = false;
        }
    }

    return StillAlive;
}

void ModelInstance::PostPhysicsUpdate(float deltaT)
{
    // Copy world transform from rigid body if the rigid body is dynamic
    if (m_pRigidBody != nullptr && m_pRigidBody->IsDynamic())
    {
        const Matrix4 matWorld(m_pRigidBody->GetWorldTransform());
        XMStoreFloat4x4(&m_WorldTransform, matWorld);

        if (m_pVehicle != nullptr && m_WheelCount > 0)
        {
            assert(m_pWheelData != nullptr);
            for (UINT32 i = 0; i < m_WheelCount; ++i)
            {
                XMFLOAT3 Position;
                XMFLOAT4 Orientation;
                m_pVehicle->GetWheelTransform(i, &Orientation, &Position);
                m_pWheelData[i].Position.SetRawValue(XMLoadFloat3(&Position));
                m_pWheelData[i].Orientation.SetRawValue(XMLoadFloat4(&Orientation));
            }
        }
    }

    // Send world transform to network transform if we're a local network object
    if (IsLocalNetworkObject())
    {
        SetNetworkMatrix(GetWorldTransform(), false);
    }
}

void RenderModel(ModelRenderContext &MRC, Graphics::Model* pModel, const Matrix4& WorldTransform)
{
    GraphicsContext* pContext = MRC.pContext;

    assert(pModel->m_InputLayoutIndex != -1);
    if (MRC.LastInputLayoutIndex != pModel->m_InputLayoutIndex)
    {
        GraphicsPSO* pPso = MRC.pPsoCache->SpecializePso(pModel->m_InputLayoutIndex);
        pContext->SetPipelineState(*pPso);
        MRC.LastInputLayoutIndex = pModel->m_InputLayoutIndex;
    }
    UINT32 LastInputLayoutIndex = -1;

    struct VSConstants
    {
        Matrix4 modelToProjection;
        Matrix4 modelToShadow;
        Matrix4 modelToShadowOuter;
        Matrix4 modelToWorld;
        XMFLOAT3 viewerPos;
    } vsConstants;

    XMVECTOR NewRow3 = XMVectorSelect(g_XMOne, WorldTransform.GetW() - Vector4(MRC.CameraPosition), g_XMSelect1110);
    Matrix4 WT(WorldTransform);
    WT.SetW(Vector4(NewRow3));

    vsConstants.modelToProjection = MRC.ViewProjection * WT;
    vsConstants.modelToShadow = MRC.ModelToShadow * WT;
    vsConstants.modelToShadowOuter = MRC.ModelToShadowOuter * WT;
    vsConstants.modelToWorld = WT;
    XMStoreFloat3(&vsConstants.viewerPos, g_XMZero);

    pContext->SetDynamicConstantBufferView(0, sizeof(vsConstants), &vsConstants);

    uint32_t materialIdx = 0xFFFFFFFFul;

    uint32_t VertexStride = pModel->m_VertexStride;

    pContext->SetIndexBuffer(pModel->m_IndexBuffer.IndexBufferView());
    pContext->SetVertexBuffer(0, pModel->m_VertexBuffer.VertexBufferView());

    for (unsigned int meshIndex = 0; meshIndex < pModel->m_Header.meshCount; meshIndex++)
    {
        const Model::Mesh& mesh = pModel->m_pMesh[meshIndex];

        uint32_t indexCount = mesh.indexCount;
        uint32_t startIndex = mesh.indexDataByteOffset / sizeof(uint16_t);
        uint32_t baseVertex = mesh.vertexDataByteOffset / VertexStride;

        if (mesh.materialIndex != materialIdx)
        {
            materialIdx = mesh.materialIndex;
            pContext->SetDynamicDescriptors(3, 0, 6, pModel->GetSRVs(materialIdx));
        }

        pContext->DrawIndexed(indexCount, startIndex, baseVertex);
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

    Matrix4 WorldTransform = GetScaledWorldTransform();
    Matrix4 RenderOffset(AffineTransform(m_pTemplate->GetRenderOffset()));
    Matrix4 RenderTransform = WorldTransform * RenderOffset;
    RenderModel(MRC, m_pModel, RenderTransform);

    if (m_WheelCount > 0 && m_pTemplate->GetWheelModel() != nullptr)
    {
        Model* pWM = m_pTemplate->GetWheelModel();
        for (UINT32 i = 0; i < m_WheelCount; ++i)
        {
            Matrix4 WheelTransform = GetWheelTransform(i);
            RenderModel(MRC, pWM, WheelTransform);
        }
    }
}

Matrix4 ModelInstance::GetWheelTransform(UINT32 WheelIndex) const
{
    if (WheelIndex >= m_WheelCount)
    {
        return Matrix4();
    }
    return Matrix4(XMLoadFloat4x4(&m_pWheelData[WheelIndex].Transform));
}

void ModelInstance::ServerProcessInput(const NetworkInputState& InputState, FLOAT DeltaTime, DOUBLE AbsoluteTime)
{
    if (m_pVehicle != nullptr)
    {
        float Gas = InputState.RightTrigger;
        float Brake = InputState.LeftTrigger;
        if (m_pVehicle->GetSpeedMSec() < 0.5f && Brake > Gas)
        {
            //Utility::Printf("Reverse drive %0.3f\n", m_pVehicle->GetSpeedMSec());
            Gas = -InputState.LeftTrigger;
            Brake = InputState.RightTrigger;
        }
        if (InputState.Buttons[1])
        {
            Brake = 1.0f;
        }
        m_pVehicle->SetGasAndBrake(Gas, Brake);
        m_pVehicle->SetSteering(-InputState.XAxis0);
    }
}

void World::Initialize(bool GraphicsEnabled, IWorldNotifications* pNotify)
{
    m_GraphicsEnabled = GraphicsEnabled;
    m_pNotify = pNotify;
    m_PhysicsWorld.Initialize(0, XMVectorSet(0, -9.8f, 0, 0));
}

void World::InitializeTerrain(TessellatedTerrain* pTerrain)
{
    m_TerrainPhysicsMap.Initialize(&m_PhysicsWorld, pTerrain, pTerrain->GetWorldScale() * 0.25f);
}

void World::Tick(float deltaT, INT64 Ticks)
{
    auto iter = m_ModelInstances.begin();
    auto end = m_ModelInstances.end();
    while (iter != end)
    {
        ModelInstance* pMI = *iter;
        auto nextiter = iter;
        ++nextiter;
        bool StillAlive = pMI->PrePhysicsUpdate(deltaT, Ticks);
        if (!StillAlive)
        {
            m_ModelInstances.erase(iter);
            if (m_pNotify != nullptr)
            {
                m_pNotify->ModelInstanceDeleted(pMI);
            }
            delete pMI;
        }
        iter = nextiter;
    }

    m_PhysicsWorld.Update(deltaT);

    iter = m_ModelInstances.begin();
    end = m_ModelInstances.end();
    while (iter != end)
    {
        ModelInstance* pMI = *iter++;
        pMI->PostPhysicsUpdate(deltaT);
        if (pMI->IsDynamic())
        {
            m_TerrainPhysicsMap.TrackObject(pMI->GetWorldPosition(), pMI->GetWorldVelocity(), pMI->GetRadius());
        }
    }

    m_TerrainPhysicsMap.Update();
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

    pMI->SetRemote((BOOL)IsRemote);
    pMI->SetWorldTransform(InitialTransform.GetMatrix());

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
    Terminate();
}

void World::Terminate()
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

