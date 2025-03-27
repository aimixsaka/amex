#include "include/amex.h"
#include "debug.h"


void disassemble_chunk(Chunk *chunk, const char *name)
{
	printf("== function %s ==\n", name);

	for (int offset = 0; offset < chunk->count;) {
		offset = disassemble_instruction(chunk, offset);
	}

	printf("== function %s end ==\n", name);
}

/*
void dump_talbe(Table *table)
{
}
*/

static int simple_instruction(const char *name, int offset)
{
	printf("%s\n", name);
	return offset + 1;
}

static int one_byte_instruction(const char *name, Chunk *chunk,
				int offset)
{
	uint8_t arg = chunk->code[offset + 1];
	printf("[%-16s %6d] ", name, arg);
	printf("\n");
	return offset + 2;
}

static int two_bytes_instruction(const char *name, Chunk *chunk,
				 int offset)
{
	uint16_t constant = (uint16_t)((chunk->code[offset + 1] << 8) | (chunk->code[offset + 2]));
	printf("[%-16s %6d] ", name, constant);
	print_value(&chunk->constants.values[constant], "\n");
	return offset + 3;
}


int disassemble_instruction(Chunk *chunk, int offset)
{
	printf("%04d ", offset);

	uint8_t instruction = chunk->code[offset];
	switch (instruction) {
	case OP_NIL:
		return simple_instruction("OP_NIL", offset);
	case OP_TRUE:
		return simple_instruction("OP_TRUE", offset);
	case OP_FALSE:
		return simple_instruction("OP_FALSE", offset);
	case OP_EMPTY_TUPLE:
		return simple_instruction("OP_EMPTY_TUPLE", offset);
	case OP_PRINT:
		return simple_instruction("OP_PRINT", offset);
	case OP_RETURN:
		return simple_instruction("OP_RETURN", offset);
	case OP_POP:
		return simple_instruction("OP_POP", offset);
	case OP_SAVE_TOP:
		return simple_instruction("OP_SAVE_TOP", offset);
	case OP_RESTORE_TOP:
		return simple_instruction("OP_RESTORE_TOP", offset);
	case OP_EQUAL:
		return simple_instruction("OP_EQUAL", offset);
	case OP_GREATER:
		return simple_instruction("OP_GREATER", offset);
	case OP_LESS:
		return simple_instruction("OP_LESS", offset);
	case OP_SUMN:
		return simple_instruction("OP_SUMN", offset);
	case OP_SUBTRACTN:
		return simple_instruction("OP_SUBTRACTN", offset);
	case OP_MULTIPLYN:
		return simple_instruction("OP_MULTIPLYN", offset);
	case OP_DIVIDEN:
		return simple_instruction("OP_DIVIDEN", offset);
	case OP_POPN:
		return one_byte_instruction("OP_POPN", chunk, offset);
	case OP_GET_LOCAL:
		return one_byte_instruction("OP_GET_LOCAL", chunk, offset);
	case OP_SET_LOCAL:
		return one_byte_instruction("OP_SET_LOCAL", chunk, offset);
	case OP_GET_UPVALUE:
		return one_byte_instruction("OP_GET_UPVALUE", chunk, offset);
	case OP_SET_UPVALUE:
		return one_byte_instruction("OP_SET_UPVALUE", chunk, offset);
	case OP_CONSTANT:
		return two_bytes_instruction("OP_CONSTANT", chunk, offset);
	case OP_CLOSE_UPVALUE:
		return two_bytes_instruction("OP_CLOSE_UPVALUE", chunk, offset);
	case OP_GET_GLOBAL:
		return two_bytes_instruction("OP_GET_GLOBAL", chunk, offset);
	case OP_DEFINE_GLOBAL:
		return two_bytes_instruction("OP_DEFINE_GLOBAL", chunk, offset);
	case OP_SET_GLOBAL:
		return two_bytes_instruction("OP_SET_GLOBAL", chunk, offset);
	case OP_JUMP_IF_FALSE:
		return two_bytes_instruction("OP_JUMP_IF_FALSE", chunk, offset);
	case OP_JUMP:
		return two_bytes_instruction("OP_JUMP", chunk, offset);
	case OP_LOOP:
		return two_bytes_instruction("OP_LOOP", chunk, offset);
	case OP_CALL:
		return one_byte_instruction("OP_CALL", chunk, offset);
	case OP_CLOSURE: {
		uint16_t constant = (uint16_t)((chunk->code[offset + 1] << 8) | (chunk->code[offset + 2]));
		printf("[%-16s %6d] ", "OP_CLOSURE", constant);
		Value v = chunk->constants.values[constant];
		Function *f = AS_FUNCTION(v);
		print_value(&v, "\n");
		offset += 3;
		for (int j = 0; j < f->upval_count; ++j) {
			int is_local = chunk->code[offset++];
			int index = chunk->code[offset++];
			printf("%04d      |                     %s %d\n",
			       offset - 2, is_local ? "local" : "upvalue", index);
		}
		return offset;
	}
	default:
		printf("Unknown opcode %d\n", instruction);
		return offset + 1;
	}
}

