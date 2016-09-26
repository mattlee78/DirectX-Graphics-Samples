#pragma once

#include <windows.h>
#include <deque>
#include <assert.h>
#include "StateObjects.h"
#include "ReliableMessage.h"
#include "DebugPrint.h"

struct NetFrameStatistics;

interface ISendState : public IStateSnapshotDiff
{
    virtual VOID SetNetFrameStatistics( NetFrameStatistics* pStats ) {}

    virtual VOID BeginSnapshot( UINT32 Index ) = 0;

    virtual VOID SendReliableMessage( const ReliableMessage& msg ) = 0;
    
    virtual VOID SendUnreliableMessage( const ReliableMessage& msg ) = 0;

    virtual VOID SendAcknowledge( const UINT SnapshotIndex ) = 0;

    virtual VOID EndSnapshot( UINT32 Index ) = 0;
};

class SnapshotSendQueue
{
private:
    typedef std::deque<StateSnapshot*> SnapshotQueue;
    SnapshotQueue m_Queue;

    volatile UINT32 m_NextReliableMessageIndex;
    ReliableMessageQueue m_PendingMsgQueue;
    CRITICAL_SECTION m_PendingQueueCritSec;

    ReliableMessageQueue m_ReliableMsgQueue;
    ReliableMessageQueue m_UnreliableMsgQueue;

    UINT32 m_QueuedAck;

    UINT32 m_LastAckSnapshot;
    UINT32 m_LastSentSnapshot;
    StateSnapshot* m_pNullSnapshot;

    LARGE_INTEGER m_SendThrottle;

public:
    SnapshotSendQueue();
    ~SnapshotSendQueue();

    VOID Initialize( StateSnapshot* pNullSnapshot );

    // send to client:
    VOID QueueAcknowledge( UINT SnapshotIndex );
    VOID QueueSnapshot( StateSnapshot* pSnapshot );
    UINT SendUpdate( ISendState* pISS, NetFrameStatistics* pStats );

    VOID QueueReliableMessage( const ReliableMessage& msg );
    template< typename T >
    VOID QueueReliableMessage( UINT Opcode, const T* pPayload )
    {
        ReliableMessage msg;
        msg.Opcode = Opcode;
        msg.BufferSizeBytes = sizeof(T);
        assert( sizeof(T) <= sizeof(msg.Buffer) );
        memcpy( msg.Buffer, (const VOID*)pPayload, sizeof(T) );
        QueueReliableMessage( msg );
    }

    VOID QueueReliableMessage( UINT Opcode )
    {
        ReliableMessage msg;
        msg.Opcode = Opcode;
        msg.BufferSizeBytes = 0;
        QueueReliableMessage( msg );
    }

    VOID QueueUnreliableMessage( const ReliableMessage& msg );
    template< typename T >
    VOID QueueUnreliableMessage( UINT Opcode, const T* pPayload )
    {
        ReliableMessage msg;
        msg.Opcode = Opcode;
        msg.BufferSizeBytes = sizeof(T);
        assert( sizeof(T) <= sizeof(msg.Buffer) );
        memcpy( msg.Buffer, (const VOID*)pPayload, sizeof(T) );
        QueueUnreliableMessage( msg );
    }

    VOID QueueUnreliableMessage( UINT Opcode )
    {
        ReliableMessage msg;
        msg.Opcode = Opcode;
        msg.BufferSizeBytes = 0;
        QueueUnreliableMessage( msg );
    }

    // recv from client:
    VOID AckSnapshot( UINT32 Index );

    // client & server:
    UINT GetLastAckSnapshot() const { return m_LastAckSnapshot; }
};

class SnapshotAckTracker
{
private:
    SnapshotSendQueue* m_pSendQueue;
    UINT m_SnapshotIndex;
    UINT m_PacketCount;
    UINT m_AcknowledgeCount;
    UINT m_LastSuccessfulSnapshotIndex;
    BOOL m_LastSnapshotFractured;

public:
    SnapshotAckTracker()
        : m_pSendQueue( nullptr ),
          m_SnapshotIndex( 0 ),
          m_PacketCount( 0 ),
          m_LastSuccessfulSnapshotIndex( 0 ),
          m_AcknowledgeCount( 0 ),
          m_LastSnapshotFractured( FALSE )
    { }

    VOID Initialize( SnapshotSendQueue* pSendQueue )
    {
        m_pSendQueue = pSendQueue;
    }

    UINT GetCurrentSnapshotIndex() const { return m_SnapshotIndex; }

    UINT GetAcknowledgeCount() const { return m_AcknowledgeCount; }
    BOOL LastSnapshotFractured() const { return m_LastSnapshotFractured; }

    BOOL BeginSnapshot( UINT Index )
    {
        if( Index < m_SnapshotIndex )
        {
            return FALSE;
        }
        assert( Index >= m_SnapshotIndex );
        if( Index > m_SnapshotIndex )
        {
            m_SnapshotIndex = Index;
            m_PacketCount = 1;
        }
        else
        {
            ++m_PacketCount;
        }
        return TRUE;
    }

    VOID EndSnapshot( UINT Index, UINT PacketCount )
    {
        assert( Index == m_SnapshotIndex );
        if( PacketCount == m_PacketCount )
        {
            assert( m_pSendQueue != nullptr );
            m_pSendQueue->QueueAcknowledge( m_SnapshotIndex );
            ++m_AcknowledgeCount;
            m_LastSuccessfulSnapshotIndex = Index;
            m_LastSnapshotFractured = FALSE;
            //DebugPrint( "accepted snapshot index %u\n", m_SnapshotIndex );
        }
        else
        {
            m_LastSnapshotFractured = TRUE;
            //DebugPrint( "fractured snapshot index %u\n", m_SnapshotIndex );
        }
    }
};