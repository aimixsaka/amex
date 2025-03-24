#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "corelib.h"
#include "parser.h"
#include "str.h"
#include "value.h"
#include "include/amexconf.h"
#include "vm.h"
#include "debug.h"


static void repl()
{
	Parser p;
	Function *f;
	char buffer[AMEX_REPL_LINE_MAX_LENGTH] = { 0 };
	const char *reader;

	puts("Amex " AMEX_LANG_VERSION_STRING);
	puts("Press Ctrl+d to exit.");
	init_vm();
	set_vm_globals(core_env(NULL));
	for (;;) {
		/* Flush the input buffer */
		buffer[0] = '\0';
		reader = buffer;
		init_parser(&p);
		/*
		 * Parse until we got a full form, 
		 * which means we allow multi-line expression(typical lisp way).
		 */
		while (p.status == PARSER_PENDING || p.status == PARSER_EOF) {
			/* Read line again after we processed a line */
			if (*reader == '\0') {
				printf(AMEX_PROMPT_STRING);
				if (!fgets(buffer, sizeof(buffer), stdin)) {
					goto on_error;
				}
				p.index = 0;
				reader = buffer;
			}
			reader += parse_cstring(&p, reader);
		}

		if (p.error && p.status == PARSER_ERROR) {
			unsigned i;
			printf("\n");
			printf("%s\n", buffer);
			for (i = 0; i < p.index; ++i)
				printf(" ");
			printf("^\n");
			printf("\nParse error: %s\n", p.error);
			free_parser(&p);
			continue;
		}

		/* Compile to Function(which contains bytecode) */
		f = compile(p.value);
		if (f == NULL)
			continue;

		/* Execute bytecode in VM */
		InterpretResult res = interpret(f);
		if (res.status == INTERPRET_OK)
			print_value(&res.ret, "\n");
	}
on_error:
	free_parser(&p);
	free_vm();
}

static char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	if (f == NULL) {
		fprintf(stderr, "Could not open file \"%s\".\n", path);
		exit(74);
	}

	/* idiom to get file size in bytes */
	fseek(f, 0L, SEEK_END);
	size_t fsize = ftell(f);
	rewind(f);

	char *buf = (char *)malloc(fsize + 1);
	if (buf == NULL) {
		fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
		exit(74);
	}
	size_t bytes_read = fread(buf, sizeof(char), fsize, f);
	if (bytes_read < fsize) {
		fprintf(stderr, "Could not read file \"%s\".\n", path);
		exit(74);
	}
	/* use trailing EOF as sentinel */
	buf[bytes_read] = '\0';

	fclose(f);
	return buf;
}

static void run_file(const char *path)
{
	char *source = read_file(path);

	Parser p;
	Function *f;
	const char *reader = source;


	init_vm();
	set_vm_globals(core_env(NULL));
	for (;;) {
		init_parser(&p);
		/*
		 * Parse until we got a full form, 
		 * which means we allow multi-line expression(typical lisp way).
		 */
		while (p.status == PARSER_PENDING) {
			reader += parse_cstring(&p, reader);
		}

		if (p.error) {
			// unsigned i;
			// printf("\n");
			// printf("%s\n", buffer);
			// for (i = 0; i < p.index; ++i)
			// 	printf(" ");
			// printf("^\n");
			// printf("\nParse error: %s\n", p.error);
			
			printf("Parse error: %s\n", p.error);
			free_parser(&p);
			exit(65);
		}
		if (*reader == '\0')
			break;

		/* Compile to Function(which contains bytecode) */
#ifdef DEBUG_TRACE_EXECUTION
		puts("**** dump ast start ****");
		print_value(&p.value, "\n");
		puts("**** dump ast end ****");
#endif /* DEBUG_TRACE_EXECUTION */
		f = compile(p.value);
		if (f == NULL)
			exit(66);

		/* Execute bytecode in VM */
		InterpretResult res = interpret(f);
		if (res.status == INTERPRET_RUNTIME_ERROR)
			exit(67);
	}
	free(source);
	free_vm();
}

int main(int argc, const char *argv[])
{
	if (argc == 1) {
		repl();
	} else if (argc == 2) {
		run_file(argv[1]);
	} else {
		fprintf(stderr, "Usage: amex [path]\n");
		exit(64);
	}

	return 0;
}
