#ifndef AMEX_CHUNK_H
#define AMEX_CHUNK_H

#include <stdint.h>

#include "array.h"

typedef enum {
	OP_NIL,			/* 1 */
	OP_EMPTY_TUPLE,		/* 2 */
	OP_TRUE,		/* 3 */
	OP_FALSE,		/* 4 */
	OP_EQUAL,		/* 5 */
	OP_NOT_EQUAL,		/* 6 */
	OP_GREATER,		/* 7 */
	OP_LESS,		/* 8 */
	OP_GREATER_EQUAL,	/* 9 */
	OP_LESS_EQUAL,		/* 10 */
	OP_DEFINE_GLOBAL,	/* 11 */
	OP_GET_LOCAL,		/* 12 */
	OP_SET_LOCAL,		/* 13 */
	OP_GET_UPVALUE,		/* 14 */
	OP_SET_UPVALUE,		/* 15 */
	OP_CLOSE_UPVALUE,	/* 16 */
	OP_GET_GLOBAL,		/* 17 */
	OP_SET_GLOBAL,		/* 18 */
	OP_CONSTANT,		/* 19 */
	OP_POP,			/* 20 */
	OP_SAVE_TOP,		/* 21 */
	OP_RESTORE_TOP,		/* 22 */
	OP_SUM0,		/* 23 */
	OP_SUM1,		/* 24 */
	OP_SUM2,		/* 25 */
	OP_SUMN,		/* 26 */
	OP_SUBTRACT0,		/* 27 */
	OP_SUBTRACT1,		/* 28 */
	OP_SUBTRACT2,		/* 29 */
	OP_SUBTRACTN,		/* 30 */
	OP_MULTIPLY0,		/* 31 */
	OP_MULTIPLY1,		/* 32 */
	OP_MULTIPLY2,		/* 33 */
	OP_MULTIPLYN,		/* 34 */
	OP_DIVIDE0,		/* 35 */
	OP_DIVIDE1,		/* 36 */
	OP_DIVIDE2,		/* 37 */
	OP_DIVIDEN,		/* 38 */
	OP_OR,			/* 39 */
	OP_AND,			/* 40 */
	OP_JUMP,		/* 41 */
	OP_JUMP_IF_FALSE,	/* 42 */
	OP_CLOSURE,		/* 43 */
	OP_CALL,		/* 44 */
	OP_PRINT,		/* 45 */
	OP_RETURN,		/* 46 */
} OpCode;

typedef struct {
	uint32_t	count;
	uint32_t 	capacity;
	uint8_t		*code;
	Array		constants;
	// LineArray	lines;
} Chunk;

void init_chunk(Chunk *chunk);
void free_chunk(Chunk *chunk);
void write_chunk(Chunk *chunk, uint8_t byte);
void write_short(Chunk *chunk, uint16_t num);

#endif /* AMEX_CHUNK_H */
