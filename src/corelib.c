#include <string.h>

#include "chunk.h"
#include "compiler.h"
#include "str.h"
#include "table.h"
#include "value.h"

static Closure *op_function(String *name, OpCode op)
{
	Function *f = new_function();
	f->name = name;
	/* we will peek argument number at runtime */
	f->arity = -1;
	write_chunk(&f->chunk, op);
	write_chunk(&f->chunk, OP_RETURN);
	
	Closure *closure = new_closure(f);
	return closure;
}

Table *core_env(Table *replacement)
{
	Table *env = (replacement == NULL) ?
		new_table(12) : replacement;
#define SET_ENTRY(name, op)				\
do {							\
	String *s = copy_string(name, strlen(name));	\
	table_set(					\
		env,					\
		STRING_VAL(s),				\
		CLOSURE_VAL(op_function(s, op))		\
	);						\
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
