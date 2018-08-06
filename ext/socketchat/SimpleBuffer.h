#pragma once

#include <stdint.h>

namespace simplebuffer
{

class SimpleBuffer
{
public:

	static SimpleBuffer *create(uint32_t defaultSize,uint32_t maxGrowSize);

	// Conume this many bytes of the current buffer; retaining whatever is left
	virtual void consume(uint32_t removeLen) = 0;

	virtual uint32_t getSize(void) const = 0;

    // Shrinks the current buffer back down to the default size, or current size whichever is greater
    // Returns the new buffer size after shrinking.
    // The only purpose of this is say the buffer grow extremely huge, but you want to 'garbage collect' the buffer
    virtual uint32_t shrinkBuffer(void) = 0;

    // The current maximum size of the buffer
	virtual uint32_t getMaxBufferSize(void) const = 0;

    // Return the maximum size this buffer is ever allowed to grow, beyond which is fails to add new data
    virtual uint32_t getMaxGrowSize(void) const = 0;

	// Get the current data buffer.  It can be modified..but...you cannot go beyond
	// the current length
	virtual uint8_t *getData(uint32_t &dataLen) const = 0;

	// Clear the contents of the buffer (simply resets the length back to zero)
	virtual void 		clear(void) = 0; 	// clear the buffer

	// Add this data to the current buffer.  If 'data' is null, it doesn't copy any data
	virtual bool 		addBuffer(const void *data,uint32_t dataLen) = 0;

	// Make sure the buffer is large enough for this capacity; return the *current* read location in the buffer
	virtual	uint8_t	*confirmCapacity(uint32_t capacity) = 0;

	// Note, the reset command does not retain the previous data buffer!
	virtual void		reset(uint32_t defaultSize) = 0;

	// Release the SimpleBuffer instance
	virtual void		release(void) = 0;

protected:
	virtual ~SimpleBuffer(void)
	{
	}

};

}
