#include "include/amex.h"

static void print_function(Function *f, const char *sep)
{
	if (f->name) {
		printf("<function %s>%s", f->name->chars, sep);
	} else {
		printf("<function %p>%s", f, sep);
	}
}

void print_value(Value *v, const char *sep)
{
	switch (v->type) {
	case TYPE_NIL:
		printf("nil%s", sep);
		break;
	case TYPE_BOOL:
		printf("%s%s", v->data.boolean ? "true" : "false", sep);
		break;
	case TYPE_NUMBER:
		printf("%g%s", v->data.number, sep);
		break;
	case TYPE_STRING:
		printf("\"%s\"%s", (v->data.string)->chars, sep);
		break;
	case TYPE_KEYWORD:
		printf(":%s%s", (v->data.string)->chars, sep);
		break;
	case TYPE_SYMBOL:
		printf("%s%s", (v->data.string)->chars, sep);
		break;
	case TYPE_TUPLE:
	case TYPE_ARRAY: {
		Array *form = AS_ARRAY(*v);
		int len = form->count;
		if (len == 0) {
			printf("()%s", sep);
			break;
		}
		printf("%s", (v->type == TYPE_TUPLE) ? "(" : "[");
		for (int i = 0; i < len - 1; i++) {
			print_value(&form->values[i], " ");
		}
		print_value(&form->values[len - 1], "");
		printf("%s%s",
		       (v->type == TYPE_TUPLE) ? ")" : "]", sep);
		break;
	}
	case TYPE_FUNCTION:
		print_function(v->data.func, sep);
		break;
	case TYPE_CLOSURE:
		print_function(v->data.closure->function, sep);
		break;
	default:
		fprintf(stderr, "print_value: Unimplemented print for type: %d!\n", v->type);
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
