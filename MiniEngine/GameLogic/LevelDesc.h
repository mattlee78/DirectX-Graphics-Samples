#pragma once

#include <DirectXMath.h>
#include <unordered_map>
#include "DataFile.h"
#include "StringID.h"

enum PlaceNodeType
{
    PNT_ModelOrNull = 0,
    PNT_Template = 1,
    PNT_Script = 2,
};

struct PlaceNode
{
    void* pGameObject;
    std::vector<PlaceNode*> Children;
    DirectX::XMFLOAT3 Position;
    FLOAT Scale;
    FLOAT RotationYaw;
    FLOAT RotationPitch;
    FLOAT RotationRoll;
    PlaceNodeType Type;
    StringID TemplateName;

    DirectX::XMMATRIX GetLocalTransform() const;
};
STRUCT_TEMPLATE_EXTERNAL(PlaceNode);

struct TemplateDesc
{
    StringID Name;
    std::vector<PlaceNode*> Nodes;
};
STRUCT_TEMPLATE_EXTERNAL(TemplateDesc);

typedef std::unordered_map<const WCHAR*, TemplateDesc*> TemplateDescMap;

struct LevelDesc
{
    DirectX::XMFLOAT3 GravityVector;
    StringID ScriptFileName;
    std::vector<PlaceNode*> Nodes;
    std::vector<TemplateDesc*> Templates;
};
STRUCT_TEMPLATE_EXTERNAL(LevelDesc);
