#pragma  once

// This code comes from:
// https://github.com/jratcliff63367/socketchat
//
#include <stdint.h>

namespace wsocket
{
	class Wsocket;
}

namespace socketchat 
{

// Pure virtual callback interface to receive messages from the server.
class SocketChatCallback
{
public:
	virtual void receiveMessage(const char *data) = 0;
};

class SocketChat 
{
public:
	enum ReadyStateValues 
	{ 
		CLOSING, 
		CLOSED, 
		CONNECTING, 
		OPEN 
	};

    static SocketChat *create(const char *host, uint32_t port);

	// Create call for the server when a new client connection is established
	static SocketChat *create(wsocket::Wsocket *clientSocket);

	virtual ~SocketChat(void)
	{
	}


	// This client is polled from a single thread.  These methods are *not* thread safe.
	// If you call them from different threads, you will need to create your own mutex.
	// While this 'poll' call is non-blocking, you can still run the whole socket connection in it's own
	// thread.  This is recommended for maximum performance
	// Calling the 'poll' routine will process all sends and receives
	// If any new messages have been received from the sever and you have provided a valid 'callback' pointer, then
	// it will send incoming messages back through that interface
	virtual void poll(SocketChatCallback *callback,int32_t timeout = 0) = 0; // timeout in milliseconds

	// Send a text message to the server.  Assumed zero byte terminated ASCIIZ string
	virtual void sendText(const char *str) = 0;

	// Close the connection
	virtual void close() = 0;

	// Retrieve the current state of the connection
	virtual ReadyStateValues getReadyState() const = 0;

	// Returns the total memory used by the transmit and receive buffers
	virtual uint32_t getMemoryUsage(void) const = 0;

	// Return the amount of memory being used by pending transmit frames
	virtual uint32_t getTransmitBufferSize(void) const = 0;

	// Maximum size of the buffer
	virtual uint32_t getTransmitBufferMaxSize(void) const = 0;

    // Log all sends and receives
    virtual bool setLogFile(const char *fileName) = 0;


};

// Initialize sockets one time for your app.
void socketStartup(void);

// Shutdown sockets on exit from your app
void socketShutdown(void);

} // namespace socketchat


