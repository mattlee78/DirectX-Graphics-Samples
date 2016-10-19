#include "pch.h"
#include "NetServerBase.h"
#include <assert.h>
#include "NetConstants.h"

VOID ConnectedClient::Send( NetFrameStatistics* pStats )
{
    UINT SentSnapshot = m_SendQueue.SendUpdate( &m_Encoder, pStats );
}

NetServerBase::NetServerBase(void)
    : m_Running( FALSE ),
      m_Started( FALSE ),
      m_hThread( INVALID_HANDLE_VALUE ),
      m_CurrentSnapshotIndex( 0 ),
      m_NetworkPacketLogging( FALSE ),
      m_PacketDiscardFraction( 0 )
{
    InitializeCriticalSection( &m_CritSec );
    m_PostInitializeHold = true;
    QueryPerformanceFrequency( &m_PerfFreq );
    m_DecodeHandlers.AddHandler( this );
    m_StateIO.SetClientMode( FALSE );
    SetName( "Server" );
    AddDebugListener( &m_LogFile );

    ZeroMemory( m_Statistics, sizeof(m_Statistics) );
    m_CurrentStatisticsIndex = 0;
    m_pCurrentStats = &m_Statistics[0];
}

NetServerBase::~NetServerBase(void)
{
    StopLogging();
    DeleteCriticalSection( &m_CritSec );
}

NetFrameStatistics* NetServerBase::NextStatisticsFrame()
{
    m_pCurrentStats->Finished = TRUE;

    m_CurrentStatisticsIndex = ( m_CurrentStatisticsIndex + 1 ) % ARRAYSIZE( m_Statistics );
    m_pCurrentStats = &m_Statistics[m_CurrentStatisticsIndex];
    m_pCurrentStats->Zero();
    return m_pCurrentStats;
}

HRESULT NetServerBase::Start( UINT FramesPerSecond, USHORT PortNum, bool Threaded )
{
    assert( !m_Running );

    m_FrameTicks = m_PerfFreq.QuadPart / (INT64)FramesPerSecond;

    InitializeWinsock();
    HRESULT hr = m_ListenSocket.Bind( PortNum );
    if( FAILED(hr) )
    {
        return E_FAIL;
    }

    m_Running = TRUE;

    if (Threaded)
    {
        m_hThread = CreateThread(NULL, 0, ThreadEntry, this, 0, NULL);
    }
    else
    {
        m_hThread = nullptr;
        InitializeServer();
        QueryPerformanceCounter(&m_StartTime);
        m_NextFrameTime = m_StartTime.QuadPart + m_FrameTicks;
        m_LastFrameTime = m_StartTime.QuadPart;
        m_NextClientReport = m_StartTime.QuadPart;
        m_CurrentTime = m_StartTime.QuadPart;
        m_Started = TRUE;
        CompletePostInitialize();
    }
    return S_OK;
}

DWORD NetServerBase::ThreadEntry( VOID* pParam )
{
    NetServerBase* pSS = (NetServerBase*)pParam;
    return pSS->Loop();
}

VOID NetServerBase::Stop()
{
    m_Running = FALSE;

    if (m_hThread != nullptr)
    {
        WaitForSingleObject(m_hThread, 5000);
        m_hThread = INVALID_HANDLE_VALUE;
    }
    else
    {
        // one last tick
        SingleThreadedTick();
    }

    m_ListenSocket.Disconnect();

    TerminateWinsock();
}

DWORD NetServerBase::Loop()
{
    assert( !m_Started );

    InitializeServer();

    QueryPerformanceCounter( &m_StartTime );
    m_NextFrameTime = m_StartTime.QuadPart + m_FrameTicks;
    m_LastFrameTime = m_StartTime.QuadPart;
    m_NextClientReport = m_StartTime.QuadPart;
    m_CurrentTime = m_StartTime.QuadPart;

    m_Started = TRUE;

    while (m_PostInitializeHold)
    {
        Sleep(5);
    }

    while( m_Running )
    {
        bool TickExecuted = SingleThreadedTick();
        if (!TickExecuted)
        {
            Sleep(1);
        }
    }

    return 0;
}

bool NetServerBase::SingleThreadedTick()
{
    if (!m_Running)
    {
        if (m_Started)
        {
            m_Started = FALSE;
            TerminateServer();
        }
        return false;
    }

    LARGE_INTEGER CurrentTime;
    QueryPerformanceCounter(&CurrentTime);

    if (CurrentTime.QuadPart < m_NextFrameTime)
    {
        return false;
    }

    m_NextFrameTime = CurrentTime.QuadPart + m_FrameTicks;
    m_CurrentTime = CurrentTime.QuadPart;

    INT64 DeltaTicks = CurrentTime.QuadPart - m_LastFrameTime;
    DeltaTicks = std::min(DeltaTicks, m_FrameTicks);
    const DOUBLE SecondsPerTick = 1.0 / (DOUBLE)m_PerfFreq.QuadPart;
    DOUBLE AbsoluteTime = (DOUBLE)(CurrentTime.QuadPart - m_StartTime.QuadPart) * SecondsPerTick;
    FLOAT DeltaTime = (FLOAT)((DOUBLE)(DeltaTicks)* SecondsPerTick);
    m_LastFrameTime = CurrentTime.QuadPart;

    assert(m_pCurrentStats != nullptr);
    m_pCurrentStats->Timestamp.QuadPart = CurrentTime.QuadPart;

    // Process all incoming packets from all clients:
    BOOL IncomingResult = ProcessIncomingPackets();

    // Scan for dead clients:
    {
        const INT64 ExpiredTime = m_CurrentTime - m_PerfFreq.QuadPart * NET_TIMEOUT_SECONDS;

        EnterLock();
        auto iter = m_Clients.begin();
        auto end = m_Clients.end();
        while (iter != end)
        {
            auto nextiter = iter;
            ++nextiter;
            ConnectedClient* pCC = iter->second;
            if (pCC->m_LastRecvTime <= ExpiredTime)
            {
                const USHORT ID = pCC->m_ID;
                ProcessClientDisconnected(pCC);

                delete pCC;
                m_Clients.erase(iter);

                auto iditer = m_ClientsByID.find(ID);
                if (iditer != m_ClientsByID.end())
                {
                    m_ClientsByID.erase(iditer);
                }
            }
            iter = nextiter;
        }
        LeaveLock();
    }

    if (m_NextClientReport <= m_CurrentTime && m_LogFile.IsOpen())
    {
        GenerateClientReport();
        const INT64 ClientReportInterval = m_PerfFreq.QuadPart * 5;
        m_NextClientReport = m_CurrentTime + ClientReportInterval;
    }

    TickServer(DeltaTime, AbsoluteTime);

    StateSnapshot* pCurrentSnapshot = m_StateIO.CreateSnapshot();
    m_CurrentSnapshotIndex = pCurrentSnapshot->GetIndex();

    // Distribute snapshot to client send queues
    {
        EnterLock();
        auto iter = m_Clients.begin();
        auto end = m_Clients.end();
        while (iter != end)
        {
            ConnectedClient* pCC = iter->second;
            if (pCC->IsConnected())
            {
                pCC->m_SendQueue.QueueSnapshot(pCurrentSnapshot);
                pCC->Send(m_pCurrentStats);
            }
            ++iter;
        }
        LeaveLock();
    }

    pCurrentSnapshot->Release();

    NextStatisticsFrame();

    return true;
}

VOID NetServerBase::ProcessClientDisconnected( ConnectedClient* pClient )
{
    ReliableMessage rmsg;
    rmsg.Opcode = (UINT)ReliableMessageType::ClientDisconnected;
    auto* pPayload = rmsg.CreatePayload<RMsg_ClientDisconnected>();
    pPayload->Nonce = pClient->m_ID;

    auto iter = m_Clients.begin();
    auto end = m_Clients.end();
    while( iter != end )
    {
        ConnectedClient* pCC = iter->second;
        ++iter;

        if( !pCC->IsConnected() )
        {
            continue;
        }

        if( pCC == pClient )
        {
            continue;
        }

        pCC->m_SendQueue.QueueReliableMessage( rmsg );
    }

    ClientDisconnected( pClient );

    pClient->m_Encoder.CloseLogFile();

    const CHAR* strDisconnectReason = "timeout";
    if( pClient->m_LastRecvTime == 0 )
    {
        strDisconnectReason = "clean disconnect";
    }

    SendServerAnnouncement( "%S disconnected from the server (%s).", pClient->m_strUserName, strDisconnectReason );
    DbgPrint( "Client \"%S\" [CID %u] disconnected. (%s)\n", pClient->m_strUserName, pClient->m_ID, strDisconnectReason );
}

BOOL NetServerBase::ProcessIncomingPackets()
{
    static BYTE PacketBuffer[NET_SEND_RECV_BUFFER_SIZE_BYTES];

    UINT BytesReceived = 0;
    do 
    {
        SOCKADDR_IN SenderAddress;
        HRESULT hr = m_ListenSocket.RecvFrom( PacketBuffer, sizeof(PacketBuffer), &BytesReceived, &SenderAddress );
        if( SUCCEEDED(hr) )
        {
            if( BytesReceived > 0 )
            {
                if( m_PacketDiscardFraction > 0 )
                {
                    if( rand() <= m_PacketDiscardFraction )
                    {
                        continue;
                    }
                }

//                 CHAR strAddress[20];
//                 FormatAddress( strAddress, ARRAYSIZE(strAddress), SenderAddress );
//                 DbgPrint( "Received %u bytes from %s port %u\n", BytesReceived, strAddress, SenderAddress.sin_port );
                m_pCurrentStats->BytesReceived += BytesReceived;
                m_pCurrentStats->PacketsReceived++;
                ProcessPacket( PacketBuffer, BytesReceived, SenderAddress );
            }
        }
        else
        {
            return FALSE;
        }
    } while ( BytesReceived > 0 );

    return TRUE;
}

BOOL NetServerBase::ProcessPacket( const BYTE* pPacket, UINT32 SizeBytes, const SOCKADDR_IN& SenderAddress )
{
    ConnectedClient* pClient = FindOrAddClient( SenderAddress );

    pClient->m_LastRecvTime = m_CurrentTime;

    NetDecoder::DecodePacket( pClient, &pClient->m_LastReliableMessageIndex, &m_DecodeHandlers, &m_StateIO, pPacket, SizeBytes, m_pCurrentStats, nullptr, nullptr );

    return TRUE;
}

BOOL NetServerBase::HandleReliableMessage( VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes )
{
    auto* pClient = (ConnectedClient*)pSenderContext;

    //DbgPrint( "Server reliable msg index %u type %u from client %I64x\n", UniqueIndex, Opcode, pSenderContext );

    switch( Opcode )
    {
    case ReliableMessageType::ConnectAttempt:
        {
            assert( PayloadSizeBytes >= sizeof(RMsg_ConnectAttempt) );
            auto* pData = (const RMsg_ConnectAttempt*)pPayload;

            if( pClient->m_ID == pData->Nonce )
            {
                // duplicate connect message; discard
                return TRUE;
            }
            else if( pClient->m_ID == 0 )
            {
                BOOL AckSuccess = TRUE;

                if( pData->ProtocolVersion != NET_PROTOCOL_VERSION )
                {
                    AckSuccess = FALSE;
                }

                // TODO: validate password in pData->strHashedPassword

                pClient->m_ID = pData->Nonce;
                m_ClientsByID[pClient->m_ID] = pClient;
                QueryPerformanceCounter( &pClient->m_ServerTicksAtConnect );
                pClient->m_ClientTicksAtConnect = pData->ClientTicks;
                pClient->m_ClientTickFreq = pData->ClientTickFreq;

                ReliableMessage RMsg;
                RMsg.Opcode = (UINT)ReliableMessageType::ConnectAck;
                auto* pAck = RMsg.CreatePayload<RMsg_ConnectAck>();
                pAck->Nonce = pClient->m_ID;
                pAck->Success = AckSuccess;
                pAck->ServerTicks = pClient->m_ServerTicksAtConnect;
                QueryPerformanceFrequency( &pAck->ServerTickFreq );
                pAck->ClientTicks = pData->ClientTicks;

                pClient->m_SendQueue.QueueReliableMessage( RMsg );

                if( AckSuccess )
                {
                    wcscpy_s( pClient->m_strUserName, pData->strUserName );
                    ProcessClientConnected( pClient );
                }
                else
                {
                    DbgPrint( "Client CID %u did not connect successfully.\n", pClient->m_ID );
                    pClient->m_LastRecvTime = 0;
                }

                return TRUE;
            }
            else if( pClient->m_ID != pData->Nonce )
            {
                DbgPrint( "Invalid client connection!\n" );
            }

            return TRUE;
        }
    case ReliableMessageType::Disconnect:
        {
            pClient->m_LastRecvTime = 0;
            return TRUE;
        }
    case ReliableMessageType::SubmitChat:
        {
            assert( PayloadSizeBytes >= sizeof(RMsg_SubmitChat) );
            auto* pData = (const RMsg_SubmitChat*)pPayload;
            const CHAR* pChat = (const CHAR*)( (const BYTE*)pData + sizeof(RMsg_SubmitChat) );

            if( m_LogFile.IsOpen() )
            {
                m_LogFile.WriteLine( m_CurrentTime, "Chat from %S: %s\n", pClient->m_strUserName, pChat );
            }

            if( pChat[0] == '\\' || pChat[0] == '/' )
            {
                HandleChatCommand( pClient, pData->DestinationClientID, pChat );
            }
            else
            {
                SendAnnouncement( pClient->m_ID, pData->DestinationClientID, pChat );
            }
            return TRUE;
        }
    default:
        return FALSE;
    }
}

VOID NetServerBase::ProcessClientConnected( ConnectedClient* pClient )
{
    CHAR strAddress[32];
    FormatAddress( strAddress, ARRAYSIZE(strAddress), pClient->m_Address );
    DbgPrint( "Client \"%S\" [CID %u] connected from %s.  Client ticks %I64d freq %I64d\n", pClient->m_strUserName, pClient->m_ID, strAddress, pClient->m_ClientTicksAtConnect.QuadPart, pClient->m_ClientTickFreq.QuadPart );
    SendServerAnnouncement( "%S connected to the server.", pClient->m_strUserName );

    if( m_NetworkPacketLogging )
    {
        pClient->m_Encoder.OpenLogFile( L"Server", pClient->m_strUserName );
    }

    ReliableMessage rmsg;
    rmsg.Opcode = (UINT)ReliableMessageType::ClientConnected;
    auto* pPayload = rmsg.CreatePayload<RMsg_ClientConnected>();
    pPayload->Nonce = pClient->m_ID;
    wcscpy_s( pPayload->strUserName, pClient->m_strUserName );

    auto iter = m_Clients.begin();
    auto end = m_Clients.end();
    while( iter != end )
    {
        ConnectedClient* pCC = iter->second;
        ++iter;

        if( !pCC->IsConnected() )
        {
            continue;
        }

        if( pCC == pClient || pCC->m_ID == pClient->m_ID )
        {
            // send all clients incl self
            auto newiter = m_Clients.begin();
            auto newend = m_Clients.end();
            while( newiter != newend )
            {
                ConnectedClient* pOther = newiter->second;
                ++newiter;

                if( !pOther->IsConnected() )
                {
                    continue;
                }

                ReliableMessage rmsgOther;
                rmsgOther.Opcode = (UINT)ReliableMessageType::ClientConnected;
                auto* pPayload = rmsgOther.CreatePayload<RMsg_ClientConnected>();
                pPayload->Nonce = pOther->m_ID;
                wcscpy_s( pPayload->strUserName, pOther->m_strUserName );

                pCC->m_SendQueue.QueueReliableMessage( rmsgOther );
            }
        }
        else
        {
            // send just the new client connect msg
            pCC->m_SendQueue.QueueReliableMessage( rmsg );
        }
    }

    ClientConnected( pClient );
}

BOOL NetServerBase::HandleBeginSnapshot( VOID* pSenderContext, const UINT SnapshotIndex )
{
    auto* pClient = (ConnectedClient*)pSenderContext;

    pClient->m_AckTracker.BeginSnapshot( SnapshotIndex );
    //DbgPrint( "Begin snapshot %u from client %u\n", SnapshotIndex, pClient->m_ID );

    return TRUE;
}

BOOL NetServerBase::HandleEndSnapshot( VOID* pSenderContext, const UINT SnapshotIndex, const UINT PacketCount )
{
    auto* pClient = (ConnectedClient*)pSenderContext;

    pClient->m_AckTracker.EndSnapshot( SnapshotIndex, PacketCount );
    //DbgPrint( "End snapshot %u from client %u\n", SnapshotIndex, pClient->m_ID );

    return TRUE;
}

BOOL NetServerBase::HandleAcknowledge( VOID* pSenderContext, const UINT SnapshotIndex )
{
    auto* pClient = (ConnectedClient*)pSenderContext;

    pClient->m_SendQueue.AckSnapshot( SnapshotIndex );
    //DbgPrint( "Received ack snapshot %u from client %u\n", SnapshotIndex, pClient->m_ID );

    return TRUE;
}

ConnectedClient* NetServerBase::FindOrAddClient( const SOCKADDR_IN& Address )
{
    const UINT64 Hash = HashAddress( Address );

    EnterLock();

    auto iter = m_Clients.find( Hash );
    if( iter != m_Clients.end() )
    {
        LeaveLock();
        return iter->second;
    }

    ConnectedClient* pClient = new ConnectedClient();
    pClient->m_SendQueue.Initialize( m_StateIO.GetNullSnapshot() );
    pClient->m_ID = 0;
    pClient->m_Address = Address;
    pClient->m_Encoder.Initialize( &m_ListenSocket, &Address );
    pClient->m_AckTracker.Initialize( &pClient->m_SendQueue );
    pClient->m_pServerData = nullptr;

    m_Clients[ Hash ] = pClient;

    LeaveLock();

    return pClient;
}

BOOL NetServerBase::HandleCreateNode( VOID* pSenderContext, StateInputOutput* pStateIO, const UINT32 ParentNodeID, const UINT32 NodeID, const StateNodeType Type, const UINT32 CreationCode, const SIZE_T CreationDataSizeBytes, const VOID* pCreationData )
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
        // Determine if we have a parent remote proxy object:
        INetworkObject* pParentProxy = FindRemoteProxyObject( ParentNodeID );

        // Create a new remote proxy for the given object type, using pCreationData to determine what type of object to create:
        INetworkObject* pProxy = CreateRemoteProxyObject( pSenderContext, pParentProxy, NodeID, pCreationData, CreationDataSizeBytes );

        // Create the state link node for this client proxy:
        pStateIO->CreateNode( ParentNodeID, NodeID, Type, nullptr, 0, 0, pCreationData, CreationDataSizeBytes, TRUE );
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
                pStateIO->CreateNode( ParentNodeID, NodeID, Type, pMemberData, MemberSizeBytes, CreationCode, nullptr, 0, TRUE );
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
                pStateIO->CreateNode( ParentNodeID, NodeID, Type, pMemberData, MemberSizeBytes, 0, pCreationData, CreationDataSizeBytes, TRUE );
            }
            else
            {
                DbgPrint( "Failed to create dynamic child node %u of node %u.\n", NodeID, ParentNodeID );
            }
        }
    }

    return TRUE;
}

BOOL NetServerBase::HandleDeleteNode( VOID* pSenderContext, StateInputOutput* pStateIO, const UINT32 NodeID )
{
    DeleteRemoteProxyObject( NodeID );
    pStateIO->DeleteNodeAndChildren( NodeID );

    return TRUE;
}

INetworkObject* NetServerBase::FindRemoteProxyObject( UINT ID )
{
    if( ID == 0 )
    {
        return nullptr;
    }

    NetworkObjectMap::iterator iter = m_RemoteProxies.find( ID );
    if( iter != m_RemoteProxies.end() )
    {
        return iter->second;
    }

    return nullptr;
}

VOID NetServerBase::DeleteRemoteProxyObject( UINT ID )
{
    assert( ID != 0 );

    NetworkObjectMap::iterator iter = m_RemoteProxies.find( ID );
    if( iter != m_RemoteProxies.end() )
    {
        DeleteRemoteObject( iter->second );
        m_RemoteProxies.erase( iter );
    }
}

INetworkObject* NetServerBase::CreateRemoteProxyObject( VOID* pSenderContext, INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes )
{
    assert( ID != 0 );
    assert( FindRemoteProxyObject( ID ) == nullptr );

    INetworkObject* pProxy = CreateRemoteObject( pSenderContext, pParentObject, ID, pCreationData, CreationDataSizeBytes );

    if( pProxy != nullptr )
    {
        pProxy->SetNodeID( ID );
        m_RemoteProxies[ID] = pProxy;
    }

    return pProxy;
}

HRESULT NetServerBase::StartLogging()
{
    WCHAR strLogFileName[MAX_PATH];
    SYSTEMTIME Time;
    GetLocalTime( &Time );
    swprintf_s( strLogFileName, L"NetServer-%u%02u%02u-%02u%02u%02u.txt", Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond );

    return m_LogFile.Open( strLogFileName );
}

HRESULT NetServerBase::StopLogging()
{
    return m_LogFile.Close();
}

VOID NetServerBase::GenerateClientReport()
{
    const UINT ServerSnapshotIndex = m_CurrentSnapshotIndex;
    m_LogFile.WriteLine( m_CurrentTime, "Current snapshot index: %u\n", ServerSnapshotIndex );
    auto iter = m_Clients.begin();
    auto end = m_Clients.end();
    while( iter != end )
    {
        ConnectedClient* pCC = iter->second;
        ++iter;

        const UINT ClientAckSnapshotIndex = pCC->m_SendQueue.GetLastAckSnapshot();
        assert( ClientAckSnapshotIndex <= ServerSnapshotIndex );
        INT SnapshotDelta = (INT)ServerSnapshotIndex - (INT)ClientAckSnapshotIndex;

        m_LogFile.WriteLine( m_CurrentTime, "Client \"%S\" [CID %u]: Last ack snapshot %u (delta %d)\n", pCC->m_strUserName, pCC->m_ID, ClientAckSnapshotIndex, SnapshotDelta );
    }
}

VOID NetServerBase::SendAnnouncement( USHORT SourceClientID, USHORT DestinationClientID, const CHAR* strMessage )
{
    if( m_LogFile.IsOpen() && SourceClientID == 0 )
    {
        m_LogFile.WriteLine( m_CurrentTime, "Chat from Server: %s\n", strMessage );
    }

    ReliableMessage RMsg;
    RMsg.Opcode = (UINT)ReliableMessageType::ReceiveChat;
    auto* pHeader = RMsg.CreatePayload<RMsg_ReceiveChat>();
    pHeader->SourceClientID = SourceClientID;

    UINT MessageLength = (UINT)strlen( strMessage ) + 1;
    UINT CopyMessageLength = 0;
    CHAR* pMsgCopy = (CHAR*)RMsg.PostPayload( MessageLength, &CopyMessageLength );
    strncpy_s( pMsgCopy, CopyMessageLength, strMessage, CopyMessageLength - 1 );
    pMsgCopy[CopyMessageLength - 1] = '\0';

    if( DestinationClientID == 0 )
    {
        auto iter = m_Clients.begin();
        auto end = m_Clients.end();
        while( iter != end )
        {
            ConnectedClient* pCC = iter->second;
            pCC->m_SendQueue.QueueReliableMessage( RMsg );
            ++iter;
        }
    }
    else
    {
        auto iter = m_ClientsByID.find( DestinationClientID );
        if( iter != m_ClientsByID.end() )
        {
            ConnectedClient* pCC = iter->second;
            pCC->m_SendQueue.QueueReliableMessage( RMsg );
        }
    }
}

VOID NetServerBase::SendServerAnnouncement( const CHAR* strFormat, ... )
{
    CHAR strLine[256];
    
    va_list args;
    va_start( args, strFormat );

    vsprintf_s( strLine, strFormat, args );

    va_end( args );

    SendAnnouncement( 0, 0, strLine );
}

VOID NetServerBase::EnablePacketDropTesting( UINT Numerator, UINT Denominator )
{
    if( Numerator == 0 || Denominator == 0 )
    {
        m_PacketDiscardFraction = 0;
        return;
    }

    Numerator = std::min( Numerator, Denominator );

    m_PacketDiscardFraction = (USHORT)( ( Numerator * RAND_MAX ) / Denominator );
}
