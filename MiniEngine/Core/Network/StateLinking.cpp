#include "pch.h"
#include "StateLinking.h"

StateInputOutput::StateInputOutput()
    : m_SnapshotIndex( 0 ),
      m_pRootNode( nullptr ),
      m_pNullSnapshot( nullptr ),
      m_ClientMode( FALSE )
{
    m_pNullSnapshot = CreateSnapshot();
}

VOID CreateSnapshotHelper( StateSnapshot* pSS, StateLinkNode* pNode, StateNode* pParentNode )
{
    assert( pSS != nullptr );
    if( pNode == nullptr )
    {
        return;
    }

    if( pNode->IncludeInSnapshot )
    {
        if( pNode->Type == StateNodeType::Complex )
        {
            StateNode* pCN = pSS->AddComplex( pParentNode, pNode->ID );
            pCN->GetCreationData().Clone( pNode->CreationData, pSS );
            CreateSnapshotHelper( pSS, pNode->pFirstChild, pCN );
        }
        else
        {
            StateNode* pN = pSS->AddDataType( pParentNode, pNode->ID, pNode->Type, pNode->pData );
            pN->GetCreationData().Clone( pNode->CreationData, pSS );
        }
    }

    CreateSnapshotHelper( pSS, pNode->pSibling, pParentNode );
}

StateSnapshot* StateInputOutput::CreateSnapshot()
{
    StateSnapshot* pSS = new StateSnapshot( m_SnapshotIndex++ );

    CreateSnapshotHelper( pSS, m_pRootNode, nullptr );

    return pSS;
}

BOOL StateInputOutput::UpdateNodeData( UINT32 ID, const VOID* pData, SIZE_T DataSizeBytes )
{
    StateLinkNode* pNode = FindNode( ID );
    if( pNode == nullptr )
    {
        return FALSE;
    }

    if( m_ClientMode && pNode->IncludeInSnapshot )
    {
        return TRUE;
    }

    assert( pNode->Type != StateNodeType::Complex );

    StateNodeTypeCodec::Decode( pNode->Type, pNode->pData, pData );

    if (m_pLoggingNode != nullptr && ( pNode == m_pLoggingNode || pNode->pParent == m_pLoggingNode ))
    {
        LogUpdate( pNode, g_CurrentRecvTimestamp );
    }

    return TRUE;
}

BOOL StateInputOutput::CreateNode( UINT32 ParentID, UINT32 ID, StateNodeType Type, VOID* pData, SIZE_T DataSizeBytes, UINT CreationCode, const VOID* pCreationData, SIZE_T CreationDataSizeBytes, BOOL IncludeInSnapshot )
{
    StateLinkNode* pParentNode = nullptr;
    if( ParentID != 0 )
    {
        pParentNode = FindNode( ParentID );
        if( pParentNode == nullptr )
        {
            return FALSE;
        }
    }

    assert( FindNode(ID) == nullptr );

    StateLinkNode* pNode = new StateLinkNode();
    pNode->ID = ID;
    pNode->IncludeInSnapshot = IncludeInSnapshot;
    pNode->Type = Type;
    pNode->pData = pData;
    pNode->pParent = pParentNode;
    pNode->pFirstChild = nullptr;

    if( pCreationData != nullptr && CreationDataSizeBytes > 0 )
    {
        pNode->CreationData.Clone( pCreationData, CreationDataSizeBytes );
    }
    else
    {
        pNode->CreationData.CreationCode = CreationCode;
    }

    if( pParentNode != nullptr )
    {
        pNode->pSibling = pParentNode->pFirstChild;
        pParentNode->pFirstChild = pNode;
    }
    else
    {
        pNode->pSibling = m_pRootNode;
        m_pRootNode = pNode;
    }

    m_NodeMap[ID] = pNode;

    return TRUE;
}

UINT StateInputOutput::CreateNodeGroup( UINT32 ParentID, UINT32 StartingNodeID, INetworkObject* pProxyObject, const VOID* pCreationData, SIZE_T CreationDataSizeBytes, BOOL IncludeInSnapshot )
{
    UINT NextNodeID = StartingNodeID;

    const UINT FirstNodeID = NextNodeID++;
    BOOL Success = CreateNode( ParentID, FirstNodeID, StateNodeType::Complex, nullptr, 0, 0, pCreationData, CreationDataSizeBytes, IncludeInSnapshot );
    if( !Success )
    {
        return StartingNodeID;
    }

    pProxyObject->SetNodeID( FirstNodeID );

    const INetworkObject::MemberDataPosition* pMemberDatas = nullptr;
    UINT MemberDataCount = 0;

    pProxyObject->GetMemberDatas( &pMemberDatas, &MemberDataCount );

    for( UINT i = 0; i < MemberDataCount; ++i )
    {
        VOID* pData = (VOID*)( (BYTE*)pProxyObject + pMemberDatas[i].OffsetBytes );
        Success = CreateNode( FirstNodeID, NextNodeID++, pMemberDatas[i].Type, pData, pMemberDatas[i].SizeBytes, i, nullptr, 0, IncludeInSnapshot );
    }

    return NextNodeID;
}

BOOL StateInputOutput::DeleteNodeAndChildren( UINT32 ID )
{
    StateLinkNodeMap::iterator iter = m_NodeMap.find( ID );
    if( iter == m_NodeMap.end() )
    {
        return FALSE;
    }

    StateLinkNode* pNode = iter->second;
    assert( pNode != nullptr );

    StateLinkNode* p = nullptr;
    if( pNode->pParent != nullptr )
    {
        p = pNode->pParent->pFirstChild;
        if( p == pNode )
        {
            pNode->pParent->pFirstChild = pNode->pSibling;
        }
    }
    else
    {
        p = m_pRootNode;
        if( p == pNode )
        {
            m_pRootNode = pNode->pSibling;
        }
    }

    while( p != nullptr )
    {
        if( p->pSibling == pNode )
        {
            p->pSibling = pNode->pSibling;
        }
        p = p->pSibling;
    }

    pNode->pSibling = nullptr;

    DeleteNodeTree( pNode );

    return TRUE;
}

VOID StateInputOutput::DeleteNodeTree( StateLinkNode* pNode )
{
    if (pNode == m_pLoggingNode)
    {
        StopLogging();
    }

    StateLinkNodeMap::iterator iter = m_NodeMap.find( pNode->ID );
    assert( iter != m_NodeMap.end() );
    m_NodeMap.erase( iter );

    if( pNode->pFirstChild != nullptr )
    {
        DeleteNodeTree( pNode->pFirstChild );
    }

    if( pNode->pSibling != nullptr )
    {
        DeleteNodeTree( pNode->pSibling );
    }

    ZeroMemory( pNode, sizeof(*pNode) );
    delete pNode;
}

StateLinkNode* StateInputOutput::FindNode( UINT32 ID )
{
    StateLinkNodeMap::iterator iter = m_NodeMap.find( ID );
    if( iter == m_NodeMap.end() )
    {
        return nullptr;
    }
    assert( iter->second->ID == ID );
    return iter->second;
}

HRESULT StateInputOutput::StartLogging( const WCHAR* strFileName )
{
    static const LogFileColumn Columns[] =
    {
        { "Timestamp", LogFileColumnType::UInt64 },
        { "NodeID", LogFileColumnType::UInt32 },
        { "Float0", LogFileColumnType::Float },
        { "Float1", LogFileColumnType::Float },
        { "Float2", LogFileColumnType::Float },
        { "Float3", LogFileColumnType::Float },
    };

    HRESULT hr = m_LogFile.Open( strFileName, ARRAYSIZE(Columns), Columns );

    if (SUCCEEDED(hr))
    {
        LARGE_INTEGER PerfFreq;
        QueryPerformanceFrequency( &PerfFreq );
        m_LogFile.SetUInt64Data( 0, 1, (UINT64*)&PerfFreq.QuadPart );
        m_LogFile.FlushLine();
    }

    return hr;
}

HRESULT StateInputOutput::StopLogging()
{
    HRESULT hr = m_LogFile.Close();
    SetLoggingNode( nullptr );
    return hr;
}

VOID StateInputOutput::LogUpdate( const StateLinkNode* pNode, LARGE_INTEGER Timestamp )
{
    if (!m_LogFile.IsOpen())
    {
        return;
    }

    m_LogFile.SetUInt64Data( 0, 1, (UINT64*)&Timestamp.QuadPart );
    m_LogFile.SetUInt32Data( 1, 1, &pNode->ID );

    switch (pNode->Type)
    {
    case StateNodeType::Float3:
    case StateNodeType::Float3AsHalf4Delta:
    case StateNodeType::Float3AsQwordDelta:
    case StateNodeType::Float3Delta:
        m_LogFile.SetFloatData( 2, 3, (FLOAT*)pNode->pData );
        break;
    case StateNodeType::Float4:
    case StateNodeType::Float4AsByteN4:
    case StateNodeType::Float4AsHalf4:
    case StateNodeType::Float4AsHalf4Delta:
        m_LogFile.SetFloatData( 2, 4, (FLOAT*)pNode->pData );
        break;
    default:
        break;
    }

    m_LogFile.FlushLine();
}
