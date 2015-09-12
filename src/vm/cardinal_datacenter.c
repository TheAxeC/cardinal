#include "cardinal_datacenter.h"

#include <string.h>
#include <stdio.h>

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

DEF_NATIVE(ptr_toString)
	char buffer[24];
	int length = sprintf(buffer, "[pointer %p]", AS_POINTER(args[0]));
	RETURN_VAL(cardinalNewString(vm, buffer, length));
END_NATIVE

DEF_NATIVE(ptr_kill)
	Obj* ptr = (Obj*) AS_POINTER(args[0]);
	cardinalFreeObjContent(vm, ptr);
	ptr->classObj = vm->metatable.nullClass;
	RETURN_PTR(AS_OBJ(args[0]));
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

DEF_NATIVE(ptr_vrealloc)
	if (IS_POINTER(args[1]) && IS_NUM(args[2])) {
		void* ptr = AS_POINTER(args[1]);
		double size = AS_NUM(args[2]);
		RETURN_PTR(realloc(ptr, (int) size *  sizeof(Value) ));
	} else {
		RETURN_VAL(args[0]);
	}
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

DEF_NATIVE(ptr_getSingleValue)
	void* ptr = AS_POINTER(args[0]);
	RETURN_VAL( ( (Value*) ptr)[0] );
END_NATIVE

DEF_NATIVE(ptr_setSingleValue)
	void* ptr = AS_POINTER(args[0]);
	( (Value*) ptr)[0] = args[1];
	RETURN_VAL(args[0]);
END_NATIVE

#define DEF_GET(type, name)		DEF_NATIVE(ptr_##name) \
							void* ptr = AS_POINTER(args[0]); \
							int ind = (double) AS_NUM(args[1]); \
							RETURN_NUM( ( (type*) ptr)[ind] ); \
						END_NATIVE
						
#define DEF_SET(type, name)		DEF_NATIVE(ptr_set##name) \
							void* ptr = AS_POINTER(args[0]); \
							int ind = (double) AS_NUM(args[1]); \
							( (type*) ptr)[ind] = (type) AS_NUM(args[2]); \
							RETURN_VAL(args[0]); \
						END_NATIVE
						
#define DEF_GETTER(type, name)		DEF_NATIVE(ptr_getSingle##name) \
							void* ptr = AS_POINTER(args[0]); \
							RETURN_NUM( ( (type*) ptr)[0] ); \
						END_NATIVE
						
#define DEF_SETTER(type, name)		DEF_NATIVE(ptr_setSingle##name) \
							void* ptr = AS_POINTER(args[0]); \
							( (type*) ptr)[0] = (type) AS_NUM(args[1]); \
							RETURN_VAL(args[0]); \
						END_NATIVE

DEF_GET(cardinalByte, byte);
DEF_SET(cardinalByte, byte);

DEF_GET(cardinalShort, short);
DEF_SET(cardinalShort, short);

DEF_GET(cardinalInt, int);
DEF_SET(cardinalInt, int);

DEF_GET(cardinalLong, long);
DEF_SET(cardinalLong, long);

DEF_GET(cardinalsByte, sbyte);
DEF_SET(cardinalsByte, sbyte);

DEF_GET(cardinalsShort, sshort);
DEF_SET(cardinalsShort, sshort);

DEF_GET(cardinalsInt, sint);
DEF_SET(cardinalsInt, sint);

DEF_GET(cardinalsLong, slong);
DEF_SET(cardinalsLong, slong);

DEF_GETTER(cardinalByte, byte);
DEF_SETTER(cardinalByte, byte);

DEF_GETTER(cardinalShort, short);
DEF_SETTER(cardinalShort, short);

DEF_GETTER(cardinalInt, int);
DEF_SETTER(cardinalInt, int);

DEF_GETTER(cardinalLong, long);
DEF_SETTER(cardinalLong, long);

DEF_GETTER(cardinalsByte, sbyte);
DEF_SETTER(cardinalsByte, sbyte);

DEF_GETTER(cardinalsShort, sshort);
DEF_SETTER(cardinalsShort, sshort);

DEF_GETTER(cardinalsInt, sint);
DEF_SETTER(cardinalsInt, sint);

DEF_GETTER(cardinalsLong, slong);
DEF_SETTER(cardinalsLong, slong);

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

DEF_NATIVE(object_getAddress)
	RETURN_PTR(AS_OBJ(args[0]));
END_NATIVE

DEF_NATIVE(object_delete)
	Obj* ptr = AS_OBJ(args[0]);
	cardinalFreeObjContent(vm, ptr);
	ptr->classObj = vm->metatable.nullClass;
	RETURN_NULL;
END_NATIVE

DEF_NATIVE(object_transfer)
	cardinalRemoveGCObject(vm, AS_OBJ(args[0]));
	RETURN_PTR(AS_OBJ(args[0]));
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// Methods
///////////////////////////////////////////////////////////////////////////////////

void bindPointerClass(CardinalVM* vm) {
	vm->metatable.pointerClass = AS_CLASS(cardinalFindVariable(vm, "Memory"));
	
	// Get the memory for objects
	NATIVE(vm->metatable.pointerClass, "*", ptr_get);
	NATIVE(vm->metatable.pointerClass, "kill()", ptr_kill);
	
	// Memory allocation
	NATIVE(vm->metatable.pointerClass->obj.classObj, "malloc(_)", ptr_malloc);
	NATIVE(vm->metatable.pointerClass->obj.classObj, "realloc(_,_)", ptr_realloc);
	NATIVE(vm->metatable.pointerClass->obj.classObj, "free(_)", ptr_dealloc);
	NATIVE(vm->metatable.pointerClass->obj.classObj, "valloc(_)", ptr_valloc);	
	NATIVE(vm->metatable.pointerClass->obj.classObj, "vrealloc(_)", ptr_vrealloc);
	
	NATIVE(vm->metatable.pointerClass, "[_]", ptr_subscript);
	NATIVE(vm->metatable.pointerClass, "[_]=(_)", ptr_subscriptSetter);
	NATIVE(vm->metatable.pointerClass, "toString", ptr_toString);
	
	// Used to write to the memory
	NATIVE(vm->metatable.pointerClass, "i8(_)", ptr_sbyte);
	NATIVE(vm->metatable.pointerClass, "i8(_,_)", ptr_setsbyte);
	NATIVE(vm->metatable.pointerClass, "ui8(_)", ptr_byte);
	NATIVE(vm->metatable.pointerClass, "ui8(_,_)", ptr_setbyte);
	NATIVE(vm->metatable.pointerClass, "i16(_)", ptr_sshort);
	NATIVE(vm->metatable.pointerClass, "i16(_,_)", ptr_setsshort);
	NATIVE(vm->metatable.pointerClass, "ui16(_)", ptr_short);
	NATIVE(vm->metatable.pointerClass, "ui16(_,_)", ptr_setshort);
	NATIVE(vm->metatable.pointerClass, "i32(_)", ptr_sint);
	NATIVE(vm->metatable.pointerClass, "i32(_,_)", ptr_setsint);
	NATIVE(vm->metatable.pointerClass, "ui32(_)", ptr_int);
	NATIVE(vm->metatable.pointerClass, "ui32(_,_)", ptr_setint);
	NATIVE(vm->metatable.pointerClass, "i64(_)", ptr_slong);
	NATIVE(vm->metatable.pointerClass, "i64(_,_)", ptr_setslong);
	NATIVE(vm->metatable.pointerClass, "ui64(_)", ptr_long);
	NATIVE(vm->metatable.pointerClass, "ui64(_,_)", ptr_setlong);
	
	NATIVE(vm->metatable.pointerClass, "i8", ptr_getSinglesbyte);
	NATIVE(vm->metatable.pointerClass, "i8=(_)", ptr_setSinglesbyte);
	NATIVE(vm->metatable.pointerClass, "ui8", ptr_getSinglebyte);
	NATIVE(vm->metatable.pointerClass, "ui8=(_)", ptr_setSinglebyte);
	NATIVE(vm->metatable.pointerClass, "i16", ptr_getSinglesshort);
	NATIVE(vm->metatable.pointerClass, "i16=(_)", ptr_setSinglesshort);
	NATIVE(vm->metatable.pointerClass, "ui16", ptr_getSingleshort);
	NATIVE(vm->metatable.pointerClass, "ui16=(_)", ptr_setSingleshort);
	NATIVE(vm->metatable.pointerClass, "i32", ptr_getSinglesint);
	NATIVE(vm->metatable.pointerClass, "i32=(_)", ptr_setSinglesint);
	NATIVE(vm->metatable.pointerClass, "ui32", ptr_getSingleint);
	NATIVE(vm->metatable.pointerClass, "ui32=(_)", ptr_setSingleint);
	NATIVE(vm->metatable.pointerClass, "i64", ptr_getSingleslong);
	NATIVE(vm->metatable.pointerClass, "i64=(_)", ptr_setSingleslong);
	NATIVE(vm->metatable.pointerClass, "ui64", ptr_getSinglelong);
	NATIVE(vm->metatable.pointerClass, "ui64=(_)", ptr_setSinglelong);
	
	NATIVE(vm->metatable.pointerClass, "value(_)", ptr_subscript);
	NATIVE(vm->metatable.pointerClass, "value(_,_)", ptr_subscriptSetter);
	NATIVE(vm->metatable.pointerClass, "value", ptr_getSingleValue);
	NATIVE(vm->metatable.pointerClass, "value=(_)", ptr_setSingleValue);
	
	// Something to save objects (read/write) (not values) (inline objects)
	
	
	// Something to reuse the object memory
	
	
	// Arithmetic on pointers
	
	
	// Manipulation
	NATIVE(vm->metatable.pointerClass, "==(_)", ptr_eqeq);
	NATIVE(vm->metatable.pointerClass, "!=(_)", ptr_bangeq);
}

void cardinalInitialiseManualMemoryManagement(CardinalVM* vm) {
	NATIVE(vm->metatable.objectClass, "decoupleGC()", object_unplug);
	NATIVE(vm->metatable.objectClass, "coupleToGC()", object_plugin);
	NATIVE(vm->metatable.objectClass, "&", object_getAddress);
	NATIVE(vm->metatable.objectClass, "delete()", object_delete);
	NATIVE(vm->metatable.objectClass, "transfer()", object_transfer);
	
	// Add sizeof to all classes
	
}

// The method binds the DataCenter to the VM 
// The Data Center can be used to store elements
void cardinalInitializeDataCenter(CardinalVM* vm) {
	cardinalInterpret(vm, "", libSource);

	bindPointerClass(vm);
}