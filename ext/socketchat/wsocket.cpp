#include "wsocket.h"
#include "wplatform.h"
#include "socketsharedmemory.h"
#include <assert.h>

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#ifdef _WIN32
#if defined(_MSC_VER) && !defined(_CRT_SECURE_NO_WARNINGS)
#define _CRT_SECURE_NO_WARNINGS // _CRT_SECURE_NO_WARNINGS for sscanf errors in MSVC2013 Express
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <fcntl.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#pragma comment( lib, "ws2_32" )
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <io.h>
#ifndef _SSIZE_T_DEFINED
typedef int ssize_t;
#define _SSIZE_T_DEFINED
#endif
#ifndef _SOCKET_T_DEFINED
typedef SOCKET socket_t;
#define _SOCKET_T_DEFINED
#endif
#if _MSC_VER >=1600
// vs2010 or later
#include <stdint.h>
#else
typedef __int8 int8_t;
typedef unsigned __int8 uint8_t;
typedef __int32 int32_t;
typedef unsigned __int32 uint32_t;
typedef __int64 int64_t;
typedef unsigned __int64 uint64_t;
#endif
#define socketerrno WSAGetLastError()
#define SOCKET_EAGAIN_EINPROGRESS WSAEINPROGRESS
#define SOCKET_EWOULDBLOCK WSAEWOULDBLOCK
#else
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdint.h>
#ifndef _SOCKET_T_DEFINED
typedef int socket_t;
#define _SOCKET_T_DEFINED
#endif
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif
#ifndef SOCKET_ERROR
#define SOCKET_ERROR   (-1)
#endif
#define closesocket(s) ::close(s)
#include <errno.h>
#define socketerrno errno
#define SOCKET_EAGAIN_EINPROGRESS EAGAIN
#define SOCKET_EWOULDBLOCK EWOULDBLOCK
#endif

#ifdef _MSC_VER
#pragma warning(disable:4100)
#endif

//#define SAVE_RECEIVE "f:\\SocketReceive.bin"
//#define SAVE_SEND "f:\\SocketSend.bin"

#define SHARED_SERVER "sharedserver"
#define SHARED_CLIENT "sharedclient"

namespace wsocket
{

class WsocketImpl : public Wsocket
{
public:
	WsocketImpl(socket_t socket)
	{
		mSocket = socket;
	}

	WsocketImpl(const char *hostName, int32_t port)
	{
		if (strcmp(hostName, "server") == 0)
		{
			mSocket = server_connect(port);
			mIsServer = true;
		}
		else
		{
			mSocket = hostname_connect(hostName, port);
		}
#ifdef SAVE_RECEIVE
        mReceiveFile = fopen(SAVE_RECEIVE, "wb");
#endif
#ifdef SAVE_SEND
		mSendFile = fopen(SAVE_SEND, "wb");
#endif
	}

	virtual ~WsocketImpl(void)
	{
		close();
#ifdef SAVE_RECEIVE
        if (mReceiveFile)
        {
            fclose(mReceiveFile);
        }
#endif
#ifdef SAVE_SEND
		if (mSendFile)
		{
			fclose(mSendFile);
		}
#endif
	}

	virtual void nullSelect(int32_t timeout) override final
	{
		timeval tv = { timeout / 1000, (timeout % 1000) * 1000 };
		::select(0, NULL, NULL, NULL, &tv);
	}

	virtual void select(int32_t timeout, size_t txBufSize) override final
	{
		fd_set rfds;
		fd_set wfds;
		timeval tv = { timeout / 1000, (timeout % 1000) * 1000 };
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		FD_SET(mSocket, &rfds);
		if (txBufSize) 
		{ 
			FD_SET(mSocket, &wfds);
		}
		::select(int(mSocket) + 1, &rfds, &wfds, 0, timeout > 0 ? &tv : 0);
	}

	virtual int32_t receive(void *dest, uint32_t maxLen) override final
	{
		int32_t ret = -1;
		if (mSocket)
		{
			ret = ::recv(mSocket, (char *)dest, int(maxLen), 0);
		}
#ifdef SAVE_RECEIVE
        if (mReceiveFile && ret > 0 )
        {
//            fwrite(&ret, sizeof(ret), 1, mReceiveFile);
//            fwrite(&maxLen, sizeof(maxLen), 1, mReceiveFile);
            fwrite(dest, ret, 1, mReceiveFile);
            fflush(mReceiveFile);
        }
#endif
		return ret;
	}

	virtual int32_t send(const void *data, uint32_t dataLen) override final
	{
		int32_t ret = ::send(mSocket, (const char *)data, int(dataLen), 0);
#ifdef SAVE_SEND
		if (mSendFile && ret > 0)
		{
			fwrite(data, ret, 1, mSendFile);
			fflush(mSendFile);
		}
#endif
		return ret;
	}

	// Not sure what this is, but it's in the original code so making it available now.
	virtual void disableNaglesAlgorithm(void) override final
	{
		int flag = 1;
		setsockopt(mSocket, IPPROTO_TCP, TCP_NODELAY, (char*)&flag, sizeof(flag)); // Disable Nagle's algorithm
#ifdef _WIN32
		u_long on = 1;
		ioctlsocket(mSocket, FIONBIO, &on);
#else
		fcntl(mSocket, F_SETFL, O_NONBLOCK);
#endif
	}

	virtual void release(void) override final
	{
		delete this;
	}

	bool isValid(void) const
	{
		return mSocket != INVALID_SOCKET;
	}

	virtual void close(void) override final
	{
		if (mSocket)
		{
			closesocket(mSocket);
		}
		mSocket = 0;
	}

	virtual bool	wouldBlock(void) override final
	{
		return socketerrno == SOCKET_EWOULDBLOCK;
	}

	virtual bool	inProgress(void) override final
	{
		return socketerrno == SOCKET_EAGAIN_EINPROGRESS;
	}

	socket_t server_connect(int port)
	{
		socket_t listenSocket = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
		if (listenSocket == INVALID_SOCKET)
			return INVALID_SOCKET;

		sockaddr_in addr = { 0 };
		addr.sin_family = AF_INET;
		addr.sin_port = htons(u_short(port));
		addr.sin_addr.s_addr = htonl(INADDR_ANY);

		bool ok = false;

		if (bind(listenSocket, (sockaddr*)&addr, sizeof(addr)) == 0)
		{
			if (::listen(listenSocket, SOMAXCONN) == 0)
			{
				ok = true;
			}
		}
		if (ok)
		{
			setBlockingInternal(listenSocket, false);
		}
		else
		{
			closesocket(listenSocket);
			listenSocket = INVALID_SOCKET;
		}
		return listenSocket;
	}

	socket_t hostname_connect(const char *hostname, int port)
	{
		addrinfo hints;
		addrinfo *result;
		addrinfo *p;
		int ret;
		socket_t sockfd = INVALID_SOCKET;
		char sport[16];
		memset(&hints, 0, sizeof(hints));

		hints.ai_family = AF_UNSPEC;
		hints.ai_socktype = SOCK_STREAM;
		wplatform::stringFormat(sport, 16, "%d", port);

		if ((ret = getaddrinfo(hostname, sport, &hints, &result)) != 0)
		{
#ifdef _MSC_VER
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerrorA(ret));
#else
			fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(ret));
#endif
			return 1;
		}
		for (p = result; p != NULL; p = p->ai_next)
		{
			sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
			if (sockfd == INVALID_SOCKET)
			{
				continue;
			}
			if (connect(sockfd, p->ai_addr, int(p->ai_addrlen)) != SOCKET_ERROR)
			{
				break;
			}
			closesocket(sockfd);
			sockfd = INVALID_SOCKET;
		}
		freeaddrinfo(result);
		return sockfd;
	}

	// If we are a server, we poll for new connections.
	// If a new connection is found, then we return an instance of a Wsocket with that connection.
	// It is the caller's responsibility to release it when finished
	virtual Wsocket *pollServer(void) override final
	{
		Wsocket *ret = nullptr;

		if (mIsServer && mSocket != INVALID_SOCKET)
		{
			socket_t clientSocket = ::accept(mSocket, 0, 0);
			if (clientSocket != INVALID_SOCKET)
			{
				WsocketImpl *w = new WsocketImpl(clientSocket);
				ret = static_cast<Wsocket *>(w);
			}
		}

		return ret;
	}

	void setBlockingInternal(socket_t socket, bool blocking)
	{
#ifdef _MSC_VER
		uint32_t mode = uint32_t(blocking ? 0 : 1);
		ioctlsocket(socket, FIONBIO, (u_long*)&mode);
#else
		int mode = fcntl(socket, F_GETFL, 0);
		if (!blocking)
			mode |= O_NONBLOCK;
		else
			mode &= ~O_NONBLOCK;
		fcntl(socket, F_SETFL, mode);
#endif
	}

	bool		mIsServer{ false };
	socket_t	mSocket{ INVALID_SOCKET };
#ifdef SAVE_RECEIVE
    FILE        *mReceiveFile{ nullptr };
#endif
#ifdef SAVE_SEND
	FILE		*mSendFile{ nullptr };
#endif
};

class WsocketPlayback : public Wsocket
{
public:
    WsocketPlayback(const char *playBackFile)
    {
        mPlaybackFile = fopen(playBackFile, "rb");
    }
    virtual ~WsocketPlayback(void)
    {
        if (mPlaybackFile)
        {
            fclose(mPlaybackFile);
        }
    }

    // performs the select operation on this socket
    virtual void select(int32_t timeOut, size_t txBufSize) override final
    {

    }

    // Performs a general select on no specific socket 
    virtual void nullSelect(int32_t timeOut) override final
    {

    }

    // Receive data from the socket connection.  
    // A return code of -1 means no data received.
    // A return code >0 is number of bytes received.
    virtual int32_t receive(void *dest, uint32_t maxLen) override final
    {
        int32_t ret = -1;

        // cannot read if we are waiting for a send
        if (mPlaybackFile )
        {
            size_t r = fread(&ret, sizeof(ret), 1, mPlaybackFile);
            if (r == 1)
            {
                uint32_t _maxLen;
                r = fread(&_maxLen, sizeof(_maxLen), 1, mPlaybackFile);
                if (r == 1)
                {
                    if (ret > 0)
                    {
                        assert(ret <= int32_t(maxLen));
                        if (ret <= int32_t(maxLen))
                        {
                            r = fread(dest, ret, 1, mPlaybackFile);
                        }
                    }
                }
            }
        }

        return ret;
    }

    // Send this much data to the socket
    virtual int32_t send(const void *data, uint32_t dataLen) override final
    {
        return int32_t(dataLen);
    }

    // Close the socket
    virtual void	close(void) override final
    {

    }

    // Returns true if the socket send 'would block'
    virtual bool	wouldBlock(void) override final
    {
        return true;
    }

    // Returns true if a socket send is currently 'in progress'
    virtual bool	inProgress(void) override final
    {
        return true;
    }

    // Not sure what this is, but it's in the original code so making it available now.
    virtual void disableNaglesAlgorithm(void) override final
    {

    }

    // Close the socket and release this class
    virtual void release(void) override final
    {
        delete this;
    }

    bool isValid(void) const
    {
        return mPlaybackFile ? true : false;
    }

	// If we are a server, we poll for new connections.
	// If a new connection is found, then we return an instance of a Wsocket with that connection.
	// It is the caller's responsibility to release it when finished
	virtual Wsocket *pollServer(void) override final
	{
		return nullptr;
	}


    FILE    *mPlaybackFile{ nullptr };
};

Wsocket *Wsocket::create(const char *hostName, int32_t port)
{
	if (strcmp(hostName, SHARED_SERVER) == 0 ||
		strcmp(hostName, SHARED_CLIENT) == 0)
	{
		return createSocketSharedMemory(hostName, port);
	}
	auto ret = new WsocketImpl(hostName, port);
	if (!ret->isValid())
	{
		delete ret;
		ret = nullptr;
	}
	return static_cast<Wsocket *>(ret);
}

Wsocket *Wsocket::create(const char *playbackFile)
{
    auto ret = new WsocketPlayback(playbackFile);
    if (!ret->isValid())
    {
        delete ret;
        ret = nullptr;
    }
    return static_cast<Wsocket *>(ret);
}

void Wsocket::startupSockets(void)
{
#ifdef _WIN32
	INT rc;
	WSADATA wsaData;

	rc = WSAStartup(MAKEWORD(2, 2), &wsaData);
#endif

}

void Wsocket::shutdownSockets(void)
{
#ifdef _WIN32
	WSACleanup();
#endif
}

}