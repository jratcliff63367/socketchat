
#ifdef _MSC_VER
#endif

#include "socketchat.h"
#include "InputLine.h"
#include "wplatform.h"

#include <stdio.h>
#include <string.h>

//#define PORT_NUMBER 6379    // Redis port number
#define PORT_NUMBER 3009    // test port number

class ReceiveData : public socketchat::SocketChatCallback
{
public:
	virtual void receiveMessage(const char *message) override final
	{
        printf("Received: %s\r\n", message);
	}
};

int main(int argc,const char **argv)
{
//	testSharedMemory();

	const char *host = "localhost";
	if (argc == 2)
	{
		host = argv[1];
	}
	{
		socketchat::socketStartup();
        socketchat::SocketChat *ws = socketchat::SocketChat::create(host,PORT_NUMBER);
		if (ws)
		{
			printf("Type: 'bye' or 'quit' or 'exit' to close the client out.\r\n");
			inputline::InputLine *inputLine = inputline::InputLine::create();
			ReceiveData rd;
			bool keepRunning = true;
			while (keepRunning)
			{
				const char *data = inputLine->getInputLine();
				if (data)
				{
					if (strcmp(data, "bye") == 0 || strcmp(data, "exit") == 0 || strcmp(data, "quit") == 0)
					{
						keepRunning = false;
					}
					ws->sendText(data);
				}
				ws->poll(&rd, 1); // poll the socket connection
			}
			delete ws;
		}
		socketchat::socketShutdown();
	}
}
