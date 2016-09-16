#include "pch.h"
#include "Model.h"
#include "Utility.h"
#include "TextureManager.h"
#include "GraphicsCore.h"
#include "DescriptorHeap.h"
#include "CommandContext.h"
#include <stdio.h>

#include "../3rdParty/BinaryMesh/BinaryMeshFile.h"

using namespace Graphics;

inline void CopyStringParameter(char* strDestination, UINT32 DestSize, const BMESH_PARAMETER& Param)
{
    switch (Param.Type)
    {
    case BMESH_PARAM_STRING:
        strcpy_s(strDestination, DestSize, Param.StringData.pString);
        break;
    }
}

UINT32 CreateInputLayoutIndex(const BMESH_MESH* pMesh)
{
    D3D12_INPUT_ELEMENT_DESC InputElements[32] = {};

    const UINT32 ElementCount = (UINT32)pMesh->VertexElements.Count;
    for (UINT32 i = 0; i < ElementCount; ++i)
    {
        D3D12_INPUT_ELEMENT_DESC& IE = InputElements[i];
        const BMESH_INPUT_ELEMENT& Src = pMesh->VertexElements[i];
        IE.AlignedByteOffset = Src.AlignedByteOffset;
        IE.Format = Src.Format;
        IE.InputSlot = Src.InputSlot;
        IE.SemanticIndex = Src.SemanticIndex;
        IE.SemanticName = Src.SemanticName.pString;
        if (_stricmp(IE.SemanticName, "BINORMAL") == 0)
        {
            IE.SemanticName = "BITANGENT";
        }
    }

    UINT32 LayoutIndex = g_InputLayoutCache.FindOrAddLayout(InputElements, ElementCount);
    return LayoutIndex;
}

bool Model::LoadBMESH(const char *filename)
{
    BYTE* pPersistentBuffer = nullptr;
    BYTE* pMeshBufferSegment = nullptr;

    FILE *file = nullptr;
    if (0 != fopen_s(&file, filename, "rb"))
        return false;

    BMESH_HEADER Header = {};
    size_t result = fread_s(&Header, sizeof(Header), sizeof(Header), 1, file);
    if (result != 1)
    {
        goto ErrorExit;
    }

    if (Header.Magic != BMESH_MAGIC ||
        Header.Version != BMESH_VERSION)
    {
        goto ErrorExit;
    }

    if (Header.HeaderSizeBytes != sizeof(Header))
    {
        goto ErrorExit;
    }

    size_t BmeshPersistentDataSizeBytes = Header.DataSegmentSizeBytes + Header.StringsSizeBytes + Header.AnimBufferSegmentSizeBytes;
    pPersistentBuffer = (BYTE*)malloc(BmeshPersistentDataSizeBytes);
    if (pPersistentBuffer == nullptr)
    {
        goto ErrorExit;
    }

    pMeshBufferSegment = (BYTE*)malloc(Header.MeshBufferSegmentSizeBytes);
    if (pMeshBufferSegment == nullptr)
    {
        goto ErrorExit;
    }

    result = fread_s(pPersistentBuffer, BmeshPersistentDataSizeBytes, BmeshPersistentDataSizeBytes, 1, file);
    if (result != 1)
    {
        goto ErrorExit;
    }

    result = fread_s(pMeshBufferSegment, Header.MeshBufferSegmentSizeBytes, Header.MeshBufferSegmentSizeBytes, 1, file);
    if (result != 1)
    {
        goto ErrorExit;
    }

    fclose(file);
    file = nullptr;

    BYTE* pDataSegment = pPersistentBuffer;
    BYTE* pStringSegment = pPersistentBuffer + Header.DataSegmentSizeBytes;
    BYTE* pAnimBufferSegment = pStringSegment + Header.StringsSizeBytes;

    BMesh::FixupPointers(&Header, pDataSegment, pStringSegment, pAnimBufferSegment, pMeshBufferSegment);

    UINT64 BaseIndexOffset = 0;
    UINT32 VertexStrideBytes = 1;
    if (Header.Meshes.Count > 0)
    {
        const BYTE* pIndexData = Header.Meshes[0].IndexData.ByteBuffer.pFirstObject;
        BaseIndexOffset = (UINT64)(pIndexData - pMeshBufferSegment);
        VertexStrideBytes = Header.Meshes[0].VertexDatas[0].StrideBytes;
    }

    const UINT64 VertexSegmentSizeBytes = BaseIndexOffset;
    const UINT64 IndexSegmentSizeBytes = Header.MeshBufferSegmentSizeBytes - BaseIndexOffset;

    const BYTE* pVertexBaseAddress = pMeshBufferSegment;
    const BYTE* pIndexBaseAddress = pVertexBaseAddress + VertexSegmentSizeBytes;

    m_Header.vertexDataByteSize = (UINT32)VertexSegmentSizeBytes;
    m_Header.indexDataByteSize = (UINT32)IndexSegmentSizeBytes;

    m_VertexBuffer.Create(L"VertexBuffer", (UINT32)(VertexSegmentSizeBytes / VertexStrideBytes), VertexStrideBytes, pMeshBufferSegment);
    m_IndexBuffer.Create(L"IndexBuffer", (UINT32)(IndexSegmentSizeBytes / 2), 2, pMeshBufferSegment + VertexSegmentSizeBytes);

    const UINT32 MaterialCount = (UINT32)Header.MaterialInstances.Count;

    m_pMaterial = new Material[MaterialCount];
    ZeroMemory(m_pMaterial, MaterialCount * sizeof(Material));
    m_Header.materialCount = MaterialCount;

    for (UINT32 i = 0; i < MaterialCount; ++i)
    {
        const BMESH_MATERIALINSTANCE& SrcMaterial = Header.MaterialInstances[i];
        Material& DestMaterial = m_pMaterial[i];

        DestMaterial.diffuse = Vector3(1, 1, 1);
        DestMaterial.opacity = 1.0f;

        const UINT32 ParamCount = (UINT32)SrcMaterial.Parameters.Count;
        for (UINT32 j = 0; j < ParamCount; ++j)
        {
            const BMESH_PARAMETER& Param = SrcMaterial.Parameters[j];
            switch (Param.Semantic)
            {
            case BMESH_SEM_TEXTURE_DIFFUSEMAP:
                CopyStringParameter(DestMaterial.texDiffusePath, ARRAYSIZE(DestMaterial.texDiffusePath), Param);
                break;
            case BMESH_SEM_TEXTURE_NORMALMAP:
                CopyStringParameter(DestMaterial.texNormalPath, ARRAYSIZE(DestMaterial.texNormalPath), Param);
                break;
            case BMESH_SEM_TEXTURE_SPECULARMAP:
                CopyStringParameter(DestMaterial.texSpecularPath, ARRAYSIZE(DestMaterial.texSpecularPath), Param);
                break;
            case BMESH_SEM_COLOR_DIFFUSEMAP:
                DestMaterial.diffuse = Vector3(XMLoadFloat4((const XMFLOAT4*)Param.FloatData));
                break;
            }
        }
    }

    const UINT32 MeshCount = (UINT32)Header.MaterialMappings.Count;
    m_pMesh = new Mesh[MeshCount];
    ZeroMemory(m_pMesh, MeshCount * sizeof(Mesh));
    m_Header.meshCount = MeshCount;

    m_InputLayoutIndex = -1;

    for (UINT32 i = 0; i < MeshCount; ++i)
    {
        const BMESH_MATERIALMAPPING& SrcMapping = Header.MaterialMappings[i];
        Mesh& DestMesh = m_pMesh[i];

        const BMESH_MESH* pSrcMesh = SrcMapping.Mesh;
        const BMESH_SUBSET& SrcSubset = pSrcMesh->Subsets[SrcMapping.MeshSubsetIndex];

        DestMesh.indexCount = SrcSubset.IndexCount;
        DestMesh.indexDataByteOffset = (UINT32)(pSrcMesh->IndexData.ByteBuffer.pFirstObject - pIndexBaseAddress);
        DestMesh.vertexCount = SrcSubset.VertexCount;
        DestMesh.vertexDataByteOffset = (UINT32)(pSrcMesh->VertexDatas[0].ByteBuffer.pFirstObject - pVertexBaseAddress);
        DestMesh.vertexStride = pSrcMesh->VertexDatas[0].StrideBytes;
        DestMesh.materialIndex = (UINT32)(SrcMapping.MaterialInstance.pObject - Header.MaterialInstances.pFirstObject);

        UINT32 InputLayoutIndex = CreateInputLayoutIndex(pSrcMesh);
        if (m_InputLayoutIndex == -1)
        {
            m_InputLayoutIndex = InputLayoutIndex;
        }
        else
        {
            assert(InputLayoutIndex == m_InputLayoutIndex);
        }
    }

    free(pMeshBufferSegment);
    pMeshBufferSegment = nullptr;

    free(pPersistentBuffer);
    pPersistentBuffer = nullptr;

    LoadTextures();

    return true;

ErrorExit:
    if (pPersistentBuffer != nullptr)
    {
        free(pPersistentBuffer);
        pPersistentBuffer = nullptr;
    }

    if (pMeshBufferSegment != nullptr)
    {
        free(pMeshBufferSegment);
        pMeshBufferSegment = nullptr;
    }

    if (file != nullptr)
    {
        fclose(file);
        file = nullptr;
    }

    return false;
}
