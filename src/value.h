#ifndef AMEX_VALUE_H
#define AMEX_VALUE_H

#include "common.h"
#include "object.h"

typedef bool			Boolean;
typedef double			Number;

/* forward declaration */
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
#define IS_ARRAY(value)		((value).type == TYPE_ARRAY)
#define IS_FUNCTION(value)	((value).type == TYPE_FUNCTION)
#define IS_CLOSURE(value)	((value).type == TYPE_CLOSURE)

#define AS_BOOL(value)		(((value).data.boolean))
#define AS_NUMBER(value)	(((value).data.number))
#define AS_STRING(value)	(((value).data.string))
#define AS_ARRAY(value)		(((value).data.array))
#define AS_CSTRING(value)	(((value).data.string->chars))
#define AS_FUNCTION(value)	(((value).data.func))
#define AS_CLOSURE(value)	(((value).data.closure))

#define NIL_VAL			((Value){ TYPE_NIL,      { .boolean = false } })
#define BOOL_VAL(val)		((Value){ TYPE_BOOL,     { .boolean = val   } })
#define NUMBER_VAL(val)		((Value){ TYPE_NUMBER,   { .number  = val   } })
#define TUPLE_VAL(val)		((Value){ TYPE_TUPLE,    { .array   = val   } })
#define STRING_VAL(val)		((Value){ TYPE_STRING,   { .string  = val   } })
#define FUNCTION_VAL(val)	((Value){ TYPE_FUNCTION, { .func    = val   } })
#define CLOSURE_VAL(val)	((Value){ TYPE_CLOSURE,  { .closure = val   } })

bool value_eq(Value v1, Value v2);
void print_value(Value *v, const char *sep);

#endif /* AMEX_VALUE_H */
