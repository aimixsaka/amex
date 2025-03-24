#ifndef AMEX_ARRAY_H
#define AMEX_ARRAY_H

#include "object.h"
#include "value.h"

struct Array {
	GCObject		gc;
	uint32_t		count;
	uint32_t		capacity;
	Value			*values;
};

Array *new_array(uint32_t capacity);
void init_array(Array *array);
void write_array(Array *array, Value val);
void free_array(Array *array);

#endif /* AMEX_ARRAY_H */
