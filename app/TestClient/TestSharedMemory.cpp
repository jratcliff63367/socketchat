#include "TestSharedMemory.h"
#include "SPSC.h"
#include "Timer.h"
#include "MemoryMap.h"
#include <thread>
#include <atomic>
#include <mutex>

const char *gSampleStrings[10] =
{
	"one",
	"two",
	"three",
	"four",
	"five",
	"six",
	"seven",
	"eight",
	"nine",
	"ten"
};

#define TEST_BUFFER_SIZE (256)
#define TEST_TIME 60

class TestSharedMemory
{
public:
	TestSharedMemory(void)
	{
		uint64_t fsize = TEST_BUFFER_SIZE;
		mMemoryMap = memorymap::MemoryMap::createMemoryMap("sharedmemorytest.bin", fsize, true, false);
		if (mMemoryMap)
		{
			mWriter.init(mMemoryMap->getBaseAddress(), uint32_t(mMemoryMap->getFileSize()), true,true);	// writer must always be initialized first!
			mReader.init(mMemoryMap->getBaseAddress(), uint32_t(mMemoryMap->getFileSize()), false,true);
			mThread = new std::thread([this]()
			{
				while (!mExit)
				{
					writeString();
				}
			});
		}
	}
	~TestSharedMemory(void)
	{
		if (mThread)
		{
			mExit = true;
			mThread->join();
			delete mThread;
		}
		if (mMemoryMap)
		{
			mMemoryMap->release();
		}
	}

	void writeString(void)
	{
		// randomly write a string
		uint32_t r = rand() % 10;
		const char *str = gSampleStrings[r];
		uint32_t slen = uint32_t(strlen(str));
		// Don't write the string unless we have room to write the *whole* thing
		if (mWriter.capacity() >= (slen + 2))
		{
			mWriter.write(str, slen);
			mWriter.write("\r\n", 2);
			mIndex++;
		}
	}

	void run(void)
	{
		if (!mMemoryMap) return;
		timer::Timer t;
#if 0
		for (uint32_t i = 0; i < 100; i++)
		{
			writeString();
		}
#endif
		while ( t.peekElapsedSeconds() < TEST_TIME )
		{
			uint32_t readLen = rand() % 60 + 1;
			char temp[64];
			uint32_t r = mReader.read(temp,readLen);	// Read some number of bytes; select a random read buffer size
			if (r > 0)
			{
				// Print the characters that just got read in
				for (uint32_t j = 0; j < r; j++)
				{
					printf("%c", temp[j]);
				}
			}
		}
		mExit = true;
	}

	uint32_t				mIndex{ 0 };
	spsc::SPSC				mReader;
	spsc::SPSC				mWriter;
	std::thread				*mThread{ nullptr };	// Worker thread to get console input
	std::atomic< bool >		mExit{ false };
	memorymap::MemoryMap	*mMemoryMap{ nullptr };
};

void testSharedMemory(void)
{
	TestSharedMemory tsm;
	tsm.run();
}