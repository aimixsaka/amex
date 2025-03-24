#ifndef AMEX_VM_H
#define AMEX_VM_H

#include "include/amexconf.h"
#include "object.h"
#include "value.h"
#include "table.h"
#include "compiler.h"

/*
 * A CallFrame/Window represents a "ongoing function call".
 *
 * function local variable index relative to the bottom of stack
 * is dynamic, while relative order in function itself is fixed,
 * so we record the relative location of local at compile time,
 * and record index of first local of function in vm,
 * then compute absolute stack index at runtime.
 *
 * function: the function being called.
 * ip: instruction pointer. Also act as the return address
 *     for upper CallFrame in frames, which means when function
 *     return, it(callee) will jump to code where previous CallFrame(caller)'s
 *     ip point to.
 * slots: index for first usable local variable,
 *        dynamically computed at runtime.
 *        all local variables sit from that.
 *        historically called frame pointer/base pointer.
 */
typedef struct CallFrame {
	Closure			*closure;
	uint8_t			*ip;
	Value			*slots;
} CallFrame;

typedef struct {
	CallFrame		frames[FRAMES_MAX];
	/* current hight of the CallFrame stack */
	uint32_t		frame_count;

	/* We use a stack based VM */
	struct Stack {
		uint32_t	count;
		uint32_t	capacity;
		Value		*values;
		Value		*stack_top;
	} stack;
	Upvalue			*open_upvalues;
	Table			*globals;	/* global variables */
	Table			strings;	/* string intern */
	GCObject		*objects;	/* for gc */
} VM;

typedef enum {
	INTERPRET_OK,
	INTERPRET_RUNTIME_ERROR
} InterpretStatus;

typedef struct {
	Value 		ret;
	InterpretStatus status;
} InterpretResult;

/*
 * FIXME:: we are using global vm now for simplicity,
 * should initialize each single vm in the future.
 */
extern VM vm;

void init_vm();
void set_vm_globals(Table *env);
void free_vm();
InterpretResult interpret(Function *f);

#endif /* AMEX_VM_H */
