#ifndef AMEX_OBJECT_H
#define AMEX_OBJECT_H

#include "common.h"

/* Object Related */
typedef enum {
	OBJ_STRING,
	OBJ_BUFFER,
	OBJ_UPVALUE,
	OBJ_ARRAY,
	OBJ_TABLE,
	OBJ_FUNCTION,
	OBJ_CLOSURE,
} ObjType;

/* forward declaration for GCObject */
typedef struct GCObject		GCObject ;
/* linked GCObject, used as type punning, and for GC */
struct GCObject {
	GCObject		*next;
	ObjType			type;
	bool			is_marked;
};

#endif /* AMEX_OBJECT_H */
