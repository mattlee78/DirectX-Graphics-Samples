#include "ModelTemplate.h"
#include "Model.h"
#include "BulletPhysics.h"

using namespace Graphics;

STRUCT_TEMPLATE_START_INLINE(ModelPlaneShape, nullptr, nullptr)
MEMBER_VECTOR4(PlaneVector)
STRUCT_TEMPLATE_END(ModelPlaneShape)

STRUCT_TEMPLATE_START_INLINE(ModelAABBShape, nullptr, nullptr)
MEMBER_VECTOR3(Center)
MEMBER_VECTOR3(HalfSize)
STRUCT_TEMPLATE_END(ModelAABBShape)

STRUCT_TEMPLATE_START_INLINE(ModelOrientedBoxShape, nullptr, nullptr)
MEMBER_VECTOR3(Center)
MEMBER_VECTOR3(HalfSize)
MEMBER_VECTOR4(Orientation)
STRUCT_TEMPLATE_END(ModelOrientedBoxShape)

STRUCT_TEMPLATE_START_INLINE(ModelSphereShape, nullptr, nullptr)
MEMBER_VECTOR3(Center)
MEMBER_FLOAT(Radius)
STRUCT_TEMPLATE_END(ModelSphereShape)

STRUCT_TEMPLATE_START_INLINE(ModelCapsuleShape, nullptr, nullptr)
MEMBER_VECTOR3(Center)
MEMBER_FLOAT(Radius)
MEMBER_FLOAT(Height)
STRUCT_TEMPLATE_END(ModelCapsuleShape)

STRUCT_TEMPLATE_START_INLINE(CollisionMeshDefinition, nullptr, nullptr)
MEMBER_PADDING_POINTER()
MEMBER_PADDING_POINTER()
MEMBER_UINT(Reserved0)
MEMBER_UINT(Reserved1)
STRUCT_TEMPLATE_END(CollisionMeshDefinition)

static const DataMemberEnum g_ModelShapeTypeEnum[] =
{
    { L"Null", ModelShapeType_Null },
    { L"Plane", ModelShapeType_Plane },
    { L"AABB", ModelShapeType_AABB },
    { L"OrientedBox", ModelShapeType_OrientedBox },
    { L"Sphere", ModelShapeType_Sphere },
    { L"ConvexMesh", ModelShapeType_ConvexMesh },
    { L"PolyMesh", ModelShapeType_PolyMesh },
    { L"Capsule", ModelShapeType_Capsule },
    MEMBER_ENUM_TERMINATOR
};

STRUCT_TEMPLATE_START_INLINE(ModelShapeData, nullptr, nullptr)
MEMBER_ENUM(Type, g_ModelShapeTypeEnum)
MEMBER_STRING(MeshName)
MEMBER_NAMELESS_UNION
MEMBER_STRUCT_VALUE(PlaneShape, ModelPlaneShape)
MEMBER_STRUCT_VALUE(AABBShape, ModelAABBShape)
MEMBER_STRUCT_VALUE(OrientedBoxShape, ModelOrientedBoxShape)
MEMBER_STRUCT_VALUE(SphereShape, ModelSphereShape)
MEMBER_STRUCT_VALUE(CapsuleShape, ModelCapsuleShape)
MEMBER_NAMELESS_UNION_END
STRUCT_TEMPLATE_END(ModelShapeData)

STRUCT_TEMPLATE_START_INLINE(ModelRigidBodyDesc, nullptr, nullptr)
MEMBER_FLOAT(Mass)
MEMBER_STRUCT_VALUE(Shape, ModelShapeData)
MEMBER_STRUCT_POINTER(VehicleConfig, VehicleConfig)
STRUCT_TEMPLATE_END(ModelRigidBodyDesc)

STRUCT_TEMPLATE_START_FILE(ModelDesc, nullptr, nullptr)
MEMBER_STRING(ModelFileName)
MEMBER_STRUCT_POINTER(RigidBody, ModelRigidBodyDesc)
MEMBER_BOOL(NoRenderInShadowPass)
MEMBER_VECTOR3(RenderOffset)
MEMBER_STRING(WheelModelFileName)
STRUCT_TEMPLATE_END(ModelDesc)

ModelTemplate::~ModelTemplate()
{
    if (m_pModel != nullptr)
    {
        delete m_pModel;
        m_pModel = nullptr;
    }
    if (m_pWheelModel != nullptr)
    {
        delete m_pWheelModel;
        m_pWheelModel = nullptr;
    }
    if (m_pDesc != nullptr)
    {
        if (m_DescIsLocallyAllocated)
        {
            delete m_pDesc->pRigidBody;
            delete m_pDesc;
            m_DescIsLocallyAllocated = 0;
        }
        else
        {
//             if (m_pDesc->pRigidBody != nullptr)
//             {
//                 DataFile::Unload(m_pDesc->pRigidBody);
//             }
//             DataFile::Unload(m_pDesc);            
        }
        m_pDesc = nullptr;
    }
}

FLOAT ModelTemplate::GetMass() const
{
    if (m_pDesc != nullptr && m_pDesc->pRigidBody != nullptr)
    {
        return m_pDesc->pRigidBody->Mass;
    }
    return 0;
}

ModelTemplate* ModelTemplate::Load(const CHAR* strName, bool GraphicsEnabled)
{
    Model* pModel = nullptr;
    CollisionShape* pShape = nullptr;
    bool Success = false;

    ModelTemplate* pMT = new ModelTemplate();

    if (pMT == nullptr)
    {
        return nullptr;
    }

    ModelDesc* pMD = (ModelDesc*)DataFile::LoadStructFromFile(STRUCT_TEMPLATE_REFERENCE(ModelDesc), strName, nullptr);
    if (pMD == nullptr)
    {
        if (strName[0] == '*')
        {
            Success = pMT->CreateDefaultTemplate(strName + 1, GraphicsEnabled);
        }
        else
        {
            Success = !GraphicsEnabled || pMT->CreateMeshOnlyTemplate(strName);
        }
    }
    else
    {
        Success = pMT->CreateDescTemplate(pMD, GraphicsEnabled);
    }

    if (!Success)
    {
        delete pMD;
        delete pMT;
        pMT = nullptr;
    }
    else
    {
        pMT->m_Name.SetAnsi(strName);
    }

    return pMT;
}

bool ModelTemplate::CreateDefaultTemplate(const CHAR* strName, bool GraphicsEnabled)
{
    ModelDesc* pMD = new ModelDesc();
    ZeroMemory(pMD, sizeof(ModelDesc));
    pMD->pRigidBody = new ModelRigidBodyDesc();
    ZeroMemory(pMD->pRigidBody, sizeof(ModelRigidBodyDesc));
    m_DescIsLocallyAllocated = 1;

    bool ModelSuccess = !GraphicsEnabled;
    Model* pModel = nullptr;
    if (GraphicsEnabled)
    {
        pModel = new Model();
    }

    if (_stricmp(strName, "cube") == 0)
    {
        Vector3 HalfDimensions(5, 5, 5);
        pMD->pRigidBody->Shape.ShapeType = ModelShapeType_AABB;
        XMStoreFloat3(&pMD->pRigidBody->Shape.AABBShape().HalfSize, HalfDimensions);
        pMD->pRigidBody->Mass = 1.0f;
        if (GraphicsEnabled)
        {
            ModelSuccess = pModel->CreateCube(HalfDimensions);
        }
    }
    else if (_stricmp(strName, "plane") == 0)
    {
        pMD->pRigidBody->Shape.ShapeType = ModelShapeType_Plane;
        XMStoreFloat4(&pMD->pRigidBody->Shape.PlaneShape().PlaneVector, g_XMIdentityR1);
        pMD->pRigidBody->Mass = 0.0f;
        if (GraphicsEnabled)
        {
            ModelSuccess = pModel->CreateXZPlane(Vector3(100, 0, 100), Vector3(20, 20, 0));
            pMD->NoRenderInShadowPass = TRUE;
        }
    }
    else
    {
        delete pMD->pRigidBody;
        delete pMD;
        pMD = nullptr;
    }

    if (!ModelSuccess)
    {
        delete pModel;
        pModel = nullptr;
    }

    if (pMD != nullptr)
    {
        m_pDesc = pMD;
        m_pModel = pModel;
        return true;
    }

    return false;
}

bool ModelTemplate::CreateMeshOnlyTemplate(const CHAR* strName)
{
    Model* pModel = new Model();
    if (pModel == nullptr)
    {
        return false;
    }

    if (!pModel->Load(strName))
    {
        delete pModel;
        return false;
    }

    m_pDesc = new ModelDesc();
    ZeroMemory(m_pDesc, sizeof(ModelDesc));
    m_Name.SetAnsi(strName);

    m_pModel = pModel;
    return true;
}

bool ModelTemplate::CreateDescTemplate(ModelDesc* pMD, bool GraphicsEnabled)
{
    m_pDesc = pMD;
    bool Result = true;
    if (GraphicsEnabled)
    {
        if (m_pDesc->strModelFileName != nullptr)
        {
            m_pModel = new Model();
            if (!m_pModel->Load(m_pDesc->strModelFileName))
            {
                delete m_pModel;
                m_pModel = nullptr;
                Result = false;
            }
        }
        if (m_pDesc->strWheelModelFileName != nullptr)
        {
            m_pWheelModel = new Model();
            if (!m_pWheelModel->Load(m_pDesc->strWheelModelFileName))
            {
                delete m_pWheelModel;
                m_pWheelModel = nullptr;
                Result = false;
            }
        }
    }
    if (pMD->pRigidBody != nullptr)
    {
        const ModelShapeData& SD = pMD->pRigidBody->Shape;
        if (SD.ShapeType == ModelShapeType_ConvexMesh || SD.ShapeType == ModelShapeType_PolyMesh)
        {
            m_pCollisionMesh = new CollisionMesh();
            if (!m_pCollisionMesh->Load(SD.strMeshName, SD.ShapeType == ModelShapeType_ConvexMesh))
            {
                delete m_pCollisionMesh;
                m_pCollisionMesh = nullptr;
                Result = false;
            }
        }
    }
    return Result;
}

CollisionShape* ModelTemplate::GetCollisionShape(FLOAT Scale)
{
    if (m_pDesc == nullptr || m_pDesc->pRigidBody == nullptr)
    {
        return nullptr;
    }

    const ModelShapeData& ShapeData = m_pDesc->pRigidBody->Shape;
    if (ShapeData.ShapeType == ModelShapeType_Null)
    {
        return nullptr;
    }

    const XMVECTOR vScale = XMVectorReplicate(Scale);

    CollisionShape* pShape = nullptr;
    XMVECTOR Offset = g_XMZero;
    XMVECTOR Orientation = g_XMIdentityR3;
    switch (ShapeData.ShapeType)
    {
    case ModelShapeType_AABB:
    {
        const ModelAABBShape& AABB = ShapeData.AABBShape();
        pShape = CollisionShape::CreateBox(XMLoadFloat3(&AABB.HalfSize) * vScale);
        Offset = XMLoadFloat3(&AABB.Center) * vScale;
        break;
    }
    case ModelShapeType_Sphere:
    {
        const ModelSphereShape& Sphere = ShapeData.SphereShape();
        pShape = CollisionShape::CreateSphere(Sphere.Radius * Scale);
        Offset = XMLoadFloat3(&Sphere.Center) * vScale;
        break;
    }
    case ModelShapeType_OrientedBox:
    {
        const ModelOrientedBoxShape& OBB = ShapeData.OrientedBoxShape();
        pShape = CollisionShape::CreateBox(XMLoadFloat3(&OBB.HalfSize) * vScale);
        Offset = XMLoadFloat3(&OBB.Center) * vScale;
        Orientation = XMLoadFloat4(&OBB.Orientation);
        break;
    }
    case ModelShapeType_Plane:
    {
        const ModelPlaneShape& Plane = ShapeData.PlaneShape();
        pShape = CollisionShape::CreatePlane(XMLoadFloat4(&Plane.PlaneVector));
        break;
    }
    case ModelShapeType_ConvexMesh:
    {
        if (m_pCollisionMesh != nullptr)
        {
            assert(m_pCollisionMesh->pIndexData == nullptr && m_pCollisionMesh->pVertexData != nullptr);
            pShape = CollisionShape::CreateConvexHull((const XMFLOAT3*)m_pCollisionMesh->pVertexData, m_pCollisionMesh->VertexCount, m_pCollisionMesh->VertexStrideBytes);
        }
        break;
    }
    default:
        assert(FALSE);
        break;
    }

    if (pShape != nullptr)
    {
        XMMATRIX matRotation = XMMatrixRotationQuaternion(Orientation);
        matRotation.r[3] = XMVectorSelect(g_XMOne, Offset, g_XMSelect1110);
        CompoundCollisionShape* pCompound = new CompoundCollisionShape();
        pCompound->AddShape(pShape, matRotation);
        return pCompound;
    }

    return nullptr;
}
