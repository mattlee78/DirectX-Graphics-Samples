#pragma once

#include "NetConstants.h"
#include "StructuredLogFile.h"

struct NetConnectionBase
{
    USHORT m_ID;
    WCHAR m_strUserName[NET_USERNAME_MAXSIZE];

    NetConnectionBase( USHORT ID = 0, const WCHAR* strUserName = L"" )
        : m_ID( ID )
    {
        wcscpy_s( m_strUserName, strUserName );
    }
};

struct NetFrameStatistics
{
    LARGE_INTEGER Timestamp;
    UINT32 PacketsReceived;
    UINT32 PacketsSent;
    UINT32 BytesReceived;
    UINT32 BytesSent;
    UINT32 ReliableMessagesReceived;
    UINT32 ReliableMessagesSent;
    UINT32 ReliableMessageBytesReceived;
    UINT32 ReliableMessageBytesSent;
    UINT32 DuplicateReliableMessagesSkipped;
    UINT32 UnreliableMessagesReceived;
    UINT32 UnreliableMessagesSent;
    UINT32 UnreliableMessageBytesReceived;
    UINT32 UnreliableMessageBytesSent;
    UINT32 NodeUpdateMessagesReceived;
    UINT32 NodeUpdateMessagesSent;
    UINT32 NodeUpdateBytesReceived;
    UINT32 AckMessagesSent;
    UINT32 AckMessagesReceived;
    UINT32 BeginSnapshotsReceived;
    UINT32 BeginSnapshotsSent;
    UINT32 EndSnapshotsReceived;
    UINT32 EndSnapshotsSent;
    BOOL Finished;

public:
    VOID Zero() { ZeroMemory( this, sizeof(NetFrameStatistics) ); }
};

interface INetStatistics
{
public:
    virtual UINT GetNumStatisticsFrames() const = 0;
    virtual UINT GetCurrentStatisticsFrame() const = 0;
    virtual const NetFrameStatistics* GetStatistics( UINT FrameIndex ) const = 0;
};

static const LogFileColumn g_ClientLogFileColumns[] = 
{
    { "Timestamp", LogFileColumnType::UInt64 },
    { "PacketsReceived", LogFileColumnType::UInt32 },
    { "PacketsSent", LogFileColumnType::UInt32 },
    { "BytesReceived", LogFileColumnType::UInt32 },
    { "BytesSent", LogFileColumnType::UInt32 },
    { "ReliableMessagesReceived", LogFileColumnType::UInt32 },
    { "ReliableMessagesSent", LogFileColumnType::UInt32 },
    { "ReliableMessageBytesReceived", LogFileColumnType::UInt32 },
    { "ReliableMessageBytesSent", LogFileColumnType::UInt32 },
    { "DuplicateReliableMessagesSkipped", LogFileColumnType::UInt32 },
    { "UnreliableMessagesReceived", LogFileColumnType::UInt32 },
    { "UnreliableMessagesSent", LogFileColumnType::UInt32 },
    { "UnreliableMessageBytesReceived", LogFileColumnType::UInt32 },
    { "UnreliableMessageBytesSent", LogFileColumnType::UInt32 },
    { "NodeUpdateMessagesReceived", LogFileColumnType::UInt32 },
    { "NodeUpdateMessagesSent", LogFileColumnType::UInt32 },
    { "NodeUpdateBytesReceived", LogFileColumnType::UInt32 },
};
