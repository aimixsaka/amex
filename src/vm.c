#include <stdarg.h>
#include <stdint.h>

#include "debug.h"
#include "common.h"
#include "vm.h"
#include "include/amexconf.h"
#include "table.h"
#include "memory.h"
#include "value.h"
#include "compiler.h"
#include "str.h"
#include "chunk.h"


#define IERROR	((InterpretResult){ NIL_VAL, INTERPRET_RUNTIME_ERROR })
#define IOK(v)	((InterpretResult){ v,       INTERPRET_OK })

VM vm;

static void reset_stack()
{
	vm.stack.stack_top = vm.stack.values;
	vm.open_upvalues = NULL;
	vm.frame_count = 0;
}

void init_vm()
{
	vm.stack.count = 0;
	vm.stack.capacity = 0;
	vm.stack.values = NULL;
	reset_stack();
	vm.objects = NULL;
	init_table(&vm.strings);
}

void set_vm_globals(Table *env)
{
	vm.globals = env;
}

static void free_vm_stack()
{
	FREE_ARRAY(Value, vm.stack.values, vm.stack.capacity);
}

void free_vm()
{
	free_vm_stack();
	free_objects();
	free_table(&vm.strings);
	free_table(vm.globals);
}

static void runtime_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputs("\n", stderr);

	reset_stack();
}

static void push(Value val)
{
	if (vm.stack.capacity < vm.stack.count + 1) {
		int old_capacity = vm.stack.capacity;
		/* NULL - NULL = 0 (?)*/
		size_t offset = vm.stack.stack_top - vm.stack.values;
		vm.stack.capacity = GROW_CAPACITY(old_capacity);
		vm.stack.values = GROW_ARRAY(Value, vm.stack.values,
					     old_capacity, vm.stack.capacity);
		vm.stack.stack_top = vm.stack.values + offset;
	}
	*vm.stack.stack_top = val;
	++vm.stack.count;
	++vm.stack.stack_top;
}

static Value popn(int n)
{
	vm.stack.stack_top -= n;
	return *vm.stack.stack_top;
}

static Value pop()
{
	return popn(1);
}

static Value peek(int distance)
{
	return vm.stack.stack_top[-1 - distance];
}

static bool is_falsey(Value value)
{
	return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static bool call(Closure *closure, uint8_t argn)
{
	int f_arity = closure->function->arity;
	if (f_arity != -1 && argn != f_arity) {
		runtime_error("expected %d arguments but got %d.",
			      closure->function->arity, argn);
		return false;
	}

	if (vm.frame_count >= FRAMES_MAX) {
		runtime_error("stack overflow.");
		return false;
	}

	CallFrame *frame = &vm.frames[vm.frame_count++];
	frame->closure = closure;
	frame->ip = closure->function->chunk.code;
	frame->slots = vm.stack.stack_top - argn - 1;
	return true;
}

static bool call_value(Value callable, uint8_t argn)
{
	switch (callable.type) {
	case TYPE_CLOSURE:
		return call(AS_CLOSURE(callable), argn);
		break;
	case TYPE_NATIVE:
		break;
	default:
		break;
	}
	runtime_error("can only call functions.");
	return false;
}

static Upvalue *capture_upvalue(Value *local)
{
#ifdef DEBUG_TRACE_EXECUTION
	printf("capturing upvalue: ");
	print_value(local, "\n");
#endif /* DEBUG_TRACE_EXECUTION */
	Upvalue *prev_upvalue = NULL;
	Upvalue *upvalue = vm.open_upvalues;
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
	Upvalue *created_upvalue = new_upvalue(local);

	/* insert the newly created upvalue */
	created_upvalue->next = upvalue;
	if (prev_upvalue == NULL) {
		vm.open_upvalues = created_upvalue;
	} else {
		prev_upvalue->next = created_upvalue;
	}

	return created_upvalue;
}

static void close_upvalues(Value *last)
{
	while (vm.open_upvalues != NULL &&
	       vm.open_upvalues->location >= last) {
		Upvalue *upvalue = vm.open_upvalues;
		upvalue->closed = *last;
		upvalue->location = &upvalue->closed;
		vm.open_upvalues = upvalue->next;
	}
}

static InterpretResult run()
{
	/*
	 * HACK: use this for spe_do and
	 * corelib functions.
	 */
	Value temp;
	CallFrame *frame = &vm.frames[vm.frame_count - 1];
#define READ_BYTE() (*frame->ip++)
#define READ_SHORT()		\
	(frame->ip += 2,	\
	 (uint16_t)((frame->ip[-2] << 8) | frame->ip[-1]))
#define READ_CONSTANT() (frame->closure->function->chunk.constants.values[READ_SHORT()])
#define ARITH_OP(op, n, init_val)				\
do {								\
	int i;							\
	Value v1, v2;						\
	double res = init_val;					\
	if (n == 0) {						\
		push(NUMBER_VAL(init_val));			\
		break;						\
	} else if (n == 1) {					\
		v1 = peek(0);					\
		if (!IS_NUMBER(v1)) {				\
			runtime_error("expected number val.");	\
			return IERROR;				\
		}						\
		push(NUMBER_VAL(init_val op AS_NUMBER(v1)));	\
		break;						\
	}							\
	v1 = peek(n - 1);					\
	v2 = peek(n - 2);					\
	if (!IS_NUMBER(v1) || !IS_NUMBER(v2)) {			\
		runtime_error("expected number val.");		\
		return IERROR;					\
	}							\
	res = AS_NUMBER(v1) op AS_NUMBER(v2);			\
	for (i = n - 3; i >= 0; --i) {				\
		v1 = peek(i);					\
		if (!IS_NUMBER(v1)) {				\
			runtime_error("expected number val.");	\
			return IERROR;				\
		}						\
		res = res op AS_NUMBER(v1);			\
	}							\
	popn(n);						\
	push(NUMBER_VAL(res));					\
} while (0)

#define COMPARE_OP(op, n, init_val)				\
do {								\
	int i;							\
	bool res = init_val;					\
	Value v1, v2;						\
	v1 = peek(n - 1);					\
	if (!IS_NUMBER(v1)) {					\
		runtime_error("expected number val.");		\
		return IERROR;					\
	}							\
	for (i = n - 1; i >= 0; --i) {				\
		v2 = peek(i);					\
		if (!IS_NUMBER(v2)) {				\
			runtime_error("expected number val.");	\
			return IERROR;				\
		}						\
		res = AS_NUMBER(v1) op AS_NUMBER(v2);		\
		if (res != init_val)				\
			break;					\
		v1 = v2;					\
	}							\
	popn(n);						\
	push(BOOL_VAL(res));					\
} while (0)

#define EQ_OP(n, init_val)					\
do {								\
	int i;							\
	Value v1, v2;						\
	bool res = init_val;					\
	v1 = peek(n - 1);					\
	for (i = n - 1; i >= 0; --i) {				\
		v2 = peek(i);					\
		res = value_eq(v1, v2);				\
		if (res != init_val)				\
			break;					\
		v1 = v2;					\
	}							\
	popn(n);						\
	push(BOOL_VAL(res));					\
} while (0)

	for (;;) {
	#ifdef DEBUG_TRACE_EXECUTION
		printf("	");
		for (Value *slot = vm.stack.values;
		     slot < vm.stack.stack_top;
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
			push(constant);
			break;
		}
		/* TODO */
		case OP_EMPTY_TUPLE:
			push(NIL_VAL);
			break;
		case OP_NIL:
			push(NIL_VAL);
			break;
		case OP_TRUE:
			push(BOOL_VAL(true));
			break;
		case OP_FALSE:
			push(BOOL_VAL(false));
			break;
		case OP_POP:
			pop();
			break;
		case OP_SAVE_TOP:
			temp = pop();
			break;
		case OP_RESTORE_TOP:
			push(temp);
			break;
		case OP_GET_LOCAL: {
			uint8_t slot = READ_BYTE();
			push(frame->slots[slot]);
			break;
		}
		case OP_SET_LOCAL: {
			uint8_t slot = READ_BYTE();
			frame->slots[slot] = peek(0);
			break;
		}
		case OP_GET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			push(*frame->closure->upvalues[slot]->location);
			break;
		}
		case OP_SET_UPVALUE: {
			uint8_t slot = READ_BYTE();
			*frame->closure->upvalues[slot]->location = peek(0);
			break;
		}
		case OP_GET_GLOBAL: {
			// int n = frame->closure->function->chunk.constants.count;
			// printf("**** constant array start: *****\n");
			// for (int i = 0; i < n; ++i)
			// 	print_value(&frame->closure->function->chunk.constants.values[i], "\n");
			// printf("**** constant array end: *****\n");
			// printf("**** index: %d ****\n", (uint16_t)((frame->ip[0] << 8) | frame->ip[1]));
			// printf("**** count: %d ****\n", n);
			Value k = READ_CONSTANT();
			Value value;
			if (!table_get(vm.globals, k, &value)) {
				runtime_error("Undefined variable '%s'.", AS_STRING(k)->chars);
				return IERROR;
			}
			push(value);
			break;
		}
		case OP_DEFINE_GLOBAL: {
			Value k = READ_CONSTANT();
			table_set(vm.globals, k, peek(0));
			break;
		}
		case OP_SET_GLOBAL: {
			Value k = READ_CONSTANT();
			if (table_set(vm.globals, k, peek(0))) {
				table_delete(vm.globals, k);
				runtime_error("Undefined variable '%s'.", AS_STRING(k)->chars);
				return IERROR;
			}
			break;
		}
		case OP_JUMP_IF_FALSE: {
			Value v = pop();
			uint16_t offset = READ_SHORT();
			if (is_falsey(v)) {
				frame->ip += offset;
			}
			break;
		}
		case OP_JUMP:
			frame->ip += READ_SHORT();
			break;
		/* TODO OP_SUM0 ...*/
		case OP_SUMN: {
			uint8_t n = AS_NUMBER(temp);
			ARITH_OP(+, n, 0);
			break;
		}
		case OP_SUBTRACTN: {
			uint8_t n = AS_NUMBER(temp);
			ARITH_OP(-, n, 0);
			break;
		}
		case OP_MULTIPLYN: {
			uint8_t n = AS_NUMBER(temp);
			ARITH_OP(*, n, 1);
			break;
		}
		case OP_DIVIDEN: {
			uint8_t n = AS_NUMBER(temp);
			ARITH_OP(/, n, 1);
			break;
		}
		case OP_GREATER: {
			uint8_t n = AS_NUMBER(temp);
			COMPARE_OP(>, n, false);
			break;
		}
		case OP_LESS: {
			uint8_t n = AS_NUMBER(temp);
			COMPARE_OP(<, n, false);
			break;
		}
		case OP_GREATER_EQUAL: {
			uint8_t n = AS_NUMBER(temp);
			COMPARE_OP(>=, n, true);
			break;
		}
		case OP_LESS_EQUAL: {
			uint8_t n = AS_NUMBER(temp);
			COMPARE_OP(<=, n, true);
			break;
		}
		case OP_EQUAL: {
			uint8_t n = AS_NUMBER(temp);
			EQ_OP(n, true);
			break;
		}
		case OP_NOT_EQUAL: {
			uint8_t n = AS_NUMBER(temp);
			EQ_OP(n, false);
			break;
		}
		case OP_PRINT: {
			Value v = pop();
			print_value(&v, "\n");
			push(NIL_VAL);
			break;
		}
		case OP_CLOSE_UPVALUE:
			close_upvalues(vm.stack.stack_top - 1);
			pop();
			break;
		case OP_CLOSURE: {
			Value v = READ_CONSTANT();
			Function *function = AS_FUNCTION(v);
			Closure *closure = new_closure(function);
			push(CLOSURE_VAL(closure));
			for (int i = 0; i < closure->upvalue_count; ++i) {
				uint8_t is_local = READ_BYTE();
				uint8_t index = READ_BYTE();
				/* capture previous frame's local variable */
				if (is_local) {
					closure->upvalues[i] =
						capture_upvalue(frame->slots + index);
				} else {
					closure->upvalues[i] =
						frame->closure->upvalues[index];
				}
			}
			break;
		}
		case OP_CALL: {
			uint8_t argn = READ_BYTE();
			temp = NUMBER_VAL(argn);
			if (!call_value(peek(argn), argn)) {
				return IERROR;
			}
			frame = &vm.frames[vm.frame_count - 1];
			break;
		}
		case OP_RETURN: {
			Value ret_val = pop();
			/* first slot contains closure */
			close_upvalues(frame->slots + 1);
			--vm.frame_count;
			if (vm.frame_count == 0) {
				pop(); /* pop top level SCRIPT_TYPE function */
				return IOK(ret_val);
			}

			vm.stack.stack_top = frame->slots;
			push(ret_val);
			frame = &vm.frames[vm.frame_count - 1];
			break;
		}
		default:
			runtime_error("unimplemented instruction: %d", instruction);
			break;
		}
	}

#undef READ_BYTE
#undef READ_SHORT
#undef READ_CONSTANT
}

InterpretResult interpret(Function *f)
{
	Closure *closure = new_closure(f);
	push(CLOSURE_VAL(closure));
	call(closure, 0);

	return run();
}
