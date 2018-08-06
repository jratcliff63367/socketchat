#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "socketchat.h"
#include "wplatform.h"
#include "wsocket.h"
#include "SimpleBuffer.h"
#include "Timer.h"


#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#define DEFAULT_TRANSMIT_BUFFER_SIZE (1024*16)	// Default transmit buffer size is 16k
#define DEFAULT_RECEIVE_BUFFER_SIZE (1024*16)	// Default transmit buffer size is 16k
#define DEFAULT_MAX_READ_SIZE (1024*4)			// Maximum size of a single read operation
#define DEFAULT_MAXIMUM_BUFFER_SIZE (1024*1024)*512  // Don't ever cache more than 64 mb of data (for the moment...)

#define CONNECTION_TIME_OUT 60	// wait no more than this number of seconds for connection to complete


#define USE_LOGGING 1

namespace socketchat
{ // private module-only namespace

	class SocketChatImpl : public socketchat::SocketChat
	{
	public:
		SocketChatImpl(wsocket::Wsocket *clientSocket)
		{
            mReadyState = ReadyStateValues::OPEN;
			mIsServerClient = true;	// we are a server connection to a client
			mSocket = clientSocket;
			mTransmitBuffer = simplebuffer::SimpleBuffer::create(DEFAULT_TRANSMIT_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
			mReceiveBuffer = simplebuffer::SimpleBuffer::create(DEFAULT_RECEIVE_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
		}

		SocketChatImpl(const char *host,uint32_t port) : mReadyState(OPEN)
		{
            {
                mTransmitBuffer = simplebuffer::SimpleBuffer::create(DEFAULT_TRANSMIT_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
                mReceiveBuffer = simplebuffer::SimpleBuffer::create(DEFAULT_RECEIVE_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
                fprintf(stderr, "socketchat: connecting: host=%s port=%d\n", host, port);
                mSocket = wsocket::Wsocket::create(host, port);
                if (mSocket == nullptr)
                {
                    fprintf(stderr, "Unable to connect to %s:%d\n", host, port);
                }
				else
				{
                    mReadyState = ReadyStateValues::OPEN;
                    mSocket->disableNaglesAlgorithm();
				}
            }
		}

		virtual ~SocketChatImpl(void)
		{
			close();
			timer::Timer t;
			#define CLOSE_TIMEOUT 1		// if we don't receive a response to our close command in this time, go ahead and exit
			while (mReadyState != CLOSED)
			{
				poll(nullptr, 1);
				if (t.getElapsedSeconds() >= CLOSE_TIMEOUT)
				{
					break;
				}
			}
			if (mSocket)
			{
				mSocket->release();
			}
			if (mReceiveBuffer)
			{
				mReceiveBuffer->release();
			}
			if (mTransmitBuffer)
			{
				mTransmitBuffer->release();
			}
#if USE_LOGGING
            if (mLogFile)
            {
                fclose(mLogFile);
            }
#endif
		}

    virtual ReadyStateValues getReadyState() const override final
    {
        return mReadyState;
    }

    virtual void poll(SocketChatCallback *callback, int timeout) override final
    { // timeout in milliseconds
        if (!mSocket) return;
        if (mReadyState == CLOSED)
        {
            if (timeout > 0)
            {
                mSocket->nullSelect(timeout);
            }
            return;
        }
        while (true)
        {
            // Get the current read buffer address, and make sure we have room for this many bytes
            uint8_t *rbuffer = mReceiveBuffer->confirmCapacity(DEFAULT_MAX_READ_SIZE);
            if (!rbuffer)
            {
                break;
            }
            // Read from the socket
            int32_t ret = mSocket->receive(rbuffer, DEFAULT_MAX_READ_SIZE);
            // If we got no data but the transmission is still valid, just exit
            if (ret < 0 && (mSocket->wouldBlock() || mSocket->inProgress()))
            {
                break;
            }
            else if (ret <= 0) // If the socket is in a bad state and we got no data, close the connection
            {
                mSocket->close();
                mReadyState = CLOSED;
                fputs(ret < 0 ? "Connection error!\n" : "Connection closed!\n", stderr);
                break;
            }
            else
            {
                // Advance the buffer pointer by the number of bytes read
                mReceiveBuffer->addBuffer(nullptr, ret);
            }
        }
        if (mReadyState == CLOSED)
        {
            return;
        }
        while (mTransmitBuffer->getSize())
        {
            uint32_t dataLen;
            const uint8_t *buffer = mTransmitBuffer->getData(dataLen);
            int32_t ret = mSocket->send(buffer, dataLen);
            if (ret < 0 && (mSocket->wouldBlock() || mSocket->inProgress()))
            {
                break;
            }
            else if (ret <= 0)
            {
                mSocket->close();
                mReadyState = CLOSED;
                fputs(ret < 0 ? "Connection error!\n" : "Connection closed!\n", stderr);
                break;
            }
            else
            {
                mTransmitBuffer->consume(ret); // shrink the transmit buffer by the number of bytes we managed to send..
            }
        }
        if (mReadyState == SocketChat::CLOSED)
        {
            return;
        }
        if (!mTransmitBuffer->getSize() && mReadyState == CLOSING)
        {
            mSocket->close();
            mReadyState = CLOSED;
        }
        if (callback)
        {
            _dispatchBinary(callback);
        }
    }

    // Look for messages in the input receive buffer
    virtual void _dispatchBinary(SocketChatCallback *callback)
    {
        while (true)
        {
            uint32_t dataLen;
            uint8_t *data = mReceiveBuffer->getData(dataLen);
            if (dataLen < 2)
            {
                break;
            }
            bool haveMessage = false;
            uint32_t messageEnd = 0;
            for (uint32_t i = 0; i < (dataLen - 1); i++)
            {
                if (data[i] == 13 &&
                    data[i+1] == 10)
                {
                    haveMessage = true;
                    messageEnd = i;
                }
            }
            if (!haveMessage)
            {
                break;
            }
            data[messageEnd] = 0;
            callback->receiveMessage((const char *)data);
            mReceiveBuffer->consume(messageEnd + 1);
        }
    }

		virtual void sendText(const char *str) override final
		{
            size_t len = str ? strlen(str) : 0;
            mTransmitBuffer->addBuffer(str, uint32_t(len));
            mTransmitBuffer->addBuffer("\r\n", 2);
		}

#if USE_LOGGING
        void logReceive(const void *messageData, uint32_t message_size)
        {
            if (mLogFile)
            {
                fprintf(mLogFile, "===================================================================\r\n");
                fprintf(mLogFile, "RECEIVE[%d] : %d bytes\r\n", ++mReceiveCount, message_size);
                fprintf(mLogFile, "===================================================================\r\n");
                logData(messageData, message_size);
                fprintf(mLogFile, "\r\n");
                fprintf(mLogFile, "===================================================================\r\n");
                fprintf(mLogFile, "\r\n");
                fflush(mLogFile);
            }
        }

        void logData(const void *messageData, uint32_t message_size)
        {
            const uint8_t *scan = (const uint8_t *)messageData;
            for (uint32_t i = 0; i < message_size; i++)
            {
                uint8_t c = scan[i];
                if (c >= 32 && c < 128)
                {
                    fprintf(mLogFile, "%c", c);
                }
                else
                {
                    fprintf(mLogFile, "$%02X", uint32_t(c));
                }
            }
        }

        void logSend(const void *messageData, uint32_t message_size)
        {
            if (mLogFile)
            {
                fprintf(mLogFile, "===================================================================\r\n");
                fprintf(mLogFile, "SEND[%d] : %d bytes\r\n", ++mSendCount, message_size);
                fprintf(mLogFile, "===================================================================\r\n");
                logData(messageData, message_size);
                fprintf(mLogFile, "\r\n");
                fprintf(mLogFile, "===================================================================\r\n");
                fprintf(mLogFile, "\r\n");
                fflush(mLogFile);
            }
        }
#endif


		virtual void close() override final
		{
            {
                if (mReadyState == CLOSING || mReadyState == CLOSED)
                {
                    return;
                }
                // add the 'close frame' command to the transmit buffer and set the closing state on
                mReadyState = CLOSING;
            }
		}

		bool isValid(void) const
		{
			bool ret = mSocket ? true : false;
            return ret;
		}

		// Returns the total memory used by the transmit and receive buffers
		uint32_t getMemoryUsage(void) const override final
		{
            uint32_t ret = 0;
            {
                ret = mTransmitBuffer->getMaxBufferSize();
                ret += mReceiveBuffer->getMaxBufferSize();
            }
			return ret;
		}

		// Return the amount of memory being consumed by the pending transmit buffer
		virtual uint32_t getTransmitBufferSize(void) const override final
		{
            return mTransmitBuffer ? mTransmitBuffer->getSize() : 0;
		}

		// Maximum size of the buffer
		virtual uint32_t getTransmitBufferMaxSize(void) const override final
		{
            return mTransmitBuffer ? mTransmitBuffer->getMaxBufferSize() : 0;
		}

        // Log all sends
        virtual bool setLogFile(const char *fileName) override final
        {
#if USE_LOGGING
            if (mLogFile == nullptr)
            {
                char scratch[512];
                static uint32_t gSaveCount = 0;
                snprintf(scratch, 512, "%s%d.txt", fileName, ++gSaveCount);
                mLogFile = fopen(scratch, "wb");
                fprintf(mLogFile, "======================================================================\r\n");
                fprintf(mLogFile, "**** NEW SocketChat INSTANCE[%d] ****\r\n", gSaveCount);
                fprintf(mLogFile, "======================================================================\r\n");
                fflush(mLogFile);
            }

            return mLogFile ? true : false;
#else
			(fileName);
			return false;
#endif
        }

	private:
        SocketChatCallback           *mCallback{ nullptr };
		simplebuffer::SimpleBuffer	*mReceiveBuffer{ nullptr };		// receive buffer
		simplebuffer::SimpleBuffer	*mTransmitBuffer{ nullptr };	// transmit buffer
		wsocket::Wsocket			*mSocket{ nullptr };
		ReadyStateValues			mReadyState{ CLOSED };
		bool						mIsServerClient{ false }; // We are a server and this is a connection to a remote client
        uint32_t                    mSendCount{ 0 };
        uint32_t                    mReceiveCount{ 0 };
#if USE_LOGGING
        FILE                        *mLogFile{ nullptr };
#endif
};

SocketChat *SocketChat::create(const char *host,uint32_t port)
{
    auto ret = new SocketChatImpl(host, port);
	if (!ret->isValid())
	{
		delete ret;
		ret = nullptr;
	}
	return static_cast<SocketChat *>(ret);
}

// Create call for the server when a new client connection is established
SocketChat *SocketChat::create(wsocket::Wsocket *clientSocket)
{
	auto ret = new SocketChatImpl(clientSocket);
	if (!ret->isValid())
	{
		delete ret;
		ret = nullptr;
	}
	return static_cast<SocketChat *>(ret);
}


void socketStartup(void)
{
	wsocket::Wsocket::startupSockets();
}

void socketShutdown(void)
{
	wsocket::Wsocket::shutdownSockets();
}

} // namespace socketchat
