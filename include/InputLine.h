#pragma once

#include <stdint.h>

namespace inputline
{

class InputLine
{
public:
	static InputLine *create(void);
	virtual const char *getInputLine(void) = 0;
	virtual void release(void) = 0;
protected:
	virtual ~InputLine(void)
	{

	}
};

}


