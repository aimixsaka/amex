#include "common.h"
#include "value.h"
#include "memory.h"
#include "array.h"

void init_array(Array *array)
{
	array->values = NULL;
	array->capacity = 0;
	array->count = 0;
}

void write_array(Array *array, Value value)
{
	if (array->capacity < array->count + 1) {
		int old_capacity = array->capacity;
		array->capacity = GROW_CAPACITY(old_capacity);
		array->values = GROW_ARRAY(Value, array->values,
					   old_capacity, array->capacity);
	}

	array->values[array->count++] = value;
}

void free_array(Array *array)
{
	FREE_ARRAY(Value, array->values, array->capacity);
	init_array(array);
}
