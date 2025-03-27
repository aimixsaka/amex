#include "include/amex.h"

static void dump_parse_state(ParseState *state)
{

}

#define PERROR(p, e) ((p)->error = (e), (p)->status = PARSER_ERROR)

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
	       (c >= '*' && c <= '/') ||
	       (c >= '#' && c <= '&') ||
	       (c == '_') ||
	       (c == '^') ||
	       (c == '!');
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
	if (!p->count) {
		PERROR(p, "Parser stack underflow. (Peek)");
		return NULL;
	}
	return p->stack + p->count - 1;
}

static void parser_push(Parser *p, ParserType type)
{
	ParseState *top;
	if (p->count >= p->capacity) {
		uint32_t new_capability = GROW_CAPACITY(p->capacity);
		p->stack = GROW_ARRAY(ParseState, p->stack, p->capacity, new_capability);
		p->capacity = new_capability;
	}
	++p->count;
	top = parser_peek(p);
	if (!top) {
		fprintf(stderr, "top is null ?\n");
		return;
	}
	top->type = type;
	switch (type) {
	case PTYPE_ROOT:
		break;
	case PTYPE_STRING:
		top->buf.string.buffer = new_buffer(p->vm, 10);
		top->buf.string.state = STRING_STATE_BASE;
		break;
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
		/*
		 * We handle special chars(' ~ , ;) in
		 * special_char_state() and parser_top_append().
		 */
		break;
	}
}

static ParseState *parser_pop(Parser *p)
{
	if (!p->count) {
		PERROR(p, "Parser stack underflow. (Pop)");
		return NULL;
	}
	return p->stack + --p->count;
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
		Value pair, spe_form;
		Array *arr2 = new_array(p->vm, 2);
		ParseState *top = parser_peek(p);
		parser_pop(p);

		spe_form = top->buf.spe_form;
		write_array(arr2, spe_form);
		write_array(arr2, x);
		pair.type = TYPE_TUPLE;
		pair.data.array = arr2;
		parser_top_append(p, pair);
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
static Value buf_build_token(Parser *p, Buffer *buf)
{
	Value x;
	Number n;
	uint32_t len = buf->length;
	const char first_char = buf->data[0];
	const char *start = buf->data;
	const char *end = buf->data + len;
	if (read_as_num(start, end, &n, false)) {
		x.type = TYPE_NUMBER;
		x.data.number = n;
	} else if (str_eq(start, len, "nil", 3)) {
		x.type = TYPE_NIL;
		x.data.boolean = 0;
	} else if (str_eq(start, len, "true", 4)) {
		x.type = TYPE_BOOL;
		x.data.boolean = 1;
	} else if (str_eq(start, len, "false", 5)) {
		x.type = TYPE_BOOL;
		x.data.boolean = 0;
	} else {
		if (is_digit(first_char)) {
			PERROR(p, "Symbols cannot start with digits.");
			x.type = TYPE_NIL;
		} else if (first_char == ':') {
			x.type = TYPE_KEYWORD;
			x.data.string = buf_to_str(p->vm, buf);
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
	if (is_whitespace(c))
		return 1;
	if (is_symbol_char(c)) {
		parser_push(p, PTYPE_TOKEN);
		return 0;
	}
	if (is_special_char(c)) {
		parser_push(p, PTYPE_SPECIAL_FORM);
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
		parser_top_append(p, buf_build_token(p, buf));
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

/*
 * Handle a special char, transform it to symbol,
 * we will handle that in compiler as a special form.
 */
static int special_char_state(Parser *p, const char c)
{
	ParseState *top = parser_peek(p);
	Value spe_form;
	spe_form.type = TYPE_TUPLE;
	switch (c) {
	case '\'':	/* quote */
		spe_form.data.string = copy_string(p->vm, "quote", 5);
		break;
	case '~':	/* quasiquote */
		spe_form.data.string = copy_string(p->vm, "quasiquote", 10);
		break;
	case ',':	/* unquote */
		spe_form.data.string = copy_string(p->vm, "unquote", 7);
		break;
	case ';':	/* splice */
		spe_form.data.string = copy_string(p->vm, "splice", 6);
		break;
	}
	top->buf.spe_form = spe_form;
	return 1;
}

static bool check_eof(Parser *p, ParseState *state, const char c)
{
	switch (state->type) {
	case PTYPE_ROOT:
		if (c == '\0') {
			p->status = PARSER_FULL;
			return true;
		}
	case PTYPE_TOKEN:
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

void free_parser(Parser *p)
{
	FREE_ARRAY(ParseState, p->stack, p->capacity);
}

void init_parser(VM *vm, Parser *p)
{
	ParseState *data = NULL;
	p->vm = vm;
	p->stack = data;
	p->count = 0;
	p->capacity = 0;
	p->index = 0;
	p->error = NULL;
	p->status = PARSER_PENDING;
	p->value.type = TYPE_NIL;
	parser_push(p, PTYPE_ROOT);
}
