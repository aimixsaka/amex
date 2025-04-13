#include "include/amex.h"

String *buf_to_str(VM *vm, Buffer *buf)
{
	/* HACK: GC GUARD */
	((GCObject*)buf)->is_marked = true;
	
	String *res = copy_string(vm, buf->data, buf->length);

	((GCObject*)buf)->is_marked = false;
	return res;
}

void buf_push(VM *vm, Buffer *buf, const char c)
{
	int length = buf->length, old_capacity = buf->capacity;
	if (old_capacity < length + 1) {
		/* HACK: GC GUARD */
		((GCObject*)buf)->is_marked = true;
		
		buf->capacity = GROW_CAPACITY(old_capacity);
		buf->data = GROW_ARRAY(vm, char, buf->data,
				old_capacity, buf->capacity);
		
		((GCObject*)buf)->is_marked = false;
	}
	buf->data[buf->length++] = c;
}
