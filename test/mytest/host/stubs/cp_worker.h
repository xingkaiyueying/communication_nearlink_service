#pragma once
#include <stdint.h>
uint32_t CP_PostTaskBlocked(void (*callback)(void *), void *arg, void (*freeCallback)(void *), int timeout);
