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
#define CERROR(...)				\
do {						\
	fputs("compile error: ", stderr);	\
	fprintf(stderr, __VA_ARGS__);		\
	longjmp(on_error, 1);			\
} while (0)

static inline Chunk *current_chunk()
{
	return &current->function->chunk;
}

static void emit_byte(uint8_t byte)
{
	write_chunk(current->vm, current_chunk(), byte);
}

static void emit_short(uint16_t num)
{
	write_short(current->vm, current_chunk(), num);
}

static int add_constant(const Value constant)
{
	if (current_chunk()->constants.count >= UINT16_MAX - 1) {
		CERROR("too many constants in function.\n");
	}
	
	/* HACK: GC GUARD */ 
	push(current->vm, constant);
	
	write_array(current->vm, &current_chunk()->constants, constant);
	
	pop(current->vm);
	
	return current_chunk()->constants.count - 1;
}

static void emit_constant(const Value constant)
{

	int index = add_constant(constant);

	emit_byte(OP_CONSTANT);
	emit_short((uint16_t)index);
}

static void define_global(String *name, uint8_t var_flags)
{
	/* HACK: GC GUARD */
	push(current->vm, STRING_VAL(name));
	
	Array *fv_pair = new_array(current->vm, 2);
	write_array(current->vm, fv_pair, NUMBER_VAL(var_flags));
	write_array(current->vm, fv_pair, NIL_VAL);
	Value var = STRING_VAL(name);
	
	push(current->vm, ARRAY_VAL(fv_pair));

	/* pre variable set for macro */
	table_set(current->vm, current->vm->globals,
		  var, ARRAY_VAL(fv_pair));

	int index = add_constant(var);
	emit_byte(OP_DEFINE_GLOBAL);
	emit_short((uint16_t)index);

	popn(current->vm, 2);
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
	write_chunk(current->vm, current_chunk(), byte1);
	write_chunk(current->vm, current_chunk(), byte2);
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
	c->recursion_guard = MAX_RECURSION;
	c->function = new_function(c->vm);
	if (type != SCRIPT_TYPE && fname)
		c->function->name = copy_string(c->vm, fname->chars, fname->length);
	Local *local = &c->locals[c->local_count++];
	local->name = fname;
	local->depth = 0;
	local->flags = 0;
	local->index = 0;
	c->constant_count++;
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
	for (i = c->local_count - 1; i >= 0; i--) {
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
static void add_local(String *name, uint8_t var_flags)
{
	if (current->local_count >= UINT8_COUNT) {
		CERROR("too many local variables in function.\n");
	}
	Local *l = &current->locals[current->local_count++];
	l->name = name;
	l->depth = -1;
	l->flags = var_flags;
	l->index = current->constant_count;
}

static int add_upvalue(Compiler *c, uint8_t index,
		       bool is_local)
{
	int upval_count = c->function->upval_count;
	/* return existing upvalue if found */
	for (int i = 0; i < upval_count; i++) {
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
		c->enclosing->locals[local].flags |= LOCAL_IS_CAPTURED;
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
	current->scope_depth++;
}

static void end_scope(uint8_t n)
{
	current->scope_depth--;

	while (current->local_count > 0 &&
	       current->locals[current->local_count-1].depth >
	       current->scope_depth) {
		if (current->locals[current->local_count - 1].flags & LOCAL_IS_CAPTURED) {

			/*
			 * here is a brilliant idea:
			 * we only close local variable
			 * that is captured and to be out of scope(out of stack).
			 */
			emit_byte(OP_CLOSE_UPVALUE);
			emit_short((uint16_t)(current->locals[current->local_count - 1].index));
		}
		current->local_count--;
	}
	emit_bytes(OP_POPN, (uint8_t)n);
}

/*
 * Add uninitialized local variable
 * (of which declaration *really* mean).
 * Will check if declared before.
 */
static void declare_variable(String *v, uint8_t var_flags)
{
	/* find till initialized and lower depth local variable */
	for (int i = current->local_count - 1; i >= 0; i--) {
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

	add_local(v, var_flags);
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
	declare_variable(name, 0);
	mark_initialized();
}

/* forward delcaration */
static bool check_args(const Array *arr);
static void compile_ast(Value ast, uint8_t flags);
static void compile_symbol(String *name, bool get_op);
static void compile_args(const Array *arr, int *min_arity, int *arity);
static void compile_body(uint8_t elmn, const Value *elms, uint8_t flags);
static void compile_form(uint8_t elemn, const Value *elms, uint8_t flags);

/* Special Form Start */
typedef struct {
	const char	*name;
	void (*fn)(uint8_t argn, const Value *argv, uint8_t flags);
} SpecialFn;

static void spe_quote(uint8_t argn, const Value *argv, uint8_t flags)
{
	if (argn != 1)
		CERROR("quote need exactly 1 argument.\n");
	emit_constant(argv[0]);
}

static void quasiquote(Value x, int depth, int level, uint8_t flags)
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
					flags |= OPT_ACCEPT_SPLICE;
					compile_ast(elms[1], flags);
					return;
				} else {
					level--;
				}
			} else if (strcmp(sym, "quasiquote") == 0) {
				level++;
			}
		}
		for (i = 0; i < len; i++)
			quasiquote(elms[i], depth - 1, level, flags);
		emit_bytes(OP_TUPLE, len);
		break;
	}
	case TYPE_ARRAY: {
		uint8_t len, i;
		Array *arr = x.data.array;
		len = arr->count;
		for (i = 0; i < len; i++)
			quasiquote(arr->values[i], depth - 1, level, flags);
		emit_bytes(OP_ARRAY, len);
		break;
	}
	case TYPE_TABLE:
		CERROR("quasiquote for type %d unimplemented.\n", x.type);
	/* non-container type, behave same with quote */
	default:
		emit_constant(x);
		break;
	}
}

static void spe_quasiquote(uint8_t argn, const Value *argv, uint8_t flags)
{
	if (argn != 1)
		CERROR("quote need exactly 1 argument.\n");

	quasiquote(argv[0], MAX_QUOTE_LEVEL, 0, flags);
}

static void spe_unquote(uint8_t argn, const Value *argv, uint8_t flags)
{
	CERROR("cannot use unquote here, use it in quasiquote instead.\n");
}

static void spe_splice(uint8_t argn, const Value *argv, uint8_t flags)
{
	if (!(flags | OPT_ACCEPT_SPLICE))
		CERROR(
			"splice can only be used in function parameters and data constructors,"
			"it has no effect here.\n"
		);
	if (argn != 1)
		CERROR("splice need exactly 1 argument.\n");

	Value head = argv[0];
	if (IS_TUPLE(head)) {
		Array *tup = AS_ARRAY(head);
		if (tup->count >= 1 &&
		    IS_SYMBOL(tup->values[0]) &&
		    (strcmp(AS_CSTRING(tup->values[0]), "splice") == 0))
			CERROR("multi level splice is unsupported.\n");
	}
	compile_ast(head, flags);
	emit_byte(OP_SPLICE);
}

static void spe_fn(uint8_t argn, const Value *argv, uint8_t flags)
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
			args = AS_ARRAY(head);
			if (args->count >= UINT8_MAX)
				CERROR("can't have more than 255 parameters.\n");
			check_args(args);
			init_compiler(current->vm, &compiler, FUNCTION_TYPE, NULL);
			int min_arity;
			int arity;
			compile_args(args, &min_arity, &arity);
			begin_scope();
			compiler.function->min_arity = min_arity;
			compiler.function->arity = arity;
			emit_byte(OP_NIL); /* return value */
			break;
		default:
			CERROR("expect function name or function parameters.\n");
		}
	} else if (argn >= 2) {
		switch (head.type) {
		case TYPE_ARRAY: {	/* (fn [a b] (+ a b) ...) */
			args = AS_ARRAY(head);
			if (args->count >= UINT8_MAX)
				CERROR("can't have more than 255 parameters.\n");
			if (!check_args(args)) {
				CERROR("function parameter should be a symbol.\n");
			}
			init_compiler(current->vm, &compiler, FUNCTION_TYPE, NULL);
			int min_arity;
			int arity;
			compile_args(args, &min_arity, &arity);
			begin_scope();
			compiler.function->min_arity = min_arity;
			compiler.function->arity = arity;
			compile_body(argn - 1, argv + 1, flags);
			break;
		}
		case TYPE_SYMBOL: {	/* (fn fname [a b] (+ a b) ...) */
			if (argv[1].type != TYPE_ARRAY) {
				CERROR("expect function parameters.\n");
			}
			fname = AS_STRING(head);
			args = AS_ARRAY(argv[1]);
			if (args->count >= UINT8_MAX)
				CERROR("can't have more than 255 parameters.\n");
			if (!check_args(args)) {
				CERROR("function parameter should be a symbol.\n");
			}
			init_compiler(current->vm, &compiler, FUNCTION_TYPE, fname);
			/* arguments and function name should be in scope 0 */
			int min_arity;
			int arity;
			compile_args(args, &min_arity, &arity);
			begin_scope();
			compiler.function->min_arity = min_arity;
			compiler.function->arity = arity;
			compile_body(argn - 2, argv + 2, flags);
			break;
		}
		default:
			CERROR("epxect function name or function parameters.\n");
		}
	} else {
		CERROR("compiler internal error, unexpected argument number!\n");
	}
	f = end_compiler();

	/* HACK: GC GUARD */
	push(current->vm, FUNCTION_VAL(f));

	emit_byte(OP_CLOSURE);
	emit_short(add_constant(FUNCTION_VAL(f)));

	pop(current->vm);

	for (int i = 0; i < f->upval_count; i++) {
		emit_byte(compiler.upvals[i].is_local ? 1 : 0);
		emit_byte(compiler.upvals[i].index);
	}
}

static void spe_def(uint8_t argn, const Value *argv, uint8_t flags)
{
	if (argn < 2) {
		CERROR("def need at least 2 arguments.\n");
	}
	Value k = argv[0];
	if (!IS_SYMBOL(k)) {
		CERROR("variable name should be a symbol.\n");
	}

	uint8_t var_flags = 0;
	for (int i = 1; i < argn - 1; i++) {
		if (!IS_KEYWORD(argv[i]))
			CERROR("variable attribute should be a keyword.\n");
		if (strcmp(AS_CSTRING(argv[i]), "macro") == 0)
			var_flags |= VAR_IS_MACRO;
		else
			CERROR("unimplemented variable attribute: %s\n", AS_CSTRING(argv[i]));
	}

	String *var = AS_STRING(k);
	Value v = argv[argn - 1];

	/* define local variable */
	if (current->scope_depth > 0) {
		declare_variable(var, var_flags);
		compile_ast(v, flags);
		mark_initialized();
	} else {
	/* define global variable */
		compile_ast(v, flags);
		define_global(var, var_flags);
	}
}

static void spe_set(uint8_t argn, const Value *argv, uint8_t flags)
{
	if (argn != 2) {
		CERROR("def need exactly 2 arguments.\n");
	}
	Value k = argv[0];
	Value v = argv[1];
	if (!IS_SYMBOL(k)) {
		CERROR("variable name should be a symbol.\n");
	}
	compile_ast(v, flags);
	String *var = AS_STRING(k);
	compile_symbol(var, false);
}

static void spe_do(uint8_t argn, const Value *argv, uint8_t flags)
{
	if (argn == 0) {
		emit_byte(OP_NIL);
		return;
	}
	begin_scope();
	compile_body(argn, argv, flags);
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
		    const Value *e2, uint8_t flags)
{
	int index1, index2;
	compile_ast(*b, flags);
	index1 = emit_jump(OP_JUMP_IF_FALSE);
	compile_ast(*exp, flags);
	if (e2) {
		index2 = emit_jump(OP_JUMP);
	}
	patch_jump(index1);
	if (e2) {
		compile_ast(*e2, flags);
		patch_jump(index2);
	}
}


static void spe_if(uint8_t argn, const Value *argv, uint8_t flags)
{
	if (argn == 2)
		emit_if(&argv[0], &argv[1], NULL, flags);
	else if (argn == 3)
		emit_if(&argv[0], &argv[1], &argv[2], flags);
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

static void spe_while(uint8_t argn, const Value *argv, uint8_t flags)
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
	compile_ast(argv[0], flags);

	int exit_jump = emit_jump(OP_JUMP_IF_FALSE);

	compile_body(argn - 1, argv + 1, flags);
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
	for (int i = 0; i < arr->count; i++)
		if (arr->values[i].type != TYPE_SYMBOL)
			return false;
	return true;
}

/*
 * arity == -1, min_arity = -1		op_function
 * arity == min_arity >= 0		normal function
 * arity == -1, min_arity > 0		variable arguments function
 */
static void compile_args(const Array *arr,
			 int *min_arity, int *arity)
{
	String *v;
	*min_arity = 0;
	*arity = 0;
	bool has_vararg = false;
	if (arr->count == 0) {
		return;
	} else if (arr->count >= 1) {
		for (int i = 0; i < arr->count; i++) {
			v = AS_STRING(arr->values[i]);
			if (strcmp(v->chars, "&") == 0) {
				if (i != arr->count - 2)
					CERROR("& in unexpected location");
				*min_arity = i;
				*arity = -1;
				has_vararg = true;
				continue;
			}
			declare_arg(v);
			current->constant_count++;
		}
		if (!has_vararg) {
			*arity = arr->count;
			*min_arity = arr->count;
		}
	} else {
		CERROR("compiler internal error: "
		       "compile_args: unexpected argument number: %d",
		       arr->count);
	}
}

static void compile_body(uint8_t elemn, const Value *elms, uint8_t flags)
{
	if (elemn == 0) {
		emit_byte(OP_NIL);
		current->constant_count++;
		return;
	}
	for (int i = 0; i < elemn; i++) {
		compile_ast(elms[i], flags);
		current->constant_count++;
	}
}

static bool macroexpand1(Value x, Value *out,
			 const SpecialFn **sp_fn)
{
	if (!IS_TUPLE(x))
		return false;
	const Array *form = AS_ARRAY(x);
	if (form->count == 0)
		return false;
	Value head = form->values[0];
	if (!IS_SYMBOL(head))
		return false;

	String *s = AS_STRING(head);
	const char *name = s->chars;
	/*
		* Here is a interesting semantic choice though:
		*   "special forms have higher priority than user defined functions".
		*/
	const SpecialFn *fn = get_special_fn(name);
	if (fn) {
		*sp_fn = fn;
		return false;
	}

	Value tmp;
	Compiler c;
	if (!table_get(current->vm->globals, STRING_VAL(s), &tmp))
		return false;
	Array *fv_pair = AS_ARRAY(tmp);
	if (!((uint8_t)AS_NUMBER(fv_pair->values[0]) & VAR_IS_MACRO) ||
		!IS_CLOSURE(fv_pair->values[1]))
		return false;
	if (form->count - 1 >= UINT8_MAX) {
		CERROR("can't have more than 255 arguments.\n");
	}

	init_compiler(current->vm, &c, SCRIPT_TYPE, NULL);
	/*
	 * TODO:real "local variable as macro" support.
	 * (use global variables table and pass that as argument for interpret() ?)
	 */
	Closure *closure = AS_CLOSURE(fv_pair->values[1]);
	emit_constant(CLOSURE_VAL(closure));
	uint8_t argn = form->count - 1;
	Value *argv = form->values + 1;
	for (int i = 0; i < argn; i++)
		emit_constant(argv[i]);
	emit_bytes(OP_CALL, (uint8_t)argn);
	Function *f = end_compiler();
	InterpretResult res = interpret(current->vm, f);
	if (res.status == INTERPRET_RUNTIME_ERROR)
		CERROR("macro expand failed.\n");
	*out = res.ret;
	return true;
}

static void compile_form(uint8_t elemn, const Value *elms, uint8_t flags)
{
	if (elemn >= UINT8_MAX) {
		CERROR("can't have more than 255 arguments.\n");
	}
	if (elemn == 0) {
		Value x;
		x.type = TYPE_TUPLE;
		x.data.array = new_array(current->vm, 0);
		emit_constant(x);
		return;
	}

	Value head = elms[0];
	switch (head.type) {
	case TYPE_SYMBOL:
		if (!get_special_fn(AS_CSTRING(head)))
			current->constant_count++;
	case TYPE_TUPLE:
		for (int i = 0; i < elemn; i++) {
			compile_ast(elms[i], flags);
		}
		emit_bytes(OP_CALL, elemn - 1);
		current->constant_count--;
		break;
	default:
		CERROR("expect a function call.\n");
	}
}

static void compile_array(uint8_t elemn, const Value *elms, uint8_t flags)
{
	for (int i = 0; i < elemn; i++)
		compile_ast(elms[i], flags);
	emit_bytes(OP_ARRAY, elemn);
}

static void compile_ast(Value ast, uint8_t flags)
{

	if (--current->recursion_guard <= 0)
		CERROR("ast recursed too deep.\n");

	const SpecialFn *sp_fn = NULL;
	/* we expand the macro first, then execute the expansion result */
	uint8_t macroi = MAX_MACRO_EXPAND;
	while (macroi && macroexpand1(ast, &ast, &sp_fn)) {
		macroi--;
	}

	/* HACK: GC GUARD */
	push(current->vm, ast);

	if (sp_fn) {
		sp_fn->fn(ast.data.array->count - 1, ast.data.array->values + 1, flags);
	} else {
		switch (ast.type) {
		case TYPE_NIL:
			emit_byte(OP_NIL);
			break;
		case TYPE_BOOL:
			emit_byte(AS_BOOL(ast) ? OP_TRUE : OP_FALSE);
			break;
		case TYPE_SYMBOL:
			compile_symbol(AS_STRING(ast), true);
			break;
		case TYPE_TUPLE: {
			compile_form(ast.data.array->count, ast.data.array->values, flags);
			break;
		}
		case TYPE_ARRAY:
			compile_array(ast.data.array->count, ast.data.array->values, flags);
			break;
		case TYPE_FUNCTION:
                case TYPE_CLOSURE:
                case TYPE_NATIVE:
			pop(current->vm);

			CERROR("unexpected function type in compile_ast.\n");
                /* TODO: implement table compiling */
                case TYPE_TABLE:
			pop(current->vm);

			CERROR("compile table: unimplemented.\n");
                default:
                        emit_constant(ast);
		}
	}

	pop(current->vm);
}

void mark_compiler_roots(VM *vm)
{
	Compiler *c = current;
	while (c != NULL) {
		mark_object(vm, (GCObject*)c->function);
		c = c->enclosing;
	}
}

Function *compile(VM *vm, Value ast)
{
	/* HAKC: GC GUARD */
	push(vm, ast);
	
	/* TODO: use normal control flow, other than setjmp */
	Compiler compiler;
	if (setjmp(on_error)) {
		current = NULL;
		return NULL;
	}
	init_compiler(vm, &compiler, SCRIPT_TYPE, NULL);
	compile_ast(ast, 0);

	pop(vm);
	
	return end_compiler();
}
