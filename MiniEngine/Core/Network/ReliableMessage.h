#pragma once

#include <windows.h>
#include <deque>

#include "NetConstants.h"

struct ReliableMessage
{
    UINT SequenceIndex;
    UINT UniqueIndex;
    UINT Opcode;
    UINT BufferSizeBytes;
    BYTE Buffer[NET_MAX_RELIABLE_MESSAGE_SIZE_BYTES - 4 * sizeof(UINT)];

    SIZE_T GetSizeBytes() const { return BufferSizeBytes + sizeof(UINT) * 4; }

    template<class T>
    T* CreatePayload()
    {
        assert( sizeof(T) < sizeof(Buffer) );
        BufferSizeBytes = sizeof(T);
        return (T*)Buffer;
    }

    VOID* PostPayload( UINT RequestedSizeBytes, UINT* pReturnedSizeBytes )
    {
        UINT BytesRemaining = sizeof(Buffer) - BufferSizeBytes;
        UINT PostPayloadSize = std::min( BytesRemaining, RequestedSizeBytes );
        BYTE* pReturn = &Buffer[BufferSizeBytes];
        BufferSizeBytes += PostPayloadSize;
        assert( BufferSizeBytes <= sizeof(Buffer) );
        *pReturnedSizeBytes = PostPayloadSize;
        return pReturn;
    }
};

C_ASSERT( sizeof(ReliableMessage) == NET_MAX_RELIABLE_MESSAGE_SIZE_BYTES );

typedef std::deque<ReliableMessage> ReliableMessageQueue;

enum class ReliableMessageType
{
    ConnectAttempt = 1,
    ConnectAck = 2,
    Disconnect = 3,
    ClientConnected = 4,
    ClientDisconnected = 5,
    SubmitChat = 6,
    ReceiveChat = 7,
    FirstImplReliableMessage = 16,
    FirstUserReliableMessage = 64,
};

struct RMsg_ConnectAttempt
{
    USHORT ProtocolVersion;
    USHORT Nonce;
    WCHAR strUserName[NET_USERNAME_MAXSIZE];
    WCHAR strHashedPassword[NET_HASHEDPASSWORD_MAXSIZE];
    LARGE_INTEGER ClientTicks;
    LARGE_INTEGER ClientTickFreq;
};

struct RMsg_ConnectAck
{
    UINT Success;
    USHORT Nonce;
    LARGE_INTEGER ServerTicks;
    LARGE_INTEGER ServerTickFreq;
};

struct RMsg_ClientConnected
{
    USHORT Nonce;
    WCHAR strUserName[NET_USERNAME_MAXSIZE];
};

struct RMsg_ClientDisconnected
{
    USHORT Nonce;
};

struct RMsg_SubmitChat
{
    USHORT DestinationClientID;
    // variable size payload follows
};

struct RMsg_ReceiveChat
{
    USHORT SourceClientID;
    // variable size payload follows
};
