#include "include/amex.h"
#include "util.h"

#define ALLOCATE_OBJ(vm, type, obj_type)				\
	(type*)allocate_object(vm, sizeof(type), obj_type)

static GCObject *allocate_object(VM *vm, size_t size, ObjType type)
{
	GCObject *object = (struct GCObject *)reallocate(NULL, 0, size);

	object->next = vm->objects;
	object->type = type;
	object->is_marked = false;
	vm->objects = object;

#ifdef DEBUG_LOG_GC
	printf("%p allocate %zu for %d\n", (void *)object, size, type);
#endif /* DEBUG_LOG_GC */

	return object;
}

static String *allocate_string(VM *vm, char *chars,
			       uint32_t length, uint32_t hash)
{
	String *s = ALLOCATE_OBJ(vm, String, OBJ_STRING);
	s->length = length;
	s->chars = chars;
	s->hash = hash;
	/* intern string if not in table */
	table_set(&vm->strings, STRING_VAL(s), NIL_VAL);
	return s;
}

String *copy_string(VM *vm, const char *chars, uint32_t length)
{
	uint32_t hash;
	String *interned;

	hash = hash_cstring(chars, length);
	interned = table_find_string(&vm->strings, chars, length, hash);

	if (interned != NULL) {
		return interned;
	}

	char *heap_chars = ALLOCATE(char, length + 1);
	memcpy(heap_chars, chars, length);
	heap_chars[length] = '\0';

	return allocate_string(vm, heap_chars, length, hash);
}

Buffer *new_buffer(VM *vm, uint32_t capacity)
{
	Buffer *buf = ALLOCATE_OBJ(vm, Buffer, OBJ_BUFFER);
	buf->length = 0;
	buf->capacity = capacity;
	buf->data = ALLOCATE(char, capacity);
	return buf;
}

Array *new_array(VM *vm, uint32_t capacity)
{
	Array *array = ALLOCATE_OBJ(vm, Array, OBJ_ARRAY);
	array->count = 0;
	array->capacity = capacity;
	array->values = ALLOCATE(Value, capacity);
	return array;
}

Table *new_table(VM *vm,uint32_t capacity)
{
	Table *table = ALLOCATE_OBJ(vm, Table, OBJ_TABLE);
	table->count = 0;
	table->capacity = capacity;
	table->entries = ALLOCATE(Entry, capacity);
	for (int i = 0; i < capacity; ++i) {
		table->entries[i].key = NIL_VAL;
		table->entries[i].value = NIL_VAL;
	}
	return table;
}

Function *new_function(VM *vm)
{
	Function *f = ALLOCATE_OBJ(vm, Function, OBJ_FUNCTION);
	f->name = NULL;
	f->arity = 0;
	f->upval_count = 0;
	init_chunk(&f->chunk);
	return f;
}

Upvalue *new_upvalue(VM *vm, Value *slot)
{
	Upvalue *upvalue = ALLOCATE_OBJ(vm, Upvalue, OBJ_UPVALUE);
	upvalue->location = slot;
	upvalue->closed = NIL_VAL;
	upvalue->next = NULL;
	return upvalue;
}

Closure *new_closure(VM *vm, Function *function)
{
	Upvalue **upvalues = ALLOCATE(Upvalue*,
				      function->upval_count);
	for (int i = 0; i < function->upval_count; ++i)
		upvalues[i] = NULL;
	Closure *closure = ALLOCATE_OBJ(vm, Closure, OBJ_CLOSURE);
	closure->function = function;
	closure->upvalues = upvalues;
	closure->upvalue_count = function->upval_count;
	return closure;
}
