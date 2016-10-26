#include "pch.h"
#include "NetClientBase.h"
#include "StateObjects.h"
#include "LineProtocol.h"

NetClientBase::NetClientBase()
    : m_hThread( INVALID_HANDLE_VALUE ),
      m_ServerPort( 0 ),
      m_ConnectionState( ConnectionState::Disconnected ),
      m_Disconnect( FALSE ),
      m_DataReceivedRecently( TRUE ),
      m_LastReliableMessageIndex( 0 ),
      m_PacketDiscardFraction( 0 ),
      m_ProcessPacketQueueOnMainThread(false)
{
    m_pNullSnapshot = new StateSnapshot( 0 );
    m_SendQueue.Initialize( m_pNullSnapshot );
    m_DecodeHandlers.AddHandler( this );
    m_AckTracker.Initialize( &m_SendQueue );
    m_StateIO.SetClientMode( TRUE );

    ZeroMemory( m_Statistics, sizeof(m_Statistics) );
    m_CurrentStatisticsIndex = 0;
    m_pCurrentStats = &m_Statistics[0];
    
    do 
    {
        LARGE_INTEGER Time;
        QueryPerformanceCounter(&Time);
        m_Nonce = (USHORT)Time.LowPart;
    } while ( m_Nonce == 0 );

    SetName( "Client%u", (UINT)m_Nonce );
    QueryPerformanceFrequency( &m_PerfFreq );
    g_LerpThresholdTicks.QuadPart = m_PerfFreq.QuadPart;
    InitializeWinsock();
    InitializeCriticalSection(&m_PacketQueueCritSec);
    m_pCurrentPacketQueue = new PacketQueue();
}


NetClientBase::~NetClientBase()
{
    m_pNullSnapshot->Release();
    TerminateWinsock();
    DeleteCriticalSection(&m_PacketQueueCritSec);
}

NetFrameStatistics* NetClientBase::NextStatisticsFrame()
{
    m_pCurrentStats->Finished = TRUE;

    if( m_LogFile.IsOpen() )
    {
        m_LogFile.SetUInt64Data( 0, 1, (const UINT64*)&m_pCurrentStats->Timestamp );
        m_LogFile.SetUInt32Data( 1, 12, &m_pCurrentStats->PacketsReceived );
        m_LogFile.FlushLine();
    }

    m_CurrentStatisticsIndex = ( m_CurrentStatisticsIndex + 1 ) % ARRAYSIZE( m_Statistics );
    m_pCurrentStats = &m_Statistics[m_CurrentStatisticsIndex];
    m_pCurrentStats->Zero();
    return m_pCurrentStats;
}

VOID NetClientBase::Connect( UINT FramesPerSecond, const CHAR* strServerName, USHORT PortNum, const WCHAR* strUserName, const WCHAR* strPassword )
{
    // TODO: reset thread if trying to reconnect

    assert( m_hThread == INVALID_HANDLE_VALUE );
    assert( m_ConnectionState == ConnectionState::Disconnected );
    m_ConnectionState = ConnectionState::Connecting;

    strcpy_s( m_strServerName, strServerName );
    m_ServerPort = PortNum;

    if( strUserName != nullptr )
    {
        wcscpy_s( m_strUserName, strUserName );
    }
    else
    {
        swprintf_s( m_strUserName, L"Player%u", (UINT)m_Nonce );
    }

    HashPassword( strPassword );

    m_FrameTicks = m_PerfFreq.QuadPart / FramesPerSecond;
    m_AverageDeltaTime = 1.0f / (FLOAT)FramesPerSecond;

    m_Disconnect = FALSE;
    m_ConnectAttempts = 0;
    m_hThread = CreateThread( NULL, 0, ThreadEntry, this, 0, NULL );
}

VOID NetClientBase::HashPassword( const WCHAR* strPassword )
{
    if( strPassword != nullptr )
    {
        //TODO: proper password hashing
        wcscpy_s( m_strHashedPassword, L"password" );
    }
    else
    {
        m_strHashedPassword[0] = L'\0';
    }
}

DWORD NetClientBase::ThreadEntry( VOID* pParam )
{
    NetClientBase* pClient = (NetClientBase*)pParam;
    return pClient->Loop();
}

VOID NetClientBase::DisconnectAndWait()
{
    StopNodeLogging();
    RequestDisconnect();

    WaitForSingleObject( m_hThread, 5000 );
    m_hThread = INVALID_HANDLE_VALUE;

    m_ConnectionState = ConnectionState::Disconnected;
}

DWORD NetClientBase::Loop()
{
    assert( m_ConnectionState == ConnectionState::Connecting );

    SOCKADDR_IN ServerAddress;
    HRESULT hr = NetUdpSocket::DnsLookupHostname( m_strServerName, m_ServerPort, &ServerAddress );
    if( FAILED(hr) )
    {
        m_ConnectionState = ConnectionState::InvalidHostname;
        return 1;
    }

    hr = m_Socket.Initialize( ServerAddress );
    if( FAILED(hr) )
    {
        m_ConnectionState = ConnectionState::InvalidHostname;
        return 1;
    }

    m_Encoder.Initialize( &m_Socket, nullptr );
    m_SendSnapshotIndex = 1;

    const UINT64 ConnectTimeoutMsec = 2000;

    // Attempt connection:
    ReliableMessage ConnectMsg;
    ZeroMemory( &ConnectMsg, sizeof(ConnectMsg) );
    ConnectMsg.Opcode = (UINT)ReliableMessageType::ConnectAttempt;
    ConnectMsg.BufferSizeBytes = sizeof(RMsg_ConnectAttempt);
    auto* pCA = (RMsg_ConnectAttempt*)ConnectMsg.Buffer;
    pCA->ProtocolVersion = NET_PROTOCOL_VERSION;
    pCA->Nonce = m_Nonce;
    pCA->ClientTicks.QuadPart = 0;
    QueryPerformanceFrequency( &pCA->ClientTickFreq );
    wcscpy_s( pCA->strUserName, m_strUserName );
    wcscpy_s( pCA->strHashedPassword, m_strHashedPassword );

    m_CurrentTime = 0;
    m_LastRecvTime = 0;

    while( m_ConnectAttempts < 5 && m_ConnectionState == ConnectionState::Connecting )
    {
        UINT64 EndTime = GetTickCount64() + ConnectTimeoutMsec;

        // Send connect attempt:
        StateSnapshot* pSnapshot = new StateSnapshot( m_SendSnapshotIndex++ );
        m_SendQueue.QueueSnapshot( pSnapshot );
        QueryPerformanceCounter( &pCA->ClientTicks );
        m_SendQueue.QueueReliableMessage( ConnectMsg );
        m_SendQueue.SendUpdate( &m_Encoder, nullptr );

        while( GetTickCount64() < EndTime )
        {
            // Try to receive connect attempt:
            Sleep(1);

            HRESULT hr = ReceiveFromServer();
            if( m_ConnectionState != ConnectionState::Connecting )
            {
                break;
            }
        }

        ++m_ConnectAttempts;
    }

    if( m_ConnectionState != ConnectionState::Connected )
    {
        m_ConnectionState = ConnectionState::Timeout;
        return 0;
    }

    LARGE_INTEGER StartTime;
    QueryPerformanceCounter( &StartTime );
    LARGE_INTEGER CurrentTime;
    CurrentTime.QuadPart = StartTime.QuadPart;
    INT64 NextTime = CurrentTime.QuadPart;
    INT64 LastTime = CurrentTime.QuadPart;

    m_LastRecvTime = CurrentTime.QuadPart;

    const DOUBLE SecondsPerTick = 1.0 / (DOUBLE)m_PerfFreq.QuadPart;

    InitializeClient();

    m_StateIO.SetSnapshotIndex( m_SendSnapshotIndex );

    // Client simulation loop:
    while( !m_Disconnect )
    {
        while( CurrentTime.QuadPart < NextTime )
        {
            Sleep(1);
            if( m_Disconnect )
            {
                break;
            }
            QueryPerformanceCounter( &CurrentTime );
        }

        m_CurrentTime = CurrentTime.QuadPart;

        assert( m_pCurrentStats != nullptr );
        m_pCurrentStats->Timestamp.QuadPart = CurrentTime.QuadPart;

        NextTime = CurrentTime.QuadPart + m_FrameTicks;

        INT64 DeltaTicks = CurrentTime.QuadPart - LastTime;
        DeltaTicks = std::min( DeltaTicks, m_FrameTicks );
        DOUBLE AbsoluteTime = (DOUBLE)( CurrentTime.QuadPart - StartTime.QuadPart ) * SecondsPerTick;
        FLOAT DeltaTime = (FLOAT)( (DOUBLE)( CurrentTime.QuadPart - LastTime ) * SecondsPerTick );
        LastTime = CurrentTime.QuadPart;

        g_CurrentRecvTimestamp = CurrentTime;
        HRESULT hr = ReceiveFromServer();
        if( FAILED(hr) )
        {
            m_Disconnect = TRUE;
        }

        StateSnapshot* pCurrentSnapshot = m_StateIO.CreateSnapshot();

        // Add locally generated state to snapshot and queue any reliable messages:
        TickClient( DeltaTime, AbsoluteTime, pCurrentSnapshot, &m_SendQueue );

        m_SendQueue.QueueSnapshot( pCurrentSnapshot );

        m_SendQueue.SendUpdate( &m_Encoder, m_pCurrentStats );

        pCurrentSnapshot->Release();

        NextStatisticsFrame();
    }

    if( m_ConnectionState == ConnectionState::Connected )
    {
        m_SendQueue.QueueReliableMessage( (UINT)ReliableMessageType::Disconnect );
        StateSnapshot* pCurrentSnapshot = m_StateIO.CreateSnapshot();
        m_SendQueue.QueueSnapshot( pCurrentSnapshot );
        m_SendQueue.SendUpdate( &m_Encoder, nullptr );
        pCurrentSnapshot->Release();
    }

    m_ConnectionState = ConnectionState::Disconnected;

    TerminateClient();

    return 0;
}

HRESULT NetClientBase::ReceiveFromServer()
{
    static BYTE ReceiveBuffer[NET_SEND_RECV_BUFFER_SIZE_BYTES];

    m_DecoderLog.ResetIndices();

    while( TRUE )
    {
        UINT BytesReceived = 0;
        SOCKADDR_IN Addr;
        HRESULT hr = m_Socket.RecvFrom( ReceiveBuffer, sizeof(ReceiveBuffer), &BytesReceived, &Addr );
        if( FAILED(hr) )
        {
            return E_FAIL;
        }
        if( BytesReceived == 0 )
        {
            if( m_CurrentTime >= ( m_LastRecvTime + m_PerfFreq.QuadPart / 2 ) )
            {
                m_DataReceivedRecently = FALSE;
            }
            if( m_CurrentTime >= ( m_LastRecvTime + m_PerfFreq.QuadPart * NET_TIMEOUT_SECONDS ) )
            {
                // Disconnect from timeout.
                DbgPrint( "Disconnected from server. (timeout)\n" );
                return E_FAIL;
            }
            return S_OK;
        }
        m_DataReceivedRecently = TRUE;

        if( m_PacketDiscardFraction > 0 )
        {
            if( rand() <= m_PacketDiscardFraction )
            {
                // Drop packet
                continue;
            }
        }

        m_pCurrentStats->BytesReceived += BytesReceived;
        m_pCurrentStats->PacketsReceived++;

        m_LastRecvTime = m_CurrentTime;

        //     CHAR strAddress[32];
        //     FormatAddress( strAddress, ARRAYSIZE(strAddress), Addr );
        //     DbgPrint( "Received %u bytes from %s port %u\n", BytesReceived, strAddress, Addr.sin_port );
        AllocatePacketQueue();
        assert(m_pCurrentPacketQueue != nullptr);
        hr = NetDecoder::DecodePacket( nullptr, &m_LastReliableMessageIndex, &m_DecodeHandlers, nullptr, ReceiveBuffer, BytesReceived, m_pCurrentStats, m_pCurrentPacketQueue, &m_DecoderLog );
        if( FAILED(hr) )
        {
            return hr;
        }
    }

    return S_OK;
}

BOOL NetClientBase::HandleReliableMessage( VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes )
{
    assert( pSenderContext == nullptr );

    switch( Opcode )
    {
    case ReliableMessageType::ConnectAck:
        {
            LARGE_INTEGER CurrentTime;
            QueryPerformanceCounter(&CurrentTime);
            assert( PayloadSizeBytes >= sizeof(RMsg_ConnectAck) );
            auto* pData = (const RMsg_ConnectAck*)pPayload;
            if( pData->Success != 0 )
            {
                DbgPrint( "Server accepted connection attempt.\n" );
                m_ConnectionState = ConnectionState::Connected;
                m_ServerTimeBase = pData->ServerTicks.QuadPart;
                m_ServerTickFreq = pData->ServerTickFreq.QuadPart;
                m_ClientTimeBase = (CurrentTime.QuadPart + pData->ClientTicks.QuadPart) >> 1;
            }
            else
            {
                DbgPrint( "Server rejected connection attempt.\n" );
                m_ConnectionState = ConnectionState::Disconnected;
            }
            return TRUE;
        }
    case ReliableMessageType::ClientConnected:
        {
            assert( PayloadSizeBytes >= sizeof(RMsg_ClientConnected) );
            auto* pData = (const RMsg_ClientConnected*)pPayload;
            auto iter = m_NetworkClients.find( pData->Nonce );
            if( iter != m_NetworkClients.end() )
            {
                assert( iter->second->m_ID == pData->Nonce );
                return TRUE;
            }
            NetworkClient* pNewClient = new NetworkClient();
            pNewClient->m_ID = pData->Nonce;
            wcscpy_s( pNewClient->m_strUserName, pData->strUserName );
            pNewClient->Self = ( pNewClient->m_ID == m_Nonce );
            m_NetworkClients[pNewClient->m_ID] = pNewClient;
            return TRUE;
        }
    case ReliableMessageType::ClientDisconnected:
        {
            assert( PayloadSizeBytes >= sizeof(RMsg_ClientDisconnected) );
            auto* pData = (const RMsg_ClientDisconnected*)pPayload;
            auto iter = m_NetworkClients.find( pData->Nonce );
            if( iter != m_NetworkClients.end() )
            {
                NetworkClient* pClient = iter->second;
                m_NetworkClients.erase( iter );
                delete pClient;
            }
            return TRUE;
        }
    }

    return FALSE;
}

BOOL NetClientBase::HandleAcknowledge( VOID* pSenderContext, const UINT SnapshotIndex )
{
    m_SendQueue.AckSnapshot( SnapshotIndex );
    //DbgPrint( "Received server ack of client snapshot %u.\n", SnapshotIndex );
    return TRUE;
}

BOOL NetClientBase::HandleBeginSnapshot( VOID* pSenderContext, const UINT SnapshotIndex )
{
    if( SnapshotIndex <= 1 && m_AckTracker.GetCurrentSnapshotIndex() >= 2 )
    {
        // Client session has reconnected to a new server.  Disconnect now.
        m_Disconnect = TRUE;
    }

    //DbgPrint( "Received begin server snapshot %u.\n", SnapshotIndex );
    return m_AckTracker.BeginSnapshot( SnapshotIndex );
}

BOOL NetClientBase::HandleEndSnapshot( VOID* pSenderContext, const UINT SnapshotIndex, const UINT PacketCount )
{
    m_AckTracker.EndSnapshot( SnapshotIndex, PacketCount );
    //DbgPrint( "Received end server snapshot %u.\n", SnapshotIndex );

    bool IsSnapshotGood = !m_AckTracker.LastSnapshotFractured();
    CompletePacketQueue(IsSnapshotGood);

    return TRUE;
}

BOOL NetClientBase::HandleCreateNode( VOID* pSenderContext, StateInputOutput* pStateIO, const UINT32 ParentNodeID, const UINT32 NodeID, const StateNodeType Type, const UINT32 CreationCode, const SIZE_T CreationDataSizeBytes, const VOID* pCreationData )
{
    assert( pStateIO == &m_StateIO );
    assert( NodeID != 0 );

    // Check for duplicate creation:
    if( FindRemoteProxyObject( NodeID ) != nullptr || pStateIO->FindNode( NodeID ) != nullptr )
    {
        return TRUE;
    }

    if( Type == StateNodeType::Complex )
    {
        // Create the state link node for this client proxy:
        pStateIO->CreateNode( ParentNodeID, NodeID, Type, nullptr, 0, 0, nullptr, 0, FALSE );

        // Determine if we have a parent remote proxy object:
        INetworkObject* pParentProxy = FindRemoteProxyObject( ParentNodeID );

        // Create a new remote proxy for the given object type, using pCreationData to determine what type of object to create:
        INetworkObject* pProxy = CreateRemoteProxyObject( pParentProxy, NodeID, pCreationData, CreationDataSizeBytes );
    }
    else
    {
        assert( ParentNodeID != 0 );

        // Find the parent remote proxy object using ParentNodeID:
        INetworkObject* pParentProxy = FindRemoteProxyObject( ParentNodeID );
        if( pParentProxy == nullptr )
        {
            return FALSE;
        }

        if( CreationDataSizeBytes == 0 )
        {
            // Decode CreationContext to get a pointer to the member data storage for that member:
            VOID* pMemberData = nullptr;
            SIZE_T MemberSizeBytes = 0;
            BOOL FoundMember = pParentProxy->FindMemberData( Type, CreationCode, &pMemberData, &MemberSizeBytes );

            // Create a state link node to point to the member data storage:
            if( FoundMember )
            {
                pStateIO->CreateNode( ParentNodeID, NodeID, Type, pMemberData, MemberSizeBytes, 0, nullptr, 0, FALSE );
            }
        }
        else
        {
            assert( pCreationData != nullptr );
            VOID* pMemberData = nullptr;
            SIZE_T MemberSizeBytes = 0;
            BOOL CreatedMember = pParentProxy->CreateDynamicChildNode( pCreationData, CreationDataSizeBytes, Type, &pMemberData, &MemberSizeBytes );
            if( CreatedMember )
            {
                pStateIO->CreateNode( ParentNodeID, NodeID, Type, pMemberData, MemberSizeBytes, 0, pCreationData, CreationDataSizeBytes, FALSE );
            }
            else
            {
                DbgPrint( "Failed to create dynamic child node %u of node %u.\n", NodeID, ParentNodeID );
            }
        }
    }

    return TRUE;
}

BOOL NetClientBase::HandleDeleteNode( VOID* pSenderContext, StateInputOutput* pStateIO, const UINT32 NodeID )
{
    DeleteRemoteProxyObject( NodeID );
    pStateIO->DeleteNodeAndChildren( NodeID );

    return TRUE;
}

INetworkObject* NetClientBase::FindRemoteProxyObject( UINT ID )
{
    if( ID == 0 )
    {
        return nullptr;
    }

    RemoteProxyObjectMap::iterator iter = m_RemoteProxies.find( ID );
    if( iter != m_RemoteProxies.end() )
    {
        return iter->second;
    }

    return nullptr;
}

VOID NetClientBase::DeleteRemoteProxyObject( UINT ID )
{
    assert( ID != 0 );

    RemoteProxyObjectMap::iterator iter = m_RemoteProxies.find( ID );
    if( iter != m_RemoteProxies.end() )
    {
        DeleteRemoteObject( iter->second );
        m_RemoteProxies.erase( iter );
    }
}

INetworkObject* NetClientBase::CreateRemoteProxyObject( INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes )
{
    assert( ID != 0 );
    assert( FindRemoteProxyObject( ID ) == nullptr );

    INetworkObject* pProxy = CreateRemoteObject( pParentObject, ID, pCreationData, CreationDataSizeBytes );

    if( pProxy != nullptr )
    {
        pProxy->SetNodeID( ID );
        m_RemoteProxies[ID] = pProxy;
    }

    return pProxy;
}

const NetworkClient* NetClientBase::FindClient( USHORT ID ) const
{
    auto iter = m_NetworkClients.find( ID );
    if( iter != m_NetworkClients.end() )
    {
        return iter->second;
    }
    return nullptr;
}

VOID NetClientBase::SendAnnouncement( USHORT DestinationClientID, const CHAR* strMessage )
{
    ReliableMessage RMsg;
    RMsg.Opcode = (UINT)ReliableMessageType::SubmitChat;
    auto* pHeader = RMsg.CreatePayload<RMsg_SubmitChat>();
    pHeader->DestinationClientID = DestinationClientID;

    UINT MessageLength = (UINT)strlen( strMessage ) + 1;
    UINT CopyMessageLength = 0;
    CHAR* pMsgCopy = (CHAR*)RMsg.PostPayload( MessageLength, &CopyMessageLength );
    strncpy_s( pMsgCopy, CopyMessageLength, strMessage, CopyMessageLength - 1 );
    pMsgCopy[CopyMessageLength - 1] = '\0';

    m_SendQueue.QueueReliableMessage( RMsg );
}

HRESULT NetClientBase::StartLogging( const WCHAR* strFileName )
{
    return m_LogFile.Open( strFileName, ARRAYSIZE(g_ClientLogFileColumns), g_ClientLogFileColumns );
}

HRESULT NetClientBase::StopLogging()
{
    return m_LogFile.Close();
}

HRESULT NetClientBase::StartPacketLogging()
{
    HRESULT hr = m_Encoder.OpenLogFile( L"Client", m_strUserName );
    if( FAILED(hr) )
    {
        return hr;
    }
    return m_DecoderLog.OpenLogFile( L"Client", m_strUserName );
}

HRESULT NetClientBase::StopPacketLogging()
{
    m_Encoder.CloseLogFile();
    return m_DecoderLog.CloseLogFile();
}

VOID NetClientBase::EnablePacketDropTesting( UINT Numerator, UINT Denominator )
{
    if( Numerator == 0 || Denominator == 0 )
    {
        m_PacketDiscardFraction = 0;
        return;
    }

    Numerator = std::min( Numerator, Denominator );

    m_PacketDiscardFraction = (USHORT)( ( Numerator * RAND_MAX ) / Denominator );
}

HRESULT NetClientBase::StartNodeLogging( const WCHAR* strFileName, UINT32 NodeID )
{
    StateLinkNode* pNode = m_StateIO.FindNode( NodeID );
    if (pNode != nullptr)
    {
        HRESULT hr = m_StateIO.StartLogging( strFileName );
        if (SUCCEEDED(hr))
        {
            m_StateIO.SetLoggingNode( pNode );
        }
        return hr;
    }
    return E_FAIL;
}

HRESULT NetClientBase::StopNodeLogging()
{
    return m_StateIO.StopLogging();
}

void NetClientBase::CompletePacketQueue(bool IsQueueGood)
{
    assert(m_pCurrentPacketQueue != nullptr);

    if (IsQueueGood && m_pCurrentPacketQueue->GetUsedSizeBytes() > 0)
    {
        if (m_ProcessPacketQueueOnMainThread)
        {
            EnterCriticalSection(&m_PacketQueueCritSec);
            m_PacketQueues.push_back(m_pCurrentPacketQueue);
            LeaveCriticalSection(&m_PacketQueueCritSec);
            m_pCurrentPacketQueue = nullptr;
        }
        else
        {
            const BYTE* pBuffer = nullptr;
            SIZE_T SizeBytes = 0;
            m_pCurrentPacketQueue->GetBuffer(&pBuffer, &SizeBytes);
            NetDecoder::DecodePacket(nullptr, nullptr, &m_DecodeHandlers, &m_StateIO, pBuffer, (UINT32)SizeBytes, nullptr, nullptr, nullptr);
        }
    }

    if (!m_ProcessPacketQueueOnMainThread)
    {
        m_pCurrentPacketQueue->Reset();
    }
}

PacketQueue* NetClientBase::AllocatePacketQueue()
{
    if (m_pCurrentPacketQueue != nullptr)
    {
        return m_pCurrentPacketQueue;
    }

    assert(m_ProcessPacketQueueOnMainThread);

    EnterCriticalSection(&m_PacketQueueCritSec);
    if (!m_UnusedPacketQueues.empty())
    {
        m_pCurrentPacketQueue = m_UnusedPacketQueues.front();
        m_UnusedPacketQueues.pop_front();
    }
    LeaveCriticalSection(&m_PacketQueueCritSec);

    if (m_pCurrentPacketQueue == nullptr)
    {
        m_pCurrentPacketQueue = new PacketQueue();
    }

    return m_pCurrentPacketQueue;
}

void NetClientBase::SingleThreadedTick()
{
    assert(m_ProcessPacketQueueOnMainThread);

    // apply latest snapshots now
    while (!m_PacketQueues.empty())
    {
        PacketQueue* pPQ = nullptr;

        EnterCriticalSection(&m_PacketQueueCritSec);
        if (!m_PacketQueues.empty())
        {
            pPQ = m_PacketQueues.front();
            m_PacketQueues.pop_front();
        }
        LeaveCriticalSection(&m_PacketQueueCritSec);

        if (pPQ != nullptr)
        {
            const BYTE* pBuffer = nullptr;
            SIZE_T SizeBytes = 0;
            pPQ->GetBuffer(&pBuffer, &SizeBytes);
            NetDecoder::DecodePacket(nullptr, nullptr, &m_DecodeHandlers, &m_StateIO, pBuffer, (UINT32)SizeBytes, nullptr, nullptr, nullptr);
            pPQ->Reset();

            EnterCriticalSection(&m_PacketQueueCritSec);
            m_UnusedPacketQueues.push_back(pPQ);
            LeaveCriticalSection(&m_PacketQueueCritSec);
        }
    }
}

INT64 NetClientBase::GetCurrentServerTimeEstimate() const
{
    LARGE_INTEGER CurrentClientTime;
    QueryPerformanceCounter(&CurrentClientTime);
    const INT64 ClientDelta = CurrentClientTime.QuadPart - m_ClientTimeBase;
    const INT64 ServerDelta = (ClientDelta * m_ServerTickFreq) / m_PerfFreq.QuadPart;
    return m_ServerTimeBase + ServerDelta;
}