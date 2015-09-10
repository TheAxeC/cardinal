#include "cardinal_datacenter.h"

#include <string.h>
#include "cardinal_config.h"
#include "cardinal_value.h"
#include "cardinal_debug.h"

// Binds a native method named [name] implemented using C function
// [fn] to `ObjClass` [cls].
#define NATIVE(cls, name, func) \
	{ \
		int symbol = cardinalSymbolTableEnsure(vm, &vm->methodNames, name, strlen(name)); \
		Method method; \
		method.type = METHOD_PRIMITIVE; \
		method.fn.primitive = native_##func; \
		cardinalBindMethod(vm, cls, symbol, method); \
	}

// Return a single value from a function
#define RETURN_VAL(value) {args[0] = value; return PRIM_VALUE;}

// Return an object
#define RETURN_OBJ(obj)     RETURN_VAL(OBJ_VAL(obj))
// Return a bool
#define RETURN_BOOL(value)  RETURN_VAL(BOOL_VAL(value))
// Return a number
#define RETURN_NUM(value)   RETURN_VAL(NUM_VAL(value))
// Return an integer
#define RETURN_INT(value)	RETURN_VAL(INT_VAL(value))

// Return the NULL value
#define RETURN_NULL         RETURN_VAL(NULL_VAL)

// Return the constant [False]
#define RETURN_FALSE        RETURN_VAL(FALSE_VAL)
// Return the constant [True]
#define RETURN_TRUE         RETURN_VAL(TRUE_VAL)

// Defines a native method whose C function name is [native]. This abstracts
// the actual type signature of a native function and makes it clear which C
// functions are intended to be invoked as natives.
#define DEF_NATIVE(native) \
	static PrimitiveResult native_##native(CardinalVM* vm, ObjFiber* fiber, Value* args, int* numargs) { \
		UNUSED(vm); \
		UNUSED(fiber); \
		UNUSED(args); \
		UNUSED(numargs);
		
#define END_NATIVE }

#define RETURN_ERROR(msg) \
	do { \
		args[0] = cardinalNewString(vm, msg, strlen(msg)); \
		return PRIM_ERROR; \
	} while (0);

static const char* libSource =
"class Ptr {}\n";

DEF_NATIVE(ptr_new)
	RETURN_OBJ(cardinalNewPointer(vm));
END_NATIVE

DEF_NATIVE(ptr_writeAt)
END_NATIVE

DEF_NATIVE(ptr_write)
END_NATIVE

DEF_NATIVE(ptr_get)
END_NATIVE

DEF_NATIVE(ptr_getAt)
END_NATIVE

DEF_NATIVE(ptr_malloc)
	ObjPointer* ptr = AS_POINTER(args[0]);
	double size = AS_NUM(args[1]);

	ptr->memory = malloc((int) size);
	RETURN_OBJ(ptr);
END_NATIVE

DEF_NATIVE(ptr_dealloc)
	ObjPointer* ptr = AS_POINTER(args[0]);
	
	free(ptr->memory);
	ptr->memory = NULL;
	
	RETURN_OBJ(ptr);
END_NATIVE

void bindPointerClass(CardinalVM* vm) {
	vm->metatable.pointerClass = AS_CLASS(cardinalFindVariable(vm, "Ptr"));
	NATIVE(vm->metatable.pointerClass->obj.classObj, "new", ptr_new);
	NATIVE(vm->metatable.pointerClass->obj.classObj, "make", ptr_new);
	NATIVE(vm->metatable.pointerClass, "set()", ptr_write);
	NATIVE(vm->metatable.pointerClass, "setAt(_)", ptr_writeAt);
	NATIVE(vm->metatable.pointerClass, "get()", ptr_get);
	NATIVE(vm->metatable.pointerClass, "getAt(_)", ptr_getAt);
	NATIVE(vm->metatable.pointerClass, "malloc(_)", ptr_malloc);
	//NATIVE(vm->metatable.pointerClass, "realloc(_)", ptr_get);
	NATIVE(vm->metatable.pointerClass, "free()", ptr_dealloc);
	
}

// The method binds the DataCenter to the VM 
// The Data Center can be used to store elements
void cardinalInitializeDataCenter(CardinalVM* vm) {
	cardinalInterpret(vm, "", libSource);

	bindPointerClass(vm);
}