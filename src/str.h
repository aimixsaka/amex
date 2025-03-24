#ifndef AMEX_STRING_H
#define AMEX_STRING_H

#include "object.h"

struct String {
	GCObject		gc;
	uint32_t		length;
	uint32_t		hash;
	char			*chars;
};

String *copy_string(const char *chars, uint32_t length);

#endif /* AMEX_STRING_H */
