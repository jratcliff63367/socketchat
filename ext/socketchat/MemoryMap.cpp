#include "MemoryMap.h"

#ifdef _MSC_VER
#include <stdio.h>
#include <malloc.h>
#include <Windows.h>
#else
#include <errno.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <dirent.h>
#endif

namespace memorymap
{

#ifdef _MSC_VER
	class MemoryMapImpl :public MemoryMap

	{
	public:
		MemoryMapImpl(const char *mappingObject, uint64_t &size, bool createOk, bool readOnly)
		{
			mData = nullptr;
			mMapFile = nullptr;
			mMapHandle = nullptr;
			bool createFile = true;
			bool fileOk = false;
			if ( !createOk )
			{
				HANDLE h = CreateFileA(mappingObject, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (h != INVALID_HANDLE_VALUE)
				{
					size = getFileSize(h);
					fileOk = true;
					CloseHandle(h);
				}
			}
			if (createFile && createOk)
			{
				HANDLE _h = CreateFileA(mappingObject, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (_h != INVALID_HANDLE_VALUE)
				{
					#define ONEMB (1024*1024)
					char *temp = (char *)malloc(ONEMB);
					if (temp)
					{
						memset(temp, 0, ONEMB);
						uint64_t writeSize = size;
						while (writeSize > 0)
						{
							DWORD w = writeSize >= ONEMB ? ONEMB : (DWORD)writeSize;
							DWORD bytesWritten = 0;
							WriteFile(_h, temp, w, &bytesWritten, nullptr);
							if (bytesWritten != w)
							{
								break;
							}
							writeSize -= w;
						}
						if (writeSize == 0)
						{
							fileOk = true;
						}
					}
					CloseHandle(_h);
				}
			}
			if (fileOk)
			{
				mMapSize = size;
				DWORD flags = GENERIC_READ;
				if (!readOnly)
				{
					flags |= GENERIC_WRITE;
				}
				mMapFile = CreateFileA(mappingObject, flags, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (mMapFile != INVALID_HANDLE_VALUE)
				{
					mMapHandle = CreateFileMappingA(mMapFile, nullptr, readOnly ? PAGE_READONLY : PAGE_READWRITE, 0, 0, nullptr);
					if (mMapHandle == INVALID_HANDLE_VALUE)
					{
						CloseHandle(mMapFile);
						mMapFile = nullptr;
					}
					else
					{
						mData = MapViewOfFile(mMapHandle, readOnly ? FILE_MAP_READ : FILE_MAP_WRITE, 0, 0, 0);
						if (mData == nullptr)
						{
							CloseHandle(mMapHandle);
							CloseHandle(mMapFile);
							mMapHandle = nullptr;
							mMapFile = nullptr;
						}
					}
				}
				else
				{
					mMapFile = nullptr;
				}
			}
		}

		virtual ~MemoryMapImpl(void)
		{
			if (mMapHandle != INVALID_HANDLE_VALUE)
			{
				CloseHandle(mMapHandle);
			}
			if (mMapFile != INVALID_HANDLE_VALUE)
			{
				CloseHandle(mMapFile);
			}
		}

		virtual void *getBaseAddress(void) override final
		{
			return mData;
		}

		virtual void release(void) override final
		{
			delete this;
		}

		virtual uint64_t getFileSize(void) override final
		{
			return mMapSize;
		}

		uint64_t getFileSize(HANDLE h)
		{
			DWORD highSize = 0;
			DWORD lowSize = GetFileSize(h, &highSize);
			uint64_t ret;
			DWORD *d = (DWORD *)&ret;
			d[0] = lowSize;
			d[1] = highSize;
			return ret;
		}

		uint64_t	mMapSize{ 0 };
		HANDLE	    mMapFile{ INVALID_HANDLE_VALUE };
		HANDLE      mMapHandle{ INVALID_HANDLE_VALUE };
		void	    *mData{ nullptr };
	};
#else

	class MemoryMapImpl :public MemoryMap
	{
	public:
		MemoryMapImpl(const char *mappingObject, uint64_t &size, bool createOk, bool readOnly)
		{
			mFileNumber = 0;
			mData = nullptr;
			mMapLength = 0;
			mFileNumber = open(mappingObject, O_RDONLY);
			if (mFileNumber != -1)
			{
				mMapLength = lseek(mFileNumber, 0L, SEEK_END);
				if (mMapLength)
				{
					mData = mmap(0, mMapLength, PROT_READ, MAP_SHARED, mFileNumber, 0);
				}
				if (mData == MAP_FAILED)
				{
					close(mFileNumber);
					mFileNumber = 0;
					mMapLength = 0;
					mData = nullptr;
				}
				else
				{
					size = mMapLength;
				}
			}
		}

		virtual ~MemoryMapImpl(void)
		{
			if (mFileNumber)
			{
				close(mFileNumber);
			}
		}

		virtual uint64_t getFileSize(void) override final
		{
			return mMapLength;
		}

		virtual void *getBaseAddress(void) override final
		{
			return mData;
		}

		virtual void release(void) final
		{
			delete this;
		}

		int32_t     mFileNumber;
		size_t      mMapLength;
		void        *mData;
	};
#endif



MemoryMap * MemoryMap::createMemoryMap(const char *fileName, uint64_t &size, bool createOk, bool readOnly)
{
	MemoryMapImpl *m = new MemoryMapImpl(fileName, size, createOk, readOnly);
	if (m->getBaseAddress() == nullptr)
	{
		m->release();
		m = nullptr;
	}
	return static_cast<MemoryMap *>(m);
}

}
