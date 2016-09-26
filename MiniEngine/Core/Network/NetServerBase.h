#pragma once

#include <winsock2.h>
#include <windows.h>
#include <vector>
#include <unordered_map>
#include <DirectXMath.h>

#include "DebugPrint.h"

#include "StateLinking.h"
#include "SnapshotSendQueue.h"

#include "NetShared.h"
#include "NetSocket.h"
#include "NetEncoder.h"
#include "NetDecoder.h"

struct ConnectedClient : public NetConnectionBase
{
    SOCKADDR_IN m_Address;

    SnapshotSendQueue m_SendQueue;
    NetEncoder m_Encoder;

    SnapshotAckTracker m_AckTracker;

    INT64 m_LastRecvTime;

    UINT32 m_LastReliableMessageIndex;

    UINT m_ConnectionBaseObjectID;

    LARGE_INTEGER m_ServerTicksAtConnect;
    LARGE_INTEGER m_ClientTicksAtConnect;
    LARGE_INTEGER m_ClientTickFreq;

    ConnectedClient()
        : NetConnectionBase( 0, L"" ),
          m_LastReliableMessageIndex( 0 ),
          m_ConnectionBaseObjectID( 0 )
    {
        m_ClientTicksAtConnect.QuadPart = 0;
        m_ClientTickFreq.QuadPart = 0;
    }

    VOID* m_pServerData;

    BOOL IsConnected() const { return m_ID != 0; }
    VOID Send( NetFrameStatistics* pStats );

    std::vector<INetworkObject*> m_SystemObjects;
};
typedef std::unordered_map<UINT64, ConnectedClient*> ClientMap;
typedef std::unordered_map<USHORT, ConnectedClient*> ClientLookupMap;

class NetServerBase : public IDecodeHandler, public DebugPrintContext, public INetStatistics
{
private:
    LARGE_INTEGER m_PerfFreq;
    INT64 m_FrameTicks;
    LARGE_INTEGER m_StartTime;
    INT64 m_NextFrameTime;
    INT64 m_LastFrameTime;

    HANDLE m_hThread;
    volatile BOOL m_Running;
    BOOL m_Started;

    NetUdpSocket m_ListenSocket;

    ClientLookupMap m_ClientsByID;

    CRITICAL_SECTION m_CritSec;

    NetFrameStatistics m_Statistics[10];
    NetFrameStatistics* m_pCurrentStats;
    UINT m_CurrentStatisticsIndex;

    UINT m_CurrentSnapshotIndex;

    BOOL m_NetworkPacketLogging;
    INT64 m_NextClientReport;

    USHORT m_PacketDiscardFraction;

    bool m_PostInitializeHold;

    DecodeHandlerStack m_DecodeHandlers;

protected:
    StateInputOutput m_StateIO;
    NetworkObjectMap m_RemoteProxies;
    INT64 m_CurrentTime;
    TimestampedLogFile m_LogFile;
    ClientMap m_Clients;

public:
    NetServerBase();
    ~NetServerBase();

    HRESULT Start( UINT FramesPerSecond, USHORT PortNum, bool Threaded );
    VOID Stop();

    bool SingleThreadedTick();

    BOOL IsStarted() const { return m_Started; }
    void CompletePostInitialize() { m_PostInitializeHold = false; }

    VOID EnableNetworkPacketLogging( BOOL Enabled ) { m_NetworkPacketLogging = Enabled; }

    HRESULT StartLogging();
    HRESULT StopLogging();

    ClientMap::const_iterator BeginClients() const { return m_Clients.begin(); }
    ClientMap::const_iterator EndClients() const { return m_Clients.end(); }

    VOID EnterLock() { EnterCriticalSection( &m_CritSec ); }
    VOID LeaveLock() { LeaveCriticalSection( &m_CritSec ); }

    INT64 GetServerTime() const { return m_CurrentTime; }
    INT64 GetServerTimeFreq() const { return m_PerfFreq.QuadPart; }
    UINT GetCurrentSnapshotIndex() const { return m_CurrentSnapshotIndex; }

    NetworkObjectMap::const_iterator BeginRemoteObjects() const { return m_RemoteProxies.cbegin(); }
    NetworkObjectMap::const_iterator EndRemoteObjects() const { return m_RemoteProxies.cend(); }

    UINT GetNumStatisticsFrames() const { return ARRAYSIZE(m_Statistics) - 1; }
    UINT GetCurrentStatisticsFrame() const { return m_CurrentStatisticsIndex; }
    const NetFrameStatistics* GetStatistics( UINT FrameIndex ) const
    { 
        UINT Index = ( m_CurrentStatisticsIndex + ARRAYSIZE(m_Statistics) - FrameIndex - 1 ) % ARRAYSIZE( m_Statistics );
        return &m_Statistics[Index];
    }

    VOID SendAnnouncement( USHORT SourceClientID, USHORT DestinationClientID, const CHAR* strMessage );
    VOID SendServerAnnouncement( const CHAR* strFormat, ... );

    VOID EnablePacketDropTesting( UINT Numerator, UINT Denominator );

    VOID AddDecodeHandler( IDecodeHandler* pHandler ) { EnterLock(); m_DecodeHandlers.AddHandler( pHandler ); LeaveLock(); }

protected:
    virtual VOID InitializeServer() = 0;
    virtual VOID TickServer( FLOAT DeltaTime, DOUBLE AbsoluteTime ) = 0;
    virtual VOID ClientConnected( ConnectedClient* pClient ) {}
    virtual VOID ClientDisconnected( ConnectedClient* pClient ) {}
    virtual VOID HandleChatCommand( ConnectedClient* pSrcClient, USHORT DestinationClientID, const CHAR* strChatLine ) {}
    virtual VOID TerminateServer() = 0;

    virtual INetworkObject* CreateRemoteObject( VOID* pSenderContext, INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes ) = 0;
    virtual VOID DeleteRemoteObject( INetworkObject* pObject ) = 0;

    INetworkObject* CreateRemoteProxyObject( VOID* pSenderContext, INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes );
    INetworkObject* FindRemoteProxyObject( UINT ID );
    VOID DeleteRemoteProxyObject( UINT ID );

private:
    virtual BOOL HandleReliableMessage( VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes );
    virtual BOOL HandleAcknowledge( VOID* pSenderContext, const UINT SnapshotIndex );
    virtual BOOL HandleBeginSnapshot( VOID* pSenderContext, const UINT SnapshotIndex );
    virtual BOOL HandleEndSnapshot( VOID* pSenderContext, const UINT SnapshotIndex, const UINT PacketCount );
    virtual BOOL HandleCreateNode( VOID* pSenderContext, StateInputOutput* pStateIO, const UINT32 ParentNodeID, const UINT32 NodeID, const StateNodeType Type, const UINT32 CreationCode, const SIZE_T CreationDataSizeBytes, const VOID* pCreationData );
    virtual BOOL HandleDeleteNode( VOID* pSenderContext, StateInputOutput* pStateIO, const UINT32 NodeID );

private:
    static DWORD WINAPI ThreadEntry( VOID* pParam );
    DWORD Loop();
    BOOL ProcessIncomingPackets();
    BOOL ProcessPacket( const BYTE* pPacket, UINT32 SizeBytes, const SOCKADDR_IN& SenderAddress );
    ConnectedClient* FindOrAddClient( const SOCKADDR_IN& Address );

    VOID ProcessClientConnected( ConnectedClient* pClient );
    VOID ProcessClientDisconnected( ConnectedClient* pClient );

    NetFrameStatistics* NextStatisticsFrame();

    VOID GenerateClientReport();
};

