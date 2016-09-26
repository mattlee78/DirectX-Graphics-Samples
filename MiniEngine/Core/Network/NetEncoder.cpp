#include "pch.h"
#include "NetEncoder.h"
#include "LineProtocol.h"
#include "NetShared.h"

NetEncoder::NetEncoder()
    : m_SnapshotIndex( 0 ),
      m_BytesRemaining( 0 ),
      m_PacketCount( 0 ),
      m_MessageIndex( 0 ),
      m_pStats( nullptr )
{
}


NetEncoder::~NetEncoder()
{
}

HRESULT NetEncoder::OpenLogFile( const WCHAR* strPrefix, const WCHAR* strPlayerName )
{
    WCHAR strFileName[MAX_PATH];
    SYSTEMTIME Time;
    GetLocalTime( &Time );
    swprintf_s( strFileName, L"%sEncoder-%u%02u%02u-%02u%02u%02u-%s.csv", strPrefix, Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, strPlayerName );

    static const LogFileColumn Columns[] =
    {
        { "SnapshotID", LogFileColumnType::UInt32 },
        { "PacketIndex", LogFileColumnType::UInt32 },
        { "MessageIndex", LogFileColumnType::UInt32 },
        { "PacketType", LogFileColumnType::Enum, g_strPacketTypes },
        { "NodeID", LogFileColumnType::UInt32 },
        { "ParentNodeID", LogFileColumnType::UInt32 },
        { "Bytes", LogFileColumnType::UInt32 },
    };

    return m_LogFile.Open( strFileName, ARRAYSIZE(Columns), Columns );
}

HRESULT NetEncoder::CloseLogFile()
{
    return m_LogFile.Close();
}

HRESULT NetEncoder::Initialize( NetUdpSocket* pSocket, const SOCKADDR_IN* pSendAddr )
{
    m_pSocket = pSocket;
    if( pSendAddr != nullptr )
    {
        m_UseSendAddr = TRUE;
        m_SendAddr = *pSendAddr;
    }
    else
    {
        m_UseSendAddr = FALSE;
    }
    return S_OK;
}

BYTE* NetEncoder::AllocateBytes( UINT SizeBytes )
{
    if( SizeBytes > m_BytesRemaining )
    {
        Flush( FALSE );
    }
    assert( SizeBytes <= m_BytesRemaining );
    BYTE* pReturn = m_Buffer + BytesUsed();
    m_BytesRemaining -= SizeBytes;
    return pReturn;
}

VOID NetEncoder::Flush( BOOL LastPacket )
{
    if( m_PacketCount > 0 )
    {
        // send packet
        if( m_UseSendAddr )
        {
            m_pSocket->SendTo( &m_SendAddr, m_Buffer, (UINT)BytesUsed() );
        }
        else
        {
            m_pSocket->Send( m_Buffer, (UINT)BytesUsed() );
        }
        if( m_pStats != nullptr ) { m_pStats->BytesSent += (UINT32)BytesUsed(); m_pStats->PacketsSent++; }
    }

    m_BytesRemaining = sizeof(m_Buffer);
    ++m_PacketCount;
    if( !LastPacket && m_PacketCount > 1 )
    {
        assert( m_SnapshotIndex != 0 );
        BeginSnapshot( m_SnapshotIndex );
    }
}

VOID NetEncoder::BeginSnapshot( UINT32 Index )
{
    auto* pBegin = AllocateMessage<NetPacketBeginSnapshot>();

    pBegin->SetIndex( Index );

    if( m_SnapshotIndex == 0 )
    {
        m_SnapshotIndex = Index;
    }
    else
    {
        assert( Index == m_SnapshotIndex );
    }

    if( m_pStats != nullptr ) { m_pStats->BeginSnapshotsSent++; }
    LogMessage( (UINT32)NetPacketType::BeginSnapshot, Index, 0, sizeof(NetPacketBeginSnapshot) );
}

VOID NetEncoder::EndSnapshot( UINT32 Index )
{
    assert( m_SnapshotIndex == Index );

    auto* pEnd = AllocateMessage<NetPacketEndSnapshot>();
    pEnd->SetIndex( Index );
    pEnd->PacketCount = m_PacketCount;

    LogMessage( (UINT32)NetPacketType::EndSnapshot, Index, m_PacketCount, sizeof(NetPacketEndSnapshot) );

    Flush( TRUE );

    if( m_pStats != nullptr ) { m_pStats->EndSnapshotsSent++; }

    m_SnapshotIndex = 0;
    m_PacketCount = 0;
    m_BytesRemaining = 0;
    m_MessageIndex = 0;
}

VOID NetEncoder::SendReliableMessage( const ReliableMessage& msg )
{
    BYTE* pPayload = nullptr;
    auto* pMsg = AllocateMessageWithPayload<NetPacketReliableMessage>( msg.BufferSizeBytes, &pPayload );
    pMsg->SetOpcode( msg.Opcode );
    pMsg->UniqueIndex = msg.UniqueIndex;
    memcpy( pPayload, msg.Buffer, msg.BufferSizeBytes );
    LogMessage( (UINT32)NetPacketType::ReliableMessage, 0, 0, (UINT32)pMsg->GetByteCount() );
}

VOID NetEncoder::SendUnreliableMessage( const ReliableMessage& msg )
{
    BYTE* pPayload = nullptr;
    auto* pMsg = AllocateMessageWithPayload<NetPacketUnreliableMessage>( msg.BufferSizeBytes, &pPayload );
    pMsg->SetOpcode( msg.Opcode );
    memcpy( pPayload, msg.Buffer, msg.BufferSizeBytes );
    LogMessage( (UINT32)NetPacketType::UnreliableMessage, 0, 0, (UINT32)pMsg->GetByteCount() );
}

VOID NetEncoder::SendAcknowledge( const UINT SnapshotIndex )
{
    auto* pMsg = AllocateMessage<NetPacketAck>();
    pMsg->SetIndex( SnapshotIndex );
    LogMessage( (UINT32)NetPacketType::Acknowledge, SnapshotIndex, 0, sizeof(NetPacketAck) );
}

VOID NetEncoder::NodeChangedWorker( StateNode* pPrev, StateNode* pCurrent, bool UpdatePrevChanged )
{
    const VOID* pPayloadSrc = pCurrent->GetRawData();
    const SIZE_T PayloadSizeBytes = pCurrent->GetStorageDataSize();

    BYTE* pPayloadDest = nullptr;
    auto* pMsg = AllocateMessageWithPayload<NetPacketNodeUpdate>( (UINT)PayloadSizeBytes, &pPayloadDest );
    pMsg->SetID( pCurrent->GetID() );
    memcpy( pPayloadDest, pPayloadSrc, PayloadSizeBytes );

    if (IsDeltaType(pCurrent->GetType()) && UpdatePrevChanged)
    {
        pCurrent->SetPreviouslyChanged();
    }

    if( m_pStats != nullptr ) { m_pStats->NodeUpdateMessagesSent++; }
    LogMessage( (UINT32)NetPacketType::NodeUpdate, pMsg->GetID(), 0, (UINT32)pMsg->GetByteCount() );
}

VOID NetEncoder::NodeChanged( StateNode* pPrev, StateNode* pCurrent )
{
    NodeChangedWorker( pPrev, pCurrent, true );
}

VOID NetEncoder::NodeSame( StateNode* pPrev, StateNode* pCurrent )
{
    if (pPrev->WasPreviouslyChanged())
    {
        NodeChangedWorker( pPrev, pCurrent, false );
    }
}

VOID NetEncoder::NodeCreated( StateNode* pNode, StateNode* pParentNode )
{
    assert( pNode != nullptr );

    const StateNodeCreationData& CreationData = pNode->GetCreationData();

    const UINT32 ParentID = ( pParentNode != nullptr ) ? pParentNode->GetID() : 0;

    if( CreationData.SizeBytes > 0 )
    {
        BYTE* pPayloadDest = nullptr;
        auto* pMsg = AllocateMessageWithPayload<NetPacketNodeCreateComplex>( (UINT)CreationData.SizeBytes, &pPayloadDest );
        pMsg->ParentID = ParentID;
        pMsg->SetID( pNode->GetID() );
        pMsg->NodeType = (UINT32)pNode->GetType();
        memcpy( pPayloadDest, CreationData.pBuffer, CreationData.SizeBytes );
        LogMessage( (UINT32)NetPacketType::NodeCreateComplex, pMsg->GetID(), pMsg->ParentID, (UINT32)pMsg->GetByteCount() );
    }
    else
    {
        auto* pMsg = AllocateMessage<NetPacketNodeCreateSimple>();
        pMsg->SetParentID( ParentID );
        pMsg->SetID( pNode->GetID() );
        pMsg->SetNodeType( (UINT32)pNode->GetType() );
        pMsg->SetCreationCode( CreationData.CreationCode );
        LogMessage( (UINT32)NetPacketType::NodeCreateSimple, pMsg->GetID(), pMsg->GetParentID(), (UINT32)pMsg->GetByteCount() );
    }

    if( !pNode->IsComplex() )
    {
        NodeChanged( nullptr, pNode );
    }
}

VOID NetEncoder::NodeDeleted( StateNode* pNode )
{
    auto* pMsg = AllocateMessage<NetPacketNodeDelete>();
    pMsg->SetID( pNode->GetID() );
    LogMessage( (UINT32)NetPacketType::NodeDelete, pMsg->GetID(), 0, sizeof(NetPacketNodeDelete) );
}
