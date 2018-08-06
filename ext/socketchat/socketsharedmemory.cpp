#include "socketsharedmemory.h"
#include "wsocket.h"
#include "MemoryMap.h"
#include "wplatform.h"
#include "SPSC.h"

#ifdef _MSC_VER
#pragma warning(disable:4100)
#endif

#define SHARED_BUFFER_SIZE (1024*16)

namespace wsocket
{

class WsocketSharedMemory : public Wsocket
{
public:
	WsocketSharedMemory(const char *hostName,int32_t port)
	{
		mIsServer = strcmp(hostName, SHARED_SERVER) == 0;
		char scratch[512];
		wplatform::stringFormat(scratch, 512, "f:\\@sharedserver.%d.cache", port);
		uint64_t fsize = SHARED_BUFFER_SIZE;
		mServerFile = memorymap::MemoryMap::createMemoryMap(scratch, fsize, mIsServer, false);
		wplatform::stringFormat(scratch, 512, "f:\\@sharedclient.%d.cache", port);
		fsize = SHARED_BUFFER_SIZE;
		mClientFile = memorymap::MemoryMap::createMemoryMap(scratch, fsize, mIsServer, false);
		if (mServerFile && mClientFile)
		{
			// If I'm the server, then I write to the server file and read from the client file
			if (mIsServer)
			{
				mWriter.init(mServerFile->getBaseAddress(), uint32_t(mServerFile->getFileSize()), true,true);
				mReader.init(mClientFile->getBaseAddress(), uint32_t(mClientFile->getFileSize()), false,true);
			}
			else
			{
				// If I am a client..then I write to the client file and read from the server file
				mWriter.init(mClientFile->getBaseAddress(), uint32_t(mClientFile->getFileSize()), true,false);
				mReader.init(mServerFile->getBaseAddress(), uint32_t(mServerFile->getFileSize()), false,false);
				mWriter.incrementSequenceNumber();
			}
		}
	}

	virtual ~WsocketSharedMemory(void)
	{
		if (mClientFile)
		{
			mClientFile->release();
		}
		if (mServerFile)
		{
			mServerFile->release();
		}
	}

	// If we are a server, we poll for new connections.
	// If a new connection is found, then we return an instance of a Wsocket with that connection.
	// It is the caller's responsibility to release it when finished
	virtual Wsocket *pollServer(void) override final
	{
		Wsocket *ret = nullptr;

		if (mIsServer)
		{
			if (mReader.getSequenceNumber() != mSequenceNumber)
			{
				if (mFirst)
				{
					mSequenceNumber = mReader.getSequenceNumber();
					mFirst = false;
					ret = this;
				}
			}
		}

		return ret;
	}

	// performs the select operation on this socket
	virtual void select(int32_t timeOut, size_t txBufSize) override final
	{
		// nothing to do here...
	}

	// Performs a general select on no specific socket 
	virtual void nullSelect(int32_t timeOut) override final
	{
		// nothing to do here
	}

	// Receive data from the socket connection.  
	// A return code of -1 means no data received.
	// A return code >0 is number of bytes received.
	virtual int32_t receive(void *dest, uint32_t maxLen) override final
	{
		int32_t ret = -1;

		uint32_t rcount = mReader.read(dest, maxLen);
		if (rcount > 0)
		{
			ret = int32_t(rcount);
		}

		return ret;
	}

	// Send this much data to the socket
	virtual int32_t send(const void *data, uint32_t dataLen) override final
	{
		int32_t ret = -1;

		uint32_t scount = mWriter.write(data, dataLen);
		if (scount > 0)
		{
			ret = int32_t(scount);
		}

		return ret;
	}

	// Close the socket
	virtual void	close(void) override final
	{
		// don't do anything in particular yet...
	}

	// Returns true if the socket send 'would block'
	virtual bool	wouldBlock(void) override final
	{
		bool ret = true;

		return ret;
	}

	// Returns true if a socket send is currently 'in progress'
	virtual bool	inProgress(void) override final
	{
		bool ret = false;

		return ret;
	}

	// Not sure what this is, but it's in the original code so making it available now.
	virtual void disableNaglesAlgorithm(void) override final
	{
		// nothing to do
	}

	// Close the socket and release this class
	virtual void release(void) override final
	{
		delete this;
	}

	bool isValid(void) const
	{
		bool ret = false;
		if (mServerFile && mClientFile)
		{
			ret = true;
		}
		return ret;
	}

	bool					mFirst{ true };
	uint32_t				mSequenceNumber{ 0 };
	bool					mIsServer{ false };
	spsc::SPSC				mReader;
	spsc::SPSC				mWriter;
	bool					mOwnClientServerFiles{ true };
	memorymap::MemoryMap	*mServerFile{ nullptr };
	memorymap::MemoryMap	*mClientFile{ nullptr };
};

Wsocket *createSocketSharedMemory(const char *hostName,int32_t port)
{
	auto ret = new WsocketSharedMemory(hostName, port);
	if (!ret->isValid())
	{
		delete ret;
		ret = nullptr;
	}
	return static_cast<Wsocket *>(ret);
}

}
