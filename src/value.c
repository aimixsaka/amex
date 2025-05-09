#include "include/amex.h"
#include "include/amexconf.h"
#include <stdio.h>

static int max_print_recursion = 100000;

static void print_function(Function *f, FILE *out, const char *sep)
{
	if (f->name) {
		fprintf(out,  "<function %s>%s", f->name->chars, sep);
	} else {
		fprintf(out,  "<function %p>%s", f, sep);
	}
}

void print_object(GCObject *obj, FILE *out, const char *sep)
{
	Value x;
	switch (obj->type) {
	case OBJ_STRING: {
		obj_to_value(obj, TYPE_STRING, &x);
		print_value(&x, out, sep);
		break;
	}
	case OBJ_BUFFER: {
		Buffer *buf = (Buffer*)obj;
		fprintf(out,  "Buffer(\"%.*s\")%s", buf->length, buf->data, sep);
		break;
	}
	case OBJ_ARRAY: {
		obj_to_value(obj, TYPE_ARRAY, &x);
		print_value(&x, out, sep);
		break;
	}
	case OBJ_UPVALUE: {
		fprintf(out,  "Upvalue%s", sep);
		break;
	}
	case OBJ_CLOSURE: {
		obj_to_value(obj, TYPE_CLOSURE, &x);
		print_value(&x, out, sep);
		break;
	}
	case OBJ_FUNCTION: {
		obj_to_value(obj, TYPE_FUNCTION, &x);
		print_value(&x, out, sep);
		break;
	}
	case OBJ_NATIVE_FN: {
		obj_to_value(obj, TYPE_NATIVE, &x);
		print_value(&x, out, sep);
		break;
	}
	case OBJ_TABLE: {
		obj_to_value(obj, TYPE_TABLE, &x);
		print_value(&x, out, sep);
		break;
	}
	default:
		fprintf(out,  "print_object: Unimplemented for type %d!",
			obj->type);
		break;
	}
}

void print_value(Value *v, FILE *out, const char *sep)
{
	if (max_print_recursion-- <= 0) {
		fprintf(out,  "print_value recursion too deep.\n");
		exit(8);
	}

	switch (v->type) {
	case TYPE_NIL:
		fprintf(out, "nil%s", sep);
		break;
	case TYPE_BOOL:
		fprintf(out, "%s%s", v->data.boolean ? "true" : "false", sep);
		break;
	case TYPE_NUMBER:
		fprintf(out, "%g%s", v->data.number, sep);
		break;
	case TYPE_STRING:
		fprintf(out, "\"%s\"%s", (v->data.string)->chars, sep);
		break;
	case TYPE_KEYWORD:
		fprintf(out, ":%s%s", (v->data.string)->chars, sep);
		break;
	case TYPE_SYMBOL:
		fprintf(out, "%s%s", (v->data.string)->chars, sep);
		break;
	case TYPE_TUPLE:
	case TYPE_ARRAY: {
		Array *form = AS_ARRAY(*v);
		int len = form->count;
		if (len == 0) {
			fprintf(out, "()%s", sep);
			break;
		}
		fprintf(out, "%s", (v->type == TYPE_TUPLE) ? "(" : "[");
		for (int i = 0; i < len - 1; i++) {
			print_value(&form->values[i], out, " ");
		}
		print_value(&form->values[len - 1], out, "");
		fprintf(out, "%s%s",
		       (v->type == TYPE_TUPLE) ? ")" : "]", sep);
		break;
	}
	case TYPE_FUNCTION:
		print_function(v->data.func, out, sep);
		break;
	case TYPE_CLOSURE:
		print_function(v->data.closure->function, out, sep);
		break;
	case TYPE_TABLE:
		fprintf(out, "TYPE_TABLE%s", sep);
		break;
	default:
		fprintf(stderr,  "print_value: Unimplemented print for type: %s!\n", type_string(v->type));
		break;
	}
}

void dump_ast(Value *v, FILE *out, const char *sep)
{
	if (max_print_recursion-- <= 0) {
		fprintf(stderr,  "print_value recursion too deep.\n");
		exit(8);
}

	switch (v->type) {
	case TYPE_NIL:
	fprintf(out, "Nil%s", sep);
		break;
	case TYPE_BOOL:
		fprintf(out, "Bool(%s)%s", v->data.boolean ? "true" : "false", sep);
		break;
	case TYPE_NUMBER:
		fprintf(out, "Number(%g)%s", v->data.number, sep);
		break;
	case TYPE_STRING:
		fprintf(out, "String(\"%s\")%s", (v->data.string)->chars, sep);
		break;
	case TYPE_KEYWORD:
		fprintf(out, "Keyword(:%s)%s", (v->data.string)->chars, sep);
		break;
	case TYPE_SYMBOL:
		fprintf(out, "Symbol(%s)%s", (v->data.string)->chars, sep);
		break;
	case TYPE_TUPLE:
	case TYPE_ARRAY: {
		Array *form = AS_ARRAY(*v);
		int len = form->count;
		if (len == 0) {
			fprintf(out, "Array([])%s", sep);
			break;
		}
		fprintf(out, "%s", (v->type == TYPE_TUPLE) ? "Tuple((" : "Array([");
		for (int i = 0; i < len - 1; i++) {
			dump_ast(&form->values[i], out, " ");
		}
		dump_ast(&form->values[len - 1], out, "");
		fprintf(out, "%s%s",
		       (v->type == TYPE_TUPLE) ? "))" : "])", sep);
		break;
	}
	case TYPE_FUNCTION:
		print_function(v->data.func, out, sep);
		break;
	case TYPE_CLOSURE:
		print_function(v->data.closure->function, out, sep);
		break;
	case TYPE_TABLE:
		fprintf(out, "TYPE_TABLE%s", sep);
		break;
	default:
		fprintf(stderr,  "dump_ast: Unimplemented print for type: %s!\n", type_string(v->type));
		break;
	}
}

bool value_eq(Value v1, Value v2)
{
	if (v1.type != v2.type)
		return false;
	switch (v1.type) {
	case TYPE_NIL:
		return IS_NIL(v2);
		break;
	case TYPE_BOOL:
		return AS_BOOL(v1) == AS_BOOL(v2);
		break;
	case TYPE_NUMBER:
		return AS_NUMBER(v1) == AS_NUMBER(v2);
		break;
	case TYPE_STRING:
	case TYPE_KEYWORD:
	case TYPE_SYMBOL:
		return v1.data.string == v2.data.string; /* All String is interned, so just compare address */
		break;
	default:
		fprintf(stderr, "value_eq: Unimplemented!");
		break;
	}
	return false;
}

bool value_is_object(Value *v)
{
	switch (v->type) {
	case TYPE_STRING:
	case TYPE_SYMBOL:
	case TYPE_KEYWORD:
	case TYPE_TUPLE:
	case TYPE_ARRAY:
	case TYPE_FUNCTION:
	case TYPE_CLOSURE:
	case TYPE_NATIVE:
	case TYPE_TABLE:
		return true;
	default:
		return false;
	}
}

void obj_to_value(GCObject *obj, ValueType t, Value *x)
{
	switch (t) {
	case TYPE_STRING:
	case TYPE_SYMBOL:
	case TYPE_KEYWORD:
		*x = STRING_VAL((String*)obj);
		break;
	case TYPE_TUPLE:
	case TYPE_ARRAY:
		*x = ARRAY_VAL((Array*)obj);
		break;
	case TYPE_FUNCTION:
		*x = FUNCTION_VAL((Function*)obj);
		break;
	case TYPE_CLOSURE:
		*x = CLOSURE_VAL((Closure*)obj);
		break;
	case TYPE_NATIVE:
		*x = NATIVE_FN_VAL((NativeFunction*)obj);
		break;
	case TYPE_TABLE:
		*x = TABLE_VAL((Table*)obj);
		break;
	default:
		exit(5);
	}
}

GCObject *value_to_obj(Value *x)
{
	switch (x->type) {
	case TYPE_STRING:
	case TYPE_SYMBOL:
	case TYPE_KEYWORD:
		return (GCObject*)x->data.string;
	case TYPE_TUPLE:
	case TYPE_ARRAY:
		return (GCObject*)x->data.array;
	case TYPE_FUNCTION:
		return (GCObject*)x->data.func;
	case TYPE_CLOSURE:
		return (GCObject*)x->data.closure;
	case TYPE_NATIVE:
		return (GCObject*)x->data.native_fn;
	case TYPE_TABLE:
		return (GCObject*)x->data.table;
	default:
		exit(6);
	}
}

const char *type_string(ValueType t)
{
	switch (t) {
	case TYPE_NIL:
		return "TYPE_NIL";
	case TYPE_BOOL:
		return "TYPE_BOOL";
	case TYPE_NUMBER:
		return "TYPE_NUMBER";
	case TYPE_SYMBOL:
		return "TYPE_SYMBOL";
	case TYPE_STRING:
		return "TYPE_STRING";
	case TYPE_KEYWORD:
		return "TYPE_KEYWORD";
	case TYPE_ARRAY:
		return "TYPE_ARRAY";
	case TYPE_TUPLE:
		return "TYPE_TUPLE";
	case TYPE_FUNCTION:
		return "TYPE_FUNCTION";
	case TYPE_CLOSURE:
		return "TYPE_CLOSURE";
	case TYPE_NATIVE:
		return "TYPE_NATIVE";
	case TYPE_TABLE:
		return "TYPE_TABLE";
	default:
		fprintf(stderr, "Unknown ValueType: %d\n", t);
		exit(4);
	}
}

const char* obj_type_string(GCObject *obj)
{
	switch (obj->type) {
	case OBJ_STRING:
		return "OBJ_STRING";
	case OBJ_BUFFER:
		return "OBJ_BUFFER";
	case OBJ_ARRAY:
		return "OBJ_ARRAY";
	case OBJ_UPVALUE:
		return "OBJ_UPVALUE";
	case OBJ_CLOSURE:
		return "OBJ_CLOSURE";
	case OBJ_FUNCTION:
		return "OBJ_FUNCTION";
	case OBJ_NATIVE_FN:
		return "OBJ_NATIVE_FN";
	case OBJ_TABLE:
		return "OBJ_TABLE";
	default:
		fprintf(stderr, "obj_string: Unimplemented for type %d!",
			obj->type);
		exit(6);
	}
}
