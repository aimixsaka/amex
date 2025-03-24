#include <stdint.h>

#include "object.h"
#include "value.h"

uint32_t hash_number(Number n);

uint32_t hash_cstring(const char *key, uint32_t length);
