#ifndef udog_vm_h
#define udog_vm_h

#include "udog_utils.h"
#include "udog_compiler.h"
#include "udog_debugger.h"

///////////////////////////////////////////////////////////////////////////////////
//// VIRTUAL MACHINE
///////////////////////////////////////////////////////////////////////////////////

// The maximum number of temporary objects that can be made visible to the GC
// at one time.
#define UDOG_MAX_TEMP_ROOTS 10

// Mark [obj] as a GC root so that it doesn't get collected. Initializes
// [pinned], which must be then passed to [unpinObj].
#define UDOG_PIN(vm, obj) udogPushRoot(vm, (Obj*) obj)

// Remove the most recently pinned object from the list of pinned GC roots.
#define UDOG_UNPIN(vm) udogPopRoot(vm)

// Enumeration for the different opcodes
// The enumeration is used for the virtual machine
typedef enum Code {
	#define OPCODE(name) CODE_##name,
	#include "udog_opcodes.h"
	#undef OPCODE	
} Code;

typedef enum GCPhase {
	GC_SWEEP,
	GC_MARK,
	GC_RESET,
	GC_MARK_ALL,
	GC_SWEEP_ALL,
	GC_FREE_ALL
} GCPhase;

/// The Garbage collector
/// All variables and data concerning the collector are stored
/// The garbage collector uses a mark and sweep algorithm
/// It also marks all object using the tri-coloring method
typedef struct UDogGC {
	/// The current phase of the allocator
	GCPhase phase;
	
	/// The number of bytes that are known to be currently allocated. Includes all
	/// memory that was proven live after the last GC, as well as any new bytes
	/// that were allocated since then. Does *not* include bytes for objects that
	/// were freed since the last GC.
	size_t bytesAllocated;
	
	/// amount of bytes allocated
	size_t nbAllocations;
	
	/// number of frees
	size_t nbFrees;
	
	/// indicates that the garbage collector is busy
	bool isWorking;

	/// The number of total allocated bytes that will trigger the next GC.
	size_t nextGC;

	/// The minimum value for [nextGC] when recalculated after a collection.
	size_t minNextGC;

	/// The scale factor used to calculate [nextGC] from the current number of in
	/// use bytes, as a percent. For example, 150 here means that nextGC will be
	/// 50% larger than the current number of in-use bytes.
	int heapScalePercent;
  
	/// List of all white garbage collected objects
	/// These objects are subject to free'ing
	/// all objects (except for the pinned, globals and locals) start in this list
	Obj* whiteList;
	/// The gray list, objects that need to be checked by the garbage collector
	Obj* grayList;
	/// The black list, objects that are accessable by the GC
	Obj* blackList;

	/// The first object in the linked list of all currently allocated objects.
	Obj* first;
	
	/// The list of temporary roots. This is for temporary or new objects that are
	/// not otherwise reachable but should not be collected.
	///
	/// They are organized as a stack of pointers stored in this array. This
	/// implies that temporary roots need to have stack semantics: only the most
	/// recently pushed object can be released.
	Obj* tempRoots[UDOG_MAX_TEMP_ROOTS];
	
	/// Number of active roots
	int numTempRoots;
	
	/// Indicates the number of active objects
	int active;
	
	/// Indicates the number of destroyed objects
	int destroyed;

} UDogGC;

/// Contains all metatables from the Virtual machine
typedef struct UDogMetaTable {
		/// Metatables 
	/// The core class diagram ends up looking like this, where single lines point
	/// to a class's superclass, and double lines point to its metaclass:
	///
	///           .------------.    .========.
	///           |            |    ||      ||
	///           v            |    v       ||
	///     .---------.   .--------------.  ||
	///     | Object  |==>|    Class     |==='
	///     '---------'   '--------------'
	///          ^               ^
	///          |               |
	///     .---------.   .--------------.   -.
	///     |  Base   |==>|  Base.type   |    |
	///     '---------'   '--------------'    |
	///          ^               ^            | Hypothetical example classes
	///          |               |            |
	///     .---------.   .--------------.    |
	///     | Derived |==>| Derived.type |    |
	///     '---------'   '--------------'   -'

	/// Metatables for the bool type
	/// Holds all functions related to bool type
	ObjClass* boolClass;
	
	/// Metatables for the num type
	/// Holds all functions related to num type
	ObjClass* numClass;
	
	/// Define the root Object class. This has to be done a little specially
	/// because it has no superclass and an unusual metaclass (Class).
	ObjClass* objectClass;
	
	/// Metatables for the fiber type
	/// Holds all functions related to fiber type
	ObjClass* fiberClass;
	/// Metatables for the function type
	/// Holds all functions related to function type
	ObjClass* fnClass;
	/// Metatables for the list type
	/// Holds all functions related to list type
	ObjClass* listClass;
	/// Metatables for the null type
	/// Holds all functions related to null type
	ObjClass* nullClass;
	/// Metatables for the string type
	/// Holds all functions related to string type
	ObjClass* stringClass;
	/// Metatables for the range type
	/// Holds all functions related to range type
	ObjClass* rangeClass;
	/// Metatables for the table type
	/// Holds all functions related to the table type
	ObjClass* tableClass;
	/// Metatables for the table type
	/// Holds all functions related to the table type
	ObjClass* mapClass;
	/// Metatable for modules
	ObjClass* moduleClass;
	/// Metatable for methods
	ObjClass* methodClass;
	
	/// Metatables for the classes type (shows all different type/classes there are
	/// Class, which is a subclass of Object, but Object's
	/// metaclass.
	ObjClass* classClass;
} UDogMetaTable;

/// Adds a newly allocated object to the GC
void udogAddGCObject(UDogVM* vm, Obj* obj);

/// Used to get statistics from the Garbage collector
void udogGetGCStatistics(UDogVM* vm, int* size, int* destroyed, int* detected, int* newobj, int* gcNext, int* nbHosts);

/// The host objects from this application
typedef struct UDogHost {
	/// The host objects from this application
	ObjTable* hostObjects;
	
	/// Index of all free indexes to use as a key
	ObjList* freeNums;
	
	/// highest index
	udog_number max;
} UDogHost;

/// The declaration for the VM
/// Is used as the main source for script execution
typedef struct UDogVM {
	/// Metatables 
	UDogMetaTable metatable;	
	
	/// The loaded modules. Each key is an ObjString (except for the main module,
	/// whose key is null) for the module's name and the value is the ObjModule
	/// for the module.
	ObjMap* modules;
	
	/// The function used to load modules.
	udogLoadModuleFn loadModule;
	
	/// The externally-provided function used to allocate memory.
	udogReallocateFn reallocate;
	
	///The garbage collector used by this VM
	UDogGC garbageCollector;

	/// The fiber that is currently running.
	ObjFiber* fiber;
	
	/// Method calls are dispatched directly by index in this table.
	SymbolTable methodNames;
	
	/// The host objects from this application
	UDogHost hostObjects;
	
	/// Compiler used by the VM
	UDogCompiler* compiler;
		
	/// Callback function for the debugger
	udogCallBack callBackFunction;
	
	/// debugger
	DebugData* debugger;
	
	/// Callback used when printing output to the console
	udogPrintCallBack printFunction;
	
	/// The root directoy
	ObjString* rootDirectory;
	
	/// Indicates whether the VM is in debugMode or not
	bool debugMode;
	
	/// The maximum stack size
	int stackMax;
	
	/// The maximum callframe depth
	int callDepth;
} UDogVM;

ObjFiber* loadModuleFiber(UDogVM* vm, Value name, Value source);

bool runInterpreter(UDogVM* vm); 

ObjModule* udogImportModuleVar(UDogVM* vm, Value name);

// Checks whether a module with the given name exists, and if so
// Replaces it with the given module
// Otherwise the module is added to the module list
void udogSaveModule(UDogVM* vm, ObjModule* module, ObjString* name);

// Returns the value of the module-level variable named [name] in the main
// module.
Value udogFindVariable(UDogVM* vm, const char* name);

ObjModule* udogReadyNewModule(UDogVM* vm);

// Adds a new implicitly declared global named [name] to the global namespace.
//
// Does not check to see if a global with that name is already declared or
// defined. Returns the symbol for the new global or -2 if there are too many
// globals defined.
int udogDeclareVariable(UDogVM* vm, ObjModule* module, const char* name,
                        size_t length);

// Adds a new global named [name] to the global namespace.
//
// Returns the symbol for the new global, -1 if a global with the given name
// is already defined, or -2 if there are too many globals defined.
int udogDefineVariable(UDogVM* vm, ObjModule* module, const char* name,
                       size_t length, Value value);
					   
int udogFindVariableSymbol(UDogVM* vm, ObjModule* module, const char* name, int length);

// Sets the current Compiler being run to [compiler].
void udogSetCompiler(UDogVM* vm, UDogCompiler* compiler);

// Mark [obj] as a GC root so that it doesn't get collected.
void udogPushRoot(UDogVM* vm, Obj* obj);

// Remove the most recently pushed temporary root.
void udogPopRoot(UDogVM* vm);

// Runs the garbage collector
void udogCollectGarbage(UDogVM* vm);

// Set the garbage collector enabled or disabled
void udogEnableGC(UDogVM* vm, bool enable);

// Returns the class of [value].
//
// Defined here instead of in udog_value.h because it's critical that this be
// inlined. That means it must be defined in the header, but the udog_value.h
// header doesn't have a full definitely of UDogVM yet.
static inline ObjClass* udogGetClassInline(UDogVM* vm, Value value) {
	if (IS_NUM(value)) return vm->metatable.numClass;
	if (IS_OBJ(value)) return AS_OBJ(value)->classObj;
	
#if UDOG_NAN_TAGGING
	switch (GET_TAG(value)) {
		case TAG_FALSE: return vm->metatable.boolClass; break;
		case TAG_NAN: return vm->metatable.numClass; break;
		case TAG_NULL: return vm->metatable.nullClass; break;
		case TAG_TRUE: return vm->metatable.boolClass; break;
		default:
		case TAG_UNDEFINED: UNREACHABLE("class inline");
	}
#else
  switch (value.type) {
    case VAL_FALSE: return vm->metatable.boolClass;
	case VAL_TRUE: return vm->metatable.boolClass;
	
    case VAL_NULL: return vm->metatable.nullClass;
    case VAL_NUM: return vm->metatable.numClass;
	case VAL_INT: return vm->metatable.intClass;
    
    case VAL_OBJ: return AS_OBJ(value)->classObj;
	default:
		UNREACHABLE("class inline");
  }
#endif
  UNREACHABLE("class inline");
  return NULL;
}

////////////////////////////////////////////////////////////////////////////////////
//// MEMORY ALLOCATION
///////////////////////////////////////////////////////////////////////////////////

// A generic allocation function that handles all explicit memory management.
// It's used like so:
//
// - To allocate new memory, [memory] is NULL and [oldSize] is zero. It should
//   return the allocated memory or NULL on failure.
//
// - To attempt to grow an existing allocation, [memory] is the memory,
//   [oldSize] is its previous size, and [newSize] is the desired size.
//   It should return [memory] if it was able to grow it in place, or a new
//   pointer if it had to move it.
//
// - To shrink memory, [memory], [oldSize], and [newSize] are the same as above
//   but it will always return [memory].
//
// - To free memory, [memory] will be the memory to free and [newSize] and
//   [oldSize] will be zero. It should return NULL.
void* udogReallocate(UDogVM* vm, void* buffer, size_t oldSize, size_t newSize);

// Check if we need to grow or shrink the stack
bool udogFiberStack(UDogVM* vm, ObjFiber* fiber, Value** stackstart);
bool udogFiberCallFrame(UDogVM* vm, ObjFiber* fiber, CallFrame** frame);



#endif
