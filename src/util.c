#include <stdint.h>

#include "value.h"
#include "str.h"
#include "util.h"

uint32_t hash_cstring(const char *key, uint32_t length)
{
	uint32_t hash = 2166136261u;
	for (int i = 0; i < length; i++) {
		hash ^= (uint8_t)key[i];
		hash *= 16777619;
	}
	return hash;
}


uint32_t hash_number(Number n)
{
	return ((uint32_t)n) < 0 ? -n : n;
}

const void *str_binary_search(

		)
{

}
