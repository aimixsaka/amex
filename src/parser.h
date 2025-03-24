#ifndef AMEX_PARSER_H
#define AMEX_PARSER_H

#include "common.h"
#include "array.h"
#include "table.h"


typedef enum {
	PTYPE_ROOT,
	PTYPE_ARRAY,
	PTYPE_FORM,
	PTYPE_TABLE,
	PTYPE_STRING,
	PTYPE_TOKEN,
	PTYPE_SPECIAL_FORM,
} ParserType;

typedef struct {
	ParserType type;
	union {
		Array *array;
		struct {
			Table		*table;
			Value		key;
			bool		key_found;
		} table_state;
		struct {
			Buffer		*buffer;
			enum {
					STRING_STATE_BASE,
					STRING_STATE_ESCAPE,
			} state;
		} string;
		Value spe_form;
	} buf;
} ParseState;

typedef struct {
	uint32_t	count;
	uint32_t	capacity;
	uint32_t	index;
	ParseState	*stack;	/* parser stack to process parsing */
	Value		value;	/* final value after parsing */
	uint32_t	status;
	const char	*error;
} Parser;


/* Parser Status */
#define PARSER_PENDING	0
#define PARSER_FULL	1
#define PARSER_ERROR	2
#define PARSER_EOF	3


void init_parser(Parser *p);
void free_parser(Parser *p);
int parse_cstring(Parser *p, const char *string);

#endif /* AMEX_PARSER_H */
