#include "include/amex.h"

/* keep first error */
#define PERROR(p, e)			\
do {					\
	if ((p)->error)			\
		break;			\
	(p)->error = (e);		\
	(p)->status = PARSER_ERROR;	\
} while (0)

static const char UNEXPECTED_CLOSING_DELIM[] = "Unexpected closing delimiter";
static const char UNEXPECTED_EOF[] = "Unexpected EOF";

static bool is_whitespace(const char c)
{
	return c == ' ' || c == '\t' ||
	       c == '\n' || c == '\r' ||
	       c == '\0';
}

static bool is_digit(const char c)
{
	return c >= '0' && c <= '9';
}

static inline int char2int(const char c)
{
	return c - '0';
}

static bool is_symbol_char(const char c)
{
	return is_digit(c) ||
	       (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z') ||
	       (c >= '<' && c <= '@') ||
	       (c >= '#' && c <= '&') ||
	       (c >= '*' && c <= '/' && c != ',') ||
	       (c == '_') || (c == '^') || (c == '!');
}

static bool is_special_char(const char c)
{
	return c == '\'' || c == '~' ||
	       c == ',' || c == ';';
}

/*
 * https://oi-wiki.org/math/binary-exponentiation/
 */
static double amex_exp10(int power)
{
	double res = 1;
	long long base = 10;
	bool negative = power < 0 ? true : false;
	power = negative ? -power : power;
	while (power > 0) {
		/* check last bit */
		if (power & 1)
			res = res * base;
		base = base * base;
		power >>= 1;
	}
	if (negative)
		return 1.0 / res;
	return res;
}

/*
 * valid number: [+/-]<double>[[e/E]<int>]
 */
static bool read_as_num(const char *str, const char *end,
			Number *n, bool force_int)
{
	int sign = 1, x = 0;
	double acc = 0, exp = 1, place = 1;
	if (*str == '-') {
		sign = -1;
		++str;
	} else if (*str == '+') {
		++str;
	}

	if (str >= end)
		return false;
	while (str < end) {
		if (*str == '.' && !force_int) {
			place = 0.1;
		} else if (!force_int && (*str == 'e' || *str == 'E')) {
			++str;
			/* number after e/E should be a valid integer */
			if (!read_as_num(str, end, &exp, true)) {
				return false;
			}
			exp = amex_exp10(exp);
			break;
		} else {
			x = *str;
			if (!is_digit(x))
				return 0;
			x = char2int(x);
			if (place < 1) {
				acc += x *  place;
				place *= 0.1;
			} else {
				acc *= 10;
				acc += x;
			}
		}
		++str;
	}
	*n = acc * sign * exp;
	return true;
}


static ParseState *parser_peek(Parser *p)
{
	return p->parser_top - 1;
}

static void parser_push(Parser *p, ParserType type)
{
	ParseState *top;
	size_t stack_offset = p->parser_top - p->stack;
	if (stack_offset >= PARSER_STACK_MAX) {
		PERROR(p, "Parser Stack Overflow.");
		return;
	}
	if (p->capacity < stack_offset + 1) {
		int old_capacity = p->capacity;
		p->capacity = GROW_CAPACITY(p->capacity);
		p->stack = GROW_ARRAY(ParseState, p->stack,
				      old_capacity, p->capacity);
		p->parser_top = p->stack + stack_offset;
	}
	++p->parser_top;
	top = parser_peek(p);
	if (!top) {
		PERROR(p, "top is null ?");
		return;
	}
	top->type = type;
	switch (type) {
	case PTYPE_COMMENT:
	case PTYPE_ROOT:
		break;
	case PTYPE_STRING:
		top->buf.string.buffer = new_buffer(p->vm, 10);
		top->buf.string.state = STRING_STATE_BASE;
		break;
	case PTYPE_KEYWORD:
	case PTYPE_TOKEN:
		top->buf.string.buffer = new_buffer(p->vm, 8);
		break;
	case PTYPE_ARRAY:
	case PTYPE_TUPLE:
		top->buf.array = new_array(p->vm, 8);
		break;
	case PTYPE_TABLE:
		top->buf.table_state.table = new_table(p->vm, 8);
		top->buf.table_state.key_found = false;
		break;
	case PTYPE_SPECIAL_FORM:
		top->buf.array = new_array(p->vm, 2);
		break;
	}
}

static ParseState *parser_pop(Parser *p)
{
	--p->parser_top;
	if (!p->parser_top) {
		PERROR(p, "Parser stack underflow. (Pop)");
		return NULL;
	}
	return p->parser_top;
}

/* Append Value x to current top most state in Parser stack */
static void parser_top_append(Parser *p, Value x)
{
	ParseState *top = parser_peek(p);
	/* ParseState Stack Overflow */
	if (!top)
		return;
	switch (top->type) {
	case PTYPE_ROOT:
		p->value = x;
		p->status = PARSER_FULL; /* Parsed a full lisp form */
		break;
	case PTYPE_SPECIAL_FORM: {
		write_array(top->buf.array, x);
		Value y;
		y.type = TYPE_TUPLE;
		y.data.array = top->buf.array;
		parser_pop(p);
		parser_top_append(p, y);
		break;
	}
	case PTYPE_ARRAY:
	case PTYPE_TUPLE:
		write_array(top->buf.array, x);
		break;
	case PTYPE_TABLE:
		/* even as Value */
		if (top->buf.table_state.key_found) {
			table_set(top->buf.table_state.table, top->buf.table_state.key, x);
		} else {
		/* odd as Key */
			top->buf.table_state.key = x;
		}
		top->buf.table_state.key_found = !top->buf.table_state.key_found;
		break;
	default:
		PERROR(p, "Expected container type.");
		break;
	}
}

static inline bool str_eq(const char *s1, uint32_t len1,
			  const char *s2, uint32_t len2)
{
	return len1 == len2 && memcmp(s1, s2, len1) == 0;
}


/*
 * Construct underlaying token from raw chars(parser buffer)
 */
static Value buf_build_token(Parser *p, Buffer *buf, bool is_keyword)
{
	Value x;
	Number n;
	uint32_t len = buf->length;
	const char first_char = buf->data[0];
	const char *start = buf->data;
	const char *end = buf->data + len;
	if (is_keyword) {
		x.type = TYPE_KEYWORD;
		x.data.string = buf_to_str(p->vm, buf);
	} else if (read_as_num(start, end, &n, false)) {
		x.type = TYPE_NUMBER;
		x.data.number = n;
	} else if (str_eq(start, len, "nil", 3)) {
		x.type = TYPE_NIL;
		x.data.boolean = false;
	} else if (str_eq(start, len, "true", 4)) {
		x.type = TYPE_BOOL;
		x.data.boolean = true;
	} else if (str_eq(start, len, "false", 5)) {
		x.type = TYPE_BOOL;
		x.data.boolean = false;
	} else {
		if (is_digit(first_char)) {
			PERROR(p, "Symbols cannot start with digits.");
			x.type = TYPE_NIL;
		} else {
			x.type = TYPE_SYMBOL;
			x.data.string = buf_to_str(p->vm, buf);
		}
	}
	return x;
}

static int main_state(Parser *p, char c)
{
	if (c == '(') {
		parser_push(p, PTYPE_TUPLE);
		return 1;
	}
	if (c == '[') {
		parser_push(p, PTYPE_ARRAY);
		return 1;
	}
	if (c == '{') {
		parser_push(p, PTYPE_TABLE);
		return 1;
	}
	if (c == '"') {
		parser_push(p, PTYPE_STRING);
		return 1;
	}
	if (c == ':') {
		parser_push(p, PTYPE_KEYWORD);
		return 1;
	}
	if (c == '#') {
		parser_push(p, PTYPE_COMMENT);
		return 1;
	}
	if (is_whitespace(c))
		return 1;
	if (is_special_char(c)) {
		parser_push(p, PTYPE_SPECIAL_FORM);
		String *qs;
		Value v;
		v.type = TYPE_SYMBOL;
		ParseState *top = parser_peek(p);
		Array *quote_pair = top->buf.array;
		switch (c) {
		case '\'':	/* quote */
			qs = copy_string(p->vm, "quote", 5);
			break;
		case '~':	/* quasiquote */
			qs = copy_string(p->vm, "quasiquote", 10);
			break;
		case ',':	/* unquote */
			qs = copy_string(p->vm, "unquote", 7);
			break;
		case ';':	/* splice */
			qs = copy_string(p->vm, "splice", 6);
			break;
		}
		v.data.string = qs;
		write_array(quote_pair, v);
		return 1;
	}
	if (is_symbol_char(c)) {
		parser_push(p, PTYPE_TOKEN);
		return 0;
	}
	PERROR(p, "Unexpected character.");
	return 1;
}

/*
 * Entrypoint for the stack parser.
 */
static int root_state(Parser *p, char c)
{
	if (c == ']' || c == ')' || c == '}') {
		PERROR(p, UNEXPECTED_CLOSING_DELIM);
		return 1;
	} else {
		return main_state(p, c);
	}
}

static int token_state(Parser *p, const char c)
{
	ParseState *top = parser_peek(p);
	Buffer *buf = top->buf.string.buffer;
	if (is_whitespace(c) || c == ')'|| c == ']' || c == '}') {
		parser_pop(p);
		parser_top_append(p, buf_build_token(p, buf,
					top->type == PTYPE_KEYWORD));
		return (c == ')' || c == ']' || c == '}') ? 0 : 1;
	} else if (is_symbol_char(c)) {
		buf_push(buf, c);
		return 1;
	} else {
		PERROR(p, "Expect a symbol char.");
		return 1;
	}
}

static int form_state(Parser *p, const char c)
{
	if (c == ')') {
		ParseState *top = parser_pop(p);
		Array *array = top->buf.array;
		Value x;
		x.type = TYPE_TUPLE;
		x.data.array = array;
		parser_top_append(p, x);
		return 1;
	} else if (c == ']' || c == '}') {
		PERROR(p, UNEXPECTED_CLOSING_DELIM);
		return 1;
	} else {
		return main_state(p, c);
	}
}

static int array_state(Parser *p, const char c)
{
	if (c == ']') {
		ParseState *top = parser_pop(p);
		Array *arr = top->buf.array;
		Value x;
		x.type = TYPE_ARRAY;
		x.data.array = arr;
		parser_top_append(p, x);
		return 1;
	} else if (c == ')' || c == '}') {
		PERROR(p, UNEXPECTED_CLOSING_DELIM);
		return 1;
	} else {
		return main_state(p, c);
	}
}

static int string_state(Parser *p, const char c) {
	ParseState *top = parser_peek(p);
	switch (top->buf.string.state) {
	case STRING_STATE_BASE:
		if (c == '\\') {
			top->buf.string.state = STRING_STATE_ESCAPE;
		} else if (c == '"') {
			Value x;
			x.type = TYPE_STRING;
			x.data.string = buf_to_str(p->vm, top->buf.string.buffer);
			parser_pop(p);
			parser_top_append(p, x);
		} else {
			buf_push(top->buf.string.buffer, c);
		}
		break;
	case STRING_STATE_ESCAPE: {
		char next;
		switch (c) {
		case 'n':
			next = '\n';
			break;
		case 'r':
			next = '\r';
			break;
		case 't':
			next = '\t';
			break;
		case 'f':
			next = '\f';
			break;
		case '0':
			next = '\0';
			break;
		case '"':
			next = '"';
			break;
		case '\'':
			next = '\'';
			break;
		case 'z':
			next = '\0';
			break;
		default:
			PERROR(p, "Unknown string escape sequence.");
			return 1;
		}
		buf_push(top->buf.string.buffer, next);
		top->buf.string.state = STRING_STATE_BASE;
		break;
	}
	}
	return 1;
}

/*
 * Handle lisp dictionary/talbe, which is just a list
 * with even item as key, odd item as value.
 */
static int table_state(Parser *p, const char c) {
	if (c == '}') {
		ParseState *top = parser_pop(p);
		if (!top->buf.table_state.key_found) {
			Value x;
			x.type = TYPE_TABLE;
			x.data.table = top->buf.table_state.table;
			parser_top_append(p, x);
			return 1;
		} else {
			PERROR(p, "Odd number of items in dict literal.");
			return 1;
		}
	} else if (c == ')' || c == ']') {
		PERROR(p, UNEXPECTED_CLOSING_DELIM);
		return 1;
	} else {
		return main_state(p, c);
	}
}


/* we allow multi level quote */
static int special_char_state(Parser *p, const char c)
{
	return main_state(p, c);
}

static int comment_state(Parser *p, const char c)
{
	if (c == '\n')
		parser_pop(p);
	return 1;
}

static bool check_eof(Parser *p, ParseState *state, const char c)
{
	switch (state->type) {
	case PTYPE_COMMENT:
	case PTYPE_ROOT:
		if (c == '\0') {
			p->status = PARSER_EOF;
			return true;
		}
		break;
	case PTYPE_TOKEN:
	case PTYPE_KEYWORD:
	case PTYPE_TUPLE:
	case PTYPE_ARRAY:
	case PTYPE_STRING:
	case PTYPE_TABLE:
	case PTYPE_SPECIAL_FORM:
		if (c == '\0') {
			p->status = PARSER_EOF;
			p->error = UNEXPECTED_EOF;
			return true;
		}
		break;
	}
	return false;
}

static int dispatch_char(Parser *p, const char c) {
	int done = 0;
	while (!done && p->status == PARSER_PENDING) {
		ParseState *top = parser_peek(p);
		switch (top->type) {
		case PTYPE_ROOT:
			done = root_state(p, c);
			break;
		case PTYPE_TOKEN:
			done = token_state(p, c);
			break;
		case PTYPE_KEYWORD:
			done = token_state(p, c);
			break;
		case PTYPE_TUPLE:
			done = form_state(p, c);
			break;
		case PTYPE_ARRAY:
			done = array_state(p, c);
			break;
		case PTYPE_STRING:
			done = string_state(p, c);
			break;
		case PTYPE_TABLE:
			done = table_state(p, c);
			break;
		case PTYPE_SPECIAL_FORM:
			done = special_char_state(p, c);
			break;
		case PTYPE_COMMENT:
			done = comment_state(p, c);
			break;
		}
	}
	++p->index;
	return !done;
}

/*
 * Parse until we got a complete lisp form, or the end of string line,
 * return number of parsed char.
 */
int parse_cstring(Parser *p, const char *string)
{
	int bytes_read = 0;
	p->status = PARSER_PENDING;
	while (p->status == PARSER_PENDING) {
		if (check_eof(p, parser_peek(p), string[bytes_read]))
			break;
		dispatch_char(p, string[bytes_read++]);
	}
	return bytes_read;
}

void reset_parser(Parser *p)
{
	p->index = 0;
	p->error = NULL;
	p->parser_top = p->stack;
	p->status = PARSER_PENDING;
	parser_push(p, PTYPE_ROOT);
}

void free_parser(Parser *p)
{
	FREE_ARRAY(ParseState, p->stack, p->capacity);
}

void init_parser(VM *vm, Parser *p)
{
	ParseState *data = NULL;
	p->vm = vm;
	p->stack = data;
	p->capacity = 0;
	reset_parser(p);
}
