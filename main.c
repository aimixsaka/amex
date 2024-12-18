#include <editline/history.h>
#include <editline/readline.h>

#include "common.h"

#define PROMPT_STRING	"amex> "

static void repl()
{
	puts("Amex version 0.1.0\n");
	puts("Press Ctrl+d to exit\n");

	for (;;) {
		char *input = readline(PROMPT_STRING);

		if (!input) {
			printf("\n");
			return 0;
		}

		add_history(input);

		run(input);

		free(input);
	}
}

static void run_file(const char *path)
{
	
}

int main(int argc, const char *argv[])
{
	if (argc == 1) {
		repl();
	} else if (argc == 2) {
		run_file(read_file(argv[1]);
	} else {
		fprintf(stderr, "Usage: amex [path]\n");
		exit(64);
	}

	return 0;
}
