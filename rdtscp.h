#ifndef _RDTSCP_H_
#define _RDTSCP_H_

#include <stdint.h>

static inline uint64_t
rdtscp(void)
{
	uint32_t eax, edx;

	__asm__ __volatile__("rdtscp"
		: "=a" (eax), "=d" (edx)
		:
		: "%ecx", "memory");

	return (((uint64_t)edx << 32) | eax);
}

#endif
