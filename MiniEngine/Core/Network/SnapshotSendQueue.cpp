#include "pch.h"
#include "SnapshotSendQueue.h"
#include "NetShared.h"

LARGE_INTEGER g_PerfFreq = { 0, 0 };

SnapshotSendQueue::SnapshotSendQueue()
    : m_LastAckSnapshot( 0 ),
      m_LastSentSnapshot( 0 ),
      m_pNullSnapshot( nullptr ),
      m_QueuedAck( 0 ),
      m_NextReliableMessageIndex( 0 )
{
    InitializeCriticalSection( &m_PendingQueueCritSec );
    QueryPerformanceCounter( &m_SendThrottle );
    if( g_PerfFreq.QuadPart == 0 )
    {
        QueryPerformanceFrequency( &g_PerfFreq );
    }
}

VOID SnapshotSendQueue::Initialize( StateSnapshot* pNullSnapshot )
{
    m_pNullSnapshot = pNullSnapshot;
    m_pNullSnapshot->AddRef();
}

SnapshotSendQueue::~SnapshotSendQueue(void)
{
    DeleteCriticalSection( &m_PendingQueueCritSec );
}

VOID SnapshotSendQueue::QueueReliableMessage( const ReliableMessage& msg )
{
    EnterCriticalSection( &m_PendingQueueCritSec );

    assert( msg.BufferSizeBytes <= sizeof(msg.Buffer) );
    m_PendingMsgQueue.push_back( msg );

    LeaveCriticalSection( &m_PendingQueueCritSec );
}

VOID SnapshotSendQueue::QueueUnreliableMessage( const ReliableMessage& msg )
{
    EnterCriticalSection( &m_PendingQueueCritSec );

    assert( msg.BufferSizeBytes <= sizeof(msg.Buffer) );
    m_UnreliableMsgQueue.push_back( msg );

    LeaveCriticalSection( &m_PendingQueueCritSec );
}

VOID SnapshotSendQueue::QueueAcknowledge( UINT SnapshotIndex )
{
    assert( SnapshotIndex >= m_QueuedAck );
    m_QueuedAck = SnapshotIndex;
}

VOID SnapshotSendQueue::QueueSnapshot( StateSnapshot* pSnapshot )
{
    pSnapshot->AddRef();
    m_Queue.push_back( pSnapshot );
    m_LastSentSnapshot = pSnapshot->GetIndex();
}

UINT SnapshotSendQueue::SendUpdate( ISendState* pISS, NetFrameStatistics* pStats )
{
    StateSnapshot* pLastAck = nullptr;
    StateSnapshot* pCurrent = nullptr;

    if( m_LastAckSnapshot == 0 )
    {
        pLastAck = m_pNullSnapshot;
    }
    else
    {
        pLastAck = m_Queue.front();
    }

    UINT CurrentIndex = 0;

    if( !m_Queue.empty() )
    {
        pCurrent = m_Queue.back();
        CurrentIndex = pCurrent->GetIndex();

        LARGE_INTEGER CurrentTime;
        if( m_LastAckSnapshot == 0 )
        {
            QueryPerformanceCounter( &CurrentTime );
            if( CurrentTime.QuadPart < m_SendThrottle.QuadPart )
            {
                return CurrentIndex;
            }
        }

        EnterCriticalSection( &m_PendingQueueCritSec );

        while( !m_PendingMsgQueue.empty() )
        {
            ReliableMessage& msg = m_PendingMsgQueue.front();
            msg.SequenceIndex = CurrentIndex;
            msg.UniqueIndex = InterlockedIncrement( &m_NextReliableMessageIndex );
            m_ReliableMsgQueue.push_back( msg );
            m_PendingMsgQueue.pop_front();
        }

        pISS->SetNetFrameStatistics( pStats );
        pISS->BeginSnapshot( CurrentIndex );

        {
            auto iter = m_UnreliableMsgQueue.begin();
            auto end = m_UnreliableMsgQueue.end();
            while( iter != end )
            {
                if( pStats != nullptr ) { pStats->UnreliableMessagesSent++; pStats->UnreliableMessageBytesSent += (UINT32)iter->GetSizeBytes(); }
                pISS->SendUnreliableMessage( *iter );
                ++iter;
            }
            m_UnreliableMsgQueue.clear();
        }

        LeaveCriticalSection( &m_PendingQueueCritSec );

        auto iter = m_ReliableMsgQueue.begin();
        auto end = m_ReliableMsgQueue.end();
        while( iter != end )
        {
            if( pStats != nullptr ) { pStats->ReliableMessagesSent++; pStats->ReliableMessageBytesSent += (UINT32)iter->GetSizeBytes(); }
            pISS->SendReliableMessage( *iter );
            ++iter;
        }

        pLastAck->Diff( pCurrent, pISS );

        if( m_QueuedAck != 0 )
        {
            if( pStats != nullptr ) { pStats->AckMessagesSent++; }
            pISS->SendAcknowledge( m_QueuedAck );
            m_QueuedAck = 0;
        }

        pISS->EndSnapshot( CurrentIndex );

        if( m_LastAckSnapshot == 0 )
        {
            m_SendThrottle.QuadPart = CurrentTime.QuadPart + g_PerfFreq.QuadPart;
        }
    }

    return CurrentIndex;
}

VOID SnapshotSendQueue::AckSnapshot( UINT32 Index )
{
    if( Index <= m_LastAckSnapshot )
    {
        return;
    }

    if( m_LastAckSnapshot == 0 && Index > 0 )
    {
        QueryPerformanceCounter( &m_SendThrottle );
    }

    m_LastAckSnapshot = Index;
    while( !m_Queue.empty() && m_Queue.front()->GetIndex() < m_LastAckSnapshot )
    {
        StateSnapshot* pSS = m_Queue.front();
        m_Queue.pop_front();

        UINT RefCount = pSS->Release();
    }

    while( !m_ReliableMsgQueue.empty() && m_ReliableMsgQueue.front().SequenceIndex <= m_LastAckSnapshot )
    {
        m_ReliableMsgQueue.pop_front();
    }
}
