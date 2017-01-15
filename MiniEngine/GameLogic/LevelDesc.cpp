#include "LevelDesc.h"
#include "VectorMath.h"

using namespace DirectX;
using namespace Math;

static const DataMemberEnum g_PlaceNodeTypeEnum[] =
{
    { L"Model", PNT_ModelOrNull },
    { L"Null", PNT_ModelOrNull },
    { L"Template", PNT_Template },
    { L"Script", PNT_Script },
    MEMBER_ENUM_TERMINATOR
};

STRUCT_TEMPLATE_START_INLINE(PlaceNode, nullptr, nullptr)
MEMBER_PADDING_POINTER()
MEMBER_STRUCT_STL_POINTER_VECTOR(Children, PlaceNode)
MEMBER_VECTOR3(Position)
MEMBER_FLOAT(Scale)
MEMBER_FLOAT(RotationYaw)
MEMBER_FLOAT(RotationPitch)
MEMBER_FLOAT(RotationRoll)
MEMBER_ENUM(Type, g_PlaceNodeTypeEnum)
MEMBER_STRINGID(TemplateName)
STRUCT_TEMPLATE_END(PlaceNode)

STRUCT_TEMPLATE_START_INLINE(TemplateDesc, nullptr, nullptr)
MEMBER_STRINGID(Name)
MEMBER_STRUCT_STL_POINTER_VECTOR(Nodes, PlaceNode)
STRUCT_TEMPLATE_END(TemplateDesc)

STRUCT_TEMPLATE_START_FILE(LevelDesc, nullptr, nullptr)
MEMBER_VECTOR3(GravityVector)
MEMBER_STRINGID(ScriptFileName)
MEMBER_STRUCT_STL_POINTER_VECTOR(Nodes, PlaceNode)
MEMBER_STRUCT_STL_POINTER_VECTOR(Templates, TemplateDesc)
STRUCT_TEMPLATE_END(LevelDesc)

XMMATRIX PlaceNode::GetLocalTransform() const
{
    const FLOAT DegToRad = XM_PI / 180.0f;

    XMVECTOR Orientation = XMQuaternionRotationRollPitchYaw(RotationPitch * DegToRad, RotationYaw * DegToRad, RotationRoll * DegToRad);

    Matrix4 matLocal;
    matLocal.Compose(Vector3(XMLoadFloat3(&Position)), Scale, Vector4(Orientation));

    return matLocal;
}
