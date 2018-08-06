#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include "easywsclient.h"
#include "wplatform.h"
#include "wsocket.h"
#include "SimpleBuffer.h"
#include "FastXOR.h"
#include "Timer.h"

#define USE_PROXY_SERVER 0

#if USE_PROXY_SERVER
#include "ApiServer.h"
#endif

#ifdef _MSC_VER
#pragma warning(disable:4996)
#endif

#define DEFAULT_TRANSMIT_BUFFER_SIZE (1024*16)	// Default transmit buffer size is 16k
#define DEFAULT_RECEIVE_BUFFER_SIZE (1024*16)	// Default transmit buffer size is 16k
#define DEFAULT_MAX_READ_SIZE (1024*4)			// Maximum size of a single read operation
#define DEFAULT_MAXIMUM_BUFFER_SIZE (1024*1024)*512  // Don't ever cache more than 64 mb of data (for the moment...)

#define CONNECTION_TIME_OUT 60	// wait no more than this number of seconds for connection to complete


#define USE_LOGGING 1

namespace easywsclient
{ // private module-only namespace

	enum class ConnectionPhase :uint32_t 
	{
		// These are the responses we expect from the server
		HTTP_STATUS		= 1,			// "HTTP/1.1 101 Switching Protocols"
		HCONNECTION_UPGRADE,			// "HConnection: upgrade"
		HSEC_WEBSOCKET_ACCEPT,			// "HSec-WebSocket-Accept: HSmrc0sMlYUkAGmm5OPpG2HaGWk="
		HSERVER_WEBSOCKET,				// "HServer: WebSocket++/0.7.0"
		HUPGRADE_WEBSOCKET,				// "HUpgrade: websocket"
		H,								// "H"
		SERVER_CLIENT_STRINGS,			// Server just parsing incoming strings from the client connection
	};

	class WebSocketImpl : public easywsclient::WebSocket
#if USE_PROXY_SERVER
		, public apiserver::ApiServer::Callback
#endif
	{
	public:
		// http://tools.ietf.org/html/rfc6455#section-5.2  Base Framing Protocol
		//
		//  0                   1                   2                   3
		//  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		// +-+-+-+-+-------+-+-------------+-------------------------------+
		// |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
		// |I|S|S|S|  (4)  |A|     (7)     |             (16/64)           |
		// |N|V|V|V|       |S|             |   (if payload len==126/127)   |
		// | |1|2|3|       |K|             |                               |
		// +-+-+-+-+-------+-+-------------+ - - - - - - - - - - - - - - - +
		// |     Extended payload length continued, if payload len == 127  |
		// + - - - - - - - - - - - - - - - +-------------------------------+
		// |                               |Masking-key, if MASK set to 1  |
		// +-------------------------------+-------------------------------+
		// | Masking-key (continued)       |          Payload Data         |
		// +-------------------------------- - - - - - - - - - - - - - - - +
		// :                     Payload Data continued ...                :
		// + - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - +
		// |                     Payload Data continued ...                |
		// +---------------------------------------------------------------+
		struct wsheader_type
		{
			uint32_t	header_size;		// Size of the header
			bool		fin;				// Whether or not this is the final block
			bool		mask;				// whether or not this frame uses masking
			enum opcode_type
			{
				CONTINUATION	= 0,
				TEXT_FRAME		= 1,
				BINARY_FRAME	= 2,
				CLOSE			= 8,
				PING			= 9,
				PONG			= 10
			} opcode;
			int32_t		N0;
			uint64_t	N;
			uint8_t		masking_key[4];		// Masking key used for this frame
		};

		WebSocketImpl(wsocket::Wsocket *clientSocket, bool useMask)
		{
			mIsServerClient = true;	// we are a server connection to a client
			mSocket = clientSocket;
			mUseMask = useMask;
			mTransmitBuffer = simplebuffer::SimpleBuffer::create(DEFAULT_TRANSMIT_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
			mReceivedData = simplebuffer::SimpleBuffer::create(DEFAULT_RECEIVE_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
			mReceiveBuffer = simplebuffer::SimpleBuffer::create(DEFAULT_RECEIVE_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
			mReadyState = CONNECTING;
		}

		WebSocketImpl(const char *url,const char *origin, bool useMask) : mReadyState(OPEN), mUseMask(useMask)
		{
#if USE_PROXY_SERVER
            if (strcmp(url, "apiserver") == 0)
            {
                mProxyServer = apiserver::ApiServer::create();
            }
            else
#endif
            {
                mTransmitBuffer = simplebuffer::SimpleBuffer::create(DEFAULT_TRANSMIT_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
                mReceivedData = simplebuffer::SimpleBuffer::create(DEFAULT_RECEIVE_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);
                mReceiveBuffer = simplebuffer::SimpleBuffer::create(DEFAULT_RECEIVE_BUFFER_SIZE, DEFAULT_MAXIMUM_BUFFER_SIZE);

                size_t urlSize = strlen(url);
                size_t originSize = strlen(origin);
                char host[128];
                int port = 0;
                char path[128];
                if (urlSize >= 128)
                {
                    fprintf(stderr, "ERROR: url size limit exceeded: %s\n", url);
                }
                else if (originSize >= 200)
                {
                    fprintf(stderr, "ERROR: origin size limit exceeded: %s\n", origin);
                }
                else
                {
                    if (sscanf(url, "ws://%[^:/]:%d/%s", host, &port, path) == 3)
                    {
                    }
                    else if (sscanf(url, "ws://%[^:/]/%s", host, path) == 2)
                    {
                        port = 80;
                    }
                    else if (sscanf(url, "ws://%[^:/]:%d", host, &port) == 2)
                    {
                        path[0] = '\0';
                    }
                    else if (sscanf(url, "ws://%[^:/]", host) == 1)
                    {
                        port = 80;
                        path[0] = '\0';
                    }
                    else
                    {
                        fprintf(stderr, "ERROR: Could not parse WebSocket url: %s\n", url);
                    }
                    if (port)
                    {
                        //					fprintf(stderr, "easywsclient: connecting: host=%s port=%d path=/%s\n", host, port, path);
#ifdef TEST_PLAYBACK
                        mSocket = wsocket::Wsocket::create(TEST_PLAYBACK);
#else
                        mSocket = wsocket::Wsocket::create(host, port);
#endif
                        if (mSocket == nullptr)
                        {
                            fprintf(stderr, "Unable to connect to %s:%d\n", host, port);
                        }
						else
						{
							mReadyState = ReadyStateValues::CONNECTING;
							// XXX: this should be done non-blocking,
							char line[256];
							wplatform::stringFormat(line, 256, "GET /%s HTTP/1.1\r\n", path);
							mSocket->send(line, uint32_t(strlen(line)));
							if (port == 80)
							{
								wplatform::stringFormat(line, 256, "Host: %s\r\n", host);
								mSocket->send(line, uint32_t(strlen(line)));
							}
							else
							{
								wplatform::stringFormat(line, 256, "Host: %s:%d\r\n", host, port);
								mSocket->send(line, uint32_t(strlen(line)));
							}
							wplatform::stringFormat(line, 256, "Upgrade: websocket\r\n");
							mSocket->send(line, uint32_t(strlen(line)));
							wplatform::stringFormat(line, 256, "Connection: Upgrade\r\n");
							mSocket->send(line, uint32_t(strlen(line)));
							if (originSize)
							{
								wplatform::stringFormat(line, 256, "Origin: %s\r\n", origin);
								mSocket->send(line, uint32_t(strlen(line)));
							}
							wplatform::stringFormat(line, 256, "Sec-WebSocket-Key: x3JJHMbDL1EzLkh9GBhXDw==\r\n");
							mSocket->send(line, uint32_t(strlen(line)));
							wplatform::stringFormat(line, 256, "Sec-WebSocket-Version: 13\r\n");
							mSocket->send(line, uint32_t(strlen(line)));
							wplatform::stringFormat(line, 256, "\r\n");
							mSocket->send(line, uint32_t(strlen(line)));
							mConnectionTimer.getElapsedSeconds();
							// Ok...send all of the connection strings
						}
                    }
                }
            }
		}

		virtual ~WebSocketImpl(void)
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
			if (mReceivedData)
			{
				mReceivedData->release();
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

		virtual void poll(WebSocketCallback *callback, int timeout) override final
		{ // timeout in milliseconds
#if USE_PROXY_SERVER
            if (mProxyServer)
            {
                mCallback = callback;
                mProxyServer->processResponses(this);
                mCallback = nullptr;
                return;
            }
#endif
            if (!mSocket) return;

			if (mReadyState == CONNECTING)
			{
				processConnection();
				return;
			}

			if (mReadyState == CLOSED)
			{
				if (timeout > 0)
				{
					mSocket->nullSelect(timeout);
				}
				return;
			}
#if 0
			if (timeout != 0)
			{
				mSocket->select(timeout, mTransmitBuffer->getSize());
			}
#endif
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
			if (mReadyState == WebSocket::CLOSED)
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

		virtual void _dispatchBinary(WebSocketCallback *callback)
		{
			while (true)
			{
				wsheader_type ws;
				uint32_t dataLen;
				uint8_t *data = mReceiveBuffer->getData(dataLen);
				if (dataLen < 2) 
				{ 
					break;
				}
				ws.fin		= (data[0] & 0x80) == 0x80;
				ws.opcode	= (wsheader_type::opcode_type) (data[0] & 0x0f);
				ws.mask		= (data[1] & 0x80) == 0x80;
				ws.N0		= (data[1] & 0x7f);
				ws.header_size = 2 + (ws.N0 == 126 ? 2 : 0) + (ws.N0 == 127 ? 8 : 0) + (ws.mask ? 4 : 0);

				if (dataLen < ws.header_size) 
				{ 
					break;
				}

				int32_t i = 0;
				if (ws.N0 < 126)
				{
					ws.N = ws.N0;
					i = 2;
				}
				else if (ws.N0 == 126)
				{
					ws.N = 0;
					ws.N |= ((uint64_t)data[2]) << 8;
					ws.N |= ((uint64_t)data[3]) << 0;
					i = 4;
				}
				else if (ws.N0 == 127)
				{
					ws.N = 0;
					ws.N |= ((uint64_t)data[2]) << 56;
					ws.N |= ((uint64_t)data[3]) << 48;
					ws.N |= ((uint64_t)data[4]) << 40;
					ws.N |= ((uint64_t)data[5]) << 32;
					ws.N |= ((uint64_t)data[6]) << 24;
					ws.N |= ((uint64_t)data[7]) << 16;
					ws.N |= ((uint64_t)data[8]) << 8;
					ws.N |= ((uint64_t)data[9]) << 0;
					i = 10;
				}
				if (ws.mask)
				{
					ws.masking_key[0] = ((uint8_t)data[i + 0]) << 0;
					ws.masking_key[1] = ((uint8_t)data[i + 1]) << 0;
					ws.masking_key[2] = ((uint8_t)data[i + 2]) << 0;
					ws.masking_key[3] = ((uint8_t)data[i + 3]) << 0;
				}
				else
				{
					ws.masking_key[0] = 0;
					ws.masking_key[1] = 0;
					ws.masking_key[2] = 0;
					ws.masking_key[3] = 0;
				}
                uint32_t frameSize = ws.header_size + uint32_t(ws.N);
                // If we don't have the full packet worth of data yet...
				if (dataLen < frameSize) 
				{ 
					break;
				}

				// We got a whole message, now do something with it:
				if (ws.opcode == wsheader_type::TEXT_FRAME
					|| ws.opcode == wsheader_type::BINARY_FRAME
					|| ws.opcode == wsheader_type::CONTINUATION)
				{
					if (ws.mask)
					{
						fastxor::fastXOR(data + ws.header_size,uint32_t(ws.N), ws.masking_key);
					}
					// If we are finished and there is no previous received data we can avoid a memory
					// copy by just calling back directly with this receive buffer
					if (ws.fin && mReceivedData->getSize() == 0)
					{
						if (callback )
						{
#if USE_LOGGING
                            logReceive(data + ws.header_size, uint32_t(ws.N));
#endif
							callback->receiveMessage(data+ws.header_size, uint32_t(ws.N), ws.opcode == wsheader_type::TEXT_FRAME);
						}
					}
					else
					{
						// Add the input data to the receive data frame we are accumulating
						mReceivedData->addBuffer(data + ws.header_size, uint32_t(ws.N));
						if (ws.fin)
						{
							if (callback && mReceivedData->getSize())
							{
								uint32_t dlen;
								const void *rdata = mReceivedData->getData(dlen);
#if USE_LOGGING
                                logReceive(rdata, dlen);
#endif
								callback->receiveMessage(rdata, dlen, ws.opcode == wsheader_type::TEXT_FRAME);
							}
							mReceivedData->clear();
						}
					}
                    // We just processed this packet, mark it as consumed
                    mReceiveBuffer->consume(frameSize); // It's been consumed
				}
				else if (ws.opcode == wsheader_type::PING)
				{
					if (ws.mask)
					{
						fastxor::fastXOR(data + ws.header_size, uint32_t(ws.N), ws.masking_key);
					}
					const uint8_t *pingData = nullptr;
					if (ws.N)
					{
						pingData = data + ws.header_size;
					}
					sendData(wsheader_type::PONG, pingData, ws.N);
                    mReceiveBuffer->consume(frameSize);
				}
				else if (ws.opcode == wsheader_type::PONG)
				{
                    mReceiveBuffer->consume(frameSize);
				}
				else if (ws.opcode == wsheader_type::CLOSE)
				{
					close();
					break;
				}
				else
				{
					fprintf(stderr, "ERROR: Got unexpected WebSocket message.\n");
					close();
					break;
				}
			}
		}

		virtual void sendPing() override final
		{
#if USE_PROXY_SERVER
            if (mProxyServer) return;
#endif
			sendData(wsheader_type::PING, nullptr, 0);
		}

		virtual void sendText(const char *str) override final
		{
#if USE_PROXY_SERVER
            if (mProxyServer)
            {
#if USE_LOGGING
                logSend(str, uint32_t(strlen(str)));
#endif
                mProxyServer->sendText(str);
            }
            else
#endif
            {
                size_t len = str ? strlen(str) : 0;
                sendData(wsheader_type::TEXT_FRAME, str, len);
            }
		}

		virtual void sendBinary(const void *data, uint32_t dataLen) override final
		{
#if USE_PROXY_SERVER
            if (mProxyServer)
            {
#if USE_LOGGING
                logSend(data, dataLen);
#endif
                mProxyServer->sendBinary(data, dataLen);
            }
			else
#endif
            {
                sendData(wsheader_type::BINARY_FRAME, data, dataLen);
            }
		}

		// Just get the high resolution timer as the current masking key
		// we just take the bottom 32 bits of the current high resolution time
		// It doesn't have the most entropy in the world, but it's good enough for
		// this use case and is reasonably fast and portable.
		inline void getMaskingKey(uint8_t maskingKey[4])
		{
#if 1
			uint64_t seed = wplatform::getRandomTime();
			memcpy(maskingKey, &seed, 4);
#else
            maskingKey[0] = 0;
            maskingKey[1] = 0;
            maskingKey[2] = 0;
            maskingKey[3] = 0;
#endif
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


		void sendData(wsheader_type::opcode_type type,	// Type of data we are sending
					  const void *messageData,			// The optional message data (this can be null)
					  uint64_t message_size)			// The size of the message data
		{
#if USE_LOGGING
            logSend(messageData, uint32_t(message_size));
#endif
			uint8_t masking_key[4];
			getMaskingKey(masking_key);
			// TODO: consider acquiring a lock on mTransmitBuffer...
			if (mReadyState == CLOSING || mReadyState == CLOSED)
			{
				return;
			}

			uint8_t header[14];
			uint32_t expectedHeaderLen = 2 + (message_size >= 126 ? 2 : 0) + (message_size >= 65536 ? 6 : 0) + (mUseMask ? 4 : 0);
			uint32_t headerLen = expectedHeaderLen;

			header[0] = uint8_t(0x80 | type);

			if (message_size < 126)
			{
				header[1] = (message_size & 0xff) | (mUseMask ? 0x80 : 0);
				if (mUseMask)
				{
					header[2] = masking_key[0];
					header[3] = masking_key[1];
					header[4] = masking_key[2];
					header[5] = masking_key[3];
					headerLen = 6;
				}
				else
				{
					headerLen = 2;
				}
			}
			else if (message_size < 65536)
			{
				header[1] = 126 | (mUseMask ? 0x80 : 0);
				header[2] = (message_size >> 8) & 0xff;
				header[3] = (message_size >> 0) & 0xff;
				if (mUseMask)
				{
					header[4] = masking_key[0];
					header[5] = masking_key[1];
					header[6] = masking_key[2];
					header[7] = masking_key[3];
					headerLen = 8;
				}
				else
				{
					headerLen = 4;
				}
			}
			else
			{ // TODO: run coverage testing here
				header[1] = 127 | (mUseMask ? 0x80 : 0);
				header[2] = (message_size >> 56) & 0xff;
				header[3] = (message_size >> 48) & 0xff;
				header[4] = (message_size >> 40) & 0xff;
				header[5] = (message_size >> 32) & 0xff;
				header[6] = (message_size >> 24) & 0xff;
				header[7] = (message_size >> 16) & 0xff;
				header[8] = (message_size >> 8) & 0xff;
				header[9] = (message_size >> 0) & 0xff;
				if (mUseMask)
				{
					header[10] = masking_key[0];
					header[11] = masking_key[1];
					header[12] = masking_key[2];
					header[13] = masking_key[3];
					headerLen = 14;
				}
				else
				{
					headerLen = 10;
				}
			}
			assert(headerLen == expectedHeaderLen);
			// N.B. - mTransmitBuffer will keep growing until it can be transmitted over the socket:
			mTransmitBuffer->addBuffer(header, headerLen);
			if (messageData)
			{
				mTransmitBuffer->addBuffer(messageData, uint32_t(message_size));
			}
			// If we are using masking then we need to XOR the message by the mask
			if (mUseMask)
			{
				uint32_t message_offset = mTransmitBuffer->getSize() - uint32_t(message_size);
				uint32_t dataLen;
				uint8_t *data = mTransmitBuffer->getData(dataLen);
				uint8_t *maskData = &data[message_offset];
				fastxor::fastXOR(maskData, uint32_t(message_size), masking_key);
			}
		}

		virtual void close() override final
		{
#if USE_PROXY_SERVER
            if (mProxyServer)
            {
                mReadyState = CLOSED; // it can close immediately.
            }
            else
#endif
            {
                if (mReadyState == CLOSING || mReadyState == CLOSED)
                {
                    return;
                }
                // add the 'close frame' command to the transmit buffer and set the closing state on
                mReadyState = CLOSING;
                uint8_t closeFrame[6] = { 0x88, 0x80, 0x00, 0x00, 0x00, 0x00 }; // last 4 bytes are a masking key
                mTransmitBuffer->addBuffer(closeFrame, sizeof(closeFrame));
            }
		}

		bool isValid(void) const
		{
			bool ret = mSocket ? true : false;
#if USE_PROXY_SERVER
            if (mProxyServer)
            {
                ret = true;
            }
#endif
            return ret;
		}

		// Returns the total memory used by the transmit and receive buffers
		uint32_t getMemoryUsage(void) const override final
		{
            uint32_t ret = 0;
#if USE_PROXY_SERVER
            if (!mProxyServer)
#endif
            {
                ret = mTransmitBuffer->getMaxBufferSize();
                ret += mReceiveBuffer->getMaxBufferSize();
                ret += mReceivedData->getMaxBufferSize();
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
                fprintf(mLogFile, "**** NEW EASYWSCLIENT INSTANCE[%d] ****\r\n", gSaveCount);
                fprintf(mLogFile, "======================================================================\r\n");
                fflush(mLogFile);
            }

            return mLogFile ? true : false;
#else
			(fileName);
			return false;
#endif
        }
#if USE_PROXY_SERVER
        virtual void receiveServerMessage(const void *data, uint32_t dlen, bool isAscii) override final
        {
#if USE_LOGGING
            logReceive(data, dlen);
#endif
            if (mCallback)
            {
                mCallback->receiveMessage(data, dlen, isAscii);
            }
        }
#endif

		void socketSendString(const char *str)
		{
			if (mSocket)
			{
				uint32_t slen = uint32_t(strlen(str));
				mSocket->send(str, slen);
			}
		}

		// Process connection state.
		// If we are a client, then we send the initial request and wait for responses.
		// If we are a server, we wait for the initial request and, once we get it, send responses.
		// If we fail to get a timely response within the 'CONNECTION_TIME_OUT' period, we close
		// the connection
		void processConnection(void)
		{
			if (!mSocket) return;
			double delay = mConnectionTimer.peekElapsedSeconds();
			if (delay >= CONNECTION_TIME_OUT)
			{
				mSocket->release();
				mSocket = nullptr;
				mReadyState = CLOSED;
				return;
			}
			int32_t v = mSocket->receive(&mConnectionBuffer[mConnectionIndex], 1);
			if (v > 0)
			{
				if (mConnectionBuffer[mConnectionIndex] == 0x0A)
				{
					mConnectionBuffer[mConnectionIndex] = 0;
					if (mConnectionIndex > 0)
					{
						if (mConnectionBuffer[mConnectionIndex - 1] == 0x0D)
						{
							mConnectionBuffer[mConnectionIndex - 1] = 0;
						}
					}
					mConnectionIndex = 0;

					bool ok = false;
					switch (mConnectionPhase)
					{
						case ConnectionPhase::SERVER_CLIENT_STRINGS:
							ok = true;
							// Just a CR/LF
							if ( strlen(mConnectionBuffer) == 0 )
							{
								mReadyState = WebSocket::OPEN; // we processed all incoming strings from the client as expected
								mSocket->disableNaglesAlgorithm();
							}
							break;
						case ConnectionPhase::HTTP_STATUS:
							if (mIsServerClient)
							{
								if (strcmp(mConnectionBuffer, "GET / HTTP/1.1") == 0)
								{
									ok = true;
									// when the client sends us the 'GET HTTP' command we (as a server)
									// respond with the following lines of protocol response.
									// Clearly this is hardcoded here, but it seems satisfactory for now
									socketSendString("HTTP/1.1 101 Switching Protocols\r\n");
									socketSendString("HConnection: upgrade\r\n");
									socketSendString("HSec-WebSocket-Accept: HSmrc0sMlYUkAGmm5OPpG2HaGWk=\r\n");
									socketSendString("HServer: WebSocket++/0.7.0\r\n");
									socketSendString("HUpgrade: websocket\r\n");
									socketSendString("\r\n");
									mConnectionPhase = ConnectionPhase::SERVER_CLIENT_STRINGS;
								}
							}
							else
							{
								int32_t status = 0;
								if (sscanf(mConnectionBuffer, "HTTP/1.1 %d", &status) != 1 || status != 101)
								{
									// unexpected connection format
								}
								else
								{
									ok = true;
									mConnectionPhase = ConnectionPhase::HCONNECTION_UPGRADE;
								}
							}
							break;
						case ConnectionPhase::HCONNECTION_UPGRADE:
							ok = true;
							mConnectionPhase = ConnectionPhase::HSEC_WEBSOCKET_ACCEPT;
							break;
						case ConnectionPhase::HSEC_WEBSOCKET_ACCEPT:
							ok = true;
							mConnectionPhase = ConnectionPhase::HSERVER_WEBSOCKET;
							break;
						case ConnectionPhase::HSERVER_WEBSOCKET:
							ok = true;
							mConnectionPhase = ConnectionPhase::HUPGRADE_WEBSOCKET;
							break;
						case ConnectionPhase::HUPGRADE_WEBSOCKET:
							ok = true;
							mConnectionPhase = ConnectionPhase::H;
							break;
						case ConnectionPhase::H:
							ok = true;
							mReadyState = WebSocket::OPEN; // successfully connected to websockets server!!
							mSocket->disableNaglesAlgorithm();
							break;
					}
					if (!ok)
					{
						mSocket->release();
						mSocket = nullptr;
						mReadyState = CLOSED;
					}

					mConnectionTimer.getElapsedSeconds();
				}
				else
				{
					mConnectionIndex++;
					if (mConnectionIndex == 255)
					{
						mSocket->release();
						mSocket = nullptr;
						mReadyState = CLOSED;
					}
				}
			}
		}

	private:
        WebSocketCallback           *mCallback{ nullptr };
#if USE_PROXY_SERVER
        apiserver::ApiServer    *mProxyServer{ nullptr };
#endif
		simplebuffer::SimpleBuffer	*mReceiveBuffer{ nullptr };		// receive buffer
		simplebuffer::SimpleBuffer	*mTransmitBuffer{ nullptr };	// transmit buffer
		simplebuffer::SimpleBuffer	*mReceivedData{ nullptr };		// received data
		wsocket::Wsocket			*mSocket{ nullptr };
		ReadyStateValues			mReadyState{ CLOSED };
		bool						mUseMask{ true };
		bool						mIsServerClient{ false }; // We are a server and this is a connection to a remote client
        uint32_t                    mSendCount{ 0 };
        uint32_t                    mReceiveCount{ 0 };
#if USE_LOGGING
        FILE                        *mLogFile{ nullptr };
#endif
		uint32_t					mConnectionIndex{ 0 };
		char						mConnectionBuffer[256];
		timer::Timer				mConnectionTimer;
		ConnectionPhase				mConnectionPhase{ ConnectionPhase::HTTP_STATUS };
};

WebSocket *WebSocket::create(const char *url, const char *origin,bool useMask)
{
#if USE_PROXY_SERVER
    url = "apiserver";
#endif
	auto ret = new WebSocketImpl(url, origin, useMask);
	if (!ret->isValid())
	{
		delete ret;
		ret = nullptr;
	}
	return static_cast<WebSocket *>(ret);
}

// Create call for the server when a new client connection is established
WebSocket *WebSocket::create(wsocket::Wsocket *clientSocket, bool useMask)
{
	auto ret = new WebSocketImpl(clientSocket, useMask);
	if (!ret->isValid())
	{
		delete ret;
		ret = nullptr;
	}
	return static_cast<WebSocket *>(ret);
}


void socketStartup(void)
{
	wsocket::Wsocket::startupSockets();
}

void socketShutdown(void)
{
	wsocket::Wsocket::shutdownSockets();
}

} // namespace easywsclient
