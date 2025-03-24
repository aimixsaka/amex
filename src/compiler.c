#include <setjmp.h>
#include <stdint.h>

#include "array.h"
#include "chunk.h"
#include "common.h"
#include "compiler.h"
#include "include/amexconf.h"
#include "str.h"
#include "strings.h"
#include "value.h"
#include "debug.h"


/*
 * TODO: don't use global single Compiler,
 * use argument pointer instead.
 */
static Compiler *current = NULL;
static jmp_buf	on_error;

/*
 * If there is an error during compilation,
 * longjump back to start
 */
#define CERROR(...)			\
do {					\
	fprintf(stderr, __VA_ARGS__);	\
	longjmp(on_error, 1);		\
} while (0)

static inline Chunk *current_chunk()
{
	return &current->function->chunk;
}

static void emit_byte(uint8_t byte)
{
	write_chunk(current_chunk(), byte);
}

static void emit_short(uint16_t num)
{
	write_short(current_chunk(), num);
}

static int add_constant(const Value constant)
{
	if (current_chunk()->constants.count >= UINT16_MAX - 1) {
		CERROR("too many constants in function.\n");
	}
	write_array(&current_chunk()->constants, constant);
	return current_chunk()->constants.count - 1;
}

static void emit_constant(const Value constant)
{

	int index = add_constant(constant);

	emit_byte(OP_CONSTANT);
	emit_short((uint16_t)index);
}

static void define_global(String *name)
{
	int index = add_constant(STRING_VAL(name));
	emit_byte(OP_DEFINE_GLOBAL);
	emit_short((uint16_t)index);
}

static void emit_global(const Value constant, bool set_op)
{
	int index = add_constant(constant);

	if (set_op) {
		emit_byte(OP_GET_GLOBAL);
	} else {
		emit_byte(OP_SET_GLOBAL);
	}
	emit_short((uint16_t)index);
}

static void emit_bytes(uint8_t byte1, uint8_t byte2)
{
	write_chunk(current_chunk(), byte1);
	write_chunk(current_chunk(), byte2);
}

static void init_compiler(Compiler *compiler, FunctionType type, String *fname)
{
	/* store previous compiler */
	compiler->enclosing = current;
	compiler->type = type;
	compiler->local_count = 0;
	compiler->scope_depth = 0;
	compiler->function = new_function();
	current = compiler;
	if (type != SCRIPT_TYPE && fname)
		compiler->function->name = copy_string(fname->chars, fname->length);
	Local *local = &compiler->locals[compiler->local_count++];
	local->name = fname;
	local->depth = 0;
	local->is_captured = false;
}

static Function *end_compiler()
{
	emit_byte(OP_RETURN);
	Function *f = current->function;
	/* restore previous compiler */
	current = current->enclosing;
#ifdef DEBUG_PRINT_CODE
		disassemble_chunk(&f->chunk, f->name ? f->name->chars : "<script>");
#endif /* DEBUG_PRINT_CODE */
	return f;
}

static int resolve_local(Compiler *c, String *name)
{
	int i;
	Local *l;
	for (i = c->local_count - 1; i >= 0; --i) {
		l = &c->locals[i];
		if (name == l->name) {
			if (l->depth == -1) {
				CERROR("can't read local variable in its own initializer.\n");
			}
			return i;
		}
	}

	return -1;
}

/*
 * Add a uninitialized local variable.
 */
static void add_local(String *name)
{
	if (current->local_count >= UINT8_COUNT) {
		CERROR("too many local variables in function.\n");
	}
	Local *l = &current->locals[current->local_count++];
	l->name = name;
	l->depth = -1;
	l->is_captured = false;
}

static int add_upvalue(Compiler *c, uint8_t index,
		       bool is_local)
{
	int upval_count = c->function->upval_count;
	/* return existing upvalue if found */
	for (int i = 0; i < upval_count; ++i) {
		Upval *upvalue = &c->upvals[i];
		if (upvalue->index == index && upvalue->is_local == is_local)
			return i;
	}
	if (upval_count == UINT8_COUNT) {
		CERROR("too many closure variables in function.\n");
	}
	c->upvals[upval_count].is_local = is_local;
	c->upvals[upval_count].index = index;
	return c->function->upval_count++;
}

static int resolve_upvalue(Compiler *c, String *name)
{
	if (c->enclosing == NULL)
		return -1;

	int local = resolve_local(c->enclosing, name);
	if (local != -1) {
		c->enclosing->locals[local].is_captured = true;
		return add_upvalue(c, (uint8_t)local, true);
	}

	/* middle recursive function, not that common */
	int upvalue = resolve_upvalue(c->enclosing, name);
	if (upvalue != -1)
		return add_upvalue(c, (uint8_t)upvalue, false);

	return -1;
}

static void begin_scope()
{
	++current->scope_depth;
}

static void end_scope()
{
	--current->scope_depth;

	while (current->local_count > 0 &&
	       current->locals[current->local_count-1].depth >
	       current->scope_depth) {
		if (current->locals[current->local_count - 1].is_captured) {

			/*
			 * here is a brilliant idea:
			 * we only close local variable
			 * that is captured and to be out of scope(out of stack).
			 */
			emit_byte(OP_CLOSE_UPVALUE);
		} else {
			emit_byte(OP_POP);
		}
		--current->local_count;
	}
}

/*
 * Add uninitialized local variable
 * (of which declaration *really* mean).
 * Will check if declared before.
 */
static void declare_variable(String *v)
{
	/* find till initialized and lower depth local variable */
	for (int i = current->local_count - 1; i >= 0; --i) {
		Local *local = &current->locals[i];
		if (local->depth != -1 &&
		    local->depth < current->scope_depth) {
			break;
		}
		if (local->name == v) {
			CERROR(
				"already a variable with the same name in this scope"
			);
		}
	}

	add_local(v);
}

/*
 * Mark latest declared variable as initialized.
 */
static void mark_initialized()
{
	current->locals[current->local_count - 1].depth =
		current->scope_depth;
}

/*
 * Declare and initialize a function argument or function name.
 */
static void declare_arg(String *name)
{
	declare_variable(name);
	mark_initialized();
}

/* forward delcaration */
static bool check_args(const Array *arr);
static void compile_ast(const Value *ast);
static void compile_symbol(String *name, bool get_op);
static void compile_args(const Array *arr);
static void compile_body(uint8_t elmn, const Value *elms);
static void compile_form(uint8_t elemn, const Value *elms);

/* Special Form Start */
typedef struct {
	const char	*name;
	void (*fn)(uint8_t argn, const Value *argv);
} SpecialFn;

static void spe_quote(uint8_t argn, const Value *argv)
{

}

static void spe_quasiquote(uint8_t argn, const Value *argv)
{

}

static void spe_unquote(uint8_t argn, const Value *argv)
{

}

static void spe_splice(uint8_t argn, const Value *argv)
{

}

static void spe_fn(uint8_t argn, const Value *argv)
{
	if (argn == 0) {
		CERROR("fn need at least one argument.\n");
	}
	Compiler compiler;
	Function *f;
	Array *args;
	Value head;
	String *fname;
	head = argv[0];
	if (argn == 1) {	/* (fn [a]) */
		switch (head.type) {
		case TYPE_KEYWORD:
		case TYPE_SYMBOL:	
			CERROR("expect function parameters.\n");
		case TYPE_ARRAY:
			init_compiler(&compiler, FUNCTION_TYPE, NULL);
			args = AS_ARRAY(head);
			check_args(args);
			compiler.function->arity = args->count;
			emit_byte(OP_NIL); /* return value */
			break;
		default:
			CERROR("expect function name or function parameters.\n");
		}
	} else if (argn >= 2) {
		switch (head.type) {
		case TYPE_ARRAY:	/* (fn [a b] (+ a b) ...) */
			args = AS_ARRAY(head);
			if (!check_args(args)) {
				CERROR("function parameter should be a symbol.\n");
			}
			init_compiler(&compiler, FUNCTION_TYPE, NULL);
			compile_args(args);
			begin_scope();
			compiler.function->arity = args->count;
			compile_body(argn - 1, argv + 1);
			break;
		case TYPE_SYMBOL:	/* (fn fname [a b] (+ a b) ...) */
			if (argv[1].type != TYPE_ARRAY) {
				CERROR("expect function parameters.\n");
			}
			fname = AS_STRING(head);
			args = AS_ARRAY(argv[1]);
			if (!check_args(args)) {
				CERROR("function parameter should be a symbol.\n");
			}
			init_compiler(&compiler, FUNCTION_TYPE, fname);
			/* arguments and function name should be in scope 0 */
			compile_args(args);
			begin_scope();
			compiler.function->arity = args->count;
			compile_body(argn - 2, argv + 2);
			break;
		default:
			CERROR("epxect function name or function parameters.\n");
		}
	} else {
		CERROR("compiler internal error, unexpected argument number!\n");
	}
	f = end_compiler();
	emit_byte(OP_CLOSURE);
	emit_short(add_constant(FUNCTION_VAL(f)));

	for (int i = 0; i < f->upval_count; ++i) {
		emit_byte(compiler.upvals[i].is_local ? 1 : 0);
		emit_byte(compiler.upvals[i].index);
	}
}

static void spe_def(uint8_t argn, const Value *argv)
{
	if (argn != 2) {
		CERROR("def need exactly 2 arguments.\n");
	}
	Value k = argv[0];
	Value v = argv[1];
	if (!IS_SYMBOL(k)) {
		CERROR("variable name should be a symbol.\n");
	}
	String *var = AS_STRING(k);

	/* define local variable */
	if (current->scope_depth > 0) {
		declare_variable(var);
		compile_ast(&v);
		mark_initialized();
	} else {
	/* define global variable */
		compile_ast(&v);
		define_global(var);
	}
}

static void spe_set(uint8_t argn, const Value *argv)
{
	if (argn != 2) {
		CERROR("def need exactly 2 arguments.\n");
	}
	Value k = argv[0];
	Value v = argv[1];
	if (!IS_SYMBOL(k)) {
		CERROR("variable name should be a symbol.\n");
	}
	compile_ast(&v);
	String *var = AS_STRING(k);
	compile_symbol(var, false);
}

static void spe_do(uint8_t argn, const Value *argv)
{
	if (argn == 0) {
		emit_byte(OP_NIL);
		return;
	}
	begin_scope();
	compile_body(argn, argv);
	emit_byte(OP_SAVE_TOP);
	end_scope();
	emit_byte(OP_RESTORE_TOP);
}

static int emit_jump(uint8_t inst)
{
	emit_byte(inst);
	emit_short(0x0000);
	return current_chunk()->count - 2;
}

static void patch_jump(int index)
{
	int offset = current_chunk()->count - index - 2;
	if (offset > UINT16_MAX) {
		fprintf(stderr, "jump distance too long.");
		CERROR("internal error.");
	}
	current_chunk()->code[index] = (offset >> 8) & 0xff;
	current_chunk()->code[index + 1] = offset & 0xff;
}

static void emit_if(const Value *b, const Value *exp,
		    bool has_else, const Value *e2)
{
	int index1, index2;
	compile_ast(b);
	index1 = emit_jump(OP_JUMP_IF_FALSE);
	compile_ast(exp);
	if (has_else) {
		index2 = emit_jump(OP_JUMP);
	}
	patch_jump(index1);
	if (has_else) {
		compile_ast(e2);
		patch_jump(index2);
	}
}


static void spe_if(uint8_t argn, const Value *argv)
{
	if (argn == 2) {
		emit_if(&argv[0], &argv[1], false, NULL);
	} else if (argn == 3) {
		emit_if(&argv[0], &argv[1], true, &argv[2]);
	} else {
		CERROR("expect 2 or 3 arguments.");
	}
}


/* lexicographic order, for binary search spec_fn name */
static const SpecialFn special_fns[] = {
	{	"def",		spe_def		},
	{	"do",		spe_do		},
	{	"fn",		spe_fn		},
	{	"if",		spe_if		},
	{	"quasiquote",	spe_quasiquote	},
	{	"quote",	spe_quote	},
	{	"set",		spe_set		},
	{	"splice",	spe_splice	},
	{	"unquote",	spe_unquote	},
};

static const SpecialFn *get_special_fn(const char *name)
{
	/* TOOD: binary search special function name */
	if (memcmp(name, "fn", 2) == 0)
		return &special_fns[2];
	if (memcmp(name, "def", 3) == 0)
		return &special_fns[0];
	if (memcmp(name, "if", 2) == 0)
		return &special_fns[3];
	if (memcmp(name, "do", 2) == 0)
		return &special_fns[1];
	if (memcmp(name, "set", 3) == 0)
		return &special_fns[6];
	return NULL;
}
/* Special Form End */

static void compile_symbol(String *name, bool get_op)
{
	uint8_t op;
	/* TODO: resolve upvalue */
	int arg = resolve_local(current, name);
	if (arg != -1) {
		op = get_op ? OP_GET_LOCAL : OP_SET_LOCAL;
		emit_bytes(op, (uint8_t)arg);
	} else if ((arg = resolve_upvalue(current, name)) != -1) {
		op = get_op ? OP_GET_UPVALUE : OP_SET_UPVALUE;
		emit_bytes(op, (uint8_t)arg);
	} else {
		emit_global(STRING_VAL(name), get_op);
	}
}

static bool check_args(const Array *arr)
{
	for (int i = 0; i < arr->count; ++i)
		if (arr->values[i].type != TYPE_SYMBOL)
			return false;
	return true;
}

static void compile_args(const Array *arr)
{
	check_args(arr);
	String *v;
	for (int i = 0; i < arr->count; ++i) {
		v = AS_STRING(arr->values[i]);
		declare_arg(v);
	}
}

static void compile_body(uint8_t elemn, const Value *elms)
{
	if (elemn == 0) {
		emit_byte(OP_NIL);
		return;
	}
	for (int i = 0; i < elemn; ++i)
		compile_ast(&elms[i]);
}

static void compile_form(uint8_t elemn, const Value *elms)
{
	if (elemn >= 255) {
		CERROR("can't have more than 254 arguments.\n");
	}
	if (elemn == 0) {
		/* TODO: emit empty tuple constant */
		emit_byte(OP_EMPTY_TUPLE);
		return;
	}

	Value head = elms[0];
	switch (head.type) {
	case TYPE_SYMBOL: {
		/* 
		 * FIXME: We should use inline byte code to replace
		 * these hardcoded symbol match in the future, so
		 * + - * / can be just normal functions, and can be
		 * used as arguments ...
		 */
		String *s = AS_STRING(head);
		uint8_t argn = elemn - 1;
		const Value *argv = elms + 1;

		/*
		 * Here is a interesting semantic choice though:
		 *   "special forms have higher priority than user defined functions".
		 */
		const SpecialFn *sp_fn = get_special_fn(s->chars);
		if (sp_fn) {
			sp_fn->fn(argn, argv);
			return;
		}
		compile_symbol(s, true);
		for (int i = 0; i < argn; ++i)
			compile_ast(&argv[i]);
		emit_bytes(OP_CALL, argn);
		break;
	}
	case TYPE_FORM:
		compile_form(head.data.array->count, head.data.array->values);
		for (int i = 1; i < elemn; ++i) {
			compile_ast(&elms[i]);
		}
		emit_bytes(OP_CALL, elemn - 1);
		break;
	default:
		CERROR("expect a function call.\n");
	}
}

static void compile_ast(const Value *ast)
{
	switch (ast->type) {
	case TYPE_NIL:
		emit_byte(OP_NIL);
		break;
	case TYPE_BOOL:
		emit_byte(AS_BOOL(*ast) ? OP_TRUE : OP_FALSE);
		break;
	case TYPE_NUMBER:
	case TYPE_STRING:
	case TYPE_KEYWORD:
	case TYPE_ARRAY:
		emit_constant(*ast);
		break;
	case TYPE_SYMBOL:
		compile_symbol(AS_STRING(*ast), true);
		break;
	case TYPE_FORM: {
		compile_form(ast->data.array->count, ast->data.array->values);
		break;
	}
	default:
		fprintf(stderr, "compile: Unimplemented!\n");
		break;
	}
}

Function *compile(Value ast)
{
	/* TODO: free memory ? */
	Compiler compiler;
	if (setjmp(on_error)) {
		current = NULL;
		return NULL;
	}
	init_compiler(&compiler, SCRIPT_TYPE, NULL);
	compile_ast(&ast);
	return end_compiler();
}
