#pragma once

#include "DataFile.h"
#include "StringID.h"
#include <unordered_map>
#include <DirectXMath.h>

namespace Graphics
{
    class Model;
}
class CollisionShape;

enum ModelShapeType
{
    ModelShapeType_Null,
    ModelShapeType_Plane,
    ModelShapeType_AABB,
    ModelShapeType_OrientedBox,
    ModelShapeType_Sphere,
    ModelShapeType_ConvexMesh,
    ModelShapeType_PolyMesh,
    ModelShapeType_Capsule,
};

struct ModelNullShape
{
    FLOAT Data[10];
};

struct ModelPlaneShape
{
    DirectX::XMFLOAT4 PlaneVector;
};
C_ASSERT(sizeof(ModelPlaneShape) <= sizeof(ModelNullShape));
STRUCT_TEMPLATE_EXTERNAL(ModelPlaneShape);

struct ModelAABBShape
{
    DirectX::XMFLOAT3 Center;
    DirectX::XMFLOAT3 HalfSize;
};
C_ASSERT(sizeof(ModelAABBShape) <= sizeof(ModelNullShape));
STRUCT_TEMPLATE_EXTERNAL(ModelAABBShape);

struct ModelOrientedBoxShape
{
    DirectX::XMFLOAT3 Center;
    DirectX::XMFLOAT3 HalfSize;
    DirectX::XMFLOAT4 Orientation;
};
C_ASSERT(sizeof(ModelOrientedBoxShape) <= sizeof(ModelNullShape));
STRUCT_TEMPLATE_EXTERNAL(ModelOrientedBoxShape);

struct ModelSphereShape
{
    DirectX::XMFLOAT3 Center;
    FLOAT Radius;
};
C_ASSERT(sizeof(ModelSphereShape) <= sizeof(ModelNullShape));
STRUCT_TEMPLATE_EXTERNAL(ModelSphereShape);

struct ModelCapsuleShape
{
    DirectX::XMFLOAT3 Center;
    FLOAT Radius;
    FLOAT Height;
};
C_ASSERT(sizeof(ModelCapsuleShape) <= sizeof(ModelNullShape));
STRUCT_TEMPLATE_EXTERNAL(ModelCapsuleShape);

struct ModelShapeData
{
    ModelShapeType ShapeType;
    const CHAR* strMeshName;
    ModelNullShape NullShape;

    ModelPlaneShape& PlaneShape() { return *(ModelPlaneShape*)&NullShape; }
    const ModelPlaneShape& PlaneShape() const { return *(ModelPlaneShape*)&NullShape; }
    ModelAABBShape& AABBShape() { return *(ModelAABBShape*)&NullShape; }
    const ModelAABBShape& AABBShape() const { return *(ModelAABBShape*)&NullShape; }
    ModelOrientedBoxShape& OrientedBoxShape() { return *(ModelOrientedBoxShape*)&NullShape; }
    const ModelOrientedBoxShape& OrientedBoxShape() const { return *(ModelOrientedBoxShape*)&NullShape; }
    ModelSphereShape& SphereShape() { return *(ModelSphereShape*)&NullShape; }
    const ModelSphereShape& SphereShape() const { return *(ModelSphereShape*)&NullShape; }
    ModelCapsuleShape& CapsuleShape() { return *(ModelCapsuleShape*)&NullShape; }
    const ModelCapsuleShape& CapsuleShape() const { return *(ModelCapsuleShape*)&NullShape; }
};
STRUCT_TEMPLATE_EXTERNAL(ModelShapeData);

struct ModelRigidBodyDesc
{
    FLOAT Mass;
    ModelShapeData Shape;
};
STRUCT_TEMPLATE_EXTERNAL(ModelRigidBodyDesc);

struct ModelDesc
{
    const CHAR* strModelFileName;
    ModelRigidBodyDesc* pRigidBody;
    BOOL NoRenderInShadowPass;
};
STRUCT_TEMPLATE_EXTERNAL(ModelDesc);

class ModelTemplate
{
private:
    StringID m_Name;
    ModelDesc* m_pDesc;
    Graphics::Model* m_pModel;

    UINT32 m_DescIsLocallyAllocated : 1;

public:
    ModelTemplate()
        : m_pDesc(nullptr),
          m_pModel(nullptr)
    { 
        m_DescIsLocallyAllocated = 0;
    }
    ~ModelTemplate();

    StringID GetName() const { return m_Name; }

    static ModelTemplate* Load(const CHAR* strName, bool GraphicsEnabled);

    Graphics::Model* GetModel() { return m_pModel; }
    CollisionShape* GetCollisionShape(FLOAT Scale);
    FLOAT GetMass() const;

private:
    bool CreateDefaultTemplate(const CHAR* strName, bool GraphicsEnabled);
    bool CreateMeshOnlyTemplate(const CHAR* strName);
    bool CreateDescTemplate(ModelDesc* pMD, bool GraphicsEnabled);

    void CreateCollisionShape();
};

typedef std::unordered_map<const WCHAR*, ModelTemplate*> ModelTemplateMap;
