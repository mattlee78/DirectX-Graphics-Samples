#pragma once

#ifdef _DEBUG
#define NET_TIMEOUT_SECONDS 60
#else
#define NET_TIMEOUT_SECONDS 3
#endif

#define NET_SEND_RECV_BUFFER_SIZE_BYTES 1400

#define NET_MAX_RELIABLE_MESSAGE_SIZE_BYTES 512

#define NET_PROTOCOL_VERSION 4

#define NET_STRING_SIZEBYTES 64

#define NET_USERNAME_MAXSIZE 32
#define NET_PASSWORD_MAXSIZE 16
#define NET_HASHEDPASSWORD_MAXSIZE 32
