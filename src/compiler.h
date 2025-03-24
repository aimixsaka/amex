#ifndef AMEX_COMPILER_H
#define AMEX_COMPILER_H

#include <setjmp.h>
#include <stdint.h>

#include "chunk.h"
#include "include/amexconf.h"
#include "object.h"
#include "value.h"

typedef enum {
	FUNCTION_TYPE,	/* real function */
	SCRIPT_TYPE,	/* top level code */
} FunctionType;

/*
 * We use a simple yet cleaner model that
 * every function has it's own chunk(bytecode area).
 */
struct Function {
	GCObject		gc;
	int			arity;
	int			upval_count;
	Chunk			chunk;
	String			*name;
};

/*
 * index of VM stack in Upval will change through nested function call,
 * so we use a pointer: *location, to track captured value.
 *
 * different closures may capture same local variable,
 * with `capture variable, other than value` semantics, we should search
 * existing Upvalue before creating new one.
 */
typedef struct Upvalue Upvalue;
struct Upvalue {
	GCObject		gc;
	Value			*location;
	Value			closed;
	Upvalue			*next;
};

/*
 * same upvalues are shared through closures,
 * so we use pointer to a upvalue, instead of
 * a copy of upvalue.
 */
struct Closure {
	GCObject		gc;
	Function		*function;
	Upvalue			**upvalues;
	int			upvalue_count;
};

typedef Value (*NativeFn)(int argn, const Value* argv);

struct NativeFunction {
	GCObject		gc;
	NativeFn		function;
};

/* Local variable representation */
typedef struct {
	int		depth;		/* lexical scope depth this local variable in */
	bool		is_captured;	/* we use index in Upval to track if we captured
					   some Local, here we use is_captured to track
					   if this local has been captured by any Upval */
	String		*name;		/* local variable name */
} Local;

/*
 * Upvalue used to track uppper function's
 * local variables.
 *
 * May refer to a upvaue array index,
 * or a local array index, differed by `is_local`.
 */
typedef struct {
	uint8_t		index;
	bool		is_local;
} Upval;

/*
 * Compiler consume source code of
 * function definition/whole file(also treated as anoymous function definition),
 * producing struct Function.
 * Each function definition create a new Compiler.
 * So struct Compiler contains all function level thing:
 * - Caller Function/Compiler pointer, to track scopes/variables,
 *   also for closure.
 * - to be returned `struct Function`, which contains final bytecode
 * - local variables
 * - how deep lexical scope current is
 * - local variable count
 * - function type (real function/top level code)
 *
 * NOTE: locals and upvals are only needed at
 * compile time, so we put them in struct Compiler,
 * while arity and upvalue_count are needed at compile time and runtime,
 * so we put them in struct Function.
 */
/* forward declaration */
typedef struct Compiler Compiler;
struct Compiler {
	Compiler	*enclosing;
	Function	*function;	
	FunctionType	type;
	Local		locals[UINT8_COUNT];
	Upval		upvals[UINT8_COUNT];
	int		local_count;
	int		scope_depth;
};


Function *new_function();
Upvalue *new_upvalue(Value *slot);
Closure *new_closure(Function *function);

Function *compile(Value ast);

#endif /* AMEX_COMPILER_H */
