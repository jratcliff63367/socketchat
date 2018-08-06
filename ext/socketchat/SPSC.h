#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <atomic>

// Implements a single producer single consumer data transfer class
// Lock-free thread safe communications between two threads and/or processes using shared memory
// One process/thread can write to the shared buffer
// One process/thread can read from the shared buffer
namespace spsc
{

const uint32_t cSharedMemoryVersion=100;

class SPSC
{
public:
	struct SharedMemoryHeader
	{
		std::atomic<uint32_t>	mVersionNumber{cSharedMemoryVersion};		// Version number
		std::atomic<uint32_t>	mBufferSize{0};								// Size of the shared memory buffer (including header)
		std::atomic<uint32_t>	mReadIndex{0};								// Current read index
		std::atomic<uint32_t>	mWriteIndex{0};								// Current write index
		std::atomic<uint32_t>	mSequenceNumber{ 0 };						// a sequence number that can be used for general purposes
		std::atomic<uint32_t>	mUnused1{ 0 };
		std::atomic<uint32_t>	mUnused2{ 0 };
		std::atomic<uint32_t>	mUnused3{ 0 };
	};

	bool init(void *sharedMemory,uint32_t maxLen,bool isWriter,bool isServer)
	{
		bool ret = true; // default return code
		mIsWriter = isWriter;
		if (maxLen > sizeof(SharedMemoryHeader))
		{
			mSharedMemory = (uint8_t *)sharedMemory;
			mBaseMemory = mSharedMemory + sizeof(SharedMemoryHeader);
			mHeader = (SharedMemoryHeader *)mSharedMemory;
			mCapacity = maxLen - sizeof(SharedMemoryHeader);

			if (isServer)
			{
				mHeader->mVersionNumber = cSharedMemoryVersion;
				mHeader->mBufferSize = maxLen;
				mHeader->mWriteIndex.store(0, std::memory_order_relaxed);
				mHeader->mReadIndex.store(0, std::memory_order_relaxed);
				mHeader->mSequenceNumber.store(0, std::memory_order_relaxed);
			}
			else
			{
				if (mHeader->mVersionNumber != cSharedMemoryVersion || mHeader->mBufferSize != maxLen)
				{
					mSharedMemory = nullptr;
					mBaseMemory = nullptr;
					mHeader = nullptr;
					ret = false;
				}
			}
		}
		else
		{
			ret = false;
		}
		return ret;
	}

	uint32_t read(void *dest, uint32_t maxLen)
	{
		if (mIsWriter) return 0; // writers cannot read!
		uint32_t len = size();
		if (len > maxLen)
		{
			len = maxLen;
		}
		if (len == 0)
		{
			return 0;
		}
		uint32_t readIndex = mHeader->mReadIndex.load(std::memory_order_acquire);
		uint32_t availTop = mCapacity - readIndex;
		if (availTop >= len)
		{
			memcpy(dest, &mBaseMemory[readIndex], len);
			mHeader->mReadIndex.store(readIndex + len, std::memory_order_relaxed);
		}
		else
		{
			uint32_t remainder = len - availTop;
			uint8_t *cdest = (uint8_t *)dest;
			if (availTop)
			{
				memcpy(cdest, &mBaseMemory[readIndex], availTop);
				cdest += availTop;
			}
			if (remainder)
			{
				memcpy(cdest, mBaseMemory, remainder);
			}
			mHeader->mReadIndex.store(remainder, std::memory_order_relaxed);
		}
		return len; // return number of bytes read
	}


	// Returns number of bytes written
	uint32_t write(const void *data, uint32_t dataLen)
	{
		if (!mIsWriter) return 0; // can't write if we are not a writer!
		// Find out how much room is available left to write to
		uint32_t avail = capacity();
		if (avail == 0 )
		{
			return 0; // if the buffer is completely full return
		}
		avail--; // don't write the last byte!!

		if (avail < dataLen)	// if there is less room available than bytes we want to send
		{
			dataLen = avail;	// We only copy as much data as we have room available
		}
		// Find out how much is available at the top of the write buffer
		// The total capacity minus the current write index
		uint32_t writeIndex = mHeader->mWriteIndex.load(std::memory_order_acquire);
		uint32_t availTop = mCapacity - writeIndex;
		if (dataLen <= availTop)	// If there is enough room; we can just do a single contiguous copy
		{
			memcpy(&mBaseMemory[writeIndex], data, dataLen);	// Copy the data
			mHeader->mWriteIndex.store(writeIndex + dataLen, std::memory_order_relaxed); // advance the write pointer
		}
		else
		{
			// Compute how many bytes will be left to copy after we copy the top portion
			uint32_t remainder = dataLen - availTop;
			const uint8_t *scan = (const uint8_t *)data;
			if (availTop)
			{
				// Copy up to the top of the buffer
				memcpy(&mBaseMemory[writeIndex], scan, availTop);
				scan += availTop;
			}
			if (remainder)
			{
				memcpy(mBaseMemory, scan, remainder);
			}
			// Now that the data has been written, write the new write pointer
			mHeader->mWriteIndex.store(remainder,std::memory_order_relaxed);
		}
		return dataLen;
	}

	// Size of write buffer
	inline uint32_t calcSize(uint32_t readIndex,uint32_t writeIndex) const
	{
		if (writeIndex >= readIndex)
			return writeIndex - readIndex;
		return writeIndex + mCapacity - readIndex;
	}

	inline uint32_t size(void) const
	{
		uint32_t writeIndex = mHeader->mWriteIndex.load(std::memory_order_acquire);
		uint32_t readIndex = mHeader->mReadIndex.load(std::memory_order_relaxed);
		return calcSize(readIndex, writeIndex);
	}

	inline uint32_t capacity(void) const
	{
		uint32_t avail = mCapacity - size(); // find out how much room is available.
		if (avail <= 1) // We don't allow writing to the last byte to avoid confusion between the full vs. empty condition
		{
			return 0; // if the buffer is completely full return
		}
		avail--; // don't write the last byte!!
		return avail;
	}

	uint32_t incrementSequenceNumber(void)
	{
		uint32_t ret = 0;
		if (mHeader)
		{
			ret = mHeader->mSequenceNumber++;
		}
		return ret;
	}

	uint32_t getSequenceNumber(void) const
	{
		uint32_t ret = 0;
		if (mHeader)
		{
			ret = mHeader->mSequenceNumber;
		}
		return ret;
	}

private:
	SharedMemoryHeader	*mHeader{nullptr};      		// points to the head of the shared memory;
	uint8_t				*mSharedMemory{nullptr};		// Address of shared memory between processes (includes header)
	uint8_t				*mBaseMemory{nullptr};			// Base address of the read/write circular buffer (mSharedMemory+header)
	uint32_t			mCapacity{ 0 };					// The total capacity of the read/write buffer
	bool				mIsWriter{ true };				// Whether or not we are a writer instance (can only do writes)
};

}
