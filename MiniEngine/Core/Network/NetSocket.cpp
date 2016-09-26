#include "pch.h"
#include "NetSocket.h"
#include <WS2tcpip.h>

HRESULT NetSocket::DnsLookupHostname( const CHAR* strHostName, USHORT PortNum, SOCKADDR_IN* pAddr )
{
    INT iResult = -1;

    addrinfo* pResult = NULL;
    addrinfo hints;
    ZeroMemory( &hints, sizeof(hints) );
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_protocol = IPPROTO_UDP;

    CHAR strPort[6];
    sprintf_s( strPort, "%d", PortNum );
    iResult = getaddrinfo( strHostName, strPort, &hints, &pResult );
    if( iResult == SOCKET_ERROR || pResult == NULL )
    {
        return E_FAIL;
    }

    *pAddr = *(SOCKADDR_IN*)pResult->ai_addr;
    freeaddrinfo( pResult );

    return S_OK;
}

HRESULT NetUdpSocket::Initialize( const SOCKADDR_IN& RemoteAddress )
{
    m_UdpSendAddress = RemoteAddress;

    SOCKET UdpSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( UdpSocket == INVALID_SOCKET )
    {
        return E_FAIL;
    }

    m_Socket = UdpSocket;

    CommonSocketSetup();

    return S_OK;
}

HRESULT NetUdpSocket::Bind( USHORT Port )
{
    SOCKET UdpSocket = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    if( UdpSocket == INVALID_SOCKET )
    {
        return E_FAIL;
    }

    INT iResult;

    if( Port != 0 )
    {
        SOCKADDR_IN sa_recv;
        ZeroMemory( &sa_recv, sizeof(sa_recv) );
        sa_recv.sin_family = AF_INET;
        sa_recv.sin_addr.s_addr = htonl(INADDR_ANY);
        sa_recv.sin_port = htons(Port);

        iResult = bind( UdpSocket, (SOCKADDR*)&sa_recv, sizeof(sa_recv) );
        if( iResult == SOCKET_ERROR )
        {
            closesocket( UdpSocket );
            return E_FAIL;
        }
    }

    m_Socket = UdpSocket;

    CommonSocketSetup();

    return S_OK;
}

VOID NetSocket::Disconnect()
{
    closesocket( m_Socket );
    m_Socket = INVALID_SOCKET;
}

HRESULT NetUdpSocket::Send( const BYTE* pBuffer, UINT SizeBytes )
{
    if( m_Socket == INVALID_SOCKET )
    {
        return E_FAIL;
    }

    INT iResult = sendto( m_Socket, (const CHAR*)pBuffer, SizeBytes, 0, (const SOCKADDR*)&m_UdpSendAddress, sizeof(m_UdpSendAddress) );
    if (iResult == SizeBytes)
    {
        return S_OK;
    }
    return E_FAIL;
}

HRESULT NetUdpSocket::SendTo( const SOCKADDR_IN* pAddr, const BYTE* pBuffer, UINT SizeBytes )
{
    if( m_Socket == INVALID_SOCKET )
    {
        return E_FAIL;
    }

    INT iResult = sendto( m_Socket, (const CHAR*)pBuffer, SizeBytes, 0, (const SOCKADDR*)pAddr, sizeof(SOCKADDR_IN) );
    if (iResult == SizeBytes)
    {
        return S_OK;
    }
    return E_FAIL;
}

HRESULT NetUdpSocket::RecvFrom( BYTE* pBuffer, UINT SizeBytes, UINT* pBytesReceived, SOCKADDR_IN* pRemoteAddress )
{
    INT SockaddrSize = sizeof(SOCKADDR_IN);
    INT iResult = recvfrom( m_Socket, (CHAR*)pBuffer, SizeBytes, 0, (SOCKADDR*)pRemoteAddress, &SockaddrSize );
    if( iResult == SOCKET_ERROR )
    {
        iResult = WSAGetLastError();
        if( iResult != WSAEWOULDBLOCK )
        {
            return E_FAIL;
        }
        *pBytesReceived = 0;
        return S_OK;
    }

    *pBytesReceived = iResult;
    return S_OK;
}

VOID NetUdpSocket::CommonSocketSetup()
{
    INT DontFrag = 1;
    INT iResult = setsockopt( m_Socket, IPPROTO_IP, IP_DONTFRAGMENT, (char*)&DontFrag, sizeof(DontFrag) );
    assert( iResult != SOCKET_ERROR );

    UINT UdpReceiveBufferSize = 256 * 1024;
    iResult = setsockopt( m_Socket, SOL_SOCKET, SO_RCVBUF, (char*)&UdpReceiveBufferSize, sizeof(UdpReceiveBufferSize) );
    assert( iResult != SOCKET_ERROR );

    unsigned long iUnblocking = 1;
    iResult = ioctlsocket( m_Socket, FIONBIO, &iUnblocking );
    assert( iResult == 0 );
}

HRESULT NetTcpSocket::Bind( USHORT Port )
{
    struct addrinfo hints;
    ZeroMemory(&hints, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;
    hints.ai_flags = AI_PASSIVE;

    // Resolve the server address and port
    struct addrinfo *result = NULL;
    CHAR strPort[7];
    sprintf_s(strPort, "%u", Port);
    INT iResult = getaddrinfo(NULL, strPort, &hints, &result);
    if ( iResult != 0 ) 
    {
        return E_FAIL;
    }

    m_Socket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
    if (m_Socket == INVALID_SOCKET)
    {
        freeaddrinfo(result);
        return E_FAIL;
    }

    iResult = bind( m_Socket, result->ai_addr, (int)result->ai_addrlen);
    freeaddrinfo(result);
    if (iResult == SOCKET_ERROR) 
    {
        closesocket(m_Socket);
        return E_FAIL;
    }

    iResult = listen(m_Socket, SOMAXCONN);
    if (iResult == SOCKET_ERROR) 
    {
        closesocket(m_Socket);
        return E_FAIL;
    }

    unsigned long iUnblocking = 1;
    iResult = ioctlsocket( m_Socket, FIONBIO, &iUnblocking );
    assert( iResult == 0 );
    return S_OK;
}

HRESULT NetTcpSocket::AcceptConnection( NetTcpSocket* pClientSocket )
{
    SOCKET ClientSocket = accept(m_Socket, NULL, NULL);
    if (ClientSocket == INVALID_SOCKET) 
    {
        INT iResult = WSAGetLastError();
        if( iResult != WSAEWOULDBLOCK )
        {
            return E_FAIL;
        }
        return S_FALSE;
    }
    unsigned long iUnblocking = 1;
    INT iResult = ioctlsocket( ClientSocket, FIONBIO, &iUnblocking );
    assert( iResult == 0 );
    pClientSocket->m_Socket = ClientSocket;
    return S_OK;
}

HRESULT NetTcpSocket::Send( const void* pBuffer, UINT SizeBytes )
{
    INT iResult = send( m_Socket, (const char*)pBuffer, SizeBytes, 0 );
    if (iResult == SizeBytes)
    {
        return S_OK;
    }
    return E_FAIL;
}

HRESULT NetTcpSocket::Recv( void* pBuffer, UINT SizeBytes, UINT* pBytesReceived )
{
    INT iResult = recv( m_Socket, (char*)pBuffer, SizeBytes, 0 );
    if (iResult == SOCKET_ERROR)
    {
        INT iResult = WSAGetLastError();
        if( iResult != WSAEWOULDBLOCK )
        {
            return E_FAIL;
        }
        return S_FALSE;
    }
    *pBytesReceived = iResult;
    if (iResult == 0)
    {
        return E_FAIL;
    }
    return S_OK;
}

HRESULT NetTcpSocket::Initialize( const SOCKADDR_IN& RemoteAddress )
{
    m_Socket = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    if (m_Socket == INVALID_SOCKET)
    {
        return E_FAIL;
    }

    INT iResult = connect( m_Socket, (const sockaddr*)&RemoteAddress, sizeof(RemoteAddress) );
    if (iResult == SOCKET_ERROR)
    {
        Disconnect();
        return E_FAIL;
    }

    unsigned long iUnblocking = 1;
    iResult = ioctlsocket( m_Socket, FIONBIO, &iUnblocking );
    assert( iResult == 0 );

    return S_OK;
}
