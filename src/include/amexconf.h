#ifndef AMEXCONF_H_defined
#define AMEXCONF_H_defined

#define AMEX_LANG_MAJOR_VERSION		0
#define AMEX_LANG_MINOR_VERSION		1
#define AMEX_LANG_PATCH_VERSION		0

#define AMEX_LANG_VERSION_STRING	"0.1.0"


#define AMEX_PROMPT_STRING		"> "
#define AMEX_REPL_LINE_MAX_LENGTH	2048

/* ---------- Object related ----- */
#define TABLE_MAX_LOAD			0.75


/* ---------- Compiler related ----- */
#define UINT8_COUNT			UINT8_MAX + 1
#define MAX_QUOTE_LEVEL			10


/* ---------- VM related --------- */
/* max call depth */
#define FRAMES_MAX			1024
#define STACK_MAX			4096

#endif /* #define AMEXCONF_H_defined */
