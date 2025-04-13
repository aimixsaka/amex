#ifndef AMEX_DEBUG_H
#define AMEX_DEBUG_H

#include "include/amex.h"

#ifdef DEBUG
#define	DEBUG_TRACE_EXECUTION
#define DEBUG_PRINT_CODE
/*
 * if this flag on, gc start at every `reallocate`,
 * bad for performace, good for debug gc
 * related memory management butgs.
 */
#endif /* DEBUG */

#ifdef DEBUG_GC
#define	DEBUG_TRACE_EXECUTION
#define DEBUG_PRINT_CODE
#define DEBUG_STRESS_GC
#define DEBUG_LOG_GC
#endif /* DEBUG_GC */

void disassemble_chunk(Chunk *chunk, const char *name);
int disassemble_instruction(Chunk *chunk, int offset);


#endif /* AMEX_DEBUG_H */
