#pragma once

#include <windows.h>

enum class NetPacketType
{
    NoOp = 0,
    Acknowledge = 1,
    ReliableMessage = 2,
    BeginSnapshot = 3,
    EndSnapshot = 4,
    NodeUpdate = 5,
    NodeCreateSimple = 6,
    NodeCreateComplex = 7,
    NodeDelete = 8,
    UnreliableMessage = 9,
};

static const CHAR* g_strPacketTypes[] =
{
    "NoOp",
    "Acknowledge",
    "ReliableMessage",
    "BeginSnapshot",
    "EndSnapshot",
    "NodeUpdate",
    "NodeCreateSimple",
    "NodeCreateComplex",
    "NodeDelete",
    "UnreliableMessage",
};

struct NetPacketHeader
{
private:
    SIZE_T GetFixedPacketSizeBytes() const;

public:
    union
    {
        struct  
        {
            UINT32 Type         : 4;
            UINT32 PayloadID    : 20;
            UINT32 ByteCountEnc : 8;
        };
        struct  
        {
            UINT32 MiniType     : 4;
            UINT32 Sequence     : 28;
        };
    };

    NetPacketHeader( NetPacketType PacketType, SIZE_T SizeBytes )
        : Type( (UINT)PacketType ),
          PayloadID( 0 )
    {
        assert( (UINT32)PacketType < 16 );
        SetByteCount( SizeBytes );
    }

    BOOL IsFixedSizePacket() const
    {
        switch ( (NetPacketType)Type )
        {
        case NetPacketType::Acknowledge:
        case NetPacketType::BeginSnapshot:
        case NetPacketType::EndSnapshot:
            return TRUE;
        default:
            return FALSE;
        }
    }

    VOID SetByteCount( SIZE_T SizeBytes )
    {
        if( IsFixedSizePacket() )
        {
            assert( SizeBytes == GetFixedPacketSizeBytes() );
            return;
        }

        assert( SizeBytes > 0 );
        assert( SizeBytes <= 1024 );
        assert( SizeBytes % 4 == 0 );
        ByteCountEnc = std::max( (SIZE_T)1, SizeBytes >> 2 ) - 1;
    }

    SIZE_T GetByteCount() const
    {
        if( IsFixedSizePacket() )
        {
            return GetFixedPacketSizeBytes();
        }
        return (SIZE_T)( ByteCountEnc + 1 ) << 2;
    }

    VOID SetID( UINT32 ID )
    {
        assert( ID < ( 1U << 20 ) );
        PayloadID = ID;
    }

    UINT32 GetID() const { return PayloadID; }
};

struct NetPacketAck : public NetPacketHeader
{
    NetPacketAck() : NetPacketHeader( NetPacketType::Acknowledge, sizeof(*this) ) {}

    VOID SetIndex( UINT32 Index ) { Sequence = Index; }
    UINT32 GetIndex() const { return Sequence; }
};

struct NetPacketReliableMessage : public NetPacketHeader
{
    UINT32 UniqueIndex;

    NetPacketReliableMessage() : NetPacketHeader( NetPacketType::ReliableMessage, sizeof(*this) ) {}

    VOID SetOpcode( UINT32 Opcode ) { PayloadID = Opcode; }
    UINT32 GetOpcode() const { return PayloadID; }
};

struct NetPacketUnreliableMessage : public NetPacketHeader
{
    NetPacketUnreliableMessage() : NetPacketHeader( NetPacketType::UnreliableMessage, sizeof(*this) ) {}

    VOID SetOpcode( UINT32 Opcode ) { PayloadID = Opcode; }
    UINT32 GetOpcode() const { return PayloadID; }
};

struct NetPacketBeginSnapshot : public NetPacketHeader
{
    NetPacketBeginSnapshot() : NetPacketHeader( NetPacketType::BeginSnapshot, sizeof(*this) ) {}

    VOID SetIndex( UINT32 Index ) { Sequence = Index; }
    UINT32 GetIndex() const { return Sequence; }
};

struct NetPacketEndSnapshot : public NetPacketHeader
{
    UINT32 PacketCount;

    NetPacketEndSnapshot() : NetPacketHeader( NetPacketType::EndSnapshot, sizeof(*this) ) {}

    VOID SetIndex( UINT32 Index ) { Sequence = Index; }
    UINT32 GetIndex() const { return Sequence; }
};

struct NetPacketNodeUpdate : public NetPacketHeader
{
    NetPacketNodeUpdate() : NetPacketHeader( NetPacketType::NodeUpdate, sizeof(*this) ) {}
};

struct NetPacketNodeCreateSimple : public NetPacketHeader
{
private:
    UINT32 ParentID     : 16;
    UINT32 NodeType     : 8;
    UINT32 CreationCode : 8;

public:
    NetPacketNodeCreateSimple() : NetPacketHeader( NetPacketType::NodeCreateSimple, sizeof(*this) ) {}

    VOID SetParentID( UINT32 ID )
    {
        assert( ID < 65536 );
        ParentID = ID;
    }
    UINT32 GetParentID() const { return ParentID; }

    VOID SetNodeType( UINT32 Type )
    {
        assert( Type < 256 );
        NodeType = Type;
    }
    UINT32 GetNodeType() const { return NodeType; }

    VOID SetCreationCode( UINT32 Code )
    {
        assert( Code < 256 );
        CreationCode = Code;
    }
    UINT32 GetCreationCode() const { return CreationCode; }
};

struct NetPacketNodeCreateComplex : public NetPacketHeader
{
    UINT32 ParentID : 16;
    UINT32 NodeType : 8;

    NetPacketNodeCreateComplex() : NetPacketHeader( NetPacketType::NodeCreateComplex, sizeof(*this) ) {}
};

struct NetPacketNodeDelete : public NetPacketHeader
{
    NetPacketNodeDelete() : NetPacketHeader( NetPacketType::NodeDelete, sizeof(*this) ) {}
};

inline SIZE_T NetPacketHeader::GetFixedPacketSizeBytes() const
{
    switch( (NetPacketType)Type )
    {
    case NetPacketType::Acknowledge:
        return sizeof(NetPacketAck);
    case NetPacketType::BeginSnapshot:
        return sizeof(NetPacketBeginSnapshot);
    case NetPacketType::EndSnapshot:
        return sizeof(NetPacketEndSnapshot);
    default:
        assert(FALSE);
        return 0;
    }
}