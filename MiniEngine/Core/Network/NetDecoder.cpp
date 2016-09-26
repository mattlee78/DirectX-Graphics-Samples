#include "pch.h"
#include "NetDecoder.h"
#include <assert.h>

#include "LineProtocol.h"
#include "NetShared.h"

#include "PacketQueue.h"

VOID DebugSpew( const CHAR* strFormat, ... );

HRESULT NetDecoder::DecodePacket( VOID* pSenderContext, UINT32* pLastReliableMessageIndex, IDecodeHandler* pDecodeHandler, StateInputOutput* pStateIO, const BYTE* pPacket, UINT32 PacketSizeBytes, NetFrameStatistics* pStats, PacketQueue* pQueue, NetDecoderLog* pLog )
{
    assert( pPacket != nullptr );
    assert( PacketSizeBytes > 0 );
    if (pStateIO == nullptr)
    {
        assert(pPacket != nullptr);
    }

    const BYTE* p = pPacket;
    const BYTE* pLimit = pPacket + PacketSizeBytes;

    while( p < pLimit )
    {
        auto* pHeader = (const NetPacketHeader*)p;
        if( p + pHeader->GetByteCount() > pLimit )
        {
            return E_FAIL;
        }

        switch( pHeader->Type )
        {
        case NetPacketType::ReliableMessage:
            {
                assert(pLastReliableMessageIndex != nullptr);
                auto* pPacket = (const NetPacketReliableMessage*)p;

                assert( pPacket->GetByteCount() >= sizeof(NetPacketReliableMessage) );
                SIZE_T PayloadSizeBytes = pPacket->GetByteCount() - sizeof(NetPacketReliableMessage);
                const BYTE* pPayload = PayloadSizeBytes == 0 ? nullptr : p + sizeof(NetPacketReliableMessage);

                if( pLog != nullptr ) { pLog->LogMessage( (UINT32)NetPacketType::ReliableMessage, 0, 0, (UINT32)pPacket->GetByteCount() ); }

                if( pPacket->UniqueIndex > *pLastReliableMessageIndex )
                {
                    if( pStats != nullptr ) { pStats->ReliableMessagesReceived++; pStats->ReliableMessageBytesReceived += (UINT32)pPacket->GetByteCount(); }
                    BOOL MessageHandled = pDecodeHandler->HandleReliableMessage( pSenderContext, pPacket->GetOpcode(), pPacket->UniqueIndex, pPayload, (UINT32)PayloadSizeBytes );
                    *pLastReliableMessageIndex = pPacket->UniqueIndex;
                }
                else
                {
                    // duplicate message; skip
                    //DebugSpew( "Skipped duplicate reliable message %u\n", pPacket->UniqueIndex );
                    if( pStats != nullptr ) { pStats->DuplicateReliableMessagesSkipped++; }
                }

                break;
            }
        case NetPacketType::UnreliableMessage:
            {
                auto* pPacket = (const NetPacketUnreliableMessage*)p;

                assert( pPacket->GetByteCount() >= sizeof(NetPacketUnreliableMessage) );
                SIZE_T PayloadSizeBytes = pPacket->GetByteCount() - sizeof(NetPacketUnreliableMessage);
                const BYTE* pPayload = PayloadSizeBytes == 0 ? nullptr : p + sizeof(NetPacketUnreliableMessage);

                if( pLog != nullptr ) { pLog->LogMessage( (UINT32)NetPacketType::UnreliableMessage, 0, 0, (UINT32)pPacket->GetByteCount() ); }

                if( pStats != nullptr ) { pStats->UnreliableMessagesReceived++; pStats->UnreliableMessageBytesReceived += (UINT32)pPacket->GetByteCount(); }
                BOOL MessageHandled = pDecodeHandler->HandleReliableMessage( pSenderContext, pPacket->GetOpcode(), -1, pPayload, (UINT32)PayloadSizeBytes );

                break;
            }
        case NetPacketType::NodeUpdate:
            {
                auto* pPacket = (const NetPacketNodeUpdate*)p;

                assert( pPacket->GetByteCount() > sizeof(NetPacketNodeUpdate) );

                SIZE_T PayloadSizeBytes = pPacket->GetByteCount() - sizeof(NetPacketNodeUpdate);
                const BYTE* pPayload = p + sizeof(NetPacketNodeUpdate);

                if( pLog != nullptr ) { pLog->LogMessage( (UINT32)NetPacketType::NodeUpdate, pPacket->GetID(), 0, (UINT32)pPacket->GetByteCount() ); }

                if( pStats != nullptr ) { pStats->NodeUpdateMessagesReceived++; pStats->NodeUpdateBytesReceived += (UINT32)pPacket->GetByteCount(); }

                if (pStateIO == nullptr)
                {
                    pQueue->CopyPacket(pPacket);
                    break;
                }

                BOOL MessageHandled = pStateIO->UpdateNodeData( pPacket->GetID(), pPayload, PayloadSizeBytes );
//                 if( !MessageHandled )
//                 {
//                     printf_s( "Could not process node update for node ID %u.\n", pPacket->ID );
//                 }
                break;
            }
        case NetPacketType::Acknowledge:
            {
                auto* pPacket = (const NetPacketAck*)p;

                if( pLog != nullptr ) { pLog->LogMessage( (UINT32)NetPacketType::Acknowledge, pPacket->GetIndex(), 0, (UINT32)pPacket->GetByteCount() ); }

                if( pStats != nullptr ) { pStats->AckMessagesReceived++; }

                BOOL MessageHandled = pDecodeHandler->HandleAcknowledge( pSenderContext, pPacket->GetIndex() );
                break;
            }
        case NetPacketType::BeginSnapshot:
            {
                auto* pPacket = (const NetPacketBeginSnapshot*)p;

                if( pLog != nullptr ) { pLog->SetSnapshotIndex( pPacket->GetIndex() ); pLog->LogMessage( (UINT32)NetPacketType::BeginSnapshot, pPacket->GetIndex(), 0, (UINT32)pPacket->GetByteCount() ); }

                if( pStats != nullptr ) { pStats->BeginSnapshotsReceived++; }

                BOOL MessageHandled = pDecodeHandler->HandleBeginSnapshot( pSenderContext, pPacket->GetIndex() );
                if( !MessageHandled )
                {
                    // Begin snapshot was not handled.  Discard the remainder of the packet.
                    goto End;
                }
                break;
            }
        case NetPacketType::EndSnapshot:
            {
                auto* pPacket = (const NetPacketEndSnapshot*)p;

                if( pLog != nullptr ) { pLog->LogMessage( (UINT32)NetPacketType::EndSnapshot, pPacket->GetIndex(), pPacket->PacketCount, (UINT32)pPacket->GetByteCount() ); }

                if( pStats != nullptr ) { pStats->EndSnapshotsReceived++; }

                BOOL MessageHandled = pDecodeHandler->HandleEndSnapshot( pSenderContext, pPacket->GetIndex(), pPacket->PacketCount );
                break;
            }
        case NetPacketType::NodeCreateSimple:
            {
                auto* pPacket = (const NetPacketNodeCreateSimple*)p;

                if( pLog != nullptr ) { pLog->LogMessage( (UINT32)NetPacketType::NodeCreateSimple, pPacket->GetID(), pPacket->GetParentID(), (UINT32)pPacket->GetByteCount() ); }

                if (pStateIO == nullptr)
                {
                    pQueue->CopyPacket(pPacket);
                    break;
                }

                BOOL MessageHandled = pDecodeHandler->HandleCreateNode( pSenderContext, pStateIO, pPacket->GetParentID(), pPacket->GetID(), (StateNodeType)pPacket->GetNodeType(), pPacket->GetCreationCode(), 0, nullptr );
                break;
            }
        case NetPacketType::NodeCreateComplex:
            {
                auto* pPacket = (const NetPacketNodeCreateComplex*)p;

                assert( pPacket->GetByteCount() > sizeof(NetPacketNodeCreateComplex) );
                SIZE_T PayloadSizeBytes = pPacket->GetByteCount() - sizeof(NetPacketNodeCreateComplex);
                const BYTE* pPayload = p + sizeof(NetPacketNodeCreateComplex);

                if( pLog != nullptr ) { pLog->LogMessage( (UINT32)NetPacketType::NodeCreateComplex, pPacket->GetID(), pPacket->ParentID, (UINT32)pPacket->GetByteCount() ); }

                if (pStateIO == nullptr)
                {
                    pQueue->CopyPacket(pPacket);
                    break;
                }

                BOOL MessageHandled = pDecodeHandler->HandleCreateNode( pSenderContext, pStateIO, pPacket->ParentID, pPacket->GetID(), (StateNodeType)pPacket->NodeType, 0, PayloadSizeBytes, pPayload );
                break;
            }
        case NetPacketType::NodeDelete:
            {
                auto* pPacket = (const NetPacketNodeDelete*)p;

                if( pLog != nullptr ) { pLog->LogMessage( (UINT32)NetPacketType::NodeDelete, pPacket->GetID(), 0, (UINT32)pPacket->GetByteCount() ); }

                if (pStateIO == nullptr)
                {
                    pQueue->CopyPacket(pPacket);
                    break;
                }

                BOOL MessageHandled = pDecodeHandler->HandleDeleteNode( pSenderContext, pStateIO, pPacket->GetID() );
                break;
            }
        case NetPacketType::NoOp:
            break;
        default:
            return E_FAIL;
        }

        p += pHeader->GetByteCount();
    }

End:
    if( pLog != nullptr )
    {
        pLog->IncrementPacketIndex();
    }

    return S_OK;
}

HRESULT NetDecoderLog::OpenLogFile( const WCHAR* strPrefix, const WCHAR* strPlayerName )
{
    WCHAR strFileName[MAX_PATH];
    SYSTEMTIME Time;
    GetLocalTime( &Time );
    swprintf_s( strFileName, L"%sDecoder-%u%02u%02u-%02u%02u%02u-%s.csv", strPrefix, Time.wYear, Time.wMonth, Time.wDay, Time.wHour, Time.wMinute, Time.wSecond, strPlayerName );

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

HRESULT NetDecoderLog::CloseLogFile()
{
    return m_LogFile.Close();
}
