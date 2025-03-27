#include "include/amex.h"

uint32_t hash_number(Number n);

uint32_t hash_cstring(const char *key, uint32_t length);

const void *tab_binary_search(const void *tab, size_t tabcount,
			      size_t itemsize, const char *key);
