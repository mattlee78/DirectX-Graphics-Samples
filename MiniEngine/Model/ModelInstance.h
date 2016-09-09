#pragma once

#include "VectorMath.h"
#include "CommandContext.h"
#include <unordered_map>

namespace Graphics
{
    class Model;
}
class RigidBody;
class Vehicle;

struct ModelRenderContext
{
    Math::Matrix4 ModelToShadow;
    Math::Matrix4 ViewProjection;
    Math::Vector3 CameraPosition;

    GraphicsContext* pContext;
};

class ModelInstance
{
private:
    DirectX::XMFLOAT4X4 m_WorldTransform;
    Graphics::Model* m_pModel;
    RigidBody* m_pRigidBody;
    Vehicle* m_pVehicle;

public:
    ModelInstance()
        : m_pModel(nullptr),
          m_pRigidBody(nullptr),
          m_pVehicle(nullptr)
    { 
        XMStoreFloat4x4(&m_WorldTransform, XMMatrixIdentity());
    }

    ~ModelInstance();

    bool InitializeModel(const CHAR* strFileName);

    const Graphics::Model* GetModel() const { return m_pModel; }
    const RigidBody* GetRigidBody() const { return m_pRigidBody; }

    void SetWorldTransform(const Math::Matrix4& Transform);
    Math::Matrix4 GetWorldTransform() const { return Math::Matrix4(XMLoadFloat4x4(&m_WorldTransform)); }
    Math::Vector3 GetWorldPosition() const { return Math::Vector3(XMLoadFloat3((XMFLOAT3*)&m_WorldTransform._41)); }

    void PrePhysicsUpdate(float deltaT);
    void PostPhysicsUpdate(float deltaT);

    void Render(const ModelRenderContext& MRC) const;
};

typedef std::unordered_map<UINT32, ModelInstance*> ModelInstanceMap;
