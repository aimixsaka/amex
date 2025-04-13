#include "include/amex.h"

void init_chunk(Chunk *chunk)
{
	chunk->count = 0;
	chunk->capacity = 0;
	chunk->code = NULL;
	init_array(&chunk->constants);
}

void free_chunk(VM *vm, Chunk *chunk)
{
	FREE_ARRAY(vm, uint8_t, chunk->code, chunk->capacity);
	free_array(vm, &chunk->constants);
	init_chunk(chunk);
}

void write_chunk(VM *vm, Chunk *chunk, uint8_t byte)
{
	if (chunk->capacity < chunk->count + 1) {
		int old_capacity = chunk->capacity;
		chunk->capacity = GROW_CAPACITY(old_capacity);
		chunk->code = GROW_ARRAY(vm, uint8_t, chunk->code,
				 	 old_capacity, chunk->capacity);
	}
	chunk->code[chunk->count] = byte;
	chunk->count++;
}

void write_short(VM *vm, Chunk *chunk, uint16_t num)
{
	write_chunk(vm, chunk, (uint8_t)(num >> 8));
	write_chunk(vm, chunk, (uint8_t)(num));
}
