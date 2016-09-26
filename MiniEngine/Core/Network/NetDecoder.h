#pragma once

#include <windows.h>
#include <vector>

#include "StateLinking.h"
#include "StructuredLogFile.h"

struct NetFrameStatistics;
struct PacketQueue;

interface IDecodeHandler
{
    virtual BOOL HandleReliableMessage( VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes ) = 0;
    virtual BOOL HandleAcknowledge( VOID* pSenderContext, const UINT SnapshotIndex ) { return FALSE; }
    virtual BOOL HandleBeginSnapshot( VOID* pSenderContext, const UINT SnapshotIndex ) { return FALSE; }
    virtual BOOL HandleEndSnapshot( VOID* pSenderContext, const UINT SnapshotIndex, const UINT PacketCount ) { return FALSE; }

    virtual BOOL HandleCreateNode( VOID* pSenderContext, 
                                   StateInputOutput* pStateIO,
                                   const UINT32 ParentNodeID, 
                                   const UINT32 NodeID,
                                   const StateNodeType Type,
                                   const UINT32 CreationCode,
                                   const SIZE_T CreationDataSizeBytes,
                                   const VOID* pCreationData ) { return FALSE; }

    virtual BOOL HandleDeleteNode( VOID* pSenderContext, 
                                   StateInputOutput* pStateIO,
                                   const UINT32 NodeID ) { return FALSE; }
};

class DecodeHandlerStack : public IDecodeHandler
{
private:
    std::vector<IDecodeHandler*> m_Handlers;

public:
    VOID AddHandler( IDecodeHandler* pHandler )
    {
        m_Handlers.push_back( pHandler );
    }

#define HANDLER_LOOP( FUNCTION_NAME, ... ) \
    BOOL Handled = FALSE; \
    UINT Index = 0; \
    const UINT HandlerCount = (UINT)m_Handlers.size(); \
    while( !Handled && Index < HandlerCount ) \
    { \
        Handled = m_Handlers[Index]->FUNCTION_NAME( __VA_ARGS__ ); \
        ++Index; \
    } \
    return Handled;

    virtual BOOL HandleReliableMessage( VOID* pSenderContext, const UINT Opcode, const UINT UniqueIndex, const BYTE* pPayload, const UINT PayloadSizeBytes )
    {
        HANDLER_LOOP( HandleReliableMessage, pSenderContext, Opcode, UniqueIndex, pPayload, PayloadSizeBytes );
    }

    virtual BOOL HandleAcknowledge( VOID* pSenderContext, const UINT SnapshotIndex )
    {
        HANDLER_LOOP( HandleAcknowledge, pSenderContext, SnapshotIndex );
    }

    virtual BOOL HandleBeginSnapshot( VOID* pSenderContext, const UINT SnapshotIndex )
    {
        HANDLER_LOOP( HandleBeginSnapshot, pSenderContext, SnapshotIndex );
    }

    virtual BOOL HandleEndSnapshot( VOID* pSenderContext, const UINT SnapshotIndex, const UINT PacketCount )
    {
        HANDLER_LOOP( HandleEndSnapshot, pSenderContext, SnapshotIndex, PacketCount );
    }

    virtual BOOL HandleCreateNode( VOID* pSenderContext, 
        StateInputOutput* pStateIO,
        const UINT32 ParentNodeID, 
        const UINT32 NodeID,
        const StateNodeType Type,
        const UINT32 CreationCode,
        const SIZE_T CreationDataSizeBytes,
        const VOID* pCreationData )
    {
        HANDLER_LOOP( HandleCreateNode, pSenderContext, pStateIO, ParentNodeID, NodeID, Type, CreationCode, CreationDataSizeBytes, pCreationData );
    }

    virtual BOOL HandleDeleteNode( VOID* pSenderContext, 
        StateInputOutput* pStateIO,
        const UINT32 NodeID )
    {
        HANDLER_LOOP( HandleDeleteNode, pSenderContext, pStateIO, NodeID );
    }

#undef HANDLER_LOOP
};

class NetDecoderLog
{
private:
    StructuredLogFile m_LogFile;
    UINT m_PacketIndex;
    UINT m_SnapshotIndex;
    UINT m_MessageIndex;

public:
    NetDecoderLog()
        : m_PacketIndex( 0 ),
          m_SnapshotIndex( 0 ),
          m_MessageIndex( 0 )
    { }

    HRESULT OpenLogFile( const WCHAR* strPrefix, const WCHAR* strPlayerName );
    HRESULT CloseLogFile();

    VOID IncrementPacketIndex() { ++m_PacketIndex; }
    VOID SetSnapshotIndex( UINT Index ) { m_SnapshotIndex = Index; }
    VOID ResetIndices() { m_PacketIndex = 0; m_MessageIndex = 0; }

    VOID LogMessage( UINT32 PacketType, UINT32 NodeID, UINT32 ParentNodeID, UINT32 SizeBytes )
    {
        if( !m_LogFile.IsOpen() )
        {
            return;
        }

        const UINT32 Data[] = { m_SnapshotIndex, m_PacketIndex, m_MessageIndex, PacketType, NodeID, ParentNodeID, SizeBytes };
        m_LogFile.SetUInt32Data( 0, ARRAYSIZE(Data), Data );
        m_LogFile.FlushLine();
        ++m_MessageIndex;
    }
};

class NetDecoder
{
public:
    static HRESULT DecodePacket( 
        VOID* pSenderContext, 
        UINT32* pLastReliableMessageIndex,
        IDecodeHandler* pDecodeHandler,
        StateInputOutput* pStateIO, 
        const BYTE* pPacket, 
        UINT32 PacketSizeBytes,
        NetFrameStatistics* pStats,
        PacketQueue* pQueue,
        NetDecoderLog* pLog );
};
