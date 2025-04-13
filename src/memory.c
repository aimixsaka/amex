#include "include/amex.h"
#include "debug.h"

void *reallocate(VM *vm, void *pointer, size_t old_size, size_t new_size)
{
	/*
	 * increase bytes_allocated when allocate new memory,
	 * decrease bytes_allocated when free memory.
	 */
	vm->bytes_allocated += new_size - old_size;
	/*
	 * Only start GC when before allocating, other than freeing memory,
	 * as collect_garbage it self use reallocate to free memory.
	 */
        if (new_size > old_size) {
#ifdef DEBUG_STRESS_GC
		collect_garbage(vm);
#else
		if (vm->bytes_allocated > vm->next_GC) {
			collect_garbage(vm);
		}
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

void free_object(VM *vm, GCObject *object)
{
#ifdef DEBUG_LOG_GC
	printf("free %p, type %s: ", object, obj_type_string(object));
	print_object(object, "\n");
#endif /* DEBUG_LOG_GC */

	switch (object->type) {
	case OBJ_BUFFER: {
		Buffer *buffer = (Buffer*)object;
		FREE_ARRAY(vm, char, buffer->data, buffer->capacity);
		FREE(vm, Buffer, object);
		break;
	}
	case OBJ_STRING: {
		String *s = (String*)object;
		FREE_ARRAY(vm, char, s->chars, s->length + 1);
		FREE(vm, String, object);
		break;
	}
	case OBJ_ARRAY: {
		free_array(vm, (Array*)object);
		FREE(vm, Array, object);
		break;
	}
	case OBJ_TABLE: {
		free_table(vm, (Table*)object);
		FREE(vm, Table, object);
		break;
	}
	case OBJ_FUNCTION: {
		Function *function = (Function*)object;
		free_chunk(vm, &function->chunk);
		FREE(vm, Function, object);
		break;
	}
	case OBJ_UPVALUE:
		FREE(vm, Upvalue, object);
		break;
	case OBJ_CLOSURE: {
		Closure *closure = (Closure*)object;
		FREE_ARRAY(vm, Upvalue*, closure->upvalues,
			   closure->upvalue_count);
		FREE(vm, Closure, object);
		break;
	}
	default:
		fprintf(stderr, "free_object: unimplemented for type: %d!", object->type);
		break;
	}
}

void free_objects(VM *vm)
{
	GCObject *object, *next;

	object = vm->objects;
	while (object != NULL) {
		next = object->next;
		free_object(vm, object);
		object = next;
	}

	free(vm->gray_stack);
}
