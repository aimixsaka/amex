#include "include/amex.h"

static Closure *op_function(VM *vm, String *name, OpCode op)
{
	Function *f = new_function(vm);
	
	/* HACK: GC GUARD */
	push(vm, FUNCTION_VAL(f));
	
	f->name = name;
	/* we will peek argument number at runtime */
	f->arity = -1;
	f->min_arity = -1;
	write_chunk(vm, &f->chunk, op);
	write_chunk(vm, &f->chunk, OP_RETURN);

	Closure *closure = new_closure(vm, f);
	return closure;
}

Table *core_env(VM *vm, Table *replacement)
{
	Table *env = (replacement == NULL) ?
		new_table(vm, 12) : replacement;
	/* HACK: GC GUARD */
	push(vm, TABLE_VAL(env));
#define SET_ENTRY(name, op)						\
do {									\
	String *s = copy_string(vm, name, strlen(name));		\
	/* HACK: GC GUARD */						\
	push(vm, STRING_VAL(s));					\
	Array *fv_pair = new_array(vm, 2);				\
	push(vm, ARRAY_VAL(fv_pair));				\
	write_array(vm, fv_pair, NUMBER_VAL(0));			\
	Value closure = CLOSURE_VAL(op_function(vm, s, op));		\
	push(vm, closure);					\
	write_array(vm, fv_pair, closure);				\
	table_set(							\
		vm,							\
		env,							\
		STRING_VAL(s),						\
		ARRAY_VAL(fv_pair)					\
	);								\
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
	popn(vm, env->count * 4);
	pop(vm);
	return env;
}
