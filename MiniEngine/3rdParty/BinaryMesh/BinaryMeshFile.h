#pragma once

#include <windows.h>
#include <d3d11.h>
#include <assert.h>

#pragma warning( disable:4480 )

#define BMESH_VERSION 1001
#define BMESH_MAGIC 'BMsh'

#define BMESH_MAX_VERTEX_ELEMENTS 32
#define BMESH_MATERIAL_NAME_LENGTH 32

struct BMESH_STRING
{
    union
    {
        UINT64 SegmentOffsetBytes;
        const WCHAR* pWideString;
        const CHAR* pString;
    };
};

template< typename T >
struct BMESH_INDEX
{
    union
    {
        UINT64 SegmentOffsetBytes;
        T* pObject;
    };

    operator T* () { return pObject; }
    operator const T* () const { return pObject; }
};

template< typename T >
struct BMESH_ARRAY
{
    union
    {
        UINT64 SegmentOffsetBytes;
        T* pFirstObject;
    };
    UINT64 Count;

    T& operator[] ( size_t Index ) { assert( Index < Count ); return pFirstObject[Index]; }
    const T& operator[] ( size_t Index ) const { assert( Index < Count ); return pFirstObject[Index]; }
};

struct BMESH_MATERIALMAPPING;

struct BMESH_FRAME
{
    FLOAT Transform[16];
    BMESH_STRING Name;
    BMESH_INDEX<BMESH_FRAME> pParentFrame;
    BMESH_ARRAY<BMESH_MATERIALMAPPING> MaterialMappings;
};

struct BMESH_SUBSET
{
    BMESH_STRING Name;
    D3D11_PRIMITIVE_TOPOLOGY Topology;
    UINT32 StartIndex;
    UINT32 IndexCount;
    UINT32 StartVertex;
    UINT32 VertexCount;
};

struct BMESH_VERTEXDATA
{
    union
    {
        BMESH_ARRAY<BYTE> ByteBuffer;
        ID3D11Buffer* pBuffer;
    };
    UINT32 Count;
    UINT32 StrideBytes;
};

struct BMESH_INDEXDATA
{
    union
    {
        BMESH_ARRAY<BYTE> ByteBuffer;
        ID3D11Buffer* pBuffer;
    };
    DXGI_FORMAT Format;
    UINT32 Count;
};

struct BMESH_INPUT_ELEMENT
{
    BMESH_STRING SemanticName;
    UINT32 SemanticIndex;
    DXGI_FORMAT Format;
    UINT32 InputSlot;
    UINT32 AlignedByteOffset;
};

struct BMESH_INFLUENCE
{
    BMESH_STRING Name;
};

struct BMESH_MESH
{
    BMESH_STRING Name;

    BMESH_ARRAY<BMESH_INPUT_ELEMENT> VertexElements;

    BMESH_ARRAY<BMESH_VERTEXDATA> VertexDatas;
    BMESH_INDEXDATA IndexData;

    BMESH_ARRAY<BMESH_SUBSET> Subsets;

    BMESH_ARRAY<BMESH_INFLUENCE> Influences;
};

enum BMESH_PARAMETER_TYPE
{
    BMESH_PARAM_FLOAT,
    BMESH_PARAM_FLOAT2,
    BMESH_PARAM_FLOAT3,
    BMESH_PARAM_FLOAT4,
    BMESH_PARAM_STRING,
    BMESH_PARAM_BOOLEAN,
    BMESH_PARAM_INTEGER,
    BMESH_PARAM_RESOURCE,
    BMESH_PARAM_STREAM_RESOURCE,
};

enum BMESH_PARAMETER_SEMANTIC : UINT32
{
    BMESH_SEM_NONE = 0,

    BMESH_SEM_TEXTURE = 'Tx??',
    BMESH_SEM_TEXTURE_DIFFUSEMAP = 'TxDf',
    BMESH_SEM_TEXTURE_NORMALMAP = 'TxNm',
    BMESH_SEM_TEXTURE_SPECULARMAP = 'TxSp',
    BMESH_SEM_TEXTURE_HEIGHTMAP = 'TxHe',
    BMESH_SEM_TEXTURE_EMISSIVEMAP = 'TxEm',

    BMESH_SEM_COLOR = 'Co??',
    BMESH_SEM_COLOR_DIFFUSEMAP = 'CoDf',

    BMESH_SEM_MATERIAL_TRANSPARENT = 'MtTr',
};

struct BMESH_RESOURCE
{
    ID3D11Resource* pResource;
    ID3D11ShaderResourceView* pSRView;
};

struct BMESH_STREAM_RESOURCE
{
    void* pStreamResource;
};

struct BMESH_PARAMETER
{
    BMESH_STRING Name;
    BMESH_PARAMETER_TYPE Type;
    UINT32 Semantic;
    union
    {
        BMESH_STRING StringData;
        FLOAT FloatData[4];
        BOOL BooleanData;
        INT IntegerData;
        BMESH_RESOURCE Resource;
        BMESH_STREAM_RESOURCE StreamResource;
    };
};

struct BMESH_MATERIALINSTANCE
{
    BMESH_STRING Name;
    BMESH_STRING MaterialName;
    BMESH_ARRAY<BMESH_PARAMETER> Parameters;
};

struct BMESH_MATERIALMAPPING
{
    BMESH_INDEX<BMESH_MESH> Mesh;
    UINT32 MeshSubsetIndex;
    BMESH_INDEX<BMESH_MATERIALINSTANCE> MaterialInstance;
};

struct BMESH_ANIMATIONTRACK
{
    BMESH_STRING Name;

    UINT32 PositionKeyCount;
    BMESH_ARRAY<FLOAT> PositionKeys;

    UINT32 OrientationKeyCount;
    BMESH_ARRAY<FLOAT> OrientationKeys;

    UINT32 ScaleKeyCount;
    BMESH_ARRAY<FLOAT> ScaleKeys;
};

struct BMESH_ANIMATION
{
    BMESH_STRING Name;
    FLOAT DurationSeconds;
    BMESH_ARRAY<BMESH_ANIMATIONTRACK> Tracks;
};

struct BMESH_HEADER
{
    UINT32 Magic;
    UINT32 Version;

    // size of the BMESH_HEADER
    UINT32 HeaderSizeBytes;

    // size of the data (all structs in the file)
    UINT64 DataSegmentSizeBytes;

    // size of all strings (unicode and ansi, each null terminated)
    UINT64 StringsSizeBytes;

    // size of the animation data
    UINT64 AnimBufferSegmentSizeBytes;

    // size of the vertex and index data
    UINT64 MeshBufferSegmentSizeBytes;

    BMESH_ARRAY<BMESH_FRAME> Frames;
    BMESH_ARRAY<BMESH_SUBSET> Subsets;
    BMESH_ARRAY<BMESH_VERTEXDATA> VertexDatas;
    BMESH_ARRAY<BMESH_INPUT_ELEMENT> VertexElements;
    BMESH_ARRAY<BMESH_MESH> Meshes;
    BMESH_ARRAY<BMESH_PARAMETER> MaterialParameters;
    BMESH_ARRAY<BMESH_MATERIALINSTANCE> MaterialInstances;
    BMESH_ARRAY<BMESH_MATERIALMAPPING> MaterialMappings;
    BMESH_ARRAY<BMESH_ANIMATIONTRACK> AnimationTracks;
    BMESH_ARRAY<BMESH_ANIMATION> Animations;
    BMESH_ARRAY<BMESH_INFLUENCE> Influences;
};

namespace BMesh
{
    inline HRESULT FixupPointers( BMESH_HEADER* pHeader, VOID* pDataSegment, VOID* pStringSegment, VOID* pAnimBufferSegment, VOID* pMeshBufferSegment );
}

#include "BinaryMeshFile.inl"
