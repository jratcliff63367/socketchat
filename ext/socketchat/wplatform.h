#pragma once

#include <stdint.h>
#include <stdlib.h>

// Platform specific support functions.
// Currently just a string formatting function

namespace wplatform
{

	bool getExePath(char *pathName, uint32_t maxPathSize);

	int32_t  stringFormat(char* dst, size_t dstSize, const char* format, ...);

	// uses the high resolution timer to approximate a random number.
	uint64_t getRandomTime(void);

	void sleepNano(uint64_t nanoSeconds);

}
