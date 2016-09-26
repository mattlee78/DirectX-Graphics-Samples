#pragma once

#include <windows.h>
#include "NetSocket.h"
#include "SnapshotSendQueue.h"

#include "NetConstants.h"
#include "StructuredLogFile.h"

struct NetFrameStatistics;

class NetEncoder : public ISendState
{
private:
    NetUdpSocket* m_pSocket;
    SOCKADDR_IN m_SendAddr;
    BOOL m_UseSendAddr;

    UINT m_SnapshotIndex;
    UINT m_PacketCount;

    BYTE m_Buffer[NET_SEND_RECV_BUFFER_SIZE_BYTES];
    UINT m_BytesRemaining;

    NetFrameStatistics* m_pStats;
    StructuredLogFile m_LogFile;
    UINT m_MessageIndex;

public:
    NetEncoder();
    ~NetEncoder();

    HRESULT Initialize( NetUdpSocket* pSocket, const SOCKADDR_IN* pSendAddr );

    HRESULT OpenLogFile( const WCHAR* strPrefix, const WCHAR* strPlayerName );
    HRESULT CloseLogFile();

    virtual VOID SetNetFrameStatistics( NetFrameStatistics* pStats ) { m_pStats = pStats; }

    virtual VOID BeginSnapshot( UINT32 Index );
    virtual VOID NodeCreated( StateNode* pNode, StateNode* pParentNode );
    virtual VOID NodeDeleted( StateNode* pNode );
    virtual VOID NodeChanged( StateNode* pPrev, StateNode* pCurrent );
    virtual VOID NodeSame( StateNode* pPrev, StateNode* pCurrent );
    virtual VOID SendReliableMessage( const ReliableMessage& msg );
    virtual VOID SendUnreliableMessage( const ReliableMessage& msg );
    virtual VOID SendAcknowledge( const UINT SnapshotIndex );
    virtual VOID EndSnapshot( UINT32 Index );

private:
    VOID NodeChangedWorker( StateNode* pPrev, StateNode* pCurrent, bool UpdatePrevChanged );

    inline SIZE_T BytesUsed() const { return sizeof(m_Buffer) - m_BytesRemaining; }

    BYTE* AllocateBytes( UINT SizeBytes );

    template< class T >
    T* AllocateMessage() 
    { 
        T* pMessage = (T*)AllocateBytes( sizeof(T) );
        new (pMessage) T();
        return pMessage;
    }

    template< class T >
    T* AllocateMessageWithPayload( UINT PayloadSizeBytes, BYTE** ppPayload ) 
    { 
        const UINT PacketSizeBytes = sizeof(T);
        PayloadSizeBytes = ( PayloadSizeBytes + 3 ) & ~0x3;
        BYTE* pMessageRaw = AllocateBytes( PacketSizeBytes + PayloadSizeBytes );
        *ppPayload = pMessageRaw + sizeof(T);

        T* pMessage = (T*)pMessageRaw;
        new (pMessage) T();
        pMessage->SetByteCount( PacketSizeBytes + PayloadSizeBytes );

        return pMessage;
    }

    VOID Flush( BOOL LastPacket );

    VOID LogMessage( UINT32 PacketType, UINT32 NodeID, UINT32 ParentNodeID, UINT32 SizeBytes )
    {
        if( !m_LogFile.IsOpen() )
        {
            return;
        }

        const UINT32 Data[] = { m_SnapshotIndex, m_PacketCount, m_MessageIndex, PacketType, NodeID, ParentNodeID, SizeBytes };
        m_LogFile.SetUInt32Data( 0, ARRAYSIZE(Data), Data );
        m_LogFile.FlushLine();
        ++m_MessageIndex;
    }
};

