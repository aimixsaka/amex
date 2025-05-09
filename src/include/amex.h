#ifndef AMEX_H_defined
#define AMEX_H_defined

#include "amexconf.h"
#include "../common.h"

/* ==== Object Related Start ===== */
typedef enum {
	OBJ_STRING,
	OBJ_BUFFER,
	OBJ_UPVALUE,
	OBJ_ARRAY,
	OBJ_TABLE,
	OBJ_FUNCTION,
	OBJ_CLOSURE,
	OBJ_NATIVE_FN,
} ObjType;

typedef struct GCObject		GCObject ;
/* linked GCObject, used as type punning, and for GC */
struct GCObject {
	GCObject		*next;
	ObjType			type;
	bool			is_marked;
};
/* ==== Object Related End ===== */

typedef bool			Boolean;
typedef double			Number;

/* ==== Value Related Start ==== */
/* forward declaration */
typedef struct VM VM;
typedef struct Buffer		Buffer;
typedef struct String		String;
typedef struct Array		Array;
typedef struct Table		Table;
typedef struct Function		Function;
typedef struct Closure		Closure;
typedef struct NativeFunction	NativeFunction;


/* all Value type, used as both datatype and code structure */
typedef enum ValueType {
	TYPE_NIL,
	TYPE_BOOL,
	TYPE_NUMBER,
	TYPE_STRING,
	TYPE_SYMBOL,
	TYPE_KEYWORD,
	TYPE_ARRAY,
	TYPE_TUPLE,
	TYPE_TABLE,
	TYPE_FUNCTION,
	TYPE_CLOSURE,
	TYPE_NATIVE,
} ValueType;

typedef union ValueData {
	Boolean			boolean;
	Number			number;
	String			*string;
	Buffer			*buffer;
	Array			*array;
	Table			*table;
	Function		*func;
	Closure			*closure;
	NativeFunction		*native_fn;
} ValueData;

/* tagged union Value */
typedef struct Value {
	ValueType		type;
	ValueData		data;
} Value;

#define IS_NIL(value)		((value).type == TYPE_NIL)
#define IS_BOOL(value)		((value).type == TYPE_BOOL)
#define IS_SYMBOL(value)	((value).type == TYPE_SYMBOL)
#define IS_NUMBER(value)	((value).type == TYPE_NUMBER)
#define IS_STRING(value)	((value).type == TYPE_STRING)
#define IS_KEYWORD(value)	((value).type == TYPE_KEYWORD)
#define IS_ARRAY(value)		((value).type == TYPE_ARRAY)
#define IS_TUPLE(value)		((value).type == TYPE_TUPLE)
#define IS_FUNCTION(value)	((value).type == TYPE_FUNCTION)
#define IS_CLOSURE(value)	((value).type == TYPE_CLOSURE)

#define AS_BOOL(value)		(((value).data.boolean))
#define AS_NUMBER(value)	(((value).data.number))
#define AS_STRING(value)	(((value).data.string))
#define AS_ARRAY(value)		(((value).data.array))
#define AS_CSTRING(value)	(((value).data.string->chars))
#define AS_FUNCTION(value)	(((value).data.func))
#define AS_CLOSURE(value)	(((value).data.closure))
#define AS_TABLE(value)		(((value).data.table))
#define AS_NATIVE_FN(value)	(((value).data.native_fn))

#define NIL_VAL			((Value){ TYPE_NIL,      { .boolean   = false } })
#define BOOL_VAL(val)		((Value){ TYPE_BOOL,     { .boolean   = val   } })
#define NUMBER_VAL(val)		((Value){ TYPE_NUMBER,   { .number    = val   } })
#define ARRAY_VAL(val)		((Value){ TYPE_ARRAY,    { .array     = val   } })
#define TUPLE_VAL(val)		((Value){ TYPE_TUPLE,    { .array     = val   } })
#define STRING_VAL(val)		((Value){ TYPE_STRING,   { .string    = val   } })
#define FUNCTION_VAL(val)	((Value){ TYPE_FUNCTION, { .func      = val   } })
#define CLOSURE_VAL(val)	((Value){ TYPE_CLOSURE,  { .closure   = val   } })
#define TABLE_VAL(val)		((Value){ TYPE_TABLE,    { .table     = val   } })
#define NATIVE_FN_VAL(val)	((Value){ TYPE_NATIVE,   { .native_fn = val   } })

bool value_is_object(Value *x);
void mark_object(VM *vm, GCObject *obj);
void obj_to_value(GCObject *obj, ValueType t, Value *x);
GCObject *value_to_obj(Value *x);
bool value_eq(Value v1, Value v2);
void print_object(GCObject *obj, FILE *out, const char *sep);
void print_value(Value *v, FILE *out, const char *sep);
void dump_ast(Value *v, FILE *out, const char *sep);
const char *type_string(ValueType t);
const char *obj_type_string(GCObject* obj);

/* ==== Value Related End ==== */


/** Buffer **/
/*
 * Mainly used for parser, to save temp data,
 * then transform to String.
 */
struct Buffer {
	GCObject		gc;
	uint32_t		length;
	uint32_t		capacity;
	char			*data;
};

Buffer *new_buffer(VM *vm, uint32_t capacity);
String *buf_to_str(VM *vm, Buffer *buf);
void buf_push(VM *vm, Buffer *buf, const char c);
void free_buffer();

/** String **/
struct String {
	GCObject		gc;
	uint32_t		length;
	uint32_t		hash;
	char			*chars;
};

String *copy_string(VM *vm, const char *chars, uint32_t length);

/** Array **/
struct Array {
	GCObject		gc;
	uint32_t		count;
	uint32_t		capacity;
	Value			*values;
};

Array *new_array(VM *vm, uint32_t capacity);
void init_array(Array *array);
void write_array(VM *vm, Array *array, Value val);
void free_array(VM *vm, Array *array);

/** Table **/
typedef struct Entry {
	Value			key;
	Value			value;
} Entry;

struct Table {
	GCObject		gc;
	uint32_t		count;
	uint32_t		capacity;
	Entry			*entries;
};

Table *new_table(VM *vm, uint32_t capacity);
void init_table(Table *table);
void free_table(VM *vm, Table *table);
String *table_find_string(Table *table, const char *chars,
			  uint32_t length, uint32_t hash);
bool table_get(Table *table, Value key, Value *value);
bool table_set(VM *vm, Table *table, Value key, Value value);
bool table_delete(Table *table, Value key);
void table_remove_white(Table *table);



/* ==== Parser Related Start ==== */
typedef enum {
	PTYPE_ROOT,
	PTYPE_ARRAY,
	PTYPE_TUPLE,
	PTYPE_TABLE,
	PTYPE_STRING,
	PTYPE_TOKEN,
	PTYPE_KEYWORD,
	PTYPE_SPECIAL_FORM,
	PTYPE_COMMENT,
} ParserType;

typedef struct {
	ParserType type;
	union {
		Array *array;
		struct {
			Table		*table;
			Value		key;
			bool		key_found;
		} table_state;
		struct {
			Buffer		*buffer;
			enum {
					STRING_STATE_BASE,
					STRING_STATE_ESCAPE,
			} state;
		} string;
	} buf;
} ParseState;

/* TODO: remove index ? */
typedef struct {
	VM		*vm;
	uint32_t	capacity;
	uint32_t	index;
	ParseState	*stack;	/* parser stack to process parsing */
	ParseState	*parser_top;
	Value		value;	/* final value after parsing */
	uint32_t	status;
	const char	*error;
} Parser;


/* Parser Status */
#define PARSER_PENDING	0
#define PARSER_FULL	1
#define PARSER_ERROR	2
#define PARSER_EOF	3


void init_parser(VM *vm, Parser *p);
void free_parser(Parser *p);
void reset_parser(Parser *p);
int parse_cstring(Parser *p, const char *string);
/* ==== Parser Related End ==== */



/* ==== Chunk Related Start ==== */
typedef enum {
	OP_NIL,			/* 1 */
	OP_TUPLE,		/* 2 */
	OP_ARRAY,		/* 3 */
	OP_TRUE,		/* 4 */
	OP_FALSE,		/* 5 */
	OP_EQUAL,		/* 6 */
	OP_NOT_EQUAL,		/* 7 */
	OP_GREATER,		/* 8 */
	OP_LESS,		/* 9 */
	OP_GREATER_EQUAL,	/* 10 */
	OP_LESS_EQUAL,		/* 11 */
	OP_DEFINE_GLOBAL,	/* 12 */
	OP_GET_LOCAL,		/* 13 */
	OP_SET_LOCAL,		/* 14 */
	OP_GET_UPVALUE,		/* 15 */
	OP_SET_UPVALUE,		/* 16 */
	OP_CLOSE_UPVALUE,	/* 17 */
	OP_GET_GLOBAL,		/* 18 */
	OP_SET_GLOBAL,		/* 19 */
	OP_CONSTANT,		/* 20 */
	OP_POP,			/* 21 */
	OP_POPN,		/* 22 */
	OP_SAVE_TOP,		/* 23 */
	OP_RESTORE_TOP,		/* 24 */
	OP_SUM0,		/* 25 */
	OP_SUM1,		/* 26 */
	OP_SUM2,		/* 27 */
	OP_SUMN,		/* 28 */
	OP_SUBTRACT0,		/* 29 */
	OP_SUBTRACT1,		/* 30 */
	OP_SUBTRACT2,		/* 31 */
	OP_SUBTRACTN,		/* 32 */
	OP_MULTIPLY0,		/* 33 */
	OP_MULTIPLY1,		/* 34 */
	OP_MULTIPLY2,		/* 35 */
	OP_MULTIPLYN,		/* 36 */
	OP_DIVIDE0,		/* 37 */
	OP_DIVIDE1,		/* 38 */
	OP_DIVIDE2,		/* 39 */
	OP_DIVIDEN,		/* 40 */
	OP_OR,			/* 41 */
	OP_AND,			/* 42 */
	OP_JUMP,		/* 43 */
	OP_JUMP_IF_FALSE,	/* 44 */
	OP_LOOP,		/* 45 */
	OP_CLOSURE,		/* 46 */
	OP_CALL,		/* 47 */
	OP_PRINT,		/* 48 */
	OP_RETURN,		/* 49 */
	OP_SPLICE,		/* 50 */
} OpCode;

typedef struct {
	uint32_t	count;
	uint32_t 	capacity;
	uint8_t		*code;
	Array		constants;
	// LineArray	lines;
} Chunk;

void init_chunk(Chunk *chunk);
void free_chunk(VM *vm, Chunk *chunk);
void write_chunk(VM *vm, Chunk *chunk, uint8_t byte);
void write_short(VM *vm, Chunk *chunk, uint16_t num);
/* ==== Chunk Related End ==== */


/* ==== Compiler Related Start ==== */

/* compile flags */
#define OPT_ACCEPT_SPLICE	1

typedef enum {
	FUNCTION_TYPE,	/* real function */
	SCRIPT_TYPE,	/* top level code */
} FunctionType;

/*
 * We use a simple yet cleaner model that
 * every function has it's own chunk(bytecode area).
 */
/* TODO: variable length parameters */
struct Function {
	GCObject		gc;
	int			min_arity;
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

/*
 * we use index in Upval to track if we captured some Local,
 * here we use is_captured to track
 * if this local has been captured by any Upval
 */
#define LOCAL_IS_CAPTURED		1
#define VAR_IS_MACRO			2

/* Local variable representation */
typedef struct {
	int		depth;		/* lexical scope depth this local variable in */
	int		index;
	uint8_t		flags;
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
	VM		*vm;
	Compiler	*enclosing;
	Function	*function;	
	FunctionType	type;
	Local		locals[UINT8_COUNT];
	Upval		upvals[UINT8_COUNT];
	int		constant_count;
	int		local_count;
	int		scope_depth;
	uint8_t		recursion_guard;
};


Function *new_function(VM *vm);
Upvalue *new_upvalue(VM *vm, Value *slot);
Closure *new_closure(VM *vm, Function *function);

void mark_compiler_roots(VM *vm);
Function *compile(VM *vm, Value ast);
/* ==== Compiler Related End ==== */



/** corelib **/
Table *core_env(VM *vm, Table *replacement);

/* ==== Memory Related Start ==== */
void collect_garbage(VM *vm);


#define ALLOCATE(vm, type, count)					\
	(type*)reallocate(vm, NULL, 0, sizeof(type) * (count))

#define GROW_CAPACITY(capacity) 					\
	((capacity) < 8 ? 8 : (capacity) * 2)

#define GROW_ARRAY(vm, type, pointer, old_count, new_count) 		\
	(type*)reallocate(vm, pointer, sizeof(type) * (old_count),	\
			  sizeof(type) * (new_count))

#define FREE(vm, type, pointer) reallocate(vm, pointer, sizeof(type), 0)

#define FREE_ARRAY(vm, type, pointer, old_count)			\
	reallocate(vm, pointer, sizeof(type) * (old_count), 0)


void *reallocate(VM *vm, void *pointer, size_t old_size, size_t new_size);
void free_object(VM *vm, GCObject *object);
void free_objects(VM *vm);
/* ==== Memory Related End ==== */


/* ==== VM Related Start ==== */
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

struct VM {
	CallFrame		frames[FRAMES_MAX];
	/* current hight of the CallFrame stack */
	uint32_t		frame_count;

	/*
	 * FIXME: kind of memory waste,
	 * but see note in push(...) function.
	 */
	struct {
		Value			values[VM_STACK_MAX];
		Value*			stack_top;
	} stack;
	Upvalue			*open_upvalues;
	Table			*globals;	/* global variables */
	Table			strings;	/* string intern */
	GCObject		*objects;	/* for gc */
	size_t			bytes_allocated;
	size_t			next_GC;
	int			gray_count;
	int			gray_capacity;
	GCObject		**gray_stack;
};

typedef enum {
	INTERPRET_OK,
	INTERPRET_RUNTIME_ERROR
} InterpretStatus;

typedef struct {
	Value 		ret;
	InterpretStatus status;
} InterpretResult;


bool push(VM *vm, Value val);
Value pop(VM *vm);
Value popn(VM *vm, int n);
void init_vm(VM *vm);
void set_vm_globals(VM *vm, Table *env);
void free_vm(VM *vm);
InterpretResult interpret(VM *vm, Function *f);
/* ==== VM Related End ==== */

#endif /* AMEX_H_defined */
