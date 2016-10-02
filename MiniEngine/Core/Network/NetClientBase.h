#pragma once

#include <windows.h>
#include "DebugPrint.h"
#include "SnapshotSendQueue.h"
#include "StateLinking.h"

#include "NetShared.h"
#include "NetSocket.h"
#include "NetEncoder.h"
#include "NetDecoder.h"

#include "StructuredLogFile.h"
#include "PacketQueue.h"

enum class ConnectionState
{
    Disconnected = 0,
    Connecting,
    Connected,
    InvalidHostname,
    Timeout,
};

struct NetworkClient : public NetConnectionBase
{
    BOOL Self;
};
typedef std::unordered_map<USHORT, NetworkClient*> NetworkClientMap;

class NetClientBase : public IDecodeHandler, public DebugPrintContext, public INetStatistics
{
private:
    LARGE_INTEGER m_PerfFreq;
    INT64 m_FrameTicks;

    INT64 m_LastRecvTime;

    CHAR m_strServerName[128];
    USHORT m_ServerPort;

    WCHAR m_strUserName[NET_USERNAME_MAXSIZE];
    WCHAR m_strHashedPassword[NET_HASHEDPASSWORD_MAXSIZE];

    USHORT m_Nonce;

    ConnectionState m_ConnectionState;
    UINT m_ConnectAttempts;
    BOOL m_Disconnect;

    NetUdpSocket m_Socket;
    StateSnapshot* m_pNullSnapshot;
    NetEncoder m_Encoder;
    UINT m_SendSnapshotIndex;

    HANDLE m_hThread;

    DecodeHandlerStack m_DecodeHandlers;
    SnapshotAckTracker m_AckTracker;

    typedef std::unordered_map<UINT, INetworkObject*> RemoteProxyObjectMap;
    RemoteProxyObjectMap m_RemoteProxies;

    UINT32 m_LastReliableMessageIndex;

    NetworkClientMap m_NetworkClients;

    NetFrameStatistics m_Statistics[10];
    NetFrameStatistics* m_pCurrentStats;
    UINT m_CurrentStatisticsIndex;

    StructuredLogFile m_LogFile;

    NetDecoderLog m_DecoderLog;

    USHORT m_PacketDiscardFraction;

    BOOL m_DataReceivedRecently;

    std::deque<PacketQueue*> m_PacketQueues;
    std::deque<PacketQueue*> m_UnusedPacketQueues;
    PacketQueue* m_pCurrentPacketQueue;
    bool m_ProcessPacketQueueOnMainThread;
    CRITICAL_SECTION m_PacketQueueCritSec;

protected:
    SnapshotSendQueue m_SendQueue;
    StateInputOutput m_StateIO;
    INT64 m_CurrentTime;
    FLOAT m_AverageDeltaTime;

public:
    NetClientBase();
    ~NetClientBase();

    BOOL IsConnected( UINT* pConnectAttempts ) const 
    { 
        if( pConnectAttempts != nullptr )
        {
            *pConnectAttempts = m_ConnectAttempts; 
        }
        return m_ConnectionState == ConnectionState::Connected; 
    }

    ConnectionState GetConnectionState() const { return m_ConnectionState; }
    const CHAR* GetServerName() const { return m_strServerName; }
    const USHORT GetServerPort() const { return m_ServerPort; }
    const WCHAR* GetUserName() const { return m_strUserName; }

    USHORT GetNonce() const { return m_Nonce; }

    void SetProcessOnMainThread(bool Enabled) { m_ProcessPacketQueueOnMainThread = Enabled; }
    VOID Connect( UINT FramesPerSecond, const CHAR* strServerName, USHORT PortNum, const WCHAR* strUserName, const WCHAR* strPassword );
    VOID DisconnectAndWait();
    VOID RequestDisconnect() { m_Disconnect = TRUE; }

    void SingleThreadedTick();

    NetworkClientMap::const_iterator ClientListBegin() const { return m_NetworkClients.cbegin(); }
    NetworkClientMap::const_iterator ClientListEnd() const { return m_NetworkClients.cend(); }
    const NetworkClient* FindClient( USHORT ID ) const;

    VOID SendAnnouncement( USHORT DestinationClientID, const CHAR* strMessage );

    SnapshotSendQueue& GetSendQueue() { return m_SendQueue; }

    UINT GetNumStatisticsFrames() const { return ARRAYSIZE(m_Statistics) - 1; }
    UINT GetCurrentStatisticsFrame() const { return m_CurrentStatisticsIndex; }
    const NetFrameStatistics* GetStatistics( UINT FrameIndex ) const
    { 
        UINT Index = ( m_CurrentStatisticsIndex + ARRAYSIZE(m_Statistics) - FrameIndex - 1 ) % ARRAYSIZE( m_Statistics );
        return &m_Statistics[Index];
    }

    HRESULT StartLogging( const WCHAR* strFileName );
    HRESULT StopLogging();

    HRESULT StartPacketLogging();
    HRESULT StopPacketLogging();

    HRESULT StartNodeLogging( const WCHAR* strFileName, UINT32 NodeID );
    HRESULT StopNodeLogging();

    VOID EnablePacketDropTesting( UINT Numerator, UINT Denominator );

    BOOL IsNetworkGood() const { return !m_AckTracker.LastSnapshotFractured() && m_DataReceivedRecently; }
    UINT GetGoodSnapshotCount() const { return m_AckTracker.GetAcknowledgeCount(); }

protected:
    virtual VOID InitializeClient() = 0;
    virtual INetworkObject* CreateRemoteObject( INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes ) = 0;
    virtual VOID DeleteRemoteObject( INetworkObject* pObject ) = 0;
    virtual VOID TickClient( FLOAT DeltaTime, DOUBLE AbsoluteTime, StateSnapshot* pSnapshot, SnapshotSendQueue* pSendQueue ) = 0;
    virtual VOID TerminateClient() = 0;

protected:
    virtual BOOL HandleReliableMessage( VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes );
    virtual BOOL HandleAcknowledge( VOID* pSenderContext, const UINT SnapshotIndex );
    virtual BOOL HandleBeginSnapshot( VOID* pSenderContext, const UINT SnapshotIndex );
    virtual BOOL HandleEndSnapshot( VOID* pSenderContext, const UINT SnapshotIndex, const UINT PacketCount );
    virtual BOOL HandleCreateNode( VOID* pSenderContext, StateInputOutput* pStateIO, const UINT32 ParentNodeID, const UINT32 NodeID, const StateNodeType Type, const UINT32 CreationCode, const SIZE_T CreationDataSizeBytes, const VOID* pCreationData );
    virtual BOOL HandleDeleteNode( VOID* pSenderContext, StateInputOutput* pStateIO, const UINT32 NodeID );

protected:
    INetworkObject* FindRemoteProxyObject( UINT ID );

private:
    static DWORD WINAPI ThreadEntry( VOID* pParam );
    DWORD Loop();
    HRESULT ReceiveFromServer();

    INetworkObject* CreateRemoteProxyObject( INetworkObject* pParentObject, UINT ID, const VOID* pCreationData, SIZE_T CreationDataSizeBytes );
    VOID DeleteRemoteProxyObject( UINT ID );

    VOID HashPassword( const WCHAR* strPassword );

    NetFrameStatistics* NextStatisticsFrame();

    void CompletePacketQueue(bool IsQueueGood);
    PacketQueue* AllocatePacketQueue();
};

