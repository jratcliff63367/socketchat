#pragma once

#include <stdint.h>

namespace wsocket
{

class Wsocket;

Wsocket *createSocketSharedMemory(const char *hostName, int32_t port);

}
