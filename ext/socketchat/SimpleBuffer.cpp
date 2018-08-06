#include "SimpleBuffer.h"
#include <stdlib.h>
#include <string.h>
#include <assert.h>

namespace simplebuffer
{

	class SimpleBufferImpl : public SimpleBuffer
	{
	public:
		SimpleBufferImpl(uint32_t defaultLen,uint32_t maxGrowSize) : mMaxGrowSize(maxGrowSize)
		{
            if (defaultLen > mMaxGrowSize)
            {
                mMaxGrowSize = defaultLen;
            }
			reset(defaultLen);
		}

		virtual ~SimpleBufferImpl(void)
		{
            free(mBuffer);
		}


		// Get the current data buffer.  It can be modified..but...you cannot go beyond
		// the current length
		virtual uint8_t *getData(uint32_t &dataLen) const override final
		{
            dataLen = getSize();
            return &mBuffer[mStartLoc];
		}

		// Clear the contents of the buffer (simply resets the length back to zero)
		virtual void 		clear(void) override final 	// clear the buffer
		{
            mStartLoc = mEndLoc = 0;
		}

		// Add this data to the current buffer.  If 'data' is null, it doesn't copy any data (assuming it was manually written already)
		// but does advance the buffer pointer
		virtual bool 		addBuffer(const void *data, uint32_t dataLen) override final
		{
            bool ret = false;

			uint32_t available = mMaxLen - mEndLoc; // how many bytes are currently available...
			// If we are trying to advance past the end of the available buffer space we need to grow the buffer
			if (dataLen > available) // if there isn't enough room, we need to grow the buffer
			{
                growBuffer(dataLen);
                available = mMaxLen - mEndLoc;
			}
			if (dataLen  <= available)
			{
				if (data)
				{
					memcpy(&mBuffer[mEndLoc], data, dataLen);
				}
				mEndLoc += dataLen;
                ret = true;
			}
            return ret;
		}

		// Note, the reset command does not retain the previous data buffer!
		virtual void		reset(uint32_t defaultSize) override final
		{
			if (mBuffer)
			{
				free(mBuffer);
				mBuffer = nullptr;
			}
            mStartLoc = 0;
            mEndLoc = 0;
            mDefaultSize = defaultSize;
			mMaxLen = defaultSize;
			if (mMaxLen)
			{
				mBuffer = (uint8_t *)malloc(defaultSize);
			}
		}

		// Release the SimpleBuffer instance
		virtual void		release(void) override final
		{
			delete this;
		}

		virtual uint32_t getSize(void) const override final
		{
            return mEndLoc - mStartLoc;
		}

        bool growBuffer(uint32_t dataLen)
        {
            bool ret = false;

            // See how many bytes are available at the beginning of the buffer.
            // If there are enough to hold the request, we don't grow the buffer, we just
            // adjust the content and reset the start read location
            uint32_t emptySize = mStartLoc;
            if (dataLen < emptySize)
            {
                uint32_t keepSize = getSize();
                if (keepSize)
                {
                    memcpy(mBuffer, &mBuffer[mStartLoc], keepSize);
                }
                mStartLoc = 0;              // Reset the current read location to zero
                mEndLoc = keepSize;         // The current end location is the active buffer size
                ret = true;                 // Return true that we grew enough to accommodate the request
            }
            else
            {
                // Double the size of buffer
                uint32_t newMaxLen = mMaxLen * 2;       // The current maximum size times 2
                uint32_t available = newMaxLen - mEndLoc; // Compute how many bytes available at the new size
                if (available < dataLen)    // If the available size won't accommodate the request, make sure that it can
                {
                    newMaxLen+= dataLen;   // In addition to doubling the buff make room for the requested data
                }
                if (newMaxLen > mMaxGrowSize)   // If this would grow beyond our maximum buffer size, give up
                {
                }
                else
                {
                    ret = true;             
                    mMaxLen = newMaxLen;
                    uint8_t *newBuffer = (uint8_t *)malloc(mMaxLen);
                    // If there is any old data to copy over
                    uint32_t bufferSize = getSize();
                    if (bufferSize)
                    {
                        memcpy(newBuffer, &mBuffer[mStartLoc], bufferSize);
                    }
                    free(mBuffer);      // Release the old buffer
                    mBuffer = newBuffer;    // This is the new active buffer
                    mStartLoc = 0;
                    mEndLoc = bufferSize;
                }
            }
            return ret;
        }

        // We advance the read pointer this many bytes
		virtual void consume(uint32_t removeLen) override final
		{
            uint32_t activeBufferSize = mEndLoc - mStartLoc;
            assert(removeLen <= activeBufferSize);
            if (removeLen > activeBufferSize)
            {
                removeLen = activeBufferSize;
            }
            mStartLoc += removeLen;
            if (mStartLoc == mEndLoc)
            {
                mStartLoc = 0;
                mEndLoc = 0;
            }
		}

		// Make sure the buffer is large enough for this capacity; return the *current* read location in the buffer
		virtual	uint8_t	*confirmCapacity(uint32_t capacity) override final
		{
            uint8_t *ret = nullptr;
			// Compute how many bytes are available in the current buffer
            uint32_t available = mMaxLen - mEndLoc;
			// If we are asking for more capacity than is available, we need to grow the buffer
			if (capacity > available )
			{
                growBuffer(capacity);
                available = mMaxLen - mEndLoc;
			}
            if (capacity <= available)
            {
                ret = &mBuffer[mEndLoc];
            }
            return ret;
		}

		virtual uint32_t getMaxBufferSize(void) const override final
		{
			return mMaxLen;
		}

        // Shrinks the current buffer back down to the default size, or current size whichever is greater
        virtual uint32_t shrinkBuffer(void) override final
        {
            uint32_t currentSize = getSize();
            uint32_t newSize = mDefaultSize;
            if (currentSize > newSize)
            {
                newSize = currentSize;
            }
            uint8_t *newBuffer = (uint8_t *)malloc(newSize);
            if (currentSize)
            {
                memcpy(newBuffer, &mBuffer[mStartLoc], currentSize);
            }
            mStartLoc = 0;
            mEndLoc = currentSize;
            mMaxLen = newSize;  // New buffer size
            return mMaxLen;
        }

        virtual uint32_t getMaxGrowSize(void) const override final
        {
            return mMaxGrowSize;
        }

	private:
		uint8_t		*mBuffer{ nullptr };
        uint32_t     mStartLoc{ 0 };        // Current start location of the buffer
        uint32_t     mEndLoc{ 0 };          // Current end location of the buffer
		uint32_t	mMaxLen{ 0 };           // Maximum size of the buffer
        uint32_t    mMaxGrowSize{ (1024 * 1024) * 64 }; // default maximum grow size is 64mb
        uint32_t    mDefaultSize{ 1024 };
	};

SimpleBuffer *SimpleBuffer::create(uint32_t defaultSize,uint32_t maxGrowSize)
{
	auto ret = new SimpleBufferImpl(defaultSize,maxGrowSize);
	return static_cast<SimpleBuffer *>(ret);
}


}


