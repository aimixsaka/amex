#ifndef AMEX_TABLE_H
#define AMEX_TABLE_H

#include "object.h"
#include "value.h"

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

Table *new_table(uint32_t capacity);
void init_table(Table *table);
void free_table(Table *table);
String *table_find_string(Table *table, const char *chars,
			  uint32_t length, uint32_t hash);
bool table_get(Table *table, Value key, Value *value);
bool table_set(Table *table, Value key, Value value);
bool table_delete(Table *table, Value key);

#endif /* AMEX_TABLE_H */
