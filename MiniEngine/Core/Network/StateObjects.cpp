#include "pch.h"
#include "StateObjects.h"
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include "NetConstants.h"
#include "ClientPredict.h"

using namespace DirectX;
using namespace DirectX::PackedVector;

LARGE_INTEGER g_CurrentRecvTimestamp = { 0, 0 };

SIZE_T StateNodeTypeCodec::GetExpandedSize( StateNodeType Type )
{
    switch( Type )
    {
    case StateNodeType::Complex:
        return sizeof(StateNodeChildSet);
    case StateNodeType::Integer:
        return sizeof(int);
    case StateNodeType::Integer4:
        return sizeof(int) * 4;
    case StateNodeType::Float:
        return sizeof(float);
    case StateNodeType::Float2:
    case StateNodeType::Float2AsHalf2:
        return sizeof(float) * 2;
    case StateNodeType::Float3:
    case StateNodeType::Float3Delta:
    case StateNodeType::Float3AsHalf4Delta:
    case StateNodeType::Float3AsQwordDelta:
    case StateNodeType::PredictFloat3:
        return sizeof(float) * 3;
    case StateNodeType::Float4:
    case StateNodeType::Float4AsByteN4:
    case StateNodeType::Float4AsHalf4:
    case StateNodeType::Float4AsHalf4Delta:
    case StateNodeType::PredictQuaternion:
        return sizeof(float) * 4;
    case StateNodeType::Matrix43:
        return sizeof(float) * 12;
    case StateNodeType::Matrix44:
        return sizeof(float) * 16;
    case StateNodeType::String:
    case StateNodeType::WideString:
        return NET_STRING_SIZEBYTES;
    case StateNodeType::Blob:
        return sizeof(StateBlob);
    default:
        assert( FALSE );
        return 0;
    }
}

SIZE_T StateNodeTypeCodec::GetStorageSize( StateNodeType Type )
{
    switch( Type )
    {
    case StateNodeType::Float4AsByteN4:
        return sizeof(XMBYTEN4);
    case StateNodeType::Float2AsHalf2:
        return sizeof(XMHALF2);
    case StateNodeType::Float4AsHalf4:
    case StateNodeType::Float4AsHalf4Delta:
    case StateNodeType::Float3AsHalf4Delta:
        return sizeof(XMHALF4);
    case StateNodeType::Float3AsQwordDelta:
        return sizeof(Float3Qword);
    default:
        return GetExpandedSize( Type );
    }
}

VOID StateNodeTypeCodec::Encode( StateNodeType Type, VOID* pStorageData, const VOID* pExpandedData )
{
    switch( Type )
    {
    case StateNodeType::Float4AsByteN4:
        XMStoreByteN4( (XMBYTEN4*)pStorageData, XMLoadFloat4( (const XMFLOAT4*)pExpandedData ) );
        return;
    case StateNodeType::Float2AsHalf2:
        XMStoreHalf2( (XMHALF2*)pStorageData, XMLoadFloat2( (const XMFLOAT2*)pExpandedData ) );
        return;
    case StateNodeType::Float4AsHalf4:
        XMStoreHalf4( (XMHALF4*)pStorageData, XMLoadFloat4( (const XMFLOAT4*)pExpandedData ) );
        return;
    case StateNodeType::Float3AsQwordDelta:
        {
            auto* pSrc = (const StateFloat3Delta*)pExpandedData;
            ((Float3Qword*)pStorageData)->SetFloat3( pSrc->GetRawValue() );
            return;
        }
    case StateNodeType::Float3Delta:
        {
            auto* pSrc = (const StateFloat3Delta*)pExpandedData;
            XMStoreFloat3( (XMFLOAT3*)pStorageData, pSrc->GetRawValue() );
            return;
        }
    case StateNodeType::Float3AsHalf4Delta:
        {
            auto* pSrc = (const StateFloat3Delta*)pExpandedData;
            XMStoreHalf4( (XMHALF4*)pStorageData, pSrc->GetRawValue() );
            return;
        }
    case StateNodeType::Float4AsHalf4Delta:
        {
            auto* pSrc = (const StateFloat4Delta*)pExpandedData;
            XMStoreHalf4( (XMHALF4*)pStorageData, pSrc->GetRawValue() );
            return;
        }
    case StateNodeType::PredictFloat3:
    {
        auto* pSrc = (const PredictionFloat3*)pExpandedData;
        XMStoreFloat3((XMFLOAT3*)pStorageData, pSrc->GetStaticValue());
        return;
    }
    case StateNodeType::PredictQuaternion:
    {
        auto* pSrc = (const PredictionQuaternion*)pExpandedData;
        XMStoreHalf4((XMHALF4*)pStorageData, pSrc->GetStaticValue());
        return;
    }
    default:
        assert( GetStorageSize( Type ) == GetExpandedSize( Type ) );
        memcpy( pStorageData, pExpandedData, GetStorageSize( Type ) );
        return;
    }
}

VOID StateNodeTypeCodec::Decode( StateNodeType Type, VOID* pExpandedData, const VOID* pStorageData )
{
    switch( Type )
    {
    case StateNodeType::Float4AsByteN4:
        XMStoreFloat4( (XMFLOAT4*)pExpandedData, XMLoadByteN4( (const XMBYTEN4*)pStorageData ) );
        return;
    case StateNodeType::Float2AsHalf2:
        XMStoreFloat2( (XMFLOAT2*)pExpandedData, XMLoadHalf2( (const XMHALF2*)pStorageData ) );
        return;
    case StateNodeType::Float4AsHalf4:
        XMStoreFloat4( (XMFLOAT4*)pExpandedData, XMLoadHalf4( (const XMHALF4*)pStorageData ) );
        return;
    case StateNodeType::Float3AsQwordDelta:
        {
            XMVECTOR v = ((const Float3Qword*)pStorageData)->GetFloat3();
            auto* pDest = (StateFloat3Delta*)pExpandedData;
            pDest->ReceiveNewValue( v, g_CurrentRecvTimestamp );
            return;
        }
    case StateNodeType::Float3Delta:
        {
            auto* pDest = (StateFloat3Delta*)pExpandedData;
            pDest->ReceiveNewValue( XMLoadFloat3( (const XMFLOAT3*)pStorageData ), g_CurrentRecvTimestamp );
            return;
        }
    case StateNodeType::Float3AsHalf4Delta:
        {
            auto* pDest = (StateFloat3Delta*)pExpandedData;
            pDest->ReceiveNewValue( XMLoadHalf4( (const XMHALF4*)pStorageData ), g_CurrentRecvTimestamp );
            return;
        }
    case StateNodeType::Float4AsHalf4Delta:
        {
            auto* pDest = (StateFloat4Delta*)pExpandedData;
            pDest->ReceiveNewValue( XMLoadHalf4( (const XMHALF4*)pStorageData ), g_CurrentRecvTimestamp );
            return;
        }
    case StateNodeType::PredictFloat3:
    {
        auto* pDest = (PredictionFloat3*)pExpandedData;
        pDest->UpdateFromNetwork(XMLoadFloat3((const XMFLOAT3*)pStorageData), g_CurrentRecvTimestamp.QuadPart);
        return;
    }
    case StateNodeType::PredictQuaternion:
    {
        auto* pDest = (PredictionQuaternion*)pExpandedData;
        pDest->UpdateFromNetwork(XMLoadHalf4((const XMHALF4*)pStorageData), g_CurrentRecvTimestamp.QuadPart);
        return;
    }
    default:
        assert( GetExpandedSize( Type ) == GetStorageSize( Type ) );
        memcpy( pExpandedData, pStorageData, GetExpandedSize( Type ) );
        return;
    }
}

VOID StateNodeCreationData::Clone( const StateNodeCreationData& Other, StateSnapshot* pSnapshot )
{
    CreationCode = Other.CreationCode;
    if( Other.SizeBytes > 0 && Other.pBuffer != nullptr )
    {
        assert( pSnapshot != nullptr );
        SizeBytes = Other.SizeBytes;
        OwnsMemory = FALSE;
        pBuffer = pSnapshot->GetAllocator()->AllocateBytes( SizeBytes );
        memcpy( pBuffer, Other.pBuffer, SizeBytes );
    }
    else
    {
        SizeBytes = 0;
        pBuffer = nullptr;
        OwnsMemory = FALSE;
    }
}

VOID StateNodeCreationData::Clone( const VOID* pData, SIZE_T DataSizeBytes )
{
    CreationCode = 0;
    if( DataSizeBytes > 0 && pData != nullptr )
    {
        SizeBytes = DataSizeBytes;
        OwnsMemory = TRUE;
        pBuffer = new BYTE[SizeBytes];
        memcpy( pBuffer, pData, SizeBytes );
    }
    else
    {
        SizeBytes = 0;
        pBuffer = nullptr;
        OwnsMemory = FALSE;
    }
}

BOOL StateNode::HasEqualData( const StateNode* pOther ) const
{
    assert( GetType() == pOther->GetType() );
    assert( !IsComplex() );

    const VOID* pData = GetRawData();
    const VOID* pOtherData = pOther->GetRawData();

    if( pData == pOtherData )
    {
        return TRUE;
    }

    SIZE_T DataSize = GetStorageDataSize();
    switch( GetType() )
    {
    case StateNodeType::String:
        return strcmp( (const CHAR*)pData, (const CHAR*)pOtherData ) == 0;
    case StateNodeType::WideString:
        return wcscmp( (const WCHAR*)pData, (const WCHAR*)pOtherData ) == 0;
    default:
        return ( memcmp( pData, pOtherData, DataSize ) == 0 );
    }
}

VOID* StateNode::CreateLocalData( StateSnapshot* pSnapshot, const VOID* pData )
{
    VOID* pReturn = nullptr;

    switch( m_Type )
    {
    case StateNodeType::Complex:
        {
            auto* pSNCS = pSnapshot->GetAllocator()->Allocate<StateNodeChildSet>();
            new (pSNCS) StateNodeChildSet();
            pSNCS->Initialize( pSnapshot->GetAllocator() );
            return pSNCS;
        }
    case StateNodeType::String:
        {
            const CHAR* strData = (const CHAR*)pData;
            UINT SizeBytes = (UINT)strlen(strData) + 1;
            return pSnapshot->GetAllocator()->AllocateBytes( SizeBytes );
        }
    case StateNodeType::WideString:
        {
            const WCHAR* strData = (const WCHAR*)pData;
            UINT SizeBytes = sizeof(WCHAR) * ( (UINT)wcslen(strData) + 1 );
            return pSnapshot->GetAllocator()->AllocateBytes( SizeBytes );
        }
    case StateNodeType::Blob:
        assert( FALSE );
        return nullptr;
    default:
        return pSnapshot->GetAllocator()->AllocateBytes( GetStorageDataSize() );
    }
}

VOID StateNode::CopyToLocalData( StateSnapshot* pSnapshot, const VOID* pData )
{
    switch( m_Type )
    {
    case StateNodeType::Complex:
        return;
    case StateNodeType::Blob:
        assert( FALSE );
        return;
    case StateNodeType::String:
        {
            const CHAR* strData = (const CHAR*)pData;
            UINT SizeChars = (UINT)strlen(strData) + 1;
            strcpy_s( (CHAR*)m_pData, SizeChars, strData );
            return;
        }
    case StateNodeType::WideString:
        {
            const WCHAR* strData = (const WCHAR*)pData;
            UINT SizeChars =(UINT)wcslen(strData) + 1;
            wcscpy_s( (WCHAR*)m_pData, SizeChars, strData );
            return;
        }
    default:
        assert( pData != nullptr );
        assert( m_pData != nullptr );
        StateNodeTypeCodec::Encode( m_Type, m_pData, pData );
        return;
    }
}

volatile ULONG g_SnapshotCount = 0;

StateSnapshot::StateSnapshot( UINT32 Index )
    : m_Index( Index ),
      m_Refcount( 1 )
{
    Initialize( &m_ZoneAllocator );
//     InterlockedIncrement( &g_SnapshotCount );
//     printf_s( "Creating snapshot %u %u\n", m_Index, g_SnapshotCount );
}

StateSnapshot::~StateSnapshot()
{
    m_Refcount = INT_MIN;
//     InterlockedDecrement( &g_SnapshotCount );
//     printf_s( "Deleting snapshot %u %u\n", m_Index, g_SnapshotCount );
}

UINT StateSnapshot::Release()
{
    assert( m_Refcount > 0 );
    UINT NewRefcount = m_Refcount - 1;
    m_Refcount = NewRefcount;
    if( NewRefcount == 0 )
    {
        delete this;
    }
    return NewRefcount;
}

StateNode* StateSnapshot::AddComplex( StateNode* pParent, UINT32 ID )
{
    StateNodeChildSet* pParentCS = this;
    if( pParent != nullptr )
    {
        pParentCS = pParent->GetChildSet();
        assert( pParentCS != nullptr );
    }

    VOID* pSN = m_ZoneAllocator.AllocateBytes( sizeof(StateNode) );
    StateNode* pReturn = new (pSN) StateNode( this, ID, StateNodeType::Complex, nullptr );

    pParentCS->AddChild( pReturn );

    return pReturn;
}

StateNode* StateSnapshot::AddDataType( StateNode* pParent, UINT32 ID, StateNodeType Type, const VOID* pData )
{
    StateNodeChildSet* pParentCS = this;
    if( pParent != nullptr )
    {
        pParentCS = pParent->GetChildSet();
        assert( pParentCS != nullptr );
    }

    VOID* pSN = m_ZoneAllocator.AllocateBytes( sizeof(StateNode) );
    StateNode* pReturn = new (pSN) StateNode( this, ID, Type, pData );

    pParentCS->AddChild( pReturn );

    return pReturn;
}

inline VOID DebugPrintData( IStateSnapshotDebug* pDebug, UINT Indent, StateNode* pNode )
{
    const CHAR* strTypeName = "";
    CHAR strValues[256] = "";

    BOOL FloatValue = TRUE;
    UINT NumValues = 0;

    switch( pNode->GetType() )
    {
    case StateNodeType::Integer:
        strTypeName = "Integer";
        NumValues = 1;
        FloatValue = FALSE;
        break;
    case StateNodeType::Integer4:
        strTypeName = "Integer4";
        NumValues = 4;
        FloatValue = FALSE;
        break;
    case StateNodeType::Float:
        strTypeName = "Float";
        NumValues = 1;
        break;
    case StateNodeType::Float2:
        strTypeName = "Float2";
        NumValues = 2;
        break;
    case StateNodeType::Float3:
        strTypeName = "Float3";
        NumValues = 3;
        break;
    case StateNodeType::Float4:
        strTypeName = "Float4";
        NumValues = 4;
        break;
    case StateNodeType::Float4AsByteN4:
        strTypeName = "Quaternion";
        NumValues = 4;
        break;
    case StateNodeType::Float2AsHalf2:
        strTypeName = "Half2";
        NumValues = 2;
        break;
    case StateNodeType::Float4AsHalf4:
        strTypeName = "Half4";
        NumValues = 4;
        break;
    case StateNodeType::Matrix43:
        strTypeName = "Matrix4x3";
        NumValues = 12;
        break;
    case StateNodeType::Matrix44:
        strTypeName = "Matrix4x4";
        NumValues = 16;
        break;
    default:
        assert( FALSE );
        break;
    }

    for( UINT i = 0; i < NumValues; ++i )
    {
        CHAR strValue[16];
        if( FloatValue )
        {
            auto* pF = (const FLOAT*)pNode->GetRawData();
            sprintf_s( strValue, "%0.f ", pF[i] );
        }
        else
        {
            auto* pI = (const INT*)pNode->GetRawData();
            sprintf_s( strValue, "%d ", pI[i] );
        }
        strcat_s( strValues, strValue );
    }

    pDebug->PrintLine( Indent, "Node %u: %s < %s>\n", pNode->GetID(), strTypeName, strValues );
}

VOID DebugPrintChildSet( IStateSnapshotDebug* pDebug, UINT Indent, UINT ChildSetID, StateNodeChildSet* pCS )
{
    assert( pCS != nullptr );

    if( Indent == 0 )
    {
        pDebug->PrintLine( 0, "Snapshot %d\n", ChildSetID );
    }
    else
    {
        pDebug->PrintLine( Indent, "Complex %u\n", ChildSetID );
    }

    auto iter = pCS->Begin();
    auto end = pCS->End();
    while( iter != end )
    {
        StateNode* pNode = *iter;
        if( pNode->IsComplex() )
        {
            DebugPrintChildSet( pDebug, Indent + 1, pNode->GetID(), pNode->GetChildSet() );
        }
        else
        {
            DebugPrintData( pDebug, Indent + 1, pNode );
        }

        ++iter;
    }
}

VOID StateSnapshot::DebugPrint( IStateSnapshotDebug* pDebug )
{
    DebugPrintChildSet( pDebug, 0, m_Index, this );
}

VOID DiffChildSet( IStateSnapshotDiff* pIDiff, StateNodeChildSet* pCSA, StateNode* pParentA, StateNodeChildSet* pCSB, StateNode* pParentB )
{
    if( pCSA == nullptr )
    {
        // everything in pCSB is new
        auto iter = pCSB->Begin();
        auto end = pCSB->End();
        while( iter != end )
        {
            StateNode* pN = *iter++;

            pIDiff->NodeCreated( pN, pParentB );

            if( pN->IsComplex() )
            {
                DiffChildSet( pIDiff, nullptr, nullptr, pN->GetChildSet(), pN );
            }
        }
    }
    else if( pCSB == nullptr )
    {
        // everything in pCSA is deleted
        auto iter = pCSA->Begin();
        auto end = pCSA->End();
        while( iter != end )
        {
            StateNode* pN = *iter++;

            pIDiff->NodeDeleted( pN );

            if( pN->IsComplex() )
            {
                DiffChildSet( pIDiff, pN->GetChildSet(), pN, nullptr, nullptr );
            }
        }
    }
    else
    {
        // need to diff pCSA and pCSB
        // note that child node IDs within pCSA and pCSB are sorted ascending (via std::set)
        auto iterA = pCSA->Begin();
        auto iterB = pCSB->Begin();
        auto endA = pCSA->End();
        auto endB = pCSB->End();

        while( iterA != endA || iterB != endB )
        {
            StateNode* pA = ( iterA != endA ) ? *iterA : nullptr;
            StateNode* pB = ( iterB != endB ) ? *iterB : nullptr;
            assert( pA != nullptr || pB != nullptr );

            if( pA == nullptr )
            {
                // pB is new
                pIDiff->NodeCreated( pB, pParentB );
                if( pB->IsComplex() )
                {
                    DiffChildSet( pIDiff, nullptr, nullptr, pB->GetChildSet(), pB );
                }
                ++iterB;
                continue;
            }
            else if( pB == nullptr )
            {
                // pA is deleted
                pIDiff->NodeDeleted( pA );
                if( pA->IsComplex() )
                {
                    DiffChildSet( pIDiff, pA->GetChildSet(), pA, nullptr, nullptr );
                }
                ++iterA;
                continue;
            }

            assert( pA != nullptr && pB != nullptr );

            const UINT IDA = pA->GetID();
            const UINT IDB = pB->GetID();

            if( IDA == IDB )
            {
                // node IDs match; compare their data
                if( pA->IsComplex() )
                {
                    assert( pB->IsComplex() );
                    pIDiff->NodeSame( pA, pB );
                    DiffChildSet( pIDiff, pA->GetChildSet(), pA, pB->GetChildSet(), pB );
                }
                else if( pA->HasEqualData( pB ) )
                {
                    pIDiff->NodeSame( pA, pB );
                }
                else
                {
                    pIDiff->NodeChanged( pA, pB );
                }
                ++iterA;
                ++iterB;
            }
            else if( IDA < IDB )
            {
                // sequence in B is more advanced than A; therefore pA has been deleted
                pIDiff->NodeDeleted( pA );
                if( pA->IsComplex() )
                {
                    DiffChildSet( pIDiff, pA->GetChildSet(), pA, nullptr, nullptr );
                }
                ++iterA;
            }
            else
            {
                assert( IDB < IDA );
                // sequence in B is less advanced than A; therefore pB has been created
                pIDiff->NodeCreated( pB, pParentB );
                if( pB->IsComplex() )
                {
                    DiffChildSet( pIDiff, nullptr, nullptr, pB->GetChildSet(), pB );
                }
                ++iterB;
            }
        }
    }
}

VOID StateSnapshot::Diff( StateSnapshot* pNew, IStateSnapshotDiff* pIDiff )
{
    assert( pNew->GetIndex() != GetIndex() );
    DiffChildSet( pIDiff, this, nullptr, pNew, nullptr );
}