#include "value.h"
#include "str.h"
#include "buffer.h"
#include "memory.h"

String *buf_to_str(Buffer *buf)
{
	return copy_string(buf->data, buf->length);
}

void buf_push(Buffer *buf, const char c)
{
	int length = buf->length, old_capacity = buf->capacity;
	if (old_capacity < length + 1) {
		buf->capacity = GROW_CAPACITY(old_capacity);
		buf->data = GROW_ARRAY(char, buf->data, old_capacity, buf->capacity);
	}
	buf->data[buf->length++] = c;
}
