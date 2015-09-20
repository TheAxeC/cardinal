#ifndef cardinal_value_h
#define cardinal_value_h

#include <stdbool.h>

#include "cardinal_utils.h"

// This defines the built-in types and their core representations in memory.
// Since Udog is dynamically typed, any variable can hold a value of any type,
// and the type can change at runtime. Implementing this efficiently is
// critical for performance.
//
// The main type exposed by this is [Value]. A C variable of that type is a
// storage location that can hold any Cardinal value. The stack, global variables,
// and instance fields are all implemented in C as variables of type Value.
//
// The built-in types for booleans, numbers, integers and null are unboxed: their value
// is stored directly in the Value, and copying a Value copies the value. Other
// types--classes, instances of classes, functions, lists, and strings--are all
// reference types. They are stored on the heap and the Value just stores a
// pointer to it. Copying the Value copies a reference to the same object. The
// Cardinal implementation calls these "Obj", or objects, though to a user, all
// values are objects.
//
// There are two supported Value representations. The main one uses a technique
// called "NaN tagging" (explained in detail below) to store a number, any of
// the value types, or a pointer all inside a single double-precision floating
// point value. A larger, slower, Value type that uses a struct to store these
// is also supported, and is useful for debugging the VM.
//
// The representation is controlled by the `NAN_TAGGING` define. If that's
// defined, Nan tagging is used.

//Use 64 bit variables, or use 32 bit variables
#ifdef CARDINAL_BITPLATFORM_64
	typedef double cardinal_number;
	typedef int64_t cardinal_integer;
	typedef uint64_t cardinal_uinteger;
#else
	typedef float cardinal_number;
	typedef int32_t cardinal_integer;
	typedef uint32_t cardinal_uinteger;
#endif

#define EPSILON 0.000000001

typedef uint8_t cardinalByte;
typedef uint16_t cardinalShort;
typedef uint32_t cardinalInt;
typedef uint64_t cardinalLong;

typedef int8_t cardinalsByte;
typedef int16_t cardinalsShort;
typedef int32_t cardinalsInt;
typedef int64_t cardinalsLong;

#define EXTENDS(name)

// Typedef for the instruction ptr
// Used for clarification
typedef uint8_t programcounter;

// Pointer to bytecode data
typedef uint8_t bytecodeData;

// Different flags for the garbage collector
// A tri-color algorithm is used for the GC
typedef enum GCFlag {
	GC_GRAY,
	GC_BLACK,
	GC_WHITE,
	GC_NONE,
	// The object has been marked during the mark phase of GC.
	FLAG_MARKED = 0x01
} GCFlag;	

// Different object types supported by the VM
typedef enum ObjType {
	// Class prototype
	// Contains all methods and other 
	// references needed by all instances of the class
	OBJ_CLASS,
	// A first class function that has captured upvalues
	OBJ_CLOSURE,
	// Used for threading
	OBJ_FIBER,
	// First class function object
	OBJ_FN,
	// Instance of a class (object)
	OBJ_INSTANCE,
	// List class 
	OBJ_LIST,
	// String class
	OBJ_STRING,
	// Upvalue class
	OBJ_UPVALUE,
	// Range class
	OBJ_RANGE,
	// HashTable class
	OBJ_TABLE,
	// HashTable element
	OBJ_TABLE_ELEM,
	// Hashmap
	OBJ_MAP,
	// Module
	OBJ_MODULE,
	// Method
	OBJ_METHOD,
	// Dead object
	OBJ_DEAD
} ObjType;

typedef struct ObjClass ObjClass;

/// The raw object type
/// Holds data concerning it's type
/// Has extra data used for the GC
typedef struct rawObj {
	/// Flag for the GC
	GCFlag gcflag;
	
	/// Type of the object
	ObjType type;
	
	/// metatable containing the method-data from some object
	ObjClass* classObj;
	
	/// next object in the gc list
	struct rawObj* next;
	/// previous obj in the gc list
	struct rawObj* prev;
} Obj;

#if CARDINAL_NAN_TAGGING
	typedef uint64_t Value;
#else
	// Different value types supported by the VM
	// Boolean types and the NULL value only exist within the type
	typedef enum ValueType {
		VAL_FALSE,
		VAL_TRUE,
		VAL_NULL,
		VAL_NUM,
		VAL_POINTER,
		VAL_UNDEFINED,
		VAL_OBJ
	} ValueType;

	/// Unboxed values in an union
	/// Supported types by Cardinal:
	///		- Real numbers (floating point)
	///		- Integer numbers
	///		- Garbage collected objects
	/// Cardinal supports first class objects and first class functions
	typedef union ValueUnion {
		/// Real number
		cardinal_number num;
		/// GC object
		Obj* obj;
	} ValueUnion;

	/// The actual value struct used by the VM
	/// Contains the type of the object and the value
	typedef struct Value {
		/// Type of the value
		ValueType type;
		/// The actual data stored in a union
		ValueUnion value;
	} Value;
#endif

/// Type used to expose values to the API
struct CardinalValue {
	/// The exposed key of the value stored in the host application
	int value;
};

DECLARE_BUFFER(Value, Value);
DECLARE_BUFFER(ValuePtr, Value*);

/// OBJECT
/// A string class
typedef struct ObjString { EXTENDS(Obj) 
	/// Parent
	Obj obj;

	/// The length of the string
	int length;
	
	/// The hash value of the string's contents.
	uint32_t hash;
	
	/// The contained c-string;
	char value[FLEXIBLE_ARRAY];
} ObjString;

/// OBJECT
/// The dynamically allocated data structure for a variable that has been used
/// by a closure. Whenever a function accesses a variable declared in an
/// enclosing function, it will get to it through this.
///
/// An upvalue can be either "closed" or "open". An open upvalue points directly
/// to a [Value] that is still stored on the fiber's stack because the local
/// variable is still in scope in the function where it's declared.
///
/// When that local variable goes out of scope, the upvalue pointing to it will
/// be closed. When that happens, the value gets copied off the stack into the
/// upvalue itself. That way, it can have a longer lifetime than the stack
/// variable.
typedef struct Upvalue { EXTENDS(Obj) 
	/// Parent
	Obj obj;
	/// Pointer to the variable this upvalue is referencing.
	Value* value;

	/// If the upvalue is closed (i.e. the local variable it was pointing too has
	/// been popped off the stack) then the closed-over value will be hoisted out
	/// of the stack into here. [value] will then be changed to point to this.
	Value closed;

	/// Open upvalues are stored in a linked list by the fiber. This points to the
	/// next upvalue in that list.
	struct Upvalue* next;
} Upvalue;

/// Typedef for the top of the stack
/// Used for clarification
typedef Value* stackTop;

/// Callframe for the fiber
/// A CallFrame is a function that has been called by a fiber
typedef struct CallFrame {
	/// Current instruction for this callframe
	programcounter* pc;
	
	/// Function or closure that is currently being executed
	Obj* fn;
	
	/// Top of the stack
	stackTop top;	
} CallFrame;

// Different results that can be returned from methods
// Enumeration used by methods
typedef enum PrimitiveResult {
	// A normal value has been returned.
	PRIM_VALUE,
	
	// A runtime error occurred.
	PRIM_ERROR,
	
	// A new callframe has been pushed.
	PRIM_CALL,
	
	// A fiber is being switched to.
	PRIM_RUN_FIBER,
	
	PRIM_NONE
} PrimitiveResult;

typedef struct ObjFiber ObjFiber;

// Primitive result
typedef PrimitiveResult (*Primitive)(CardinalVM* vm, ObjFiber* fiber, Value* args, int* numArgs);

// Different method types that the VM supports
typedef enum MethodType {
  // A primitive method implemented in C in the VM. Unlike foreign methods,
  // this can directly manipulate the fiber's stack.
  METHOD_PRIMITIVE,

  // A externally-defined C method.
  METHOD_FOREIGN,

  // A normal user-defined method.
  METHOD_BLOCK,
  
  // A method belonging to a superclass
  METHOD_SUPERCLASS,

  // No method for the given symbol.
  METHOD_NONE
} MethodType;

/// Value of a method without a type
/// Methods types:
/// 	- Primitive methods defined by the VM
///		- Methods defined by the user of this scripting language
/// 	- Method defined by the API of the language
typedef union MethodValue {
	/// Primitive method
	Primitive primitive;
	/// Foreign method
	cardinalForeignMethodFn foreign;

	/// May be a [ObjFn] or [ObjClosure].
	Obj* obj;
} MethodValue;

/// A method used by the VM
/// Contains both the type and the value of the method
typedef struct Method {
	/// Type of the method
	MethodType type;

	/// The method function itself. The [type] determines which field of the union
	/// is used.
	MethodValue fn;
} Method;

/// OBJECT
/// Instance of an class
/// Can be a class created in a script
/// Can also be a class or struct bound from c or c++
typedef struct ObjInstance { EXTENDS(Obj)  
	/// Parent
	Obj obj;
	
	/// Stack used to check where to access the correct fields 
	/// when we are within a super class
	CardinalStack stack;
	
	/// All the fields of the instance
	/// This field will be used to store the data used for classes
	/// bound from c++
	/// Enough memory will be allocated to be able to acces the members
	Value* fields; //[FLEXIBLE_ARRAY];
} ObjInstance;

/// OBJECT
/// Fiber object
/// Used for simulating threads
/// A fiber is a particularly lightweight thread of execution
struct ObjFiber { EXTENDS(Obj) 
	/// Parent
	Obj obj;
	
	///Stack used for the allocation of objects on the stack
	Value* stack; //[STACKSIZE];
	
	/// Top of the stack
	stackTop stacktop;

	/// Complete callframe stack
	CallFrame* frames; //[CALLFRAMESIZE];
	
	/// number of frames in use
	int numFrames;

	/// Pointer to the first node in the linked list of open upvalues that are
	/// pointing to values still on the stack. The head of the list will be the
	/// upvalue closest to the top of the stack, and then the list works downwards.
	Upvalue* openUpvalues;

	/// The fiber that ran this one. If this fiber is yielded, control will resume
	/// to this one. May be `NULL`.
	struct ObjFiber* caller;

	/// If the fiber failed because of a runtime error, this will contain the
	/// error message. Otherwise, it will be NULL.
	ObjInstance* error;
	
	/// This will be true if the caller that called this fiber did so using "try".
	/// In that case, if this fiber fails with an error, the error will be given
	/// to the caller.
	bool callerIsTrying;
	
	/// During a foreign function call, this will point to the first argument (the
	/// receiver) of the call on the fiber's stack.
	Value* foreignCallSlot;

	/// During a foreign function call, this will contain the number of arguments
	/// to the function.
	int foreignCallNumArgs;
	
	/// Size of the stack
	size_t stacksize;
	
	/// Size of the callframe
	size_t framesize;

	/// Is set when the fiber is yielded
	bool yielded;
	
	/// Root directory of this Fiber
	ObjString* rootDirectory;
};

/// Stores debugging information for a function used for things like stack
/// traces.
typedef struct FnDebug {
	/// The name of the function. Heap allocated and owned by the ObjFn.
	char* name;

	/// The name of the source file where this function was defined. An [ObjString]
	/// because this will be shared among all functions defined in the same file.
	ObjString* sourcePath;

	/// An array of line numbers. There is one element in this array for each
	/// bytecode in the function's bytecode array. The value of that element is
	/// the line in the source code that generated that instruction.
	int* sourceLines;

	/// An array of strings. Each string corresponds to the line in sourceLines
	SymbolTable lines;

	/// An array of local variable names
	SymbolTable locals;
  
} FnDebug;

typedef struct ObjFn ObjFn;

/// OBJECT
/// A loaded module and the top-level variables it defines.
typedef struct ObjModule { EXTENDS(Obj) 
	/// Parent 
	Obj obj;
	
	/// Name of the module
	ObjString* name;

	/// The currently defined top-level variables.
	ValueBuffer variables;

	/// Symbol table for the names of all module variables. Indexes here directly
	/// correspond to entries in [variables].
	SymbolTable variableNames;
	
	/// Main function for the module
	ObjFn* func;
	
	/// Amount of variables in this module
	int count;
	
	/// Source code for this module
	ObjString* source;
	
} ObjModule;

/// OBJECT
/// A first-class function object. A raw ObjFn can be used and invoked directly
/// if it has no upvalues (i.e. [numUpvalues] is zero). If it does use upvalues,
/// it must be wrapped in an [ObjClosure] first. The compiler is responsible for
/// emitting code to ensure that that happens.
struct ObjFn { EXTENDS(Obj) 
	/// Parent 
	Obj obj;
	
	/// All constants referenced by this function
	Value* constants;
	/// Pointer to the bytecode
	bytecodeData* bytecode;
	/// The module where this function was defined.
	ObjModule* module;
	/// number of upvalues
	int numUpvalues;
	/// number of constants
	int numConstants;

	/// length of the bytecode
	int bytecodeLength;

	/// The number of parameters this function expects. Used to ensure that .call
	/// handles a mismatch between number of parameters and arguments. This will
	/// only be set for fns, and not ObjFns that represent methods or scripts.
	int numParams;
	
	/// Debug data
	FnDebug* debug;
};

/// OBJECT
/// An instance of a first-class function and the environment it has closed over.
/// Unlike [ObjFn], this has captured the upvalues that the function accesses.
typedef struct ObjClosure { EXTENDS(Obj) 
	/// Parent 
	Obj obj;
	/// The function that this closure is an instance of.
	ObjFn* fn;

	/// The upvalues this function has closed over.
	Upvalue* upvalues[FLEXIBLE_ARRAY];
} ObjClosure;

DECLARE_BUFFER(Method, Method);

/// OBJECT
/// Represents a growable list
typedef struct ObjList { EXTENDS(Obj) 
	/// Parent
	Obj obj;
	
	/// The number of elements allocated.
	int capacity;

	/// The number of items in the list.
	int count;

	/// Pointer to a contiguous array of [capacity] elements.
	Value* elements;
} ObjList;

/// OBJECT
/// Base obj class
/// Is used as a "metatable" for object data
/// Contains data like regular objects (obj)
/// But also holds the number of fields, and references to the methods of the objects
/// Aswell as the name of the object
struct ObjClass { EXTENDS(Obj) 
	/// Parent
	Obj obj;
	
	/// Possible superclasses of this object
	/// This can be NULL is there is no superclass
	ObjList* superclasses;
	
	/// Possible superclass of this object
	/// This can be NULL is there is no superclass
	cardinal_integer superclass;

	/// The number of fields needed for an instance of this class, including all
	/// of its superclass fields.
	int numFields;

	/// The table of methods that are defined in or inherited by this class.
	/// Methods are called by symbol, and the symbol directly maps to an index in
	/// this table. This makes method calls fast at the expense of empty cells in
	/// the list for methods the class doesn't support.
	///
	/// You can think of it as a hash table that never has collisions but has a
	/// really low load factor. Since methods are pretty small (just a type and a
	/// pointer), this should be a worthwhile trade-off.
	MethodBuffer methods;

	/// The name of the class.
	ObjString* name;
	
	/// Destructor method
	cardinalDestructorFn destructor;
};

/// OBJECT
/// A method used by the Scripting language
/// Contains both the type and the value of the method
typedef struct ObjMethod {
	/// Parent
	Obj obj;

	/// Points to the method that has been loaded
	int symbol;
	
	/// Holds the name of the method
	ObjString* name;
	
	/// Holds the caller of the method
	Value caller;
} ObjMethod;

/// OBJECT
/// Indicates a range from - to
typedef struct ObjRange { EXTENDS(Obj)
	/// Parent
	Obj obj;

	/// The beginning of the range.
	double from;

	/// The end of the range. May be greater or less than [from].
	double to;

	/// True if [to] is included in the range.
	bool isInclusive;
} ObjRange;

/// HashValue used for hashtables
typedef struct HashValue { EXTENDS(Obj)
	/// Parent
	Obj obj;
	
	/// The value stored by the hashtable
	Value val;
	/// The key that was used
	Value key;
	/// Pointer to the next value
	struct HashValue* next;
} HashValue;

/// OBJECT
/// A hashmap object
/// This is used to store key-value pairs
typedef struct ObjTable { EXTENDS(Obj)
	/// Parent
	Obj obj;
	
	/// The allocated list
	int capacity;
	
	/// the size of the hashmap
	int count;
	
	/// The hashmap
	HashValue** hashmap;
	
} ObjTable;

/// Entry in the Map
typedef struct MapEntry {
	/// The entry's key, or UNDEFINED_VAL if the entry is not in use.
	Value key;

	/// The value associated with the key. If the key is UNDEFINED_VAL, this will
	/// be false to indicate an open available entry or true to indicate a
	/// tombstone -- an entry that was previously in use but was then deleted.
	Value value;
} MapEntry;

/// OBJECT
/// A hash table mapping keys to values.
///
/// We use something very simple: open addressing with linear probing. The hash
/// table is an array of entries. Each entry is a key-value pair. If the key is
/// the special UNDEFINED_VAL, it indicates no value is currently in that slot.
/// Otherwise, it's a valid key, and the value is the value associated with it.
///
/// When entries are added, the array is dynamically scaled by GROW_FACTOR to
/// keep the number of filled slots under MAP_LOAD_PERCENT. Likewise, if the map
/// gets empty enough, it will be resized to a smaller array. When this happens,
/// all existing entries are rehashed and re-added to the new array.
///
/// When an entry is removed, its slot is replaced with a "tombstone". This is an
/// entry whose key is UNDEFINED_VAL and whose value is TRUE_VAL. When probing
/// for a key, we will continue past tombstones, because the desired key may be
/// found after them if the key that was removed was part of a prior collision.
/// When the array gets resized, all tombstones are discarded.
typedef struct ObjMap { EXTENDS(Obj)
	/// Parent
	Obj obj;

	/// The number of entries allocated.
	uint32_t capacity;

	/// The number of entries in the map.
	uint32_t count;

	/// Pointer to a contiguous array of [capacity] entries.
	MapEntry* entries;
} ObjMap;

///////////////////////////////////////////////////////////////////////////////////
//// SIMPLE DEFINES
///////////////////////////////////////////////////////////////////////////////////

// Value -> ObjClass*.
#define AS_CLASS(value) ((ObjClass*)AS_OBJ(value))

// Value -> ObjClosure*.
#define AS_CLOSURE(value) ((ObjClosure*)AS_OBJ(value))

// Value -> ObjFiber*.
#define AS_FIBER(v) ((ObjFiber*)AS_OBJ(v))

// Value -> ObjFn*.
#define AS_FN(value) ((ObjFn*)AS_OBJ(value))

// Value -> ObjInstance*.
#define AS_INSTANCE(value) ((ObjInstance*)AS_OBJ(value))

// Value -> ObjList*.
#define AS_LIST(value) ((ObjList*)AS_OBJ(value))

// Value -> ObjString*.
#define AS_STRING(v) ((ObjString*)AS_OBJ(v))

// Value -> const char*.
#define AS_CSTRING(v) (AS_STRING(v)->value)

// Value -> ObjRange*.
#define AS_RANGE(v) ((ObjRange*)AS_OBJ(v))

// Value -> ObjTable*
#define AS_TABLE(v) ((ObjTable*)AS_OBJ(v))

// Value -> ObjMap*.
#define AS_MAP(value) ((ObjMap*)AS_OBJ(value))

// Value -> ObjModule*.
#define AS_MODULE(value) ((ObjModule*)AS_OBJ(value))

// Value -> ObjMethod*.
#define AS_METHOD(value) ((ObjMethod*)AS_OBJ(value))

// Convert [boolean] to a boolean [Value].
#define BOOL_VAL(boolean) (boolean ? TRUE_VAL : FALSE_VAL)

///////////////////////////////////////////////////////////////////////////////////
//// NAN TAGGING
///////////////////////////////////////////////////////////////////////////////////

// An IEEE 754 double-precision float is a 64-bit value with bits laid out like:
//
// 1 Sign bit
// | 11 Exponent bits
// | |           52 Mantissa (i.e. fraction) bits
// | |           |
// S(Exponent--)(Mantissa-----------------------------------------)
//
// The details of how these are used to represent numbers aren't really
// relevant here as long we don't interfere with them. The important bit is NaN.
//
// An IEEE double can represent a few magical values like NaN ("not a number"),
// Infinity, and -Infinity. A NaN is any value where all exponent bits are set:
//
// v--NaN bits
// -111111111111---------------------------------------------------
//
// Here, "-" means "doesn't matter". Any bit sequence that matches the above is
// a NaN. With all of those "-", it obvious there are a *lot* of different
// bit patterns that all mean the same thing. NaN tagging takes advantage of
// this. We'll use those available bit patterns to represent things other than
// numbers without giving up any valid numeric values.
//
// NaN values come in two flavors: "signalling" and "quiet". The former are
// intended to halt execution, while the latter just flow through arithmetic
// operations silently. We want the latter. Quiet NaNs are indicated by setting
// the highest mantissa bit:
//
// v--Mantissa bit
// -[NaN       ]1--------------------------------------------------
//
// If all of the NaN bits are set, it's not a number. Otherwise, it is.
// That leaves all of the remaining bits as available for us to play with. We
// stuff a few different kinds of things here: special singleton values like
// "true", "false", and "null", and pointers to objects allocated on the heap.
// We'll use the sign bit to distinguish singleton values from object pointers. If
// it's set, it's an object pointer.
//
// v--Pointer or singleton?
// S[NaN       ]1--------------------------------------------------
//
// For singleton values, we just enumerate the different values. We'll use the
// low three bits of the mantissa for that, and only need a couple:
//
// 3 Type bits--v
// 0[NaN       ]1-----------------------------------------------[T]
//
// For pointers, we are left with 48 bits of mantissa to store an address.
// That's more than enough room for a 32-bit address. Even 64-bit machines
// only actually use 48 bits for addresses, so we've got plenty. We just stuff
// the address right into the mantissa.
//
// Ta-da, double precision numbers, pointers, and a bunch of singleton values,
// all stuffed into a single 64-bit sequence. Even better, we don't have to
// do any masking or work to extract number values: they are unmodified. This
// means math on numbers is fast.
#if CARDINAL_NAN_TAGGING
// A mask that selects the sign bit.
	#define SIGN_BIT ((uint64_t)1 << 63)

	// The bits that must be set to indicate a quiet NaN.
	#define QNAN ((uint64_t)0x7ff8000000000000)

	// The bits that must be set to indicate a quiet NaN.
	#define QNAN_NUM ((uint64_t)0x7ffc000000000000)

	// If the NaN bits are set, it's not a number.
	#define IS_NUM(value) (((value) & QNAN) != QNAN)

	// Singleton values are NaN with the sign bit cleared. (This includes the
	// normal value of the actual NaN value used in numeric arithmetic.)
	#define IS_SINGLETON(value) (((value) & (QNAN_NUM | SIGN_BIT)) == QNAN_NUM)

	// An object pointer is a NaN with a set sign bit.
	#define IS_OBJ(value) (((value) & (QNAN_NUM | SIGN_BIT)) == (QNAN_NUM | SIGN_BIT))

	// An object pointer is a NaN with a set sign bit.
	#define IS_POINTER(value) (((value) & (QNAN_NUM | SIGN_BIT)) == (QNAN | SIGN_BIT))
	
	#define IS_FALSE(value) ((value) == FALSE_VAL)
	#define IS_TRUE(value) ((value) == TRUE_VAL)
	#define IS_NULL(value) ((value) == (QNAN_NUM | TAG_NULL))
	#define IS_UNDEFINED(value) ((value) == (QNAN_NUM | TAG_UNDEFINED))

	// Masks out the tag bits used to identify the singleton value.
	#define MASK_TAG (7)

	// Tag values for the different singleton values.
	#define TAG_NAN       (0)
	#define TAG_NULL      (1)
	#define TAG_FALSE     (2)
	#define TAG_TRUE      (3)
	#define TAG_UNDEFINED (4)
	#define TAG_UNUSED2   (5)
	#define TAG_UNUSED3   (6)
	#define TAG_UNUSED4   (7)

	// Value -> 0 or 1.
	#define AS_BOOL(value) ((value) == TRUE_VAL)

	// Value -> Obj*.
	#define AS_OBJ(value) ((Obj*)((value) & ~(SIGN_BIT | QNAN_NUM)))
	
	// Value -> Void*
	#define AS_POINTER(value) ((void*)((value) & ~(SIGN_BIT | QNAN)))

	// Singleton values.
	#define NULL_VAL      ((Value)(uint64_t)(QNAN_NUM | TAG_NULL))
	#define FALSE_VAL     ((Value)(uint64_t)(QNAN_NUM | TAG_FALSE))
	#define TRUE_VAL      ((Value)(uint64_t)(QNAN_NUM | TAG_TRUE))
	#define UNDEFINED_VAL ((Value)(uint64_t)(QNAN_NUM | TAG_UNDEFINED))

	// Gets the singleton type tag for a Value (which must be a singleton).
	#define GET_TAG(value) ((int)((value) & MASK_TAG))
	
	#define MASK_POINTER(ptr) ((ptr) & ~(QNAN_NUM | SIGN_BIT))
#else
	// Value -> 0 or 1.
	#define AS_BOOL(value) ((value).type == VAL_TRUE)

	// Value -> Obj*.
	#define AS_OBJ(v) ((v).value.obj) //((v).obj)
	
	// Value -> pointer.
	#define AS_POINTER(v) ((v).value.obj) //((v).obj)

	// Determines if [value] is a garbage-collected object or not.
	#define IS_OBJ(value) ((value).type == VAL_OBJ)

	// Determines if [value] is FALSE
	#define IS_FALSE(value) ((value).type == VAL_FALSE)

	// Determines if [value] is TRUE
	#define IS_TRUE(value) ((value).type == VAL_TRUE)
	
	// Determines if [value] is TRUE
	#define IS_POINTER(value) ((value).type == VAL_POINTER)

	// Determines if [value] is NIL
	#define IS_NULL(value) ((value).type == VAL_NULL)

	// Determines if [value] is a number
	#define IS_NUM(value) ((value).type == VAL_NUM)

	// Determines if [value] is undefined
	#define IS_UNDEFINED(value) ((value).type == VAL_UNDEFINED)

	// Singleton values.
	#define FALSE_VAL ((Value){ VAL_FALSE, {.obj = NULL} })
	#define NULL_VAL ((Value){ VAL_NULL, {.obj= NULL} })
	#define TRUE_VAL ((Value){ VAL_TRUE, {.obj= NULL} })
	#define UNDEFINED_VAL ((Value){ VAL_UNDEFINED, {.obj= NULL} })

	#define MASK_POINTER(ptr) 
#endif


///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////

// Convert [obj], an `Obj*`, to a [Value].
#define OBJ_VAL(obj) (cardinalObjectToValue((Obj*)(obj)))

// Convert [ptr], an `void*`, to a [Value].
#define PTR_VAL(ptr) (cardinalPointerToValue((void*)(ptr)))

// Returns true if [value] is a bool.
#define IS_BOOL(value) (cardinalIsBool(value))

// Returns true if [value] is a class.
#define IS_CLASS(value) (cardinalIsObjType(value, OBJ_CLASS))

// Returns true if [value] is a closure.
#define IS_CLOSURE(value) (cardinalIsObjType(value, OBJ_CLOSURE))

// Returns true if [value] is a function object.
#define IS_FN(value) (cardinalIsObjType(value, OBJ_FN))

// Returns true if [value] is an instance.
#define IS_INSTANCE(value) (cardinalIsObjType(value, OBJ_INSTANCE))

// Returns true if [value] is a string object.
#define IS_STRING(value) (cardinalIsObjType(value, OBJ_STRING))

// Returns true if [value] is a range object.
#define IS_RANGE(value) (cardinalIsObjType(value, OBJ_RANGE))

// Returns true if [value] is a fiber object.
#define IS_FIBER(value) (cardinalIsObjType(value, OBJ_FIBER))

// Returns true if [value] is a table object.
#define IS_TABLE(value) (cardinalIsObjType(value, OBJ_TABLE))

// Returns true if [value] is a method object.
#define IS_METHOD(value) (cardinalIsObjType(value, OBJ_METHOD))

// Returns true if [value] is a list object.
#define IS_LIST(value) (cardinalIsObjType(value, OBJ_LIST))

// Returns true if [value] is a upvalue object.
#define IS_UPVALUE(value) (cardinalIsObjType(value, OBJ_UPVALUE))

// double -> Value.
#define NUM_VAL(num) (cardinalNumToValue(num))

// Value -> double.
#define AS_NUM(value) (cardinalValueToNum(value))

/// A union to let us reinterpret a double as raw bits and back.
typedef union {
	/// 64 bit int
	uint64_t bits64;
	/// 2 32 bit int
	uint32_t bits32[2];
	/// double
	double num;
} DoubleBits;

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTION DECLARATION
///////////////////////////////////////////////////////////////////////////////////

ObjInstance* cardinalInsertStackTrace(ObjInstance* inst, ObjString* str);

ObjInstance* cardinalThrowException(CardinalVM* vm, ObjString* str);

ObjString* cardinalGetErrorString(CardinalVM* vm, ObjFiber* fiber);

bool cardinalIsObjInstanceOf(CardinalVM* vm, Value val, const char* className);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: CLASS	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new "raw" class. It has no metaclass or superclass whatsoever.
// This is only used for bootstrapping the initial Object and Class classes,
// which are a little special.
ObjClass* cardinalNewSingleClass(CardinalVM* vm, int numFields, ObjString* name);

// Makes [superclass] the superclass of [subclass], and causes subclass to
// inherit its methods. This should be called before any methods are defined
// on subclass.
void cardinalBindSuperclass(CardinalVM* vm, ObjClass* subclass, ObjClass* superclass);

// Makes [superclass] the superclass of [subclass], and causes subclass to
// inherit its methods. This should be called before any methods are defined
// on subclass.
void cardinalAddFirstSuper(CardinalVM* vm, ObjClass* subclass, ObjClass* superclass);

// Makes [superclass] the superclass of [subclass], and causes subclass to
// inherit its methods. This should be called before any methods are defined
// on subclass.
void cardinalAddSuperclass(CardinalVM* vm, int num, ObjClass* subclass, ObjClass* superclass);

bool cardinalIsSubClass(ObjClass* actual, ObjClass* expected);

ObjFn* copyMethodBlock(CardinalVM* vm, Method method);

// Creates a new class object as well 
// as its associated metaclass.
ObjClass* cardinalNewClass(CardinalVM* vm, ObjClass* superclass, int numFields, ObjString* name);

// Bind a method to the VM
void cardinalBindMethod(CardinalVM* vm, ObjClass* classObj, int symbol, Method method);

// Get the correct method to call
Method* cardinalGetMethod(CardinalVM* vm, ObjClass* classObj, int symbol, int& adjustment);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: METHOD	
///////////////////////////////////////////////////////////////////////////////////

// Create a new method object
ObjMethod* cardinalNewMethod(CardinalVM* vm);

// Checks whether a method is ready to be called
bool methodIsReady(CardinalVM* vm, ObjMethod* method);

// Loads an method into the method object
void cardinalLoadMethod(CardinalVM* vm, ObjMethod* method, ObjString* name);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: CLOSURE	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new closure object that invokes [fn]. Allocates room for its
// upvalues, but assumes outside code will populate it.
ObjClosure* cardinalNewClosure(CardinalVM* vm, ObjFn* fn);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: POINTER	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new pointer object. 
//ObjPointer* cardinalNewPointer(CardinalVM* vm);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: FIBER	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new fiber object that will invoke [fn], which can be a function or
// closure.
ObjFiber* cardinalNewFiber(CardinalVM* vm, Obj* fn);

// Resets [fiber] back to an initial state where it is ready to invoke [fn].
void cardinalResetFiber(ObjFiber* fiber, Obj* fn);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: FUNCTION	
///////////////////////////////////////////////////////////////////////////////////

FnDebug* cardinalNewDebug(CardinalVM* vm, ObjString* debugSourcePath, const char* debugName, int debugNameLength, 
						int* sourceLines, SymbolTable locals, SymbolTable lines);

// Creates a new function object with the given code and constants. The new
// function will take over ownership of [bytecode] and [sourceLines]. It will
// copy [constants] into its own array.
ObjFn* cardinalNewFunction(CardinalVM* vm, ObjModule* module,
                       Value* constants, int numConstants,
                       int numUpvalues, int arity,
                       uint8_t* bytecode, int bytecodeLength,
                       FnDebug* debug);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: TABLE	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new list with [numElements] elements (which are left
// uninitialized.)
ObjTable* cardinalNewTable(CardinalVM* vm, int numElements);

// Adds [value] to [list], reallocating and growing its storage if needed.
void cardinalTableAdd(CardinalVM* vm, ObjTable* list, Value value, Value key);

// Find [key] in [list], shifting down the other elements.
Value cardinalTableFind(CardinalVM* vm, ObjTable* list, Value key);

// Removes and returns the item at [index] from [list].
Value cardinalTableRemove(CardinalVM* vm, ObjTable* list, Value index);

// Print an table to the console
void cardinalTablePrint(CardinalVM* vm, ObjTable* list);

HashValue* cardinalGetTableIndex(ObjTable* table, int ind);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: INSTANCE	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new instance of the given [classObj].
Value cardinalNewInstance(CardinalVM* vm, ObjClass* classObj);

// Creates a new instance of the given [classObj].
Value cardinalNewInstance(CardinalVM* vm, ObjClass* classObj, void* mem);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: MAP	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new empty map.
ObjMap* cardinalNewMap(CardinalVM* vm);

// Looks up [key] in [map]. If found, returns the index of its entry. Otherwise,
// returns `UINT32_MAX`.
uint32_t cardinalMapFind(ObjMap* map, Value key);

Value cardinalMapGet(ObjMap* map, Value key);

// Associates [key] with [value] in [map].
void cardinalMapSet(CardinalVM* vm, ObjMap* map, Value key, Value value);

void cardinalMapClear(CardinalVM* vm, ObjMap* map);

// Removes [key] from [map], if present. Returns the value for the key if found
// or `NULL_VAL` otherwise.
Value cardinalMapRemoveKey(CardinalVM* vm, ObjMap* map, Value key);

Value cardinalMapGetInd(ObjMap* map, uint32_t ind);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: MODULE	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new module.
ObjModule* cardinalNewModule(CardinalVM* vm);

// Find and return a value from this module
Value cardinalModuleFind(CardinalVM* vm, ObjModule* module, ObjString* key);

// Set a value onto a variable
Value cardinalModuleSet(CardinalVM* vm, ObjModule* module, ObjString* key, Value val);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: LIST	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new list with [numElements] elements (which are left
// uninitialized.)
ObjList* cardinalNewList(CardinalVM* vm, int numElements);

// Adds [value] to [list], reallocating and growing its storage if needed.
void cardinalListAdd(CardinalVM* vm, ObjList* list, Value value);

// Inserts [value] in [list] at [index], shifting down the other elements.
void cardinalListInsert(CardinalVM* vm, ObjList* list, Value value, int index);

// Removes and returns the item at [index] from [list].
Value cardinalListRemoveAt(CardinalVM* vm, ObjList* list, int index);

void cardinalListRemoveLast(CardinalVM* vm, ObjList* list);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: RANGE	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new range from [from] to [to].
Value cardinalNewRange(CardinalVM* vm, double from, double to, bool isInclusive);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: STRING	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new string containing the code point in [string] starting at byte
// [index]. If [index] points into the middle of a UTF-8 sequence, returns an
// empty string.
Value cardinalStringCodePointAt(CardinalVM* vm, ObjString* string, int index);

// Creates a new string containing the UTF-8 encoding of [value].
Value cardinalStringFromCodePoint(CardinalVM* vm, int value);

// Creates a new string that is the concatenation of [left] and [right] (with
// length [leftLength] and [rightLength], respectively). If -1 is passed
// the string length is automatically calculated.
ObjString* cardinalStringConcat(CardinalVM* vm, const char* left, int leftLength,
                            const char* right, int rightLength);
							
// Format a string [str] into an cardinal String
ObjString* cardinalStringFormat(CardinalVM* vm, const char* str, ...);

// Creates a new string object of [length] and copies [text] into it.
//
// [text] may be NULL if [length] is zero.
Value cardinalNewString(CardinalVM* vm, const char* text, size_t length);

// Creates a new string object with a buffer large enough to hold a string of
// [length] but does no initialization of the buffer.
//
// The caller is expected to fully initialize the buffer after calling.
Value cardinalNewUninitializedString(CardinalVM* vm, size_t length);

// Search for the first occurence of [needle] within [haystack] and returns its
// zero-based offset. Returns `UINT32_MAX` if [haystack] does not contain
// [needle].
uint32_t cardinalStringFind(CardinalVM* vm, ObjString* haystack, ObjString* needle);

// Hash the string [string]
void hashString(ObjString* string);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: UPVALUE	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new open upvalue pointing to [value] on the stack.
Upvalue* cardinalNewUpvalue(CardinalVM* vm, Value* value);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: GARBAGE COLLECTOR	
///////////////////////////////////////////////////////////////////////////////////

// Mark [value] as reachable and still in use. This should only be called
// during the sweep phase of a garbage collection.
void cardinalMarkValue(CardinalVM* vm, Value& value);

// Mark [obj] as reachable and still in use. This should only be called
// during the sweep phase of a garbage collection.
void cardinalMarkObj(CardinalVM* vm, Obj* obj);

// Releases all memory owned by [obj], including [obj] itself.
void cardinalFreeObj(CardinalVM* vm, Obj* obj);

// Releases all memory owned by [obj], not including [obj] itself.
void cardinalFreeObjContent(CardinalVM* vm, Obj* obj);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: GET CLASS OF VALUE	
///////////////////////////////////////////////////////////////////////////////////

// Returns the class of [value].
//
// Unlike cardinalGetClassInline in cardinal_vm.h, this is not inlined. Inlining helps
// performance (significantly) in some cases, but degrades it in others. The
// ones used by the implementation were chosen to give the best results in the
// benchmarks.
ObjClass* cardinalGetClass(CardinalVM* vm, Value value);

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: COMPARES	
///////////////////////////////////////////////////////////////////////////////////

// Compare function
bool compareFloat(cardinal_number a, cardinal_number b);

// Returns true if [a] and [b] are strictly the same value. This is identity
// for object values, and value equality for unboxed values.
static inline bool cardinalValuesSame(Value a, Value b) {
#if CARDINAL_NAN_TAGGING
	// Value types have unique bit representations and we compare object types
	// by identity (i.e. pointer), so all we need to do is compare the bits.
	return a == b;
#else
	if (a.type != b.type) return false;
	if (a.type == VAL_NUM) return compareFloat(a.value.num, b.value.num); //a.num == b.num;
	return a.value.obj == b.value.obj;
#endif
}

// Returns true if [a] and [b] are equivalent. Immutable values (null, bools,
// numbers, ranges, and strings) are equal if they have the same data. All
// other values are equal if they are identical objects.
bool cardinalValuesEqual(Value a, Value b);

// For debug tracing.
void cardinalPrintValue(Value value);

// Returns true if [value] is a bool. Do not call this directly, instead use
// [IS_BOOL].
static inline bool cardinalIsBool(Value value) {
#if CARDINAL_NAN_TAGGING
	return value == TRUE_VAL || value == FALSE_VAL;
#else
	return value.type == VAL_FALSE || value.type == VAL_TRUE;
#endif
}

// Returns true if [value] is an object of type [type]. Do not call this
// directly, instead use the [IS___] macro for the type in question.
static inline bool cardinalIsObjType(Value value, ObjType type) {
  return IS_OBJ(value) && AS_OBJ(value)->type == type;
}

// Converts the raw object pointer [obj] to a [Value].
static inline Value cardinalObjectToValue(Obj* obj) {
#if CARDINAL_NAN_TAGGING
	return (Value)(SIGN_BIT | QNAN_NUM | (uint64_t)(obj));
#else
	Value value;
	value.type = VAL_OBJ;
	value.value.obj = obj;
	return value;
#endif
}

static inline Value cardinalPointerToValue(void* ptr) {
#if CARDINAL_NAN_TAGGING
	return (Value)(SIGN_BIT | QNAN | (uint64_t)(ptr));
#else
	Value value;
	value.type = VAL_POINTER;
	value.value.obj = (Obj*) ptr;
	return value;
#endif
}

// Interprets [value] as a [double].
static inline double cardinalValueToNum(Value value) {
#if CARDINAL_NAN_TAGGING
	DoubleBits data;
	data.bits64 = value;
	return data.num;
#else
	return value.value.num;
#endif
}

// Converts [num] to a [Value].
static inline Value cardinalNumToValue(double n) {
#if CARDINAL_NAN_TAGGING
	DoubleBits data;
	data.num = n;
	return data.bits64;
#else
	return (Value){ VAL_NUM, {.num = n} };
#endif
}

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: HOST OBJECTS
///////////////////////////////////////////////////////////////////////////////////

Value cardinalGetHostObject(CardinalVM* vm, CardinalValue* key);

void cardinalSetHostObject(CardinalVM* vm, Value val, CardinalValue* key);

CardinalValue* cardinalCreateHostObject(CardinalVM* vm, Value val);

void cardinalRemoveHostObject(CardinalVM* vm, CardinalValue* key);

#endif
