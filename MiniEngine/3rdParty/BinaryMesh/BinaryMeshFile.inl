#pragma once

namespace BMesh
{
    template< typename T >
    inline VOID FixupHeaderArray( BMESH_ARRAY<T>& Array, VOID* pSegment )
    {
        if( Array.Count == 0 )
        {
            Array.pFirstObject = NULL;
        }
        else
        {
            Array.pFirstObject = (T*)( (BYTE*)pSegment + Array.SegmentOffsetBytes );
        }
    }

    template< typename T >
    inline VOID FixupStructArray( BMESH_ARRAY<T>& StructArray, BMESH_ARRAY<T>& HeaderArray )
    {
        if( StructArray.Count == 0 )
        {
            StructArray.pFirstObject = NULL;
        }
        else
        {
            StructArray.pFirstObject = &HeaderArray[ (size_t)StructArray.SegmentOffsetBytes ];
        }
    }

    template< typename T >
    inline VOID FixupStructIndex( BMESH_INDEX<T>& StructIndex, BMESH_ARRAY<T>& HeaderArray )
    {
        if( StructIndex.SegmentOffsetBytes == (UINT64)-1 )
        {
            StructIndex.pObject = NULL;
        }
        else
        {
            StructIndex.pObject = &HeaderArray[ (size_t)StructIndex.SegmentOffsetBytes ];            
        }
    }

    inline VOID FixupString( BMESH_STRING& String, VOID* pSegment )
    {
        String.pString = (const CHAR*)( (BYTE*)pSegment + String.SegmentOffsetBytes );
    }

    inline HRESULT FixupPointers( BMESH_HEADER* pHeader, VOID* pDataSegment, VOID* pStringSegment, VOID* pAnimBufferSegment, VOID* pMeshBufferSegment )
    {
        FixupHeaderArray( pHeader->Frames, pDataSegment );
        FixupHeaderArray( pHeader->Subsets, pDataSegment );
        FixupHeaderArray( pHeader->VertexDatas, pDataSegment );
        FixupHeaderArray( pHeader->VertexElements, pDataSegment );
        FixupHeaderArray( pHeader->Meshes, pDataSegment );
        FixupHeaderArray( pHeader->MaterialParameters, pDataSegment );
        FixupHeaderArray( pHeader->MaterialInstances, pDataSegment );
        FixupHeaderArray( pHeader->MaterialMappings, pDataSegment );
        FixupHeaderArray( pHeader->AnimationTracks, pDataSegment );
        FixupHeaderArray( pHeader->Animations, pDataSegment );
        FixupHeaderArray( pHeader->Influences, pDataSegment );

        for( UINT i = 0; i < pHeader->Frames.Count; ++i )
        {
            BMESH_FRAME& Frame = pHeader->Frames[i];
            FixupString( Frame.Name, pStringSegment );
            FixupStructIndex( Frame.pParentFrame, pHeader->Frames );
            FixupStructArray( Frame.MaterialMappings, pHeader->MaterialMappings );
        }

        for( UINT i = 0; i < pHeader->VertexElements.Count; ++i )
        {
            BMESH_INPUT_ELEMENT& ElementDesc = pHeader->VertexElements[i];
            FixupString( ElementDesc.SemanticName, pStringSegment );
        }

        for( UINT i = 0; i < pHeader->VertexDatas.Count; ++i )
        {
            BMESH_VERTEXDATA& VertexData = pHeader->VertexDatas[i];
            FixupHeaderArray( VertexData.ByteBuffer, pMeshBufferSegment );
        }

        for( UINT i = 0; i < pHeader->Subsets.Count; ++i )
        {
            BMESH_SUBSET& Subset = pHeader->Subsets[i];
            FixupString( Subset.Name, pStringSegment );
        }

        for( UINT i = 0; i < pHeader->Meshes.Count; ++i )
        {
            BMESH_MESH& Mesh = pHeader->Meshes[i];
            FixupString( Mesh.Name, pStringSegment );
            FixupStructArray( Mesh.VertexDatas, pHeader->VertexDatas );
            FixupStructArray( Mesh.VertexElements, pHeader->VertexElements );
            FixupStructArray( Mesh.Subsets, pHeader->Subsets );
            FixupHeaderArray( Mesh.IndexData.ByteBuffer, pMeshBufferSegment );
            FixupStructArray( Mesh.Influences, pHeader->Influences );
        }

        for( UINT i = 0; i < pHeader->MaterialParameters.Count; ++i )
        {
            BMESH_PARAMETER& Param = pHeader->MaterialParameters[i];
            FixupString( Param.Name, pStringSegment );
            if( Param.Type == BMESH_PARAM_STRING )
            {
                FixupString( Param.StringData, pStringSegment );
            }
        }

        for( UINT i = 0; i < pHeader->MaterialInstances.Count; ++i )
        {
            BMESH_MATERIALINSTANCE& Mat = pHeader->MaterialInstances[i];
            FixupString( Mat.Name, pStringSegment );
            FixupString( Mat.MaterialName, pStringSegment );
            FixupStructArray( Mat.Parameters, pHeader->MaterialParameters );
        }

        for( UINT i = 0; i < pHeader->MaterialMappings.Count; ++i )
        {
            BMESH_MATERIALMAPPING& Mapping = pHeader->MaterialMappings[i];
            FixupStructIndex( Mapping.Mesh, pHeader->Meshes );
            FixupStructIndex( Mapping.MaterialInstance, pHeader->MaterialInstances );
        }

        for( UINT i = 0; i < pHeader->AnimationTracks.Count; ++i )
        {
            BMESH_ANIMATIONTRACK& Track = pHeader->AnimationTracks[i];
            FixupString( Track.Name, pStringSegment );
            FixupHeaderArray( Track.PositionKeys, pAnimBufferSegment );
            FixupHeaderArray( Track.OrientationKeys, pAnimBufferSegment );
            FixupHeaderArray( Track.ScaleKeys, pAnimBufferSegment );
        }

        for( UINT i = 0; i < pHeader->Animations.Count; ++i )
        {
            BMESH_ANIMATION& Animation = pHeader->Animations[i];
            FixupString( Animation.Name, pStringSegment );
            FixupStructArray( Animation.Tracks, pHeader->AnimationTracks );
        }

        for( UINT i = 0; i < pHeader->Influences.Count; ++i )
        {
            BMESH_INFLUENCE& Inf = pHeader->Influences[i];
            FixupString( Inf.Name, pStringSegment );
        }

        return S_OK;
    }
}
