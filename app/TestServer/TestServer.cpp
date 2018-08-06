
#ifdef _MSC_VER
#endif

#include "easywsclient.h"
#include "wsocket.h"
#include "InputLine.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string>
#include <vector>

using easywsclient::WebSocket;

class ClientConnection : public easywsclient::WebSocketCallback
{
public:
	ClientConnection(wsocket::Wsocket *client,uint32_t id) : mId(id)
	{
		mClient = easywsclient::WebSocket::create(client, true);
	}

	virtual ~ClientConnection(void)
	{
		delete mClient;
	}

	uint32_t getId(void) const
	{
		return mId;
	}

	const char * getMessage(void)
	{
		const char *ret = nullptr;

		mHaveMessage = false;
		if (mClient)
		{
			mClient->poll(this, 1);
			if (mHaveMessage)
			{
				ret = mMessage.c_str();
			}
		}

		return ret;
	}

	void sendText(const char *str)
	{
		if (mClient)
		{
			mClient->sendText(str);
		}
	}

	virtual void receiveMessage(const void *data, uint32_t dataLen, bool isAscii) override final
	{
		if (isAscii && dataLen < 511)
		{
			char temp[512];
			memcpy(temp, data, dataLen);
			temp[dataLen] = 0;
			mHaveMessage = true;
			mMessage = std::string(temp);
		}
	}

	bool isConnected(void)
	{
		bool ret = false;

		if (mClient && mClient->getReadyState() != easywsclient::WebSocket::CLOSED)
		{
			ret = true;
		}

		return ret;
	}

	bool					mHaveMessage{ false };
	std::string				mMessage;
	easywsclient::WebSocket	*mClient{ nullptr };
	uint32_t				mId{ 0 };
};

typedef std::vector< ClientConnection * > ClientConnectionVector;

class SimpleServer
{
public:
	SimpleServer(void)
	{
		mServerSocket = wsocket::Wsocket::create(SOCKET_SERVER, 3009);
//		mServerSocket = wsocket::Wsocket::create(SHARED_SERVER, 3009);
		mInputLine = inputline::InputLine::create();
		printf("Simple Websockets chat server started.\r\n");
		printf("Type 'bye', 'quit', or 'exit' to stop the server.\r\n");
		printf("Type anything else to send as a broadcast message to all current client connections.\r\n");
	}

	~SimpleServer(void)
	{
		if (mInputLine)
		{
			mInputLine->release();
		}
		for (auto &i : mClients)
		{
			delete i;
		}
		if (mServerSocket)
		{
			mServerSocket->release();
		}
	}

	void run(void)
	{
		bool exit = false;

		while (!exit)
		{
			if (mServerSocket)
			{
				wsocket::Wsocket *clientSocket = mServerSocket->pollServer();
				if (clientSocket)
				{
					uint32_t index = uint32_t(mClients.size()) + 1;
					ClientConnection *cc = new ClientConnection(clientSocket, index);
					printf("New client connection (%d) established.\r\n", index);
					mClients.push_back(cc);
				}
			}
			if (mInputLine)
			{
				const char *str = mInputLine->getInputLine();
				if (str)
				{
					if (strcmp(str, "bye") == 0 ||
						strcmp(str,"quit") == 0 ||
						strcmp(str,"exit") == 0 )
					{
						exit = true;
					}
					else
					{
						for (auto &i : mClients)
						{
							i->sendText(str);
						}
					}
				}
			}

			// See if any clients have dropped connection
			bool killed = true;
			while (killed)
			{
				killed = false;
				for (ClientConnectionVector::iterator i = mClients.begin(); i != mClients.end(); ++i)
				{
					if (!(*i)->isConnected() )
					{
						killed = true;
						ClientConnection *cc = (*i);
						printf("Lost connection to client: %d\r\n", cc->getId());
						delete cc;
						mClients.erase(i);
						break;
					}
				}
			}

			// For each active client connection..
			// we see if that client has received a new message
			// If we have received a message from a client, then we echo that message back to
			// all currently connected clients
			for (auto &i : mClients)
			{
				const char *newMessage = i->getMessage();
				if (newMessage)
				{
					printf("Client[%d] : %s\r\n", i->getId(), newMessage);
					for (auto &j : mClients)
					{
						j->sendText(newMessage);
					}
				}
			}
		}
	}

	wsocket::Wsocket		*mServerSocket{ nullptr };
	inputline::InputLine	*mInputLine{ nullptr };
	ClientConnectionVector	mClients;
};


int main()
{
	easywsclient::socketStartup();
	// Run the simple server
	{
		SimpleServer ss;
		ss.run();
	}

	easywsclient::socketShutdown();

	return 0;
}
