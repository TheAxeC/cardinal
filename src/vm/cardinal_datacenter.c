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
// Return an object
#define RETURN_PTR(ptr)     RETURN_VAL(PTR_VAL(ptr))
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
"class Memory {}\n";


///////////////////////////////////////////////////////////////////////////////////
//// Memory
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(ptr_get)
	if (IS_POINTER(args[0])) {
		void* ptr = AS_POINTER(args[0]);
		RETURN_OBJ( ((Obj*) ptr) );
	} else {
		RETURN_VAL(args[0]);
	}
END_NATIVE

DEF_NATIVE(ptr_realloc)
	if (IS_POINTER(args[1]) && IS_NUM(args[2])) {
		void* ptr = AS_POINTER(args[1]);
		double size = AS_NUM(args[2]);
		RETURN_PTR(realloc(ptr, size));
	} else {
		RETURN_VAL(args[0]);
	}
END_NATIVE

DEF_NATIVE(ptr_malloc)
	double size = AS_NUM(args[1]);
	RETURN_PTR(malloc((int) size));
END_NATIVE

DEF_NATIVE(ptr_valloc)
	double size = AS_NUM(args[1]);
	RETURN_PTR(malloc((int) size * sizeof(Value)) );
END_NATIVE

DEF_NATIVE(ptr_dealloc)
	if (IS_POINTER(args[1])) {
		free(AS_POINTER(args[1]));
		RETURN_PTR(NULL);
	} else {
		RETURN_VAL(args[0]);
	}
END_NATIVE

DEF_NATIVE(ptr_eqeq)
	if (!IS_POINTER(args[1])) RETURN_FALSE;
	RETURN_BOOL(AS_POINTER(args[0]) == AS_POINTER(args[1]));
END_NATIVE

DEF_NATIVE(ptr_bangeq)
	if (!IS_POINTER(args[1])) RETURN_TRUE;
	RETURN_BOOL(AS_POINTER(args[0]) != AS_POINTER(args[1]));
END_NATIVE

DEF_NATIVE(ptr_subscript)
	void* ptr = AS_POINTER(args[0]);
	int ind = (double) AS_NUM(args[1]);
	RETURN_VAL( ( (Value*) ptr)[ind] );
END_NATIVE


DEF_NATIVE(ptr_subscriptSetter)
	void* ptr = AS_POINTER(args[0]);
	int ind = (double) AS_NUM(args[1]);
	( (Value*) ptr)[ind] = args[2];
	RETURN_VAL(args[0]);
END_NATIVE


///////////////////////////////////////////////////////////////////////////////////
//// OBJECT
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(object_unplug)
	if (IS_OBJ(args[0])) {
		Obj* obj = AS_OBJ(args[0]);
		cardinalRemoveGCObject(vm, obj);
	}
	RETURN_VAL(args[0]);
END_NATIVE

DEF_NATIVE(object_plugin)
	if (IS_OBJ(args[0])) {
		Obj* obj = AS_OBJ(args[0]);
		cardinalAddGCObject(vm, obj);
	}
	RETURN_VAL(args[0]);
END_NATIVE

DEF_NATIVE(object_delete)
	if (IS_OBJ(args[0])) {
		Obj* obj = AS_OBJ(args[0]);
		cardinalRemoveGCObject(vm, obj);
		cardinalFreeObj(vm, obj);
		RETURN_NULL;
	} else {
		RETURN_VAL(args[0]);
	}
END_NATIVE

DEF_NATIVE(object_getAddress)
	RETURN_PTR(AS_OBJ(args[0]));
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// Methods
///////////////////////////////////////////////////////////////////////////////////

void bindPointerClass(CardinalVM* vm) {
	vm->metatable.pointerClass = AS_CLASS(cardinalFindVariable(vm, "Memory"));
	
	// Get the memory
	NATIVE(vm->metatable.pointerClass, "*", ptr_get);
	
	// Memory allocation
	NATIVE(vm->metatable.pointerClass->obj.classObj, "malloc(_)", ptr_malloc);
	NATIVE(vm->metatable.pointerClass->obj.classObj, "realloc(_,_)", ptr_realloc);
	NATIVE(vm->metatable.pointerClass->obj.classObj, "free(_)", ptr_dealloc);
	NATIVE(vm->metatable.pointerClass->obj.classObj, "valloc(_)", ptr_valloc);
	
	NATIVE(vm->metatable.pointerClass, "[_]", ptr_subscript);
	NATIVE(vm->metatable.pointerClass, "[_]=(_)", ptr_subscriptSetter);
	
	// Used to write to the memory
	NATIVE(vm->metatable.pointerClass, "i8(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "i8(_,_)", ptr_byteset);
	NATIVE(vm->metatable.pointerClass, "ui8(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "ui8(_,_)", ptr_byteset);
	NATIVE(vm->metatable.pointerClass, "i16(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "i16(_,_)", ptr_byteset);
	NATIVE(vm->metatable.pointerClass, "ui16(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "ui16(_,_)", ptr_byteset);
	NATIVE(vm->metatable.pointerClass, "i32(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "i32(_,_)", ptr_byteset);
	NATIVE(vm->metatable.pointerClass, "ui32(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "ui32(_,_)", ptr_byteset);
	NATIVE(vm->metatable.pointerClass, "i64(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "i64(_,_)", ptr_byteset);
	NATIVE(vm->metatable.pointerClass, "ui64(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "ui64(_,_)", ptr_byteset);
	NATIVE(vm->metatable.pointerClass, "value(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "value(_,_)", ptr_byte);
	
	// For all different types	
	NATIVE(vm->metatable.pointerClass, "getNum(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "setNum(_,_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "getBool(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "setBool(_,_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "getPtr(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "setPtr(_,)", ptr_byte);
	
	
	
	// Manipulation
	NATIVE(vm->metatable.pointerClass, "==(_)", ptr_eqeq);
	NATIVE(vm->metatable.pointerClass, "!=(_)", ptr_bangeq);

}

static void system_decoupleGC(CardinalVM* vm) {
	vm->garbageCollector.isCoupled = false;
}

static void system_coupleGC(CardinalVM* vm) {
	vm->garbageCollector.isCoupled = true;
}

// This methods allows the decoupling and recoupling of objects
// to the Garbage collector
void bindMemoryManagementSystem(CardinalVM* vm) {
	cardinalDefineStaticMethod(vm, NULL, "System", "decoupleGC()", system_decoupleGC);
	cardinalDefineStaticMethod(vm, NULL, "System", "coupleGC()", system_coupleGC);
}

void cardinalInitialiseManualMemoryManagement(CardinalVM* vm) {
	NATIVE(vm->metatable.objectClass, "decoupleGC()", object_unplug);
	NATIVE(vm->metatable.objectClass, "coupleToGC()", object_plugin);
	NATIVE(vm->metatable.objectClass, "delete()", object_delete);
	NATIVE(vm->metatable.objectClass, "&", object_getAddress);
	
	// Add sizeof to all classes
}

// The method binds the DataCenter to the VM 
// The Data Center can be used to store elements
void cardinalInitializeDataCenter(CardinalVM* vm) {
	cardinalInterpret(vm, "", libSource);

	bindPointerClass(vm);
}