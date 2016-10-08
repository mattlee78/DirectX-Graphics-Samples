#pragma once

#include "VectorMath.h"
#include "CommandContext.h"
#include <unordered_map>
#include "BulletPhysics.h"
#include "Network\NetworkTransform.h"
#include "ModelTemplate.h"

namespace Graphics
{
    class Model;
}
class Vehicle;

enum RenderPass
{
    RenderPass_ZPrePass = 0,
    RenderPass_Shadow = 1,
    RenderPass_Color = 2,
};

struct ModelRenderContext
{
    Math::Matrix4 ModelToShadow;
    Math::Matrix4 ViewProjection;
    Math::Vector3 CameraPosition;

    GraphicsContext* pContext;
    PsoLayoutCache* pPsoCache;
    UINT32 LastInputLayoutIndex;

    RenderPass CurrentPassType;
};

struct DecomposedTransform
{
    DirectX::XMFLOAT4 PositionScale;
    DirectX::XMFLOAT4 Orientation;

    DecomposedTransform()
        : PositionScale(0, 0, 0, 1),
          Orientation(0, 0, 0, 1)
    { }

    static DecomposedTransform CreateFromComponents(XMFLOAT3 InPosition, XMFLOAT4 InOrientation = XMFLOAT4(0, 0, 0, 1), FLOAT InScale = 1.0f)
    {
        DecomposedTransform dt;
        dt.PositionScale.x = InPosition.x;
        dt.PositionScale.y = InPosition.y;
        dt.PositionScale.z = InPosition.z;
        dt.PositionScale.w = InScale;
        dt.Orientation = InOrientation;
        return dt;
    }

    Math::Matrix4 GetMatrix() const
    {
        Math::Matrix4 m;
        m.Compose(Math::Vector4(XMLoadFloat4(&PositionScale)), Math::Vector4(XMLoadFloat4(&Orientation)));
        return m;
    }
};

class ModelInstance : public NetworkTransform
{
private:
    friend class World;

    DirectX::XMFLOAT4X4 m_WorldTransform;

    bool m_RenderInShadowPass : 1;

    ModelTemplate* m_pTemplate;
    Graphics::Model* m_pModel;
    RigidBody* m_pRigidBody;
    CollisionShape* m_pCollisionShape;
    Vehicle* m_pVehicle;

public:
    ModelInstance()
        : m_pModel(nullptr),
          m_pRigidBody(nullptr),
          m_pCollisionShape(nullptr),
          m_pVehicle(nullptr)
    { 
        XMStoreFloat4x4(&m_WorldTransform, XMMatrixIdentity());
        m_RenderInShadowPass = true;
    }

    ~ModelInstance();

    bool Initialize(World* pWorld, ModelTemplate* pTemplate, bool GraphicsEnabled, bool IsRemote);

    const Graphics::Model* GetModel() const { return m_pModel; }
    RigidBody* GetRigidBody() { return m_pRigidBody; }

    bool IsRemoteNetworkObject() const;
    bool IsLocalNetworkObject() const;

    void SetWorldTransform(const Math::Matrix4& Transform);
    Math::Matrix4 GetWorldTransform() const { return Math::Matrix4(XMLoadFloat4x4(&m_WorldTransform)); }
    Math::Vector3 GetWorldPosition() const { return Math::Vector3(XMLoadFloat3((XMFLOAT3*)&m_WorldTransform._41)); }

    void PrePhysicsUpdate(float deltaT);
    void PostPhysicsUpdate(float deltaT);

    void Render(ModelRenderContext& MRC) const;
};

typedef std::unordered_map<UINT32, ModelInstance*> ModelInstanceMap;
typedef std::unordered_set<ModelInstance*> ModelInstanceSet;

class World
{
private:
    PhysicsWorld m_PhysicsWorld;
    ModelInstanceSet m_ModelInstances;
    bool m_GraphicsEnabled;

    ModelTemplateMap m_ModelTemplates;

public:
    virtual ~World();

    void Initialize(bool GraphicsEnabled = true);
    void Tick(float deltaT);
    void Render(ModelRenderContext& MRC);

    ModelInstance* SpawnModelInstance(const CHAR* strTemplateName, const CHAR* strInstanceName, const DecomposedTransform& InitialTransform, bool IsRemote = false);
    ModelInstance* SpawnModelInstance(ModelTemplate* pTemplate, const CHAR* strInstanceName, const DecomposedTransform& InitialTransform, bool IsRemote = false);

    PhysicsWorld* GetPhysicsWorld() { return &m_PhysicsWorld; }

    ModelTemplate* FindOrCreateModelTemplate(const CHAR* strTemplateName);
};
