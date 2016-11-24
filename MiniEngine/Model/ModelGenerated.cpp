#include "pch.h"
#include "Model.h"

using namespace Graphics;

struct MeshVertex
{
    XMFLOAT3 Position;
    XMFLOAT2 TexCoord0;
    XMFLOAT3 Normal;
    XMFLOAT3 Tangent;
    XMFLOAT3 Bitangent;
};

void Model::CreateCommon(uint32_t VertexCount, uint32_t IndexCount)
{
    Clear();
    ReleaseTextures();

    m_Header.meshCount = 1;
    m_Header.materialCount = 1;
    m_pMesh = new Mesh();
    ZeroMemory(m_pMesh, sizeof(Mesh));
    m_pMaterial = new Material();
    ZeroMemory(m_pMaterial, sizeof(Material));

    m_VertexStride = sizeof(MeshVertex);
    m_pMesh->vertexStride = m_VertexStride;

    m_Header.vertexDataByteSize = m_VertexStride * VertexCount;
    m_pVertexData = new unsigned char[m_Header.vertexDataByteSize];
    m_Header.indexDataByteSize = IndexCount * 2;
    m_pIndexData = new unsigned char[m_Header.indexDataByteSize];

    m_pMesh->vertexCount = VertexCount;
    m_pMesh->indexCount = IndexCount;

    m_pMesh->attribsEnabled =
        (attrib_mask_position | attrib_mask_texcoord0 | attrib_mask_normal | attrib_mask_tangent | attrib_mask_bitangent);

    m_pMesh->attrib[0].components = 3; m_pMesh->attrib[0].format = Model::attrib_format_float; // position
    m_pMesh->attrib[1].components = 2; m_pMesh->attrib[1].format = Model::attrib_format_float; // texcoord0
    m_pMesh->attrib[2].components = 3; m_pMesh->attrib[2].format = Model::attrib_format_float; // normal
    m_pMesh->attrib[3].components = 3; m_pMesh->attrib[3].format = Model::attrib_format_float; // tangent
    m_pMesh->attrib[4].components = 3; m_pMesh->attrib[4].format = Model::attrib_format_float; // bitangent
    m_pMesh->attrib[0].offset = offsetof(MeshVertex, Position);
    m_pMesh->attrib[1].offset = offsetof(MeshVertex, TexCoord0);
    m_pMesh->attrib[2].offset = offsetof(MeshVertex, Normal);
    m_pMesh->attrib[3].offset = offsetof(MeshVertex, Tangent);
    m_pMesh->attrib[4].offset = offsetof(MeshVertex, Bitangent);

    m_pMesh->materialIndex = 0;
}

void Model::CompleteCommon(const WCHAR* strName)
{
    ComputeMeshBoundingBox(0, m_pMesh->boundingBox);
    ComputeGlobalBoundingBox(m_Header.boundingBox);

    WCHAR strResourceName[128];
    swprintf_s(strResourceName, L"VertexBuffer %s", strName);
    m_VertexBuffer.Create(strResourceName, m_Header.vertexDataByteSize / m_VertexStride, m_VertexStride, m_pVertexData);
    swprintf_s(strResourceName, L"IndexBuffer %s", strName);
    m_IndexBuffer.Create(strResourceName, m_Header.indexDataByteSize / 2, 2, m_pIndexData);

    m_InputLayoutIndex = 0;

    delete[] m_pVertexData;
    m_pVertexData = nullptr;
    delete[] m_pIndexData;
    m_pIndexData = nullptr;

    const ManagedTexture* MatTextures[6] = {};
    MatTextures[0] = TextureManager::LoadFromFile("terraindirt_a", true);
    MatTextures[1] = TextureManager::LoadFromFile("default_specular", true);
    MatTextures[3] = TextureManager::LoadFromFile("default_normal", false);

    m_SRVs = new D3D12_CPU_DESCRIPTOR_HANDLE[6];
    m_SRVs[0] = MatTextures[0]->GetSRV();
    m_SRVs[1] = MatTextures[1]->GetSRV();
    m_SRVs[2] = MatTextures[0]->GetSRV();
    m_SRVs[3] = MatTextures[3]->GetSRV();
    m_SRVs[4] = MatTextures[0]->GetSRV();
    m_SRVs[5] = MatTextures[0]->GetSRV();
}

inline void FillVector3(XMFLOAT3* pDest, size_t StrideBytes, uint32_t Count, const XMFLOAT3& Value)
{
    for (uint32_t i = 0; i < Count; ++i)
    {
        *pDest = Value;
        pDest = (XMFLOAT3*)((BYTE*)pDest + StrideBytes);
    }
}

bool Model::CreateCube(Vector3 HalfDimensions, bool UVScaled)
{
    CreateCommon(6 * 4, 6 * 6);

    FLOAT UVScaleX = 1.0f;
    FLOAT UVScaleY = 1.0f;
    FLOAT UVScaleZ = 1.0f;
    if (UVScaled)
    {
        UVScaleX = HalfDimensions.GetX() * 2.0f;
        UVScaleY = HalfDimensions.GetY() * 2.0f;
        UVScaleZ = HalfDimensions.GetZ() * 2.0f;
    }

    MeshVertex* pVerts = (MeshVertex*)m_pVertexData;
    USHORT* pIndices = (USHORT*)m_pIndexData;

    // XY -Z
    pVerts[ 0].Position = XMFLOAT3(0, 0, 0); pVerts[ 0].TexCoord0 = XMFLOAT2(0, UVScaleY);
    pVerts[ 1].Position = XMFLOAT3(0, 1, 0); pVerts[ 1].TexCoord0 = XMFLOAT2(0, 0);
    pVerts[ 2].Position = XMFLOAT3(1, 1, 0); pVerts[ 2].TexCoord0 = XMFLOAT2(UVScaleX, 0);
    pVerts[ 3].Position = XMFLOAT3(1, 0, 0); pVerts[ 3].TexCoord0 = XMFLOAT2(UVScaleX, UVScaleY);
    FillVector3(&pVerts[0].Normal,    sizeof(MeshVertex), 4, XMFLOAT3(0, 0, -1));
    FillVector3(&pVerts[0].Tangent,   sizeof(MeshVertex), 4, XMFLOAT3(1, 0, 0));
    FillVector3(&pVerts[0].Bitangent, sizeof(MeshVertex), 4, XMFLOAT3(0, -1, 0));

    // XY +Z
    pVerts[ 4].Position = XMFLOAT3(1, 0, 1); pVerts[ 4].TexCoord0 = XMFLOAT2(0, UVScaleY);
    pVerts[ 5].Position = XMFLOAT3(1, 1, 1); pVerts[ 5].TexCoord0 = XMFLOAT2(0, 0);
    pVerts[ 6].Position = XMFLOAT3(0, 1, 1); pVerts[ 6].TexCoord0 = XMFLOAT2(UVScaleX, 0);
    pVerts[ 7].Position = XMFLOAT3(0, 0, 1); pVerts[ 7].TexCoord0 = XMFLOAT2(UVScaleX, UVScaleY);
    FillVector3(&pVerts[4].Normal,    sizeof(MeshVertex), 4, XMFLOAT3(0, 0, 1));
    FillVector3(&pVerts[4].Tangent,   sizeof(MeshVertex), 4, XMFLOAT3(-1, 0, 0));
    FillVector3(&pVerts[4].Bitangent, sizeof(MeshVertex), 4, XMFLOAT3(0, -1, 0));

    // YZ -X
    pVerts[ 8].Position = XMFLOAT3(0, 0, 1); pVerts[ 8].TexCoord0 = XMFLOAT2(0, UVScaleY);
    pVerts[ 9].Position = XMFLOAT3(0, 1, 1); pVerts[ 9].TexCoord0 = XMFLOAT2(0, 0);
    pVerts[10].Position = XMFLOAT3(0, 1, 0); pVerts[10].TexCoord0 = XMFLOAT2(UVScaleZ, 0);
    pVerts[11].Position = XMFLOAT3(0, 0, 0); pVerts[11].TexCoord0 = XMFLOAT2(UVScaleZ, UVScaleY);
    FillVector3(&pVerts[8].Normal,    sizeof(MeshVertex), 4, XMFLOAT3(-1, 0, 0));
    FillVector3(&pVerts[8].Tangent,   sizeof(MeshVertex), 4, XMFLOAT3(0, 0, -1));
    FillVector3(&pVerts[8].Bitangent, sizeof(MeshVertex), 4, XMFLOAT3(0, -1, 0));

    // YZ +X
    pVerts[12].Position = XMFLOAT3(1, 0, 0); pVerts[12].TexCoord0 = XMFLOAT2(0, UVScaleY);
    pVerts[13].Position = XMFLOAT3(1, 1, 0); pVerts[13].TexCoord0 = XMFLOAT2(0, 0);
    pVerts[14].Position = XMFLOAT3(1, 1, 1); pVerts[14].TexCoord0 = XMFLOAT2(UVScaleZ, 0);
    pVerts[15].Position = XMFLOAT3(1, 0, 1); pVerts[15].TexCoord0 = XMFLOAT2(UVScaleZ, UVScaleY);
    FillVector3(&pVerts[12].Normal,    sizeof(MeshVertex), 4, XMFLOAT3(1, 0, 0));
    FillVector3(&pVerts[12].Tangent,   sizeof(MeshVertex), 4, XMFLOAT3(0, 0, 1));
    FillVector3(&pVerts[12].Bitangent, sizeof(MeshVertex), 4, XMFLOAT3(0, -1, 0));

    // XZ +Y
    pVerts[16].Position = XMFLOAT3(0, 1, 0); pVerts[16].TexCoord0 = XMFLOAT2(0, UVScaleZ);
    pVerts[17].Position = XMFLOAT3(0, 1, 1); pVerts[17].TexCoord0 = XMFLOAT2(0, 0);
    pVerts[18].Position = XMFLOAT3(1, 1, 1); pVerts[18].TexCoord0 = XMFLOAT2(UVScaleX, 0);
    pVerts[19].Position = XMFLOAT3(1, 1, 0); pVerts[19].TexCoord0 = XMFLOAT2(UVScaleX, UVScaleZ);
    FillVector3(&pVerts[16].Normal,    sizeof(MeshVertex), 4, XMFLOAT3(0, 1, 0));
    FillVector3(&pVerts[16].Tangent,   sizeof(MeshVertex), 4, XMFLOAT3(1, 0, 0));
    FillVector3(&pVerts[16].Bitangent, sizeof(MeshVertex), 4, XMFLOAT3(0, 0, -1));

    // XZ -Y
    pVerts[20].Position = XMFLOAT3(1, 0, 0); pVerts[20].TexCoord0 = XMFLOAT2(0, UVScaleZ);
    pVerts[21].Position = XMFLOAT3(1, 0, 1); pVerts[21].TexCoord0 = XMFLOAT2(0, 0);
    pVerts[22].Position = XMFLOAT3(0, 0, 1); pVerts[22].TexCoord0 = XMFLOAT2(UVScaleX, 0);
    pVerts[23].Position = XMFLOAT3(0, 0, 0); pVerts[23].TexCoord0 = XMFLOAT2(UVScaleX, UVScaleZ);
    FillVector3(&pVerts[20].Normal,    sizeof(MeshVertex), 4, XMFLOAT3(0, -1, 0));
    FillVector3(&pVerts[20].Tangent,   sizeof(MeshVertex), 4, XMFLOAT3(-1, 0, 0));
    FillVector3(&pVerts[20].Bitangent, sizeof(MeshVertex), 4, XMFLOAT3(0, 0, -1));

    for (UINT32 i = 0; i < 24; ++i)
    {
        XMVECTOR Pos = XMLoadFloat3(&pVerts[i].Position);
        Pos *= (HalfDimensions * g_XMTwo);
        Pos -= HalfDimensions;
        XMStoreFloat3(&pVerts[i].Position, Pos);
    }

    const USHORT Indices[] = {
        0, 1, 3, 3, 1, 2,
        4, 5, 7, 7, 5, 6,
        8, 9, 11, 11, 9, 10,
        12, 13, 15, 15, 13, 14,
        16, 17, 19, 19, 17, 18,
        20, 21, 23, 23, 21, 22,
    };

    memcpy(pIndices, Indices, sizeof(Indices));

    CompleteCommon(L"Cube");

    LoadPostProcess(false);
    return true;
}

bool Model::CreateXZPlane(Vector3 HalfDimensions, Vector3 UVRepeat)
{
    CreateCommon(4, 6);

    MeshVertex* pVerts = (MeshVertex*)m_pVertexData;
    USHORT* pIndices = (USHORT*)m_pIndexData;
    pVerts[0].Position = XMFLOAT3(0, 0, 0); pVerts[0].TexCoord0 = XMFLOAT2(0, 1);
    pVerts[1].Position = XMFLOAT3(0, 0, 1); pVerts[1].TexCoord0 = XMFLOAT2(0, 0);
    pVerts[2].Position = XMFLOAT3(1, 0, 1); pVerts[2].TexCoord0 = XMFLOAT2(1, 0);
    pVerts[3].Position = XMFLOAT3(1, 0, 0); pVerts[3].TexCoord0 = XMFLOAT2(1, 1);
    FillVector3(&pVerts[0].Normal, sizeof(MeshVertex), 4, XMFLOAT3(0, 1, 0));
    FillVector3(&pVerts[0].Tangent, sizeof(MeshVertex), 4, XMFLOAT3(1, 0, 0));
    FillVector3(&pVerts[0].Bitangent, sizeof(MeshVertex), 4, XMFLOAT3(0, 0, -1));

    for (UINT32 i = 0; i < 4; ++i)
    {
        XMVECTOR Pos = XMLoadFloat3(&pVerts[i].Position);
        Pos *= (HalfDimensions * g_XMTwo);
        Pos -= HalfDimensions;
        XMStoreFloat3(&pVerts[i].Position, Pos);
        XMVECTOR UV = XMLoadFloat2(&pVerts[i].TexCoord0);
        UV *= UVRepeat;
        XMStoreFloat2(&pVerts[i].TexCoord0, UV);
    }

    const USHORT Indices[] = {
        0, 1, 3, 3, 1, 2,
    };

    memcpy(pIndices, Indices, sizeof(Indices));

    CompleteCommon(L"XZPlane");

    LoadPostProcess(false);
    return true;
}
