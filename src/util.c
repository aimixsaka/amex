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

/*
 * search a string in a k-v pair tab,
 * return corresponding value.
 *
 * From https://github.com/janet-lang/janet/blob/73334f34857b0124546ce79b4bda094a4b18a019/src/core/util.c#L363
 */
const void *tab_binary_search(const void *tab, size_t tabcount,
			      size_t itemsize, const char *key)
{
	size_t low = 0;
	size_t high = tabcount;
	const char *t = (const char *)tab;
	while (low < high) {
		size_t mid = low + ((high - low) / 2);
		const char **item = (const char **)(t + mid * itemsize);
		const char *name = *item;
		int res = strcmp(key, name);
		if (res < 0)
			high = mid;
		else if (res > 0)
			low = mid + 1;
		else
			return (const void *)item;
	}
	return NULL;
}
