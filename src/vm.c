#include "debug.h"
#include "include/amexconf.h"
#include "include/amex.h"


#define IERROR	((InterpretResult){ NIL_VAL, INTERPRET_RUNTIME_ERROR })
#define IOK(v)	((InterpretResult){ v,       INTERPRET_OK })

static void reset_stack(VM *vm)
{
	vm->stack.stack_top = vm->stack.values;
	vm->open_upvalues = NULL;
	vm->frame_count = 0;
}

void init_vm(VM *vm)
{
	reset_stack(vm);
	vm->objects = NULL;
	vm->bytes_allocated = 0;
	vm->next_GC = GC_INIT_SIZE;
	vm->gray_count = 0;
	vm->gray_capacity = 0;
	vm->gray_stack = NULL;
	vm->globals = NULL;
	init_table(&vm->strings);
}

void set_vm_globals(VM *vm, Table *env)
{
	vm->globals = env;
}

void free_vm(VM *vm)
{
	free_objects(vm);
	free_table(vm, &vm->strings);
}

/* TODO: use setjmp to reset state */
static void runtime_error(VM *vm, const char *format, ...)
{
	fprintf(stderr, "runtime error: ");
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);

	reset_stack(vm);
}

/*
 * NOTE:
 * we use fixed size array instead of
 * realloc or GROW_ARRAY here,
 * because we use push() to guard a Value from GC sweep,
 * which means push itself can not use reallocate function
 * to grow capacity, as reallocate will trigger GC...
 * and for realloc, it's hard to update frame slots when
 * trigger a push that doesn't kown the current frame...
 */
bool push(VM *vm, Value val)
{
	int stack_offset = vm->stack.stack_top - vm->stack.values;
	if (stack_offset >= VM_STACK_MAX || stack_offset < 0) {
		runtime_error(vm, "vm stack overflow\n");
		return false;
	}

	*(vm->stack.stack_top) = val;
	vm->stack.stack_top++;
	return true;
}

Value popn(VM *vm, int n)
{
	vm->stack.stack_top -= n;
	return *vm->stack.stack_top;
}

Value pop(VM *vm)
{
	return popn(vm, 1);
}

static Value peek(VM *vm, int distance)
{
	return vm->stack.stack_top[-1 - distance];
}

static bool is_falsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool call(VM *vm, Closure *closure, uint8_t argn)
{
	int f_arity = closure->function->arity;
	int min_arity = closure->function->min_arity;
	if (f_arity >= 0 && min_arity >= 0 &&
			argn != f_arity) {
		runtime_error(vm, "expected %d arguments but got %d\n",
			      f_arity, argn);
		return false;
	}
	if (f_arity == -1 && min_arity > 0 &&
			min_arity > argn) {
		runtime_error(vm, "expected at least %d arguments but got %d\n",
			      min_arity, argn);
		return false;
	}
	if (vm->frame_count >= FRAMES_MAX) {
		runtime_error(vm, "stack overflow\n");
		return false;
	}

	CallFrame *frame = &vm->frames[vm->frame_count++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	frame->slots = vm->stack.stack_top - argn - 1;

	if (f_arity == -1 && min_arity > 0) {
		/*
		* FIXME: really hacky way to change argument constants at runtime,
		* maybe we can use global variables table to handle this at compile time.
		*/
		Value x;
		x.type = TYPE_ARRAY;
		uint8_t n = argn - min_arity;
		x.data.array = new_array(vm, n);
		for (int i = n - 1; i >= 0; i--)
			write_array(vm, x.data.array, peek(vm, i));
		popn(vm, n);
		if (!push(vm, x))
			return false;
	}

	return true;
}

static bool call_value(VM *vm, Value callable, uint8_t argn)
{
	switch (callable.type) {
	case TYPE_CLOSURE:
		return call(vm, AS_CLOSURE(callable), argn);
	case TYPE_NATIVE:
		break;
	default:
		break;
	}
	runtime_error(vm, "can only call functions\n");
	return false;
}

static Upvalue *capture_upvalue(VM *vm, Value *local)
{
#ifdef DEBUG_TRACE_EXECUTION
	printf("capturing upvalue: ");
	print_value(local, "\n");
#endif /* DEBUG_TRACE_EXECUTION */
	Upvalue *prev_upvalue = NULL;
	Upvalue *upvalue = vm->open_upvalues;
	/*
	 * stop when we find a upvalue that captured the same local variable or
	 * local variable that declared before searching one.
	 */
	while (upvalue != NULL && upvalue->location > local) {
		prev_upvalue = upvalue;
		upvalue = upvalue->next;
	}

	/* return existing upvalue if it captured that same local variable */
	if (upvalue != NULL && upvalue->location == local)
		return upvalue;

	/* eles create new upvalue to capture that local */
	Upvalue *created_upvalue = new_upvalue(vm, local);

	/* insert the newly created upvalue */
	created_upvalue->next = upvalue;
	if (prev_upvalue == NULL) {
		vm->open_upvalues = created_upvalue;
	} else {
		prev_upvalue->next = created_upvalue;
	}

	return created_upvalue;
}

static void close_upvalues(VM *vm, Value *last)
{
	while (vm->open_upvalues != NULL &&
	       vm->open_upvalues->location >= last) {
		Upvalue *upvalue = vm->open_upvalues;
		upvalue->closed = *last;
		upvalue->location = &upvalue->closed;
		vm->open_upvalues = upvalue->next;
	}
}

static InterpretResult run(VM *vm)
{
	/*
	 * HACK: use these temp values for
	 * spe_do and corelib functions.
	 */
	uint8_t op_temp = 0;
	/* sum of spliced_argn - num of splice call */
	uint8_t additional_spliced_argn = 0;
	Value do_temp;
	CallFrame *frame = &vm->frames[vm->frame_count - 1];
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT()		\
	(frame->ip += 2,	\
	 (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT()		\
	(frame->closure->function->chunk.constants.values[READ_SHORT()])

#define PUSH(v)				\
do {					\
	if (!push(vm, v))		\
		return IERROR;		\
} while (0)

#define ARITH_OP(op, n, init_val)					\
do {									\
	int i;								\
	Value v1, v2;							\
	double res = init_val;						\
	if (n == 0) {							\
		PUSH(NUMBER_VAL(init_val));				\
		break;							\
	} else if (n == 1) {						\
		v1 = peek(vm, 0);					\
		if (!IS_NUMBER(v1)) {					\
			runtime_error(vm, "expected number val\n");	\
			return IERROR;					\
		}							\
		PUSH(NUMBER_VAL(init_val op AS_NUMBER(v1)));		\
		break;							\
	}								\
	v1 = peek(vm, n - 1);						\
	v2 = peek(vm, n - 2);						\
	if (!IS_NUMBER(v1) || !IS_NUMBER(v2)) {				\
		runtime_error(vm, "expected number val\n");		\
		return IERROR;						\
	}								\
	res = AS_NUMBER(v1) op AS_NUMBER(v2);				\
	for (i = n - 3; i >= 0; i--) {					\
		v1 = peek(vm, i);					\
		if (!IS_NUMBER(v1)) {					\
			runtime_error(vm, "expected number val\n");	\
			return IERROR;					\
		}							\
		res = res op AS_NUMBER(v1);				\
	}								\
	popn(vm, n);							\
	PUSH(NUMBER_VAL(res));						\
} while (0)

#define COMPARE_OP(op, n, init_val)					\
do {									\
	if (n < 2) {							\
		popn(vm, n);						\
		PUSH(BOOL_VAL(init_val));				\
		break;							\
	}								\
	int i;								\
	Value v1, v2;							\
	bool res = init_val;						\
	v1 = peek(vm, n - 1);						\
	if (!IS_NUMBER(v1)) {						\
		runtime_error(vm, "expected number val\n");		\
		return IERROR;						\
	}								\
	for (i = n - 2; i >= 0; i--) {					\
		v2 = peek(vm, i);					\
		if (!IS_NUMBER(v2)) {					\
			runtime_error(vm, "expected number val.\n");	\
			return IERROR;					\
		}							\
		res = AS_NUMBER(v1) op AS_NUMBER(v2);			\
		if (!res)						\
			break;						\
		v1 = v2;						\
	}								\
	popn(vm, n);							\
	PUSH(BOOL_VAL(res == init_val));				\
} while (0)

#define EQ_OP(n, init_val)						\
do {									\
	if (n < 2) {							\
		popn(vm, n);						\
		PUSH(BOOL_VAL(init_val));				\
		break;							\
	}								\
	int i;								\
	Value v1, v2;							\
	bool res = init_val;						\
	v1 = peek(vm, n - 1);						\
	for (i = n - 2; i >= 0; i--) {					\
		v2 = peek(vm, i);					\
		res = value_eq(v1, v2);					\
		if (!res)						\
			break;						\
		v1 = v2;						\
	}								\
	popn(vm, n);							\
	PUSH(BOOL_VAL(res == init_val));				\
} while (0)

	for (;;) {
	#ifdef DEBUG_TRACE_EXECUTION
		printf("	");
		for (Value *slot = vm->stack.values;
		     slot < vm->stack.stack_top;
		     slot++) {
			printf("[ ");
			print_value(slot, "");
			printf(" ]");
		}
		printf("\n");

		disassemble_instruction(&frame->closure->function->chunk,
				        (int)(frame->ip - frame->closure->function->chunk.code));
	#endif /* DEBUG_TRACE_EXECUTION */
		uint8_t instruction;
		switch (instruction = READ_BYTE()) {
		case OP_CONSTANT: {
			Value constant = READ_CONSTANT();
			PUSH(constant);
			break;
		}
		case OP_NIL:
			PUSH(NIL_VAL);
			break;
		case OP_TRUE:
			PUSH(BOOL_VAL(true));
			break;
		case OP_FALSE:
			PUSH(BOOL_VAL(false));
			break;
		case OP_TUPLE: {
			Value x;
			x.type = TYPE_TUPLE;
			uint8_t n = READ_BYTE();
			size_t sum_temp = n;
			if (sum_temp + additional_spliced_argn >= UINT8_MAX) {
				runtime_error(vm, "can't have more than 254 arguments.\n");
				return IERROR;
			}
			n += additional_spliced_argn;
			x.data.array = new_array(vm, n);
			for (int i = n - 1; i >= 0; i--)
				write_array(vm, x.data.array, peek(vm, i));
			popn(vm, n);
			PUSH(x);
			/* clear additional_spliced_argn */
			additional_spliced_argn = 0;
			break;
		}
		case OP_ARRAY: {
			Value x;
			x.type = TYPE_ARRAY;
			uint8_t n = READ_BYTE();
			size_t sum_temp = n;
			if (sum_temp + additional_spliced_argn >= UINT8_MAX) {
				runtime_error(vm, "can't have more than 254 arguments.\n");
				return IERROR;
			}
			n += additional_spliced_argn;
			x.data.array = new_array(vm, n);
			for (int i = n - 1; i >= 0; i--)
				write_array(vm, x.data.array, peek(vm, i));
			popn(vm, n);
			PUSH(x);
			additional_spliced_argn = 0;
			break;
		}
		case OP_SPLICE: {
			Value v = pop(vm);
			size_t sum_temp = additional_spliced_argn;
			if (IS_TUPLE(v) || IS_ARRAY(v)) {
				Array *arr = v.data.array;
				for (int i = 0; i < arr->count; i++)
					PUSH(arr->values[i]);
				if ((sum_temp + arr->count - 1) >= UINT8_MAX) {
					runtime_error(vm, "can't have more than 254 arguments.\n");
					return IERROR;
				}
				additional_spliced_argn += (arr->count - 1);
			} else {
				runtime_error(vm,
					"splice expect a array or tuple\n");
				return IERROR;
			}
			break;
		}
		case OP_POP:
			pop(vm);
			break;
		case OP_POPN:
			popn(vm, READ_BYTE());
			break;
		case OP_SAVE_TOP:
			do_temp = pop(vm);
			break;
		case OP_RESTORE_TOP:
			PUSH(do_temp);
			break;
		case OP_GET_LOCAL: {
			uint8_t slot = READ_BYTE();
			PUSH(frame->slots[slot]);
			break;
		}
		case OP_SET_LOCAL: {
			uint8_t slot = READ_BYTE();
			frame->slots[slot] = peek(vm, 0);
			break;
		}
		case OP_GET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			PUSH(*frame->closure->upvalues[slot]->location);
			break;
		}
		case OP_SET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = peek(vm, 0);
			break;
		}
		case OP_GET_GLOBAL: {
			// int n = frame->closure->function->chunk.constants.count;
			// printf("**** constant array start: *****\n");
			// for (int i = 0; i < n; i++)
			// 	print_value(&frame->closure->function->chunk.constants.values[i], "\n");
			// printf("**** constant array end: *****\n");
			// printf("**** index: %d ****\n", (uint16_t)((frame->ip[0] << 8) | frame->ip[1]));
			// printf("**** count: %d ****\n", n);
			Value k = READ_CONSTANT();
			Value value;
			if (!table_get(vm->globals, k, &value)) {
				runtime_error(vm, "Undefined variable '%s'\n", AS_CSTRING(k));
				return IERROR;
			}
			PUSH(value.data.array->values[1]);
			break;
		}
		case OP_DEFINE_GLOBAL: {
			Value k = READ_CONSTANT();
			Value v;
			if (!table_get(vm->globals, k, &v)) {
				runtime_error(vm, "undeclared global variable: '%s'", AS_CSTRING(k));
				return IERROR;
			}
			Array *fv_pair = AS_ARRAY(v);
			fv_pair->values[1] = peek(vm, 0);
			table_set(vm, vm->globals, k, ARRAY_VAL(fv_pair));
			break;
		}
		case OP_SET_GLOBAL: {
			Value k = READ_CONSTANT();
			Value v;
			if (!table_get(vm->globals, k, &v)) {
				runtime_error(vm, "undefined global variable: '%s'", AS_CSTRING(k));
				return IERROR;
			}
			Array *fv_pair = AS_ARRAY(v);
			fv_pair->values[1] = peek(vm, 0);
			table_set(vm, vm->globals, k, ARRAY_VAL(fv_pair));
			break;
		}
		case OP_JUMP_IF_FALSE: {
			Value v = pop(vm);
			uint16_t offset = READ_SHORT();
			if (is_falsey(v)) {
				frame->ip += offset;
			}
			break;
		}
		case OP_JUMP: {
			uint16_t offset = READ_SHORT();
			frame->ip += offset;
			break;
		}
		case OP_LOOP: {
			uint16_t offset = READ_SHORT();
			frame->ip -= offset;
			break;
		}
		/* TODO OP_SUM0 ...*/
		case OP_SUMN: {
			ARITH_OP(+, op_temp, 0);
			break;
		}
		case OP_SUBTRACTN: {
			ARITH_OP(-, op_temp, 0);
			break;
		}
		case OP_MULTIPLYN: {
			ARITH_OP(*, op_temp, 1);
			break;
		}
		case OP_DIVIDEN: {
			ARITH_OP(/, op_temp, 1);
			break;
		}
		case OP_GREATER: {
			COMPARE_OP(>, op_temp, true);
			break;
		}
		case OP_LESS: {
			COMPARE_OP(<, op_temp, true);
			break;
		}
		case OP_GREATER_EQUAL: {
			COMPARE_OP(>=, op_temp, true);
			break;
		}
		case OP_LESS_EQUAL: {
			COMPARE_OP(<=, op_temp, true);
			break;
		}
		case OP_EQUAL: {
			EQ_OP(op_temp, true);
			break;
		}
		case OP_NOT_EQUAL: {
			EQ_OP(op_temp, false);
			break;
		}
		case OP_PRINT: {
			Value v = pop(vm);
			print_value(&v, "\n");
			PUSH(NIL_VAL);
			break;
		}
		case OP_CLOSE_UPVALUE: {
			uint16_t index = READ_SHORT();
			close_upvalues(vm, &frame->slots[index]);
			break;
		}
		case OP_CLOSURE: {
			Value v = READ_CONSTANT();
			Function *function = AS_FUNCTION(v);
			Closure *closure = new_closure(vm, function);
			PUSH(CLOSURE_VAL(closure));
			for (int i = 0; i < closure->upvalue_count; i++) {
				uint8_t is_local = READ_BYTE();
				uint8_t index = READ_BYTE();
				/* capture previous frame's local variable */
				if (is_local) {
					closure->upvalues[i] =
						capture_upvalue(vm, frame->slots + index);
				} else {
					closure->upvalues[i] =
						frame->closure->upvalues[index];
				}
			}
			break;
		}
		case OP_CALL: {
			uint8_t argn = READ_BYTE();
			size_t sum_temp = argn;
			if (sum_temp + additional_spliced_argn >= UINT8_MAX) {
				runtime_error(vm, "can't have more than 254 arguments.\n");
				return IERROR;
			}
			argn += additional_spliced_argn;
			op_temp = argn;
			/*
			 * NOTE:
			 * arguments number need to be deterministic
			 * before call call_value.
			 */
			if (!call_value(vm, peek(vm, argn), argn)) {
				return IERROR;
			}
			frame = &vm->frames[vm->frame_count - 1];
			break;
		}
		case OP_RETURN: {
			Value ret_val = pop(vm);
			/* first slot contains closure, so +1 */
			close_upvalues(vm, frame->slots + 1);
			vm->frame_count--;
			if (vm->frame_count == 0) {
				pop(vm); /* pop top level SCRIPT_TYPE function */
				return IOK(ret_val);
			}

			vm->stack.stack_top = frame->slots;
			PUSH(ret_val);
			frame = &vm->frames[vm->frame_count - 1];
			/* reset counter */
			op_temp = 0;
			additional_spliced_argn = 0;
			break;
		}
		default:
			runtime_error(vm, "unimplemented instruction: %d\n", instruction);
			return IERROR;
		}
	}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
#undef PUSH
}

InterpretResult interpret(VM *vm, Function *f)
{
	/* reset stack before each interpret */
	reset_stack(vm);
	
	/* HACK: GC GUARD */
	push(vm, FUNCTION_VAL(f));
	
	Closure *closure = new_closure(vm, f);
	
	pop(vm);
	
	push(vm, CLOSURE_VAL(closure));
	call(vm, closure, 0);

	return run(vm);
}
