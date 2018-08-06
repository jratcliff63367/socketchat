#include "InputLine.h"
#include "wplatform.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MAX_INPUT 512

namespace inputline
{

class InputLineImpl : public InputLine
{
public:
	InputLineImpl(void)
	{
		mThread = new std::thread([this]()
		{
			while (!mExit)
			{
				// Not allowed to start inputting a new line of data
				if (!mHaveNewData)
				{
					mInputBuffer[0] = 0;
					char *ret = fgets(mInputBuffer, sizeof(mInputBuffer), stdin);
					if (ret && strlen(mInputBuffer))
					{
						mHaveNewData = true;
					}
				}
				else
				{
					wplatform::sleepNano(1000);
				}
			}
		});
	}

	virtual ~InputLineImpl(void)
	{
		if (mThread)
		{
			mExit = true;
			mThread->detach();
			delete mThread;
		}
	}

	virtual const char *getInputLine(void) override final
	{
		const char *ret = nullptr;
		if (mHaveNewData)
		{
			lock();
			strcpy(mResultBuffer, mInputBuffer);
			size_t len = strlen(mResultBuffer);
			if (len && mResultBuffer[len - 1] == 0x0A)
			{
				mResultBuffer[len - 1] = 0;
			}
			mHaveNewData = false;
			unlock();
			ret = mResultBuffer;
		}

		return ret;
	}

	virtual void release(void) override final
	{
		delete this;
	}


	void lock(void)
	{
		mMutex.lock();
	}

	void unlock(void)
	{
		mMutex.unlock();
	}

protected:
	char				mInputBuffer[MAX_INPUT];
	char				mResultBuffer[MAX_INPUT];
	std::thread			*mThread{ nullptr };	// Worker thread to get console input
	std::mutex			mMutex;
	std::atomic< bool >	mExit{ false };
	std::atomic< bool > mHaveNewData{ false };
};

InputLine *InputLine::create(void)
{
	auto ret = new InputLineImpl;
	return static_cast<InputLine *>(ret);
}


}


