#include <stdlib.h>

#include "chunk.h"
#include "value.h"
#include "vm.h"
#include "memory.h"
#include "object.h"
#include "str.h"
#include "buffer.h"
#include "array.h"
#include "table.h"
#include "compiler.h"

#ifdef DEBUG_LOG_GC
#include <stdio.h>
#include "debug.h"
#endif

void *reallocate(void *pointer, size_t old_size, size_t new_size)
{
	/*
	 * Only start GC when before allocating, other than freeing memory,
	 * as collect_garbage it self use reallocate to free memory.
	 */
	if (new_size > old_size) {
#ifdef DEBUG_STRESS_GC
		collect_garbage();
#endif /* DEBUG_LOG_GC */
	}

	if (new_size == 0) {
		free(pointer);
		return NULL;
	}

	void *result = realloc(pointer, new_size);
	if (result == NULL)
		exit(1);
	return result;
}

static void free_object(GCObject *object)
{
#ifdef DEBUG_LOG_GC
	printf("%p free type %d\n", (void *)object, object->type);
#endif /* DEBUG_LOG_GC */

	switch (object->type) {
	case OBJ_BUFFER: {
		Buffer *buffer = (Buffer*)object;
		FREE_ARRAY(char, buffer->data, buffer->capacity);
		FREE(Buffer, object);
		break;
	}
	case OBJ_STRING: {
		String *s = (String*)object;
		FREE_ARRAY(char, s->chars, s->length + 1);
		FREE(String, object);
		break;
	}
	case OBJ_ARRAY:
		FREE(Array, object);
		break;
	case OBJ_TABLE:
		FREE(Table, object);
		break;
	case OBJ_FUNCTION: {
		Function *function = (Function*)object;
		free_chunk(&function->chunk);
		FREE(Function, object);
		break;
	}
	case OBJ_UPVALUE:
		FREE(Upvalue, object);
		break;
	case OBJ_CLOSURE: {
		Closure *closure = (Closure*)object;
		FREE_ARRAY(Upvalue*, closure->upvalues,
			   closure->upvalue_count);
		FREE(Closure, object);
		break;
	}
	default:
		fprintf(stderr, "free_object: unimplemented!");
		break;
	}
}

void free_objects()
{
	GCObject *object, *next;

	object = vm.objects;
	while (object != NULL) {
		next = object->next;
		free_object(object);
		object = next;
	}
}
