#include "include/amex.h"
#include "util.h"

/*
 * Used for internal only,
 * for Tables in vm, we won't use GC, instead,
 * we manage their memory manually.
 */
void init_table(Table *table)
{
	table->count = 0;
	table->capacity = 0;
	table->entries = NULL;
}

static uint32_t key_hash(Value k)
{
	uint32_t hash;
	switch (k.type) {
	case TYPE_SYMBOL:
	case TYPE_STRING:
	case TYPE_KEYWORD:
		hash = AS_STRING(k)->hash;
		break;
	case TYPE_NUMBER:
		hash = hash_number(AS_NUMBER(k));
		break;
	default:
		fprintf(stderr, "key_hash not implemented for type: %d\n", k.type);
		hash = 0;
		break;
	}
	return hash;
}

void free_table(VM *vm, Table *table)
{
	FREE_ARRAY(vm, Entry, table->entries, table->capacity);
	init_table(table);
}

/*
 * Find entry with key, if cann't find,
 * return the first tombstone or empty entry.
 */
static Entry *find_entry(Entry *entries, int capacity, Value key)
{
	uint32_t index = key_hash(key) % capacity;
	Entry *tombstone = NULL;
	Entry *entry;
	for (;;) {
		entry = &entries[index];
		if (IS_NIL(entry->key)) {
			if (IS_NIL(entry->value)) {	/* a real empty entry */
				return tombstone != NULL ? tombstone : entry;
			} else {			/* a tombstone */
				if (tombstone == NULL) {
					tombstone = entry;
				}
			}
		} else if (value_eq(entry->key, key)) {
			return entry;
		}

		index = (index + 1) % capacity;
	}
}

bool table_get(Table *table, Value key, Value *value)
{
	Entry *entry;

	if (table->count == 0)
		return false;

	entry = find_entry(table->entries, table->capacity, key);
	if (IS_NIL(entry->key))
		return false;

	*value = entry->value;
	return true;
}

static void adjust_capacity(VM *vm, Table *table, int capacity)
{
	Entry *entries = ALLOCATE(vm, Entry, capacity);
	/* rebuild count, exclude "tombstone" */
	table->count = 0;
	for (int i = 0; i < capacity; i++) {
		entries[i].key = NIL_VAL;
		entries[i].value = NIL_VAL;
	}

	for (int i = 0; i < table->capacity; i++) {
		Entry *entry = &table->entries[i];
		if (IS_NIL(entry->key))
			continue;

		Entry *dest = find_entry(entries, capacity, entry->key);
		dest->key = entry->key;
		dest->value = entry->value;
		table->count++;
	}

	FREE_ARRAY(vm, Entry, table->entries, table->capacity);
	table->entries = entries;
	table->capacity = capacity;
}

bool table_set(VM *vm, Table *table, Value key, Value value)
{
	if (table->count + 1 > table->capacity * TABLE_MAX_LOAD) {
		int capacity = GROW_CAPACITY(table->capacity);
		/*
		 * Use another function instead of GROW_ARRAY,
		 * as index changed when capacity differs,
		 * so we cann't just copy and keep the index same.
		 */
		adjust_capacity(vm, table, capacity);
	}

	Entry *entry = find_entry(table->entries, table->capacity, key);
	bool is_new_key = IS_NIL(entry->key);
	/*
	 * Only increase when it occurs to a real empty entry,
	 * don't increase count when got tombstone.
	 *
	 * We should treat "tombstone" as "ocupied bucket",
	 * otherwise endup we will get a "full tombstone bucket",
	 * in which case find_entry will enter infinite loop...
	 */
	if (is_new_key && IS_NIL(entry->value))
		table->count++;

	entry->key = key;
	entry->value = value;
	return is_new_key;
}

/*
 * Put a "tombstone" when we delete an entry, so we will not
 * break probing sequence in `find_entry`.
 *
 *          ->           ->
 * -------------------------------
 * | bagel 2 | biscuit 2 | jam 2 |
 * -------------------------------
 * Say we want to delete jam, we use find_entry to find jam, which stop
 * when we find jam or an empty entry(for performance, as in theory we
 * can go throuh all the table to find that key).
 * Normally we can find jam by find_entry, but what if `biscuit` was deleted?
 * -------------------------------
 * | bagel 2 |           | jam 2 |
 * -------------------------------
 *  We will stop at this empty, and return that instead, not our intention, right?.
 * So we use tombstone, treat it as non-empty, and then probe next, until:
 *  1. Find same key, set that as a new tombstone, return true;
 *  Or
 *  2. Find an real empty entry, which means nothing to delete, return false then;
 */
bool table_delete(Table *table, Value key)
{
	if (table->count == 0)
		return false;

	Entry *entry = find_entry(table->entries, table->capacity, key);
	if (IS_NIL(entry->key))
		return false;

	/* set a tombstone */
	entry->key = NIL_VAL;
	entry->value = BOOL_VAL(true);
	return true;
}

/*
 * Compare with find_entry:
 *	key(string) passed to find_entry should be interned already.
 *	This functino checks string length, hash and chars,
 *	used for creating intern table.
 */
String *table_find_string(Table *table, const char *chars,
			  uint32_t length, uint32_t hash)
{
	uint32_t index;
	Entry *entry;

	if (table->count == 0)
		return NULL;

	index = hash % table->capacity;
	for (;;) {
		entry = &table->entries[index];
		Value key = entry->key;
		if (IS_NIL(key)) { /* empty entry or tombstone */
			if (IS_NIL(entry->value)) {
				return NULL;
			}
		} else if (key.type == TYPE_STRING) {
			String *k = AS_STRING(key);
			if (k->length == length &&
			    k->hash == hash &&
			    memcmp(k->chars, chars, length) == 0) {
				return k;
			}
		}

		index = (index + 1) % table->capacity;
	}
}

void table_remove_white(Table *table)
{
	for (int i = 0; i < table->capacity; i++) {
		Entry *entry = &table->entries[i];
		if (value_is_object(&entry->key) &&
			!value_to_obj(&entry->key)->is_marked)
			table_delete(table, entry->key);
	}
}
