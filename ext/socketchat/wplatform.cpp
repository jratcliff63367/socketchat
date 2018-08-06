#include "wplatform.h"
#include <stdio.h>
#include <stdarg.h>
#include <chrono>
#include <thread>

#ifdef _MSC_VER
#include <windows.h>
#include <direct.h>
#else
#include <unistd.h>
#include <dirent.h>
#include <signal.h>
#endif

namespace wplatform
{

	bool getExePath(char *pathName, uint32_t maxPathSize)
	{
		bool ret = false;

#ifdef _MSC_VER
		GetModuleFileNameA(nullptr, pathName, maxPathSize);
		ret = true;
#else
		ssize_t len = readlink("/proc/self/exe", pathName, maxPathSize - 1);
		if (len > 0)
		{
			pathName[len] = '\0';
			ret = true;
		}
		else
		{
			ret = false;
		}
#endif

		return ret;
	}


	int32_t stringFormatV(char* dst, size_t dstSize, const char* src, va_list arg)
	{
		return ::vsnprintf(dst, dstSize, src, arg);
	}

	int32_t  stringFormat(char* dst, size_t dstSize, const char* format, ...)
	{
		va_list arg;
		va_start(arg, format);
		int32_t r = stringFormatV(dst, dstSize, format, arg);
		va_end(arg);
		return r;
	}

	// uses the high resolution timer to approximate a random number.
	uint64_t getRandomTime(void)
	{
		uint64_t seed = std::chrono::high_resolution_clock::now().time_since_epoch().count();
		return seed;
	}

	void sleepNano(uint64_t nanoSeconds)
	{
		std::this_thread::sleep_for(std::chrono::nanoseconds(nanoSeconds)); // s
	}

}