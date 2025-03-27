#include "include/amex.h"

static Closure *op_function(VM *vm, String *name, OpCode op)
{
	Function *f = new_function(vm);
	f->name = name;
	/* we will peek argument number at runtime */
	f->arity = -1;
	write_chunk(&f->chunk, op);
	write_chunk(&f->chunk, OP_RETURN);
	
	Closure *closure = new_closure(vm, f);
	return closure;
}

Table *core_env(VM *vm, Table *replacement)
{
	Table *env = (replacement == NULL) ?
		new_table(vm, 12) : replacement;
#define SET_ENTRY(name, op)					\
do {								\
	String *s = copy_string(vm, name, strlen(name));	\
	table_set(						\
		env,						\
		STRING_VAL(s),					\
		CLOSURE_VAL(op_function(vm, s, op))			\
	);							\
} while (0)

	SET_ENTRY("+", OP_SUMN);
	SET_ENTRY("-", OP_SUBTRACTN);
	SET_ENTRY("*", OP_MULTIPLYN);
	SET_ENTRY("/", OP_DIVIDEN);
	SET_ENTRY(">", OP_GREATER);
	SET_ENTRY("<", OP_LESS);
	SET_ENTRY(">=", OP_GREATER_EQUAL);
	SET_ENTRY("<=", OP_LESS_EQUAL);
	SET_ENTRY("=", OP_EQUAL);
	SET_ENTRY("not=", OP_NOT_EQUAL);
	SET_ENTRY("or", OP_OR);
	SET_ENTRY("and", OP_AND);
	SET_ENTRY("print", OP_PRINT);
#undef SET_ENTRY
	return env;
}
