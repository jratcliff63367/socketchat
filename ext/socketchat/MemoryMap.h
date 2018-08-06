#pragma once

#include <stdint.h>

namespace memorymap
{
    class MemoryMap
    {
    public:
    	static MemoryMap * createMemoryMap(const char *fileName, uint64_t &size, bool createOk,bool readOnly);
        virtual uint64_t getFileSize(void) = 0;
        virtual void *getBaseAddress(void) = 0;
        virtual void release(void) = 0;
    protected:
        virtual ~MemoryMap(void)
        {
        }
    };
}

