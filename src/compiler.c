#include <setjmp.h>

#include "util.h"
#include "debug.h"
#include "include/amex.h"
#include "include/amexconf.h"


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

static void init_compiler(VM *vm, Compiler *c, FunctionType type, String *fname)
{
	/* store previous compiler */
	c->vm = vm;
	c->enclosing = current;
	c->type = type;
	c->constant_count = 0;
	c->local_count = 0;
	c->scope_depth = 0;
	c->function = new_function(c->vm);
	if (type != SCRIPT_TYPE && fname)
		c->function->name = copy_string(c->vm, fname->chars, fname->length);
	Local *local = &c->locals[c->local_count++];
	local->name = fname;
	local->depth = 0;
	local->is_captured = false;
	local->index = 0;
	++c->constant_count;
	current = c;
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
			return l->index;
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
	l->index = current->constant_count;
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

static void end_scope(uint8_t n)
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
			emit_short((uint16_t)(current->locals[current->local_count - 1].index));
		}
		--current->local_count;
	}
	emit_bytes(OP_POPN, (uint8_t)n);
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
	if (argn != 1)
		CERROR("quote need exactly 1 argument.\n");
	emit_constant(argv[0]);
}

static int quasiquote(Value x, int depth, int level)
{
	if (depth == 0)
		CERROR("quasiquote nested too deep.\n");

	switch (x.type) {
	case TYPE_TUPLE: {
		uint8_t len, i;
		Array *tup = x.data.array;
		Value *elms = tup->values;
		len = tup->count;
		if (len > 1 && IS_SYMBOL(elms[0])) {
			const char *sym = AS_CSTRING(elms[0]);
			if (strcmp(sym, "unquote") == 0) {
				if (level == 0) {
					compile_ast(&elms[1]);
					return 1;
				} else {
					--level;
				}
			} else if (strcmp(sym, "quasiquote")) {
				++level;
			}
		}
		for (i = 0; i < len; ++i)
			quasiquote(elms[i], depth - 1, level);
		return len;
	}
	case TYPE_ARRAY: {
		uint8_t len, i;
		Array *arr = x.data.array;
		len = arr->count;
		for (i = 0; i < len; ++i)
			quasiquote(arr->values[i], depth - 1, level);
		return len;
	}
	case TYPE_TABLE:
		CERROR("quasiquote for type %d unimplemented.\n", x.type);
	/* non-container type, behave same with quote */
	default:
		emit_constant(x);
		return -1;
	}
}

static void spe_quasiquote(uint8_t argn, const Value *argv)
{
	if (argn != 1)
		CERROR("quote need exactly 1 argument.\n");

	int qn = quasiquote(argv[0], MAX_QUOTE_LEVEL, 0);
	if (qn != -1) {
		if (IS_TUPLE(argv[0]))
			emit_bytes(OP_TUPLE, (uint8_t)qn);
		else if (IS_ARRAY(argv[0]))
			emit_bytes(OP_ARRAY, (uint8_t)qn);
	}
}

static void spe_unquote(uint8_t argn, const Value *argv)
{
	CERROR("cannot use unquote here, use it quasiquote instead.\n");
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
			init_compiler(current->vm, &compiler, FUNCTION_TYPE, NULL);
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
			init_compiler(current->vm, &compiler, FUNCTION_TYPE, NULL);
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
			init_compiler(current->vm, &compiler, FUNCTION_TYPE, fname);
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
	/* do evaluates to the last exp in body */
	end_scope(argn - 1);
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
	if (argn == 2)
		emit_if(&argv[0], &argv[1], false, NULL);
	else if (argn == 3)
		emit_if(&argv[0], &argv[1], true, &argv[2]);
	else
		CERROR("expect 2 or 3 arguments.");
}

static void emit_loop(int loop_start)
{
	emit_byte(OP_LOOP);

	int offset = current_chunk()->count - loop_start + 2;
	if (offset > UINT16_MAX)
		CERROR("loop body too large.\n");

	emit_byte((offset >> 8) & 0xff);
	emit_byte(offset & 0xff);
}

static void spe_while(uint8_t argn, const Value *argv)
{
	if (argn < 1)
		CERROR("while expect at least 1 argument.");

	/*
	 * interesting semantic choice, this way
	 * condition of while belongs to while,
	 * not upper scope of while.
	 */
	begin_scope();

	int loop_start = current_chunk()->count;
	compile_ast(&argv[0]);	

	int exit_jump = emit_jump(OP_JUMP_IF_FALSE);

	compile_body(argn - 1, argv + 1);
	emit_loop(loop_start);

	patch_jump(exit_jump);

	end_scope(argn);
	emit_byte(OP_NIL); /* while evaluates to nil */
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
	{	"while",	spe_while	}
};

static const SpecialFn *get_special_fn(const char *name)
{
	return tab_binary_search(
			&special_fns,
			sizeof(special_fns) / sizeof(SpecialFn),
			sizeof(SpecialFn),
			name);
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
		++current->constant_count;
	}
}

static void compile_body(uint8_t elemn, const Value *elms)
{
	if (elemn == 0) {
		emit_byte(OP_NIL);
		++current->constant_count;
		return;
	}
	for (int i = 0; i < elemn; ++i) {
		compile_ast(&elms[i]);
		++current->constant_count;
	}
}

static void compile_form(uint8_t elemn, const Value *elms)
{
	if (elemn >= 255) {
		CERROR("can't have more than 254 arguments.\n");
	}
	if (elemn == 0) {
		/* TODO: emit empty tuple constant */
		Value x;
		x.type = TYPE_TUPLE;
		x.data.array = new_array(current->vm, 0);
		emit_constant(x);
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
	case TYPE_TUPLE:
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
	case TYPE_TUPLE: {
		compile_form(ast->data.array->count, ast->data.array->values);
		break;
	}
	default:
		fprintf(stderr, "compile: Unimplemented!\n");
		break;
	}
}

Function *compile(VM *vm, Value ast)
{
	/* TODO: free memory ? */
	Compiler compiler;
	if (setjmp(on_error)) {
		current = NULL;
		return NULL;
	}
	init_compiler(vm, &compiler, SCRIPT_TYPE, NULL);
	compile_ast(&ast);
	return end_compiler();
}
