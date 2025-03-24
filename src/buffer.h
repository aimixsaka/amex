#ifndef AMEX_BUFFER_H
#define AMEX_BUFFER_H

#include "object.h"
#include "value.h"

/*
 * Mainly used for parser, to save temp data,
 * then transform to String.
 */
struct Buffer {
	GCObject		gc;
	uint32_t		length;
	uint32_t		capacity;
	char			*data;
};


Buffer *new_buffer(uint32_t capacity);
String *buf_to_str(Buffer *buf);
void buf_push(Buffer *buf, const char c);
void free_buffer();

#endif /* AMEX_BUFFER_H */
