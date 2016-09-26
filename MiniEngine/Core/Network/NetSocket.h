#pragma once

#include <winsock2.h>
#include <windows.h>
#include <assert.h>

inline VOID InitializeWinsock()
{
    WSADATA wsaData;
    INT32 err = WSAStartup(MAKEWORD(2, 2), &wsaData);
    assert(err == 0);
}

inline VOID TerminateWinsock()
{
    INT32 err = WSACleanup();
    assert(err == 0);
}

inline VOID FormatAddress( CHAR* strOutput, SIZE_T NumChars, const SOCKADDR_IN& Address )
{
    sprintf_s( strOutput, NumChars, "%u.%u.%u.%u:%u", Address.sin_addr.S_un.S_un_b.s_b1, Address.sin_addr.S_un.S_un_b.s_b2, Address.sin_addr.S_un.S_un_b.s_b3, Address.sin_addr.S_un.S_un_b.s_b4, (UINT)Address.sin_port );
}

inline UINT64 HashAddress( const SOCKADDR_IN& Address )
{
    return (UINT64)Address.sin_port << 32 | (UINT64)Address.sin_addr.S_un.S_addr;
}

struct NetSocket
{
protected:
    SOCKET m_Socket;

public:
    static HRESULT DnsLookupHostname( const CHAR* strHostName, USHORT PortNum, SOCKADDR_IN* pAddr );

    NetSocket()
        : m_Socket( INVALID_SOCKET )
    { }

    VOID Disconnect();
};

struct NetUdpSocket : public NetSocket
{
    SOCKADDR_IN m_UdpSendAddress;

    NetUdpSocket()
    { }

    HRESULT Initialize( const SOCKADDR_IN& RemoteAddress );
    HRESULT Bind( USHORT Port );

    HRESULT Send( const BYTE* pBuffer, UINT SizeBytes );
    HRESULT SendTo( const SOCKADDR_IN* pAddr, const BYTE* pBuffer, UINT SizeBytes );
    HRESULT RecvFrom( BYTE* pBuffer, UINT SizeBytes, UINT* pBytesReceived, SOCKADDR_IN* pRemoteAddress );

protected:
    VOID CommonSocketSetup();
};

struct NetTcpSocket : public NetSocket
{
    NetTcpSocket()
    { }

    HRESULT Initialize( const SOCKADDR_IN& RemoteAddress );

    HRESULT Bind( USHORT Port );
    HRESULT AcceptConnection( NetTcpSocket* pClientSocket );

    HRESULT Send( const void* pBuffer, UINT SizeBytes );
    HRESULT Recv( void* pBuffer, UINT SizeBytes, UINT* pBytesReceived );
};
