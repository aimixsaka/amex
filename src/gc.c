#include "include/amex.h"
#include "debug.h"

void mark_object(VM *vm, GCObject *obj)
{
	if (!obj)
		return;
	/*
	 * NOTE:
	 * relation between objects can create circle,
	 * we should not mark "already marked object", to
	 * prevent infinite mark loop
	*/
	if (obj->is_marked)
		return;
#ifdef DEBUG_LOG_GC
	printf("mark %p, type %s: ", (void*)obj, obj_type_string(obj));
	print_object(obj, "\n");
#endif /* DEBUG_LOG_GC */
	obj->is_marked = true;
	if (vm->gray_capacity < vm->gray_count + 1) {
		vm->gray_capacity = GROW_CAPACITY(vm->gray_capacity);
		/* use raw realloc here to prevent GC self recurstion */
		vm->gray_stack = (GCObject**)realloc(
			vm->gray_stack,
			sizeof(GCObject*) * vm->gray_capacity);
		if (vm->gray_stack == NULL)
			exit(3);
	}
	
	vm->gray_stack[vm->gray_count++] = obj;
}

static void mark_value(VM *vm, Value *v)
{
	if (value_is_object(v)) {
		mark_object(vm, value_to_obj(v));
	}
}

static void mark_table(VM *vm, Table *t)
{
	for (int i = 0; i < t->capacity; i++) {
		Entry *entry = &t->entries[i];
		mark_value(vm, &entry->key);
		mark_value(vm, &entry->value);
	}
}

static void mark_roots(VM *vm)
{
	/** Runtime Mark **/
	/* mark vm stack valeus */
	for (Value *slot = vm->stack.values;
	     slot < vm->stack.stack_top;
	     slot++) {
		mark_value(vm, slot);
	}
	/* mark closures in call frame array */
	for (int i = 0; i < vm->frame_count; i++)
		mark_object(vm, (GCObject*)vm->frames[i].closure);
	for (Upvalue *upvalue = vm->open_upvalues;
	     upvalue != NULL;
	     upvalue = upvalue->next)
		mark_object(vm, (GCObject*)upvalue);
	mark_object(vm, (GCObject*)vm->globals);
	if (vm->globals)
		mark_table(vm, vm->globals);
	
	/** Compile Time Mark **/
	mark_compiler_roots(vm);
}

static void mark_array(VM *vm, Array *arr)
{
	for (int i = 0; i < arr->count; i++)
		mark_value(vm, &arr->values[i]);
}

static void blacken_object(VM *vm, GCObject *obj) {
#ifdef DEBUG_LOG_GC
	printf("blacken %p, type %s: ", (void*)obj, obj_type_string(obj));
	print_object(obj, "\n");
#endif /* DEBUG_LOG_GC */
	switch (obj->type) {
	case OBJ_ARRAY:
		mark_array(vm, (Array*)obj);
		break;
	case OBJ_TABLE:
		mark_table(vm, (Table*)obj);
		break;
	case OBJ_UPVALUE:
		mark_value(vm, &((Upvalue*)obj)->closed);
		break;
	case OBJ_FUNCTION: {
		Function *f = (Function*)obj;
		mark_object(vm, (GCObject*)f->name);
		mark_array(vm, &f->chunk.constants);
		break;
	}
	case OBJ_CLOSURE: {
		Closure *closure = (Closure*)obj;
		mark_object(vm, (GCObject*)closure->function);
		for (int i = 0; i < closure->upvalue_count; i++)
			mark_object(vm, (GCObject*)closure->upvalues[i]);
		break;
	}
	case OBJ_NATIVE_FN:
	case OBJ_STRING:
	case OBJ_BUFFER:
		break;
	}
}

static void trace_references(VM *vm)
{
	while (vm->gray_count > 0) {
		GCObject *obj = vm->gray_stack[--vm->gray_count];
		blacken_object(vm, obj);
	}
}

static void sweep(VM *vm)
{
	GCObject *previous = NULL;
	GCObject *obj = vm->objects;
	while (obj != NULL) {
		if (obj->is_marked) {
			obj->is_marked = false;
			previous = obj;
			obj = obj->next;
		} else {
			GCObject *unreached = obj;
			obj = obj->next;
			if (previous != NULL)
				previous->next = obj;
			else
				vm->objects = obj;

			free_object(vm, unreached);
		}
	}
}

void collect_garbage(VM *vm)
{
#ifdef DEBUG_LOG_GC
	printf("-- gc begin\n");
	size_t before = vm->bytes_allocated;
#endif
	/* mark all gc roots as gray */
	mark_roots(vm);
	/*
	 * Take gray object, mark it's references object
	 * as gray, push them to gray_stack, then mark itself as black
	 *
	 * So, it's white -> gray and gray -> black happened same time.
	 */
	trace_references(vm);
	/* remove dangling string references */
	table_remove_white(&vm->strings);
	sweep(vm);
	/* adjust next_GC based on still alive object bytes */
	vm->next_GC = vm->bytes_allocated * GC_HEAP_GROW_FACTOR;
#ifdef DEBUG_LOG_GC
	printf("   collected %zu bytes (from %zu to %zu), next at %zu\n",
		before - vm->bytes_allocated, before, vm->bytes_allocated,
		vm->next_GC);
	printf("-- gc end\n");
#endif /* DEBUG_LOG_GC */
}
