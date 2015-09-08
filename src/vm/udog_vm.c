#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef __STDC_LIMIT_MACROS
	#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h>

#include "udog.h"
#include "udog_config.h"
#include "udog_compiler.h"
#include "udog_core.h"
#include "udog_debug.h"
#include "udog_vm.h"

#if UDOG_USE_LIB_IO
  #include "udog_io.h"
#endif

#if UDOG_DEBUG_TRACE_MEMORY || UDOG_DEBUG_TRACE_GC
  #include <time.h>
#endif

#if UDOG_USE_DEFAULT_FILE_LOADER
	#include "udog_file.h"
#endif

#if UDOG_BYTECODE
	#include "udog_bytecode.h"
#endif

#if UDOG_DEBUGGER
	#include "udog_debugger.h"
#endif

#if UDOG_USE_REGEX
	#include "udog_regex.h"
#endif

///////////////////////////////////////////////////////////////////////////////////
//// STATIC
///////////////////////////////////////////////////////////////////////////////////

static void* defaultReallocate(void* memory, size_t oldSize, size_t newSize);
static void initGarbageCollector(UDogVM* vm, UDogConfiguration* configuration);

static Upvalue* captureUpvalue(UDogVM* vm, ObjFiber* fiber, Value* local);
static void closeUpvalue(ObjFiber* fiber);

static void bindMethod(UDogVM* vm, int methodType, int symbol, ObjClass* classObj, Value methodValue);
static void callForeign(UDogVM* vm, ObjFiber* fiber, udogForeignMethodFn foreign, int numArgs);
static ObjFiber* runtimeError(UDogVM* vm, ObjFiber* fiber, ObjString* error);
static ObjFiber* runtimeThrow(UDogVM* vm, ObjFiber* fiber, Value error);
static void callFunction(ObjFiber* fiber, Obj* function, int numArgs);

static void defineMethod(UDogVM* vm, const char* module, const char* className,
                         const char* signature,
                         udogForeignMethodFn methodFn, bool isStatic);

static void collectGarbage(UDogVM* vm);

///////////////////////////////////////////////////////////////////////////////////
//// VIRTUAL MACHINE INITIALISATION
///////////////////////////////////////////////////////////////////////////////////

static void udogLoadLibraries(UDogVM* vm) {
#if UDOG_USE_LIB_IO
	udogLoadIOLibrary(vm);
#endif
#if UDOG_USE_DEFAULT_FILE_LOADER
	udogLoadFileLibrary(vm);
#endif
#if UDOG_USE_REGEX
	udogLoadRegexLibrary(vm);
#endif
}

static void loadCallBacks(UDogConfiguration* configuration, UDogVM* vm) {
	
	udogPrintCallBack print = printf;
	udogLoadModuleFn moduleLoader = NULL;
	udogCallBack callback = NULL;
	
#if UDOG_USE_DEFAULT_FILE_LOADER
	moduleLoader = defaultModuleLoader;
#endif
#if UDOG_DEBUGGER
	callback = defaultDebugCallBack;
#endif
	
	if (configuration->printFn != NULL) {
		print = configuration->printFn;
	}
	if (configuration->loadModuleFn != NULL) {
		moduleLoader = configuration->loadModuleFn;
	}
	if (configuration->debugCallback != NULL) {
		callback = configuration->debugCallback;
	}
	
	vm->callDepth = CALLFRAME_MAX;
	vm->stackMax = STACKSIZE_MAX;
	if (configuration->stackMax != 0) {
		vm->stackMax = configuration->stackMax;
	}
	if (configuration->callDepth != 0) {
		vm->callDepth = configuration->callDepth;
	}
	
	vm->loadModule = moduleLoader;
	vm->printFunction = print;
	vm->callBackFunction = callback; 
}

UDogVM* udogNewVM(UDogConfiguration* configuration) {
	// Load the memory allocation and the VM
	udogReallocateFn reallocate = defaultReallocate;
	if (configuration->reallocateFn != NULL) {
		reallocate = configuration->reallocateFn;
	}
	UDogVM* vm = (UDogVM*) reallocate(NULL, 0, sizeof(UDogVM));
	vm->reallocate = reallocate;
	
	// Set some callbacks
	loadCallBacks(configuration, vm);

	// Initialise the GC
	initGarbageCollector(vm, configuration);
	
	// Initiate the method table
	udogSymbolTableInit(vm, &vm->methodNames);
	
	// Create a new debugger
	vm->debugger = udogNewDebugger(vm);
	vm->debugMode = false;
	
	// Set the root directory
	setRootDirectory(vm, configuration->rootDirectory);
	
	// Implicitly create a "main" module for the REPL or entry script.
	ObjModule* mainModule = udogNewModule(vm);
	udogPushRoot(vm, (Obj*)mainModule);
	vm->modules = udogNewMap(vm);
	udogMapSet(vm, vm->modules, NULL_VAL, OBJ_VAL(mainModule));
	udogPopRoot(vm);
	
	// Create the script specific libraries
	udogInitializeCore(vm);
	udogLoadLibraries(vm);
	
	udogFlushHostObjects(vm);
	
	return vm;
}



void udogFreeVM(UDogVM* vm) {
	if( vm == NULL || vm->methodNames.count == 0 ) return;
	
	// Free all of the GC objects.
	Obj* obj = vm->garbageCollector.first;
	while (obj != NULL) {
		Obj* next = obj->next;
		udogFreeObj(vm, obj);
		obj = next;
	}
	
	udogSymbolTableClear(vm, &vm->methodNames);
	udogFreeDebugger(vm, vm->debugger);
	
#if UDOG_DEBUG_TRACE_MEMORY || UDOG_DEBUG_TRACE_GC
	vm->printFunction("Memory in use: %ld\n", vm->garbageCollector.bytesAllocated);
	vm->printFunction("Nb of allocations: %ld\n", vm->garbageCollector.nbAllocations);
	vm->printFunction("Nb of frees: %ld\n", vm->garbageCollector.nbFrees);
#endif
	
	DEALLOCATE(vm, vm);
}

// Set the root directory
void setRootDirectory(UDogVM* vm, const char* path) {
	vm->rootDirectory = NULL;
	if (path == NULL) return;
	
	// Use the directory where the file is as the root to resolve imports
	// relative to.
	const char* lastSlash = strrchr(path, '/');
	if (lastSlash != NULL) {
		vm->rootDirectory = AS_STRING(udogNewString(vm, path, lastSlash - path + 1));
	}
}

// The built-in reallocation function used when one is not provided by the
// configuration.
static void* defaultReallocate(void* buffer , size_t oldSize, size_t newSize) {
	UNUSED(oldSize);
	if (newSize == 0) { free(buffer); return NULL; }
	if (buffer == NULL) return malloc(newSize);
	return realloc(buffer, newSize);
}


static void initMetaClasses(UDogVM* vm) {
	vm->metatable.stringClass = NULL;
	vm->metatable.classClass = NULL;
	vm->metatable.fiberClass = NULL;
	vm->metatable.fnClass = NULL;
	vm->metatable.listClass = NULL;
	vm->metatable.mapClass = NULL;
	vm->metatable.rangeClass = NULL;
	vm->metatable.boolClass = NULL;
	vm->metatable.methodClass = NULL;
	vm->metatable.moduleClass = NULL;
	vm->metatable.nullClass = NULL;
	vm->metatable.numClass = NULL;
	vm->metatable.objectClass = NULL;
	vm->metatable.tableClass = NULL;
}

static void initGarbageCollector(UDogVM* vm, UDogConfiguration* configuration) {
	vm->garbageCollector.bytesAllocated = 0;

	vm->garbageCollector.nextGC = 1024 * 1024 * 10;
	if (configuration->initialHeapSize != 0) {
		vm->garbageCollector.nextGC = configuration->initialHeapSize;
	}

	vm->garbageCollector.minNextGC = 1024 * 1024;
	if (configuration->minHeapSize != 0) {
		vm->garbageCollector.minNextGC = configuration->minHeapSize;
	}

	vm->garbageCollector.heapScalePercent = 150;
	if (configuration->heapGrowthPercent != 0) {
		// +100 here because the configuration gives us the *additional* size of
		// the heap relative to the in-use memory, while heapScalePercent is the
		// *total* size of the heap relative to in-use.
		vm->garbageCollector.heapScalePercent = 100 + configuration->heapGrowthPercent;
	}

	vm->garbageCollector.blackList = NULL;
	vm->garbageCollector.grayList = NULL;
	vm->garbageCollector.whiteList = NULL;
	
	vm->garbageCollector.first = NULL;

	vm->garbageCollector.phase = GC_RESET;
	vm->garbageCollector.numTempRoots = 0;
	
	vm->garbageCollector.active = 0;
	vm->garbageCollector.destroyed = 0;
	
	vm->compiler = NULL;
	vm->fiber = NULL;
	vm->rootDirectory = NULL;
	vm->modules = NULL;
	vm->hostObjects.freeNums = NULL;
	vm->hostObjects.hostObjects = NULL;
	initMetaClasses(vm);
	
	vm->garbageCollector.isWorking = false;
}

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS FOR THE VM
///////////////////////////////////////////////////////////////////////////////////

// Captures the local variable [local] into an [Upvalue]. If that local is
// already in an upvalue, the existing one will be used. (This is important to
// ensure that multiple closures closing over the same variable actually see
// the same variable.) Otherwise, it will create a new open upvalue and add it
// the fiber's list of upvalues.
static Upvalue* captureUpvalue(UDogVM* vm, ObjFiber* fiber, Value* local) {
	// If there are no open upvalues at all, we must need a new one.
	if (fiber->openUpvalues == NULL) {
		fiber->openUpvalues = udogNewUpvalue(vm, local);
		return fiber->openUpvalues;
	}

	Upvalue* prevUpvalue = NULL;
	Upvalue* upvalue = fiber->openUpvalues;

	// Walk towards the bottom of the stack until we find a previously existing
	// upvalue or pass where it should be.
	while (upvalue != NULL && upvalue->value > local) {
		prevUpvalue = upvalue;
		upvalue = upvalue->next;
	}

	// Found an existing upvalue for this local.
	if (upvalue != NULL && upvalue->value == local) return upvalue;

	// We've walked past this local on the stack, so there must not be an
	// upvalue for it already. Make a new one and link it in in the right
	// place to keep the list sorted.
	Upvalue* createdUpvalue = udogNewUpvalue(vm, local);
	if (prevUpvalue == NULL) {
		// The new one is the first one in the list.
		fiber->openUpvalues = createdUpvalue;
	}
	else {
		prevUpvalue->next = createdUpvalue;
	}

	createdUpvalue->next = upvalue;
	return createdUpvalue;
}

static void closeUpvalue(ObjFiber* fiber) {
	Upvalue* upvalue = fiber->openUpvalues;

	// Move the value into the upvalue itself and point the upvalue to it.
	upvalue->closed = *upvalue->value;
	upvalue->value = &upvalue->closed;

	// Remove it from the open upvalue list.
	fiber->openUpvalues = upvalue->next;
}

static void bindMethod(UDogVM* vm, int methodType, int symbol, ObjClass* classObj, Value methodValue) {
	ObjFn* methodFn = IS_FN(methodValue) ? AS_FN(methodValue) : AS_CLOSURE(methodValue)->fn;

	// Methods are always bound against the class, and not the metaclass, even
	// for static methods, so that constructors (which are static) get bound like
	// instance methods.
	udogBindMethodCode(vm, -1, classObj, methodFn);

	Method method;
	method.type = METHOD_BLOCK;
	method.fn.obj = AS_OBJ(methodValue);

	if (methodType == CODE_METHOD_STATIC) {
		classObj = classObj->obj.classObj;
	}

	udogBindMethod(vm, classObj, symbol, method);
}

static void callForeign(UDogVM* vm, ObjFiber* fiber, udogForeignMethodFn foreign, int numArgs) {
	vm->fiber->foreignCallSlot = fiber->stacktop - numArgs;
	vm->fiber->foreignCallNumArgs = numArgs;

	foreign(vm);

	// Discard the stack slots for the arguments (but leave one for
	// the result).
	fiber->stacktop -= numArgs - 1;

	// If nothing was returned, implicitly return null.
	if (vm->fiber->foreignCallSlot != NULL) {
		*vm->fiber->foreignCallSlot = NULL_VAL;
		vm->fiber->foreignCallSlot = NULL;
	}
}


// Pushes [function] onto [fiber]'s callstack and invokes it. Expects [numArgs]
// arguments (including the receiver) to be on the top of the stack already.
// [function] can be an `ObjFn` or `ObjClosure`.
static void callFunction(ObjFiber* fiber, Obj* function, int numArgs) {
	CallFrame* frame = &fiber->frames[fiber->numFrames];
	frame->fn = function;
	frame->top = fiber->stacktop - numArgs;

	frame->pc = 0;
	if (function->type == OBJ_FN) {
		frame->pc = ((ObjFn*)function)->bytecode;
	}
	else {
		frame->pc = ((ObjClosure*)function)->fn->bytecode;
	}

	fiber->numFrames++;
}

///////////////////////////////////////////////////////////////////////////////////
//// ERROR GENERATORS
///////////////////////////////////////////////////////////////////////////////////

// Puts [fiber] into a runtime failed state because of [error].
//
// Returns the fiber that should receive the error or `NULL` if no fiber
// caught it.
static ObjFiber* runtimeError(UDogVM* vm, ObjFiber* fiber, ObjString* error) {
	ASSERT(fiber->error == NULL, "Can only fail once.");

	// Store the error in the fiber so it can be accessed later.
	fiber->error = udogThrowException(vm, error);
	udogInsertStackTrace(fiber->error, udogDebugGetStackTrace(vm, fiber));

	// If the caller ran this fiber using "try", give it the error.
	if (fiber->callerIsTrying) {
		ObjFiber* caller = fiber->caller;

		// Make the caller's try method return the error message.
		*(caller->stacktop - 1) = OBJ_VAL(fiber->error);
		return caller;
	}

	// If we got here, nothing caught the error, so show the stack trace.
	udogDebugPrintStackTrace(vm, fiber);
	return NULL;
}

static ObjFiber* runtimeThrow(UDogVM* vm, ObjFiber* fiber, Value error) {
	if (IS_STRING(error)) return runtimeError(vm, fiber, AS_STRING(error));
	
	ASSERT(fiber->error == NULL, "Can only fail once.");

	// Store the error in the fiber so it can be accessed later.
	fiber->error = AS_INSTANCE(error);
	udogInsertStackTrace(fiber->error, udogDebugGetStackTrace(vm, fiber));

	// If the caller ran this fiber using "try", give it the error.
	if (fiber->callerIsTrying) {
		ObjFiber* caller = fiber->caller;

		// Make the caller's try method return the error message.
		*(caller->stacktop - 1) = OBJ_VAL(fiber->error);
		return caller;
	}

	// If we got here, nothing caught the error, so show the stack trace.
	udogDebugPrintStackTrace(vm, fiber);
	return NULL;
}

// Generates an error at runtime and stops execution at once
static void runtimeCrash(UDogVM* vm, ObjFiber* fiber, const char* error) {
	ASSERT(fiber->error == NULL, "Can only fail once.");

	// Store the error in the fiber so it can be accessed later.
	fiber->error =  udogThrowException(vm, AS_STRING(udogNewString(vm, error, strlen(error))));

	// If we got here, nothing caught the error, so show the stack trace.
	udogDebugPrintStackTrace(vm, fiber);
}

// Creates a string containing an appropriate method not found error for a
// method with [symbol] on [classObj].
static ObjString* methodNotFound(UDogVM* vm, ObjClass* classObj, int symbol) {
	char message[MAX_VARIABLE_NAME + MAX_METHOD_NAME + 24];
	sprintf(message, "%s does not implement '%s'.",
		classObj->name->value,
		vm->methodNames.data[symbol].buffer);

	return AS_STRING(udogNewString(vm, message, strlen(message)));
}

// Verifies that [superclass] is a valid object to inherit from. That means it
// must be a class and cannot be the class of any built-in type.
//
// If successful, returns NULL. Otherwise, returns a string for the runtime
// error message.
static ObjString* validateSuperclass(UDogVM* vm, ObjString* name,
                                     Value superclassValue) {
	// Make sure the superclass is a class.
	if (!IS_CLASS(superclassValue)) {
		return AS_STRING(udogNewString(vm, "Must inherit from a class.", 26));
	}

	// Make sure it doesn't inherit from a sealed built-in type. Primitive methods
	// on these classes assume the instance is one of the other Obj___ types and
	// will fail horribly if it's actually an ObjInstance.
	ObjClass* superclass = AS_CLASS(superclassValue);
	if (superclass == vm->metatable.classClass ||
	        superclass == vm->metatable.fiberClass ||
	        superclass == vm->metatable.fnClass || // Includes OBJ_CLOSURE.
	        superclass == vm->metatable.listClass ||
	        superclass == vm->metatable.mapClass ||
	        superclass == vm->metatable.rangeClass ||
	        superclass == vm->metatable.stringClass) {
		char message[70 + MAX_VARIABLE_NAME];
		sprintf(message, "%s cannot inherit from %s.",
		        name->value, superclass->name->value);
		return AS_STRING(udogNewString(vm, message, strlen(message)));
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////
//// MODULES
///////////////////////////////////////////////////////////////////////////////////

// Checks whether a module with the given name exists, and if so
// Replaces it with the given module
// Otherwise the module is added to the module list
void udogSaveModule(UDogVM* vm, ObjModule* module, ObjString* name) {
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = udogMapFind(vm->modules, OBJ_VAL(name));
	if (index != UINT32_MAX) {
		vm->modules->entries[index].value = OBJ_VAL(module);
		return;
	}
	
	// Store it in the VM's module registry so we don't load the same module
	// multiple times.
	module->name = name;
	udogMapSet(vm, vm->modules, OBJ_VAL(name), OBJ_VAL(module));
}

// Looks up the core module in the module map.
static ObjModule* getCoreModule(UDogVM* vm) {
	uint32_t entry = udogMapFind(vm->modules, NULL_VAL);
	ASSERT(entry != UINT32_MAX, "Could not find core module.");
	return AS_MODULE(vm->modules->entries[entry].value);
}

// Ready a new module
ObjModule* udogReadyNewModule(UDogVM* vm) {
	ObjModule* module = udogNewModule(vm);
	
	UDOG_PIN(vm, module);
	// Implicitly import the core module.
	ObjModule* coreModule = getCoreModule(vm);
	for (int i = 0; i < coreModule->variables.count; i++) {
		udogDefineVariable(vm, module,
						   coreModule->variableNames.data[i].buffer,
						   coreModule->variableNames.data[i].length,
						   coreModule->variables.data[i]);
		module->count--;
	}
	UDOG_UNPIN(vm);
	return module;
}

static ObjModule* loadModule(UDogVM* vm, Value name, Value source) {
	ObjModule* module = NULL;

	// See if the module has already been loaded.
	uint32_t index = udogMapFind(vm->modules, name);
	if (index == UINT32_MAX) {
		module = udogReadyNewModule(vm);
		module->name = AS_STRING(name);
		// Store it in the VM's module registry so we don't load the same module
		// multiple times.
		UDOG_PIN(vm, module);
		udogMapSet(vm, vm->modules, name, OBJ_VAL(module));
		UDOG_UNPIN(vm);
		if (source == NULL_VAL) return module;
	}
	else {
		// Execute the new code in the context of the existing module.
		module = AS_MODULE(vm->modules->entries[index].value);
		
		if (source == NULL_VAL) return module;
	}
	
	UDOG_PIN(vm, module);
	ObjFn* fn = udogCompile(vm, module, AS_CSTRING(name), AS_CSTRING(source));
	UDOG_UNPIN(vm);
	if (fn == NULL) return NULL;

	module->func = fn;
	module->source = AS_STRING(source);
	return module;
}

ObjFiber* loadModuleFiber(UDogVM* vm, Value name, Value source) {
	ObjModule* module = loadModule(vm, name, source);
	UDOG_PIN(vm, module);
	ObjFiber* moduleFiber = udogNewFiber(vm, (Obj*)module->func);
	UDOG_UNPIN(vm);

	// Return the fiber that executes the module.
	return moduleFiber;
}

ObjModule* udogImportModuleVar(UDogVM* vm, Value nameValue) {
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = udogMapFind(vm->modules, nameValue);
	if (index != UINT32_MAX) {
		return AS_MODULE(vm->modules->entries[index].value);
	}

	// Load the module's source code from the embedder.
	UDogValue* source = vm->loadModule(vm, AS_CSTRING(nameValue));
	if (source == NULL) {
		// Couldn't load the module, create a new Module.
		ObjModule* module = udogReadyNewModule(vm);
		UDOG_PIN(vm, module);
		// Store it in the VM's module registry so we don't load the same module
		// multiple times.
		udogMapSet(vm, vm->modules, nameValue, OBJ_VAL(module));
		UDOG_UNPIN(vm);
		return module;
	}

	ObjModule* module = loadModule(vm, nameValue, udogGetHostObject(vm, source));
	UDOG_PIN(vm, module);
	udogReleaseObject(vm, source);
	UDOG_UNPIN(vm);
	// Return the module.
	return module;
}

static ObjFiber* loadModuleNoMemory(UDogVM* vm, Value name, const char* source) {
	ObjModule* module = loadModule(vm, name, NULL_VAL);
	ObjFn* fn = udogCompile(vm, module, AS_CSTRING(name), source);
	if (fn == NULL) return NULL;
	module->func = fn;
	
	// Return the fiber that executes the module.
	return udogNewFiber(vm, (Obj*)module->func);
}

static Value importModule(UDogVM* vm, Value name) {
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = udogMapFind(vm->modules, name);
	if (index != UINT32_MAX) return NULL_VAL;

	// Load the module's source code from the embedder.
	UDogValue* source = vm->loadModule(vm, AS_CSTRING(name));
	if (source == NULL) {
		// Couldn't load the module.
		Value error = udogNewUninitializedString(vm, 25 + AS_STRING(name)->length);
		sprintf(AS_STRING(error)->value, "Could not find module '%s'.",
		        AS_CSTRING(name));
		return error;
	}

	ObjFiber* moduleFiber = loadModuleFiber(vm, name, udogGetHostObject(vm, source));
	UDOG_PIN(vm, moduleFiber);
	udogReleaseObject(vm, source);
	UDOG_UNPIN(vm);
	// Return the fiber that executes the module.
	return OBJ_VAL(moduleFiber);
}

static bool importVariable(UDogVM* vm, Value moduleName, Value variableName,
                           Value* result) {
	UNUSED(variableName);
	uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
	ASSERT(moduleEntry != UINT32_MAX, "Should only look up loaded modules.");

	ObjModule* module = AS_MODULE(vm->modules->entries[moduleEntry].value);
	
	*result = OBJ_VAL(module);
	return true;
}

///////////////////////////////////////////////////////////////////////////////////
//// CHECKING STACK AND CALLFRAME
///////////////////////////////////////////////////////////////////////////////////

bool udogFiberStack(UDogVM* vm, ObjFiber* fiber, Value** stackstart) {
	Value* oldStackBegin = fiber->stack;
	int stacktop = (fiber->stacktop - fiber->stack);
	int newSize = 0;
	// Stack is too short, increase the length
	if (stacktop + 2 > (int) fiber->stacksize) {
		newSize = fiber->stacksize * STACKSIZE_GROW_FACTOR;
		
		if (newSize > vm->stackMax)
			return true;
	}
	// Stack is too large, decrease the length
	else if (fiber->stacksize > STACKSIZE && stacktop < (int) fiber->stacksize / STACKSIZE_GROW_FACTOR) {
		newSize = fiber->stacksize / STACKSIZE_GROW_FACTOR;
	}
	else { 
		return false; 
	}
	
	fiber->stack = (Value*) udogReallocate(vm, fiber->stack, fiber->stacksize * sizeof(Value), newSize * sizeof(Value));
	fiber->stacksize = newSize;
	
	// reset stacktop
	fiber->stacktop = fiber->stack + stacktop;
	
	// reset upvalues
	Upvalue* upval = fiber->openUpvalues;
	while (upval != NULL) {
		upval->value = (upval->value - oldStackBegin) + fiber->stack;
		upval = upval->next;
	}
	
	// reset tops for all the callframes
	for(int i=0; i<fiber->numFrames; i++) {
		CallFrame* call = &(fiber->frames[i]);
		call->top = (call->top - oldStackBegin) + fiber->stack;
	}
	
	// reset stackstart variable
	*stackstart = (fiber->frames[fiber->numFrames-1]).top;

	return false;
}

// Check if we need to grow or shrink the callframe size
bool udogFiberCallFrame(UDogVM* vm, ObjFiber* fiber, CallFrame** frame) {
	int newSize = 0;
	// Stack is too short, increase the length
	if (fiber->numFrames + 2 > (int) fiber->framesize) {
		newSize = fiber->framesize * CALLFRAME_GROW_FACTOR;
		
		if (newSize > vm->callDepth)
			return true;
	}
	// Stack is too large, decrease the length
	else if (fiber->framesize > CALLFRAMESIZE && fiber->numFrames < (int) fiber->framesize / CALLFRAME_GROW_FACTOR) {
		newSize = fiber->framesize / CALLFRAME_GROW_FACTOR;
	}
	else { 
		return false; 
	}

	fiber->frames = (CallFrame*) udogReallocate(vm, fiber->frames, fiber->framesize * sizeof(CallFrame), newSize * sizeof(CallFrame));
	fiber->framesize = newSize;
	
	// reset frame variable
	*frame = &(fiber->frames[fiber->numFrames-1]);
	
	return false;
}

///////////////////////////////////////////////////////////////////////////////////
//// INTERPRETER
///////////////////////////////////////////////////////////////////////////////////

bool runInterpreter(UDogVM* vm) {
	//Load the DispatchTable
#ifdef COMPUTED_GOTO
	// Note that the order of instructions here must exacly match the Code enum
	// in udog_vm.h or horrendously bad things happen.
	static void* dispatchTable[] = {
		#define OPCODE(name) &&code_##name,
		#include "udog_opcodes.h"
		#undef OPCODE
	};

#endif

#ifdef COMPUTED_GOTO
	#define INTERPRET_LOOP DISPATCH();

	#if UDOG_DEBUG_TRACE_INSTRUCTIONS
		// Prints the stack and instruction before each instruction is executed.
		#define DISPATCH() \
			{ \
			  udogDebugPrintStack(vm, fiber); \
			  udogDebugPrintInstruction(vm, fn, (int)(ip - fn->bytecode)); \
			  instruction = (Code) *ip++; \
			  goto *dispatchTable[instruction]; \
			}
	  #else

		#define DISPATCH() goto *dispatchTable[instruction = (Code) READ_INSTRUCTION()];

	  #endif
	  
	#define CASECODE(name) code_##name
#else
	#define INTERPRET_LOOP	for (;;) switch (instruction = READ_INSTRUCTION())
	#define DISPATCH() break
	#define CASECODE(name) case CODE_##name
#endif

// Terminates the current fiber with error string [error]. If another calling
// fiber is willing to catch the error, transfers control to it, otherwise
// exits the interpreter.
#define RUNTIME_ERROR(error) \
	do { \
		STORE_FRAME(); \
		fiber = runtimeError(vm, fiber, error); \
		if (fiber == NULL) return false; \
		LOAD_FRAME(); \
		DISPATCH(); \
	} while (false)
		
#define RUNTIME_THROW(error) \
	do { \
		STORE_FRAME(); \
		fiber = runtimeThrow(vm, fiber, error); \
		if (fiber == NULL) return false; \
		LOAD_FRAME(); \
		DISPATCH(); \
	} while (false)

#define CHECK_STACK() if (udogFiberStack(vm, fiber, &stackStart)) { \
							runtimeCrash(vm, fiber, "Stack size limit reached"); \
							return false; \
						}

#define CHECK_CALLFRAME() if (udogFiberCallFrame(vm, fiber, &frame)) { \
							runtimeCrash(vm, fiber, "Callframe size limit reached"); \
							return false; \
						}					

// These macros are designed to only be invoked within this function.
#define PUSH(value)  (*fiber->stacktop++ = value)
#define POP()        (*(--fiber->stacktop))
#define DROP()       (fiber->stacktop--)
#define PEEK()       (*(fiber->stacktop - 1))
#define PEEK2()      (*(fiber->stacktop - 2))
#define READ_BYTE()  (*ip++)
#define READ_SHORT() (ip += 2, (ip[-2] << 8) | ip[-1])

#define READ_INT() (ip += 4, ((int) ip[-4] << 24) | ((int) ip[-3] << 16) | (ip[-2] << 8) | ip[-1])

#define READ_LONG() (ip += 8, ((int) ip[-8] << 56) | ((int) ip[-7] << 48) | ((int) ip[-6] << 40) | ((int) ip[-5] << 32) | \
					((int) ip[-4] << 24) | ((int) ip[-3] << 16) | (ip[-2] << 8) | ip[-1])

#define READ_INSTRUCTION() READ_BYTE()

#define READ_BOOL() READ_BYTE()

// Defines for the global argument
#if GLOBAL_BYTE == 8
	#define READ_GLOBAL() READ_LONG()
#elif GLOBAL_BYTE == 4
	#define READ_GLOBAL() READ_INT()
#elif GLOBAL_BYTE == 2
	#define READ_GLOBAL() READ_SHORT()
#elif GLOBAL_BYTE == 1
	#define READ_GLOBAL() READ_BYTE()
#else
	#error Global byte instruction size not set
#endif

// Defines for the upvalue argument
#if UPVALUE_BYTE == 8
	#define READ_UPVALUE() READ_LONG()
#elif UPVALUE_BYTE == 4
	#define READ_UPVALUE() READ_INT()
#elif UPVALUE_BYTE == 2
	#define READ_UPVALUE() READ_SHORT()
#elif UPVALUE_BYTE == 1
	#define READ_UPVALUE() READ_BYTE()
#else
	#error Upvalue byte instruction size not set
#endif

// Defines for the constant argument
#if CONSTANT_BYTE == 8
	#define READ_CONSTANT() READ_LONG()
#elif CONSTANT_BYTE == 4
	#define READ_CONSTANT() READ_INT()
#elif CONSTANT_BYTE == 2
	#define READ_CONSTANT() READ_SHORT()
#elif CONSTANT_BYTE == 1
	#define READ_CONSTANT() READ_BYTE()
#else
	#error Constant byte instruction size not set
#endif

// Defines for the local argument
#if LOCAL_BYTE == 8
	#define READ_LOCAL() READ_LONG()
#elif LOCAL_BYTE == 4
	#define READ_LOCAL() READ_INT()
#elif LOCAL_BYTE == 2
	#define READ_LOCAL() READ_SHORT()
#elif LOCAL_BYTE == 1
	#define READ_LOCAL() READ_BYTE()
#else
	#error Local byte instruction size not set
#endif

// Defines for the fields argument
#if FIELD_BYTE == 8
	#define READ_FIELD() READ_LONG()
#elif FIELD_BYTE == 4
	#define READ_FIELD() READ_INT()
#elif FIELD_BYTE == 2
	#define READ_FIELD() READ_SHORT()
#elif FIELD_BYTE == 1
	#define READ_FIELD() READ_BYTE()
#else
	#error Field byte instruction size not set
#endif

// Defines for the offset argument
#if OFFSET_BYTE == 8
	#define READ_OFFSET() READ_LONG()
#elif OFFSET_BYTE == 4
	#define READ_OFFSET() READ_INT()
#elif OFFSET_BYTE == 2
	#define READ_OFFSET() READ_SHORT()
#else
	#error offset byte instruction size not set
#endif

// Defines for the method argument
#if METHOD_BYTE == 4
	#define READ_METHOD() READ_INT()
#elif METHOD_BYTE == 2
	#define READ_METHOD() READ_SHORT()
#else
	#error Method byte instruction size not set
#endif

	// Use this before a CallFrame is pushed to store the local variables back
	// into the current one.
#define STORE_FRAME() frame->pc = ip

#define LOAD_FRAME() \
	frame = &(fiber->frames[fiber->numFrames-1]); \
	stackStart = frame->top; \
	ip = frame->pc; \
	if (frame->fn->type == OBJ_FN) { \
		fn = (ObjFn*)frame->fn; \
	} \
	else { \
		fn = ((ObjClosure*)frame->fn)->fn; \
	}

	// Try to get these into local variables (registers). They are accessed frequently in the loop
	// but assigned less frequently. Keeping them in locals and updating them when
	// a call frame has been pushed or popped gives a large speed boost.
	
	// Current running fiber
	register ObjFiber* fiber = vm->fiber;
	// current running stackframe
	CallFrame* frame = NULL;
	// Start of the stack of the current frame
	Value* stackStart = NULL;
	// Current position of the program counter
	register programcounter* ip = NULL;
	// Pointer to the current executing function
	register ObjFn* fn = NULL;

	//Load the first frame to be executed
	LOAD_FRAME();
	
	Code instruction;
	INTERPRET_LOOP {
		CASECODE(EMPTY):
			DISPATCH();
			
		// Load local [x]
		CASECODE(LOAD_LOCAL_0):
		CASECODE(LOAD_LOCAL_1):
		CASECODE(LOAD_LOCAL_2):
		CASECODE(LOAD_LOCAL_3):
		CASECODE(LOAD_LOCAL_4):
		CASECODE(LOAD_LOCAL_5):
		CASECODE(LOAD_LOCAL_6):
		CASECODE(LOAD_LOCAL_7):
		CASECODE(LOAD_LOCAL_8):
			// Push the current bytecode instruction - the local onto the stack [push stack[0] to stack[8]]
			PUSH(stackStart[instruction - CODE_LOAD_LOCAL_0]);
			CHECK_STACK();
			DISPATCH();
		// Load a local [index = nextbyte] onto the top of the stack
		CASECODE(LOAD_LOCAL):
			// Push [nextbyte] onto the top of the stack
			PUSH(stackStart[READ_LOCAL()]);
			CHECK_STACK();
			DISPATCH();
		// Load the this field (bottom stack)
		// It always refers to the instance whose method is currently being executed. 
		// This lets you invoke methods on "yourself".
		CASECODE(LOAD_FIELD_THIS):
		{
			udog_integer field = READ_FIELD();
			Value receiver = stackStart[0];
			ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
			ObjInstance* instance = AS_INSTANCE(receiver);
			ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
			PUSH(instance->fields[field]);
			CHECK_STACK();
			DISPATCH();
		}
		// Pop the top of the stack
		CASECODE(POP):   DROP(); DISPATCH();
		// Duplicate the top of the stack
		CASECODE(DUP):  
		{
			Value value = PEEK();
			PUSH(value);
			CHECK_STACK(); 
			DISPATCH();
		}
		// Push [null] onto the stack
		CASECODE(NULL):  PUSH(NULL_VAL); CHECK_STACK(); DISPATCH();
		// Push [false] onto the stack
		CASECODE(FALSE): PUSH(FALSE_VAL); CHECK_STACK(); DISPATCH();
		// Push [true] onto the stack
		CASECODE(TRUE):  PUSH(TRUE_VAL); CHECK_STACK(); DISPATCH();
	
		// Call a function with [x] arguments
		CASECODE(CALL_0):
		CASECODE(CALL_1):
		CASECODE(CALL_2):
		CASECODE(CALL_3):
		CASECODE(CALL_4):
		CASECODE(CALL_5):
		CASECODE(CALL_6):
		CASECODE(CALL_7):
		CASECODE(CALL_8):
		CASECODE(CALL_9):
		CASECODE(CALL_10):
		CASECODE(CALL_11):
		CASECODE(CALL_12):
		CASECODE(CALL_13):
		CASECODE(CALL_14):
		CASECODE(CALL_15):
		CASECODE(CALL_16):
		{
			// Add one for the implicit receiver argument.
			int numArgs = instruction - CODE_CALL_0 + 1;
			udog_integer symbol = READ_METHOD();

			Value* args = fiber->stacktop - numArgs;
			ObjClass* classObj = udogGetClassInline(vm, args[0]);
			
			// If the class's method table doesn't include the symbol, bail.
			if (symbol >= classObj->methods.count) {
				RUNTIME_ERROR(methodNotFound(vm, classObj, (int) symbol));
			}
			
			Method* method = &classObj->methods.data[symbol];
			
			switch (method->type) {
				case METHOD_PRIMITIVE:
				{
					// After calling this, the result will be in the first arg slot.
					switch (method->fn.primitive(vm, fiber, args, &numArgs)) {
						case PRIM_VALUE:
							// The result is now in the first arg slot. Discard the other
							// stack slots.
							fiber->stacktop -= numArgs - 1;
							break;

						case PRIM_ERROR:
						default:
							RUNTIME_THROW(args[0]);

						case PRIM_CALL:
							STORE_FRAME();
							callFunction(fiber, AS_OBJ(args[0]), numArgs);
							LOAD_FRAME();
						break;

						case PRIM_RUN_FIBER:
							STORE_FRAME();
							if (IS_NULL(args[0])) return true;
							fiber = AS_FIBER(args[0]);
							vm->fiber = fiber;
							LOAD_FRAME();
						break;
						
						case PRIM_NONE:
						break;
					}
					break;
				}

				case METHOD_FOREIGN:
					callForeign(vm, fiber, method->fn.foreign, numArgs);
					break;

				case METHOD_BLOCK:
					STORE_FRAME();
					callFunction(fiber, method->fn.obj, numArgs);
					LOAD_FRAME();
					break;

				case METHOD_NONE:
				default:
					RUNTIME_ERROR(methodNotFound(vm, classObj, symbol));
					break;
			}
			CHECK_CALLFRAME();
			DISPATCH();
		}
		
		// store the top of the stack into [nextbyte]
		CASECODE(STORE_LOCAL):
			stackStart[READ_LOCAL()] = PEEK();
			DISPATCH();
		
		// Push a constant onto the stack
		CASECODE(CONSTANT):
			PUSH(fn->constants[READ_CONSTANT()]);
			CHECK_STACK();
			DISPATCH();
		
		// Call a super function
		CASECODE(SUPER_0):
		CASECODE(SUPER_1):
		CASECODE(SUPER_2):
		CASECODE(SUPER_3):
		CASECODE(SUPER_4):
		CASECODE(SUPER_5):
		CASECODE(SUPER_6):
		CASECODE(SUPER_7):
		CASECODE(SUPER_8):
		CASECODE(SUPER_9):
		CASECODE(SUPER_10):
		CASECODE(SUPER_11):
		CASECODE(SUPER_12):
		CASECODE(SUPER_13):
		CASECODE(SUPER_14):
		CASECODE(SUPER_15):
		CASECODE(SUPER_16):
		{
			// Add one for the implicit receiver argument.
			int numArgs = instruction - CODE_SUPER_0 + 1;
			udog_integer symbol = READ_METHOD();

			Value* args = fiber->stacktop - numArgs;
			ObjClass* receive = udogGetClassInline(vm, args[0]);

			// Ignore methods defined on the receiver's immediate class.
			//int super = READ_CONSTANT();
			//ObjClass* classObj = AS_CLASS(receive->superclasses->elements[super]);
			ObjClass* classObj = receive;
			ObjList* list = AS_LIST(fn->constants[READ_CONSTANT()]);
			for(int i=0; i<list->count; i++) {
				uint32_t super = AS_NUM(list->elements[i]);
				classObj = AS_CLASS(classObj->superclasses->elements[super]);
			}

			// If the class's method table doesn't include the symbol, bail.
			if (symbol >= classObj->methods.count) {
				RUNTIME_ERROR(methodNotFound(vm, classObj, symbol));
			}

			Method* method = &classObj->methods.data[symbol];
			switch (method->type) {
				case METHOD_PRIMITIVE:
				{
					// After calling this, the result will be in the first arg slot.
					switch (method->fn.primitive(vm, fiber, args, &numArgs)) {
						case PRIM_VALUE:
							// The result is now in the first arg slot. Discard the other
							// stack slots.
							fiber->stacktop -= numArgs - 1;
							break;

						case PRIM_ERROR:
						default:
							RUNTIME_THROW(args[0]);

						case PRIM_CALL:
							STORE_FRAME();
							callFunction(fiber, AS_OBJ(args[0]), numArgs);
							LOAD_FRAME();
						break;

						case PRIM_RUN_FIBER:
							STORE_FRAME();
							if (IS_NULL(args[0])) return true;
							fiber = AS_FIBER(args[0]);
							vm->fiber = fiber;
							LOAD_FRAME();
						break;
					}
					break;
				}

				case METHOD_FOREIGN:
					callForeign(vm, fiber, method->fn.foreign, numArgs);
					break;

				case METHOD_BLOCK:
					STORE_FRAME();
					callFunction(fiber, method->fn.obj, numArgs);
					LOAD_FRAME();
					break;

				case METHOD_NONE:
				default:
					RUNTIME_ERROR(methodNotFound(vm, classObj, symbol));
					break;
			}
			CHECK_CALLFRAME();
			DISPATCH();
		}
		
		// Load an upvalue from the current running closure
		CASECODE(LOAD_UPVALUE):
		{
			// Get the array of upvalues
			Upvalue** upvalues = ((ObjClosure*)frame->fn)->upvalues;
			// Push onto the stack
			PUSH(*upvalues[READ_UPVALUE()]->value);
			CHECK_STACK();
			DISPATCH();
		}
		// Store an opvalue in a closure
		CASECODE(STORE_UPVALUE):
		{
			Upvalue** upvalues = ((ObjClosure*)frame->fn)->upvalues;
			*upvalues[READ_UPVALUE()]->value = PEEK();
			DISPATCH();
		}
		CASECODE(LOAD_MODULE_VAR):
			PUSH(fn->module->variables.data[READ_SHORT()]);
			CHECK_STACK();
			DISPATCH();

		CASECODE(STORE_MODULE_VAR):
			fn->module->variables.data[READ_SHORT()] = PEEK();
			DISPATCH();
		
		// Store a field from the this class
		CASECODE(STORE_FIELD_THIS):
		{
			udog_integer field = READ_FIELD();
			Value receiver = stackStart[0];
			ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
			ObjInstance* instance = AS_INSTANCE(receiver);
			ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
			instance->fields[field] = PEEK();
			DISPATCH();
		}
		
		// Load a field
		CASECODE(LOAD_FIELD):
		{
			udog_integer field = READ_FIELD();
			Value receiver = POP();
			ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
			ObjInstance* instance = AS_INSTANCE(receiver);
			ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
			PUSH(instance->fields[field]);
			CHECK_STACK();
			DISPATCH();
		}
		
		// Store a field into an instance
		CASECODE(STORE_FIELD):
		{
			udog_integer field = READ_FIELD();
			Value receiver = POP();
			ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
			ObjInstance* instance = AS_INSTANCE(receiver);
			ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
			instance->fields[field] = PEEK();
			DISPATCH();
		}
		
		// Jump around in the bytecode
		CASECODE(JUMP):
		{
			udog_integer offset = READ_OFFSET();
			ip += offset;
			DISPATCH();
		}
		
		// Jump back up to the top of the loop
		CASECODE(LOOP):
		{
			// Jump back to the top of the loop.
			udog_integer offset = READ_OFFSET();
			ip -= offset;
			DISPATCH();
		}
		
		// Jump if the top of the stack is [false] or [null], and pop the top
		CASECODE(JUMP_IF):
		{
			udog_integer offset = READ_OFFSET();
			Value condition = POP();

			if (IS_FALSE(condition) || IS_NULL(condition)) ip += offset;
			DISPATCH();
		}

		// Jump if top of the stack is [false] or [null], else pop the top of the stack
		CASECODE(AND):
		{
			udog_integer offset = READ_OFFSET();
			Value condition = PEEK();

			if (IS_FALSE(condition) || IS_NULL(condition)) {
				// Short-circuit the right hand side.
				ip += offset;
			}
			else {
				// Discard the condition and evaluate the right hand side.
				DROP();
			}
			DISPATCH();
		}
		
		// Pop the top if top of the stack is [false] or [null], else jump
		CASECODE(OR):
		{
			udog_integer offset = READ_OFFSET();
			Value condition = PEEK();

			if (IS_FALSE(condition) || IS_NULL(condition)) {
				// Discard the condition and evaluate the right hand side.
				DROP();
			}
			else {
				// Short-circuit the right hand side.
				ip += offset;
			}
			DISPATCH();
		}
		
		// Top of the stack is a class that we expect
		// Below is an value which has to have the same type
		// Check whether they have the same type
		// Push the resulting boolean onto the stack
		CASECODE(IS):
		{
			Value expected = POP();
			if (!IS_CLASS(expected)) {
				const char* message = "Right operand must be a class.";
				RUNTIME_ERROR(AS_STRING(udogNewString(vm, message, strlen(message))));
			}
			
			ObjClass* actual = udogGetClass(vm, POP());
			bool isInstance = udogIsSubClass(actual, AS_CLASS(expected));
			
			PUSH(BOOL_VAL(isInstance));
			CHECK_STACK();
			DISPATCH();
		}
	
		// Close the upvalue closest to the top of the stack
		CASECODE(CLOSE_UPVALUE):
			closeUpvalue(fiber);
			DROP();
			DISPATCH();
		
		// Return from a function
		CASECODE(RETURN):
		{
			// Pop the result
			Value result = POP();
			// Reduce the current frames in use
			fiber->numFrames--;

			// Close any upvalues still in scope.
			Value* firstValue = stackStart;
			while (fiber->openUpvalues != NULL && fiber->openUpvalues->value >= firstValue) {
				closeUpvalue(fiber);
			}

			// If the fiber is complete, end it.
			if (fiber->numFrames == 0) {
				// If this is the main fiber, we're done.
				if (fiber->caller == NULL) {
					fiber->stack[1] = result;
					return true;
				}

				// We have a calling fiber to resume.
				fiber = fiber->caller;
				vm->fiber = fiber;

				// Store the result in the resuming fiber.
				*(fiber->stacktop - 1) = result;
			}
			else {
				// Store the result of the block in the first slot, which is where the
				// caller expects it.
				stackStart[0] = result;

				// Discard the stack slots for the call frame (leaving one slot for the
				// result).
				fiber->stacktop = frame->top + 1;
			}

			LOAD_FRAME();
			CHECK_STACK();
			CHECK_CALLFRAME();
			DISPATCH();
		}

		// Create a new closure and push it onto the stack
		CASECODE(CLOSURE):
		{
			ObjFn* prototype = AS_FN(fn->constants[READ_CONSTANT()]);

			ASSERT(prototype->numUpvalues > 0,
				"Should not create closure for functions that don't need it.");

			// Create the closure and push it on the stack before creating upvalues
			// so that it doesn't get collected.
			ObjClosure* closure = udogNewClosure(vm, prototype);
			PUSH(OBJ_VAL(closure));

			// Capture upvalues.
			for (int i = 0; i < prototype->numUpvalues; i++) {
				bool isLocal = READ_BOOL();
				udog_integer index = READ_LOCAL();
				if (isLocal) {
					// Make an new upvalue to close over the parent's local variable.
					closure->upvalues[i] = captureUpvalue(vm, fiber, frame->top + index);
				}
				else {
					// Use the same upvalue as the current call frame.
					closure->upvalues[i] = ((ObjClosure*)frame->fn)->upvalues[index];
				}
			}
			CHECK_STACK();
			DISPATCH();
		}
		
		// Create a new class and push it onto the stack
		CASECODE(CLASS):
		{
			// Stack
			// Possible classes
			// NULL or first superclass
			// Name
			ObjString* name = AS_STRING(POP());
			UDOG_PIN(vm, name);
			ObjClass* superclass = vm->metatable.objectClass;

			// Use implicit Object superclass if none given.
			if (!IS_NULL(PEEK())) {
				ObjString* error = validateSuperclass(vm, name, PEEK());
				if (error != NULL) RUNTIME_ERROR(error);
				superclass = AS_CLASS(PEEK());
			}
			DROP();
			
			udog_integer numFields = READ_FIELD();
			udog_integer numSuperClasses = READ_CONSTANT() - 1;

			ObjClass* classObj;
			if (superclass == vm->metatable.objectClass)  
				classObj = udogNewClass(vm, superclass, numFields, name);
			else {
				classObj = udogNewClass(vm, NULL, numFields, name);
				UDOG_PIN(vm, classObj);
				udogAddFirstSuper(vm, classObj, superclass);
				UDOG_UNPIN(vm);
			}
			
			UDOG_UNPIN(vm);
			UDOG_PIN(vm, classObj);
			int i = 1;
			while (numSuperClasses > 0) {
				if (!IS_NULL(PEEK())) {
					ObjString* error = validateSuperclass(vm, name, PEEK());
					if (error != NULL) RUNTIME_ERROR(error);
					superclass = AS_CLASS(PEEK());
				
					udogAddSuperclass(vm, i, classObj, superclass);
				}
				DROP();
				i++;
				numSuperClasses--;
			}

			// Now that we know the total number of fields, make sure we don't
			// overflow.
			if (classObj->numFields > MAX_FIELDS) {
				char message[70 + MAX_VARIABLE_NAME];
				sprintf(message,
					"Class '%s' may not have more than %d fields, including inherited "
					"ones.", name->value, MAX_FIELDS);

				RUNTIME_ERROR(AS_STRING(udogNewString(vm, message, strlen(message))));
			}
			UDOG_UNPIN(vm);
			PUSH(OBJ_VAL(classObj));
			CHECK_STACK();
			DISPATCH();
		}
		
		// Create a new method and push it onto the stack
		CASECODE(METHOD_INSTANCE):
		CASECODE(METHOD_STATIC):
		{
			udog_integer symbol = READ_METHOD();
			ObjClass* classObj = AS_CLASS(PEEK());
			Value method = PEEK2();
			// Binds the code of methodValue to the classObj (not the metaclass)
			// if the type is a static method: the classObj will become the metaclass
			// afterwords, bind the method to the vm
			bindMethod(vm, (int) instruction, symbol, classObj, method);
			DROP();
			DROP();
			DISPATCH();
		}
		
		CASECODE(LOAD_MODULE): {
			Value name = fn->constants[READ_CONSTANT()];
			Value result = importModule(vm, name);
			
			// If it returned a string, it was an error message.
			if (IS_STRING(result)) RUNTIME_ERROR(AS_STRING(result));
		
			// Make a slot that the module's fiber can use to store its result in.
			// It ends up getting discarded, but CODE_RETURN expects to be able to
			// place a value there.
			PUSH(NULL_VAL);
		
			// If it returned a fiber to execute the module body, switch to it.
			if (IS_FIBER(result)) {
				// Return to this module when that one is done.
				AS_FIBER(result)->caller = fiber;
		
				STORE_FRAME();
				fiber = AS_FIBER(result);
				vm->fiber = fiber;
				LOAD_FRAME();
			}
			
			DISPATCH();
		}
		
		CASECODE(IMPORT_VARIABLE): {
			Value module = fn->constants[READ_CONSTANT()];
			Value variable = fn->constants[READ_CONSTANT()];
			Value result;
			if (importVariable(vm, module, variable, &result)) {
				PUSH(result);
			}
			else {
				RUNTIME_ERROR(AS_STRING(result));
			}
			DISPATCH();
		}
		// Create a new class and push it onto the stack
		CASECODE(MODULE):
		{
			//ObjString* name = AS_STRING(POP());
			ObjModule* module = AS_MODULE(POP());
			
			PUSH(OBJ_VAL(module));
			CHECK_STACK();
			DISPATCH();
		}
		
		// End code label
		CASECODE(END):
			// A CODE_END should always be preceded by a CODE_RETURN. If we get here,
			// the compiler generated wrong code.
			UNREACHABLE("end");
		
		// End code label
		CASECODE(BREAK): 
		{
			STORE_FRAME();
			checkDebugger(vm);
			if (vm->fiber == NULL)
				return true;
			LOAD_FRAME();
			DISPATCH();
		}
	}

	// We should only exit this function from an explicit return from CODE_RETURN
	// or a runtime error.
	UNREACHABLE("end interpreter");
	return false;
}

///////////////////////////////////////////////////////////////////////////////////
//// VARIABLES
///////////////////////////////////////////////////////////////////////////////////

// Sets the current Compiler being run to [compiler].
void udogSetCompiler(UDogVM* vm, UDogCompiler* compiler) {
	vm->compiler = compiler;
}

// Execute [source] in the context of the core module.
static UDogLangResult loadIntoCore(UDogVM* vm, const char* source) {
	ObjModule* coreModule = getCoreModule(vm);

	ObjFn* fn = udogCompile(vm, coreModule, "", source);
	if (fn == NULL) return UDOG_COMPILE_ERROR;

	udogPushRoot(vm, (Obj*)fn);
	vm->fiber = udogNewFiber(vm, (Obj*)fn);
	udogPopRoot(vm); // fn.

	return runInterpreter(vm) ? UDOG_SUCCESS : UDOG_RUNTIME_ERROR;
}

UDogLangResult udogInterpret(UDogVM* vm, const char* sourcePath, const char* source) {
	return udogInterpretModule(vm, sourcePath, source, "main");
}

Value udogFindVariable(UDogVM* vm, const char* name) {
	ObjModule* coreModule = getCoreModule(vm);
	int symbol = udogSymbolTableFind(&coreModule->variableNames,
	                                 name, strlen(name));
	return coreModule->variables.data[symbol];
}

int udogFindVariableSymbol(UDogVM* vm, ObjModule* module, const char* name, int length) {
	if (module == NULL) module = getCoreModule(vm);

	return udogSymbolTableFind(&module->variableNames,
	                                 name, length);
}

int udogDeclareVariable(UDogVM* vm, ObjModule* module, const char* name,
                        size_t length) {
	if (module == NULL) module = getCoreModule(vm);
	if (module->variables.count == MAX_GLOBALS) return -2;

	module->count++;
	udogValueBufferWrite(vm, &module->variables, UNDEFINED_VAL);
	return udogSymbolTableAdd(vm, &module->variableNames, name, length);
}

int udogDefineVariable(UDogVM* vm, ObjModule* module, const char* name,
                       size_t length, Value value) {
	if (module == NULL) module = getCoreModule(vm);
	if (module->variables.count == MAX_GLOBALS) return -2;

	if (IS_OBJ(value)) udogPushRoot(vm, AS_OBJ(value));

	// See if the variable is already explicitly or implicitly declared.
	int symbol = udogSymbolTableFind(&module->variableNames, name, length);

	if (symbol == -1) {
		// Brand new variable.
		symbol = udogSymbolTableAdd(vm, &module->variableNames, name, length);
		udogValueBufferWrite(vm, &module->variables, value);
		module->count++;
	}
	else if (IS_UNDEFINED(module->variables.data[symbol])) {
		// Explicitly declaring an implicitly declared one. Mark it as defined.
		module->variables.data[symbol] = value;
	}
	else {
		// Already explicitly declared.
		symbol = -1;
	}

	if (IS_OBJ(value)) udogPopRoot(vm);

	return symbol;
}

///////////////////////////////////////////////////////////////////////////////////
//// GARBAGE COLLECTOR
///////////////////////////////////////////////////////////////////////////////////

void udogPushRoot(UDogVM* vm, Obj* obj) {
	ASSERT(vm->garbageCollector.numTempRoots < UDOG_MAX_TEMP_ROOTS, "Too many temporary roots.");
	vm->garbageCollector.tempRoots[vm->garbageCollector.numTempRoots++] = obj;
}

void udogPopRoot(UDogVM* vm) {
	ASSERT(vm->garbageCollector.numTempRoots > 0, "No temporary roots to release.");
	vm->garbageCollector.numTempRoots--;
}

void udogAddGCObject(UDogVM* vm, Obj* obj) {
	obj->gcflag = (GCFlag) 0;
	
	obj->next = vm->garbageCollector.first;
	vm->garbageCollector.first = obj;
}

/// Used to get statistics from the Garbage collector
void udogGetGCStatistics(UDogVM* vm, int* size, int* destroyed, int* detected, int* newObj, int* nextCycle, int* nbHosts) {
	*size = vm->garbageCollector.bytesAllocated;
	*destroyed = vm->garbageCollector.destroyed;
	*detected = vm->garbageCollector.destroyed;
	*newObj = vm->garbageCollector.active;
	*nextCycle = vm->garbageCollector.nextGC;
	*nbHosts = vm->hostObjects.hostObjects->count;
}

static void collectGarbage(UDogVM* vm) {
	if (vm->garbageCollector.isWorking) return;
#if UDOG_DEBUG_TRACE_MEMORY || UDOG_DEBUG_TRACE_GC
	vm->printFunction("-- gc --\n");

	size_t before = vm->garbageCollector.bytesAllocated;
	double startTime = (double)clock() / CLOCKS_PER_SEC;
#endif
	// Mark all reachable objects.
	vm->garbageCollector.isWorking = true;

	// Reset this. As we mark objects, their size will be counted again so that
	// we can track how much memory is in use without needing to know the size
	// of each *freed* object.
	//
	// This is important because when freeing an unmarked object, we don't always
	// know how much memory it is using. For example, when freeing an instance,
	// we need to know its class to know how big it is, but it's class may have
	// already been freed.
	vm->garbageCollector.bytesAllocated = 0;
	
	if (vm->rootDirectory != NULL) udogMarkObj(vm, (Obj*)vm->rootDirectory);
	
	if (vm->modules != NULL) udogMarkObj(vm, (Obj*)vm->modules);

	// Temporary roots.
	for (int i = 0; i < vm->garbageCollector.numTempRoots; i++) {
		udogMarkObj(vm, vm->garbageCollector.tempRoots[i]);
	}
	
	if (vm->hostObjects.freeNums != NULL)
		udogMarkObj(vm, (Obj*) vm->hostObjects.freeNums);
	if (vm->hostObjects.hostObjects != NULL)
		udogMarkObj(vm, (Obj*) vm->hostObjects.hostObjects);
	
	// The current fiber.
	if (vm->fiber != NULL) udogMarkObj(vm, (Obj*)vm->fiber);

	// Any object the compiler is using (if there is one).
	if (vm->compiler != NULL) udogMarkCompiler(vm, vm->compiler);
	
	// Collect any unmarked objects.
	Obj** obj = &vm->garbageCollector.first;
	while (*obj != NULL) {
		if (!((*obj)->gcflag & FLAG_MARKED)) {
			// This object wasn't reached, so remove it from the list and free it.
			Obj* unreached = *obj;
			*obj = unreached->next;
			udogFreeObj(vm, unreached);
		}
		else {
			// This object was reached, so unmark it (for the next GC) and move on to
			// the next.
			(*obj)->gcflag = (GCFlag) ( (*obj)->gcflag & ~FLAG_MARKED );
			obj = &(*obj)->next;
		}
	}
	
	vm->garbageCollector.nextGC = vm->garbageCollector.bytesAllocated * vm->garbageCollector.heapScalePercent / 100;
	if (vm->garbageCollector.nextGC < vm->garbageCollector.minNextGC) vm->garbageCollector.nextGC = vm->garbageCollector.minNextGC;
	
	vm->garbageCollector.isWorking = false;
	
#if UDOG_DEBUG_TRACE_MEMORY || UDOG_DEBUG_TRACE_GC
	double elapsed = ((double)clock() / CLOCKS_PER_SEC) - startTime;
	vm->printFunction("GC %ld before, %ld after (%ld collected), next at %ld. Took %.3fs.\n",
         before, vm->garbageCollector.bytesAllocated, before - vm->garbageCollector.bytesAllocated, vm->garbageCollector.nextGC,
         elapsed);
#endif
}


///////////////////////////////////////////////////////////////////////////////////
//// MEMORY ALLOCATOR
///////////////////////////////////////////////////////////////////////////////////

void* udogReallocate(UDogVM* vm, void* buffer, size_t oldSize, size_t newSize) {
#if UDOG_DEBUG_TRACE_MEMORY
	vm->printFunction("reallocate %p %ld -> %ld\n", buffer, oldSize, newSize);
	
#endif
#if UDOG_DEBUG_TRACE_MEMORY || UDOG_DEBUG_TRACE_GC
	if (newSize != 0)
		vm->garbageCollector.nbAllocations++;
	
	if (buffer != NULL)
		vm->garbageCollector.nbFrees++;
#endif
	
	if (buffer != NULL && newSize == 0) {
		vm->garbageCollector.active--;
		vm->garbageCollector.destroyed++;
	}
	if (buffer == NULL && newSize != 0)
		vm->garbageCollector.active++;
	// If new bytes are being allocated, add them to the total count. If objects
	// are being completely deallocated, we don't track that (since we don't
	// track the original size). Instead, that will be handled while marking
	// during the next GC.
	vm->garbageCollector.bytesAllocated += newSize - oldSize;
	
#if UDOG_DEBUG_GC_STRESS
	// Since collecting calls this function to free things, make sure we don't
	// recurse.
	if (newSize > 0) collectGarbage(vm);
#else
	if (newSize > 0 && vm->garbageCollector.bytesAllocated > vm->garbageCollector.nextGC) collectGarbage(vm);
#endif

	return vm->reallocate(buffer, oldSize, newSize);
}

///////////////////////////////////////////////////////////////////////////////////
//// THE API
///////////////////////////////////////////////////////////////////////////////////

void udogCollectGarbage(UDogVM* vm) {
	collectGarbage(vm);
}

// Set the garbage collector enabled or disabled
void udogEnableGC(UDogVM* vm, bool enable) {
	vm->garbageCollector.isWorking = enable;
}

static Value* findVariable(UDogVM* vm, ObjModule* module, const char* name) {
	UNUSED(vm);
	int symbol = udogSymbolTableFind(&module->variableNames, name, strlen(name));
	if (symbol != -1) return &module->variables.data[symbol];
	return NULL;
}

static void defineMethod(UDogVM* vm, const char* module, const char* className,
                         const char* signature,
                         udogForeignMethodFn methodFn, bool isStatic) {
	ASSERT(className != NULL, "Must provide class name.");

	int length = (int)strlen(signature);
	ASSERT(signature != NULL, "Must provide signature.");
	ASSERT(strlen(signature) < MAX_METHOD_SIGNATURE, "Signature too long.");

	ASSERT(methodFn != NULL, "Must provide method function.");
	
	ObjModule* coreModule = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			coreModule = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	//ObjModule* coreModule = getCoreModule(vm);
	// Find or create the class to bind the method to.
	int classSymbol = udogSymbolTableFind(&coreModule->variableNames,
										  className, strlen(className));
	ObjClass* classObj;

	if (classSymbol != -1) {
		classObj = AS_CLASS(coreModule->variables.data[classSymbol]);
	}
	else {
		// The class doesn't already exist, so create it.
		size_t len = strlen(className);
		ObjString* nameString = AS_STRING(udogNewString(vm, className, len));
		
		udogPushRoot(vm, (Obj*)nameString);
		
		classObj = udogNewClass(vm, vm->metatable.objectClass, 0, nameString);
		udogDefineVariable(vm, coreModule, className, len, OBJ_VAL(classObj));
		udogPopRoot(vm);
	}
	// Bind the method.
	int methodSymbol = udogSymbolTableEnsure(vm, &vm->methodNames,
					   signature, length);

	Method method;
	method.type = METHOD_FOREIGN;
	method.fn.foreign = methodFn;

	if (isStatic) classObj = classObj->obj.classObj;

	udogBindMethod(vm, classObj, methodSymbol, method);
}

// Compiles [source], a string of UDog source code, to an [ObjFn] that will
// execute that code when invoked.
UDogValue* udogCompileScript(UDogVM* vm, const char* sourcePath, const char* source) {
	return udogCompileScriptModule(vm, sourcePath, source, "main");
}

// Runs [source], a string of UDog source code in a new fiber in [vm].
UDogLangResult udogInterpretModule(UDogVM* vm, const char* sourcePath, const char* source, const char* module) {
	if (strlen(sourcePath) == 0) return loadIntoCore(vm, source);
	
	Value name = udogNewString(vm, module, strlen(module));
	udogPushRoot(vm, AS_OBJ(name));
	
	ObjFiber* fiber = loadModuleNoMemory(vm, name, source);
	
	if (fiber == NULL) {
		udogPopRoot(vm);
		return UDOG_COMPILE_ERROR;
	}
	
	vm->fiber = fiber;
	
	bool succeeded = runInterpreter(vm);
	
	udogPopRoot(vm); // name
	
	return succeeded ? UDOG_SUCCESS : UDOG_RUNTIME_ERROR;
}

// Compiles [source], a string of UDog source code, to an [ObjFn] that will
// execute that code when invoked.
UDogValue* udogCompileScriptModule(UDogVM* vm, const char* sourcePath, const char* source, const char* module) {
	UNUSED(sourcePath);
	Value name = udogNewString(vm, module, strlen(module));
	ObjFiber* fiber = loadModuleNoMemory(vm, name, source);
	
	if (fiber == NULL)
		return NULL;

	// Link the fiber to the GC
	return udogCreateHostObject(vm, OBJ_VAL((Obj*) fiber));
}

// Runs a fiber loaded with a function [key] in the virtual machine [vm].
UDogLangResult udogRunFunction(UDogVM* vm, UDogValue* key) {
	Value val = udogGetHostObject(vm, key);
	if (!IS_FIBER(val)) return UDOG_COMPILE_ERROR;
	
	vm->fiber = AS_FIBER(val);
	if (runInterpreter(vm)) {
		return UDOG_SUCCESS;
	}
	else {
		return UDOG_RUNTIME_ERROR;
	}
}

// Creates an [ObjFn] that invokes a method with [signature] when called.
static ObjFn* makeCallStub(UDogVM* vm, ObjModule* module, const char* signature) {
	int signatureLength = (int)strlen(signature);

	// Count the number parameters the method expects.
	int numParams = 0;
	for (const char* s = signature; *s != '\0'; s++) {
		if (*s == '_') numParams++;
	}

	int method =  udogSymbolTableEnsure(vm, &vm->methodNames,
	                                    signature, signatureLength);

	uint8_t* bytecode = ALLOCATE_ARRAY(vm, uint8_t, 5);
	bytecode[0] = CODE_CALL_0 + numParams;
	int end = 1;
#if METHOD_BYTE == 2
	bytecode[1] = (method >> 8) & 0xff;
	bytecode[2] = method & 0xff;
	end = 3;
#elif METHOD_BYTE == 4
	bytecode[1] = (method >> 24) & 0xffff;
	bytecode[2] = (method >> 16) & 0xffff;
	bytecode[3] = (method >> 8) & 0xffff;
	bytecode[4] = method & 0xffff;
	end = 5;
#endif
	bytecode[end] = CODE_RETURN;
	bytecode[end+1] = CODE_END;

	int* debugLines = ALLOCATE_ARRAY(vm, int, end+2);
	memset(debugLines, 1, end+2);
	
	SymbolTable locals;
	SymbolTable lines;
	udogSymbolTableInit(vm, &locals);
	udogSymbolTableInit(vm, &lines);
	FnDebug* debug = udogNewDebug(vm, NULL, signature, signatureLength, debugLines, locals, lines);
	return udogNewFunction(vm, module, NULL, 0, 0, 0, bytecode, end+2, debug);
}

static UDogValue* getMethod(UDogVM* vm, ObjModule* moduleObj, Value variable,
                          const char* signature) {
	ObjFn* fn = makeCallStub(vm, moduleObj, signature);
	udogPushRoot(vm, (Obj*)fn);

	// Create a single fiber that we can reuse each time the method is invoked.
	ObjFiber* fiber = udogNewFiber(vm, (Obj*)fn);
	udogPushRoot(vm, (Obj*)fiber);

	// Create a handle that keeps track of the function that calls the method.
	UDogValue* ret = udogCreateHostObject(vm, OBJ_VAL(fiber));

	// Store the receiver in the fiber's stack so we can use it later in the call.
	*fiber->stacktop++ = variable;

	udogPopRoot(vm); // fiber.
	udogPopRoot(vm); // fn.

	return ret;
}

UDogValue* udogGetMethod(UDogVM* vm, const char* module, const char* variable,
                          const char* signature) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}

	int variableSlot = udogSymbolTableFind(&moduleObj->variableNames,
	                                       variable, strlen(variable));
	
	if (variableSlot < 0)
		return NULL;
	
	return getMethod(vm, moduleObj, moduleObj->variables.data[variableSlot], signature);
}

UDogValue* udogGetMethodObject(UDogVM* vm, const char* module, UDogValue* variable,
                          const char* signature) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}

	Value val = udogGetHostObject(vm, variable);
	
	return getMethod(vm, moduleObj, val, signature);
}

static UDogValue* staticUDogCall(UDogVM* vm, UDogValue* method, int args, UDogValue* arg, va_list argList) {
	// TODO: Validate that the number of arguments matches what the method
	// expects.

	// Push the arguments.
	ObjFiber* fiber = AS_FIBER(udogGetHostObject(vm, method));
	
	Value value = NULL_VAL;
	value = udogGetHostObject(vm, arg);
	*fiber->stacktop++ = value;
		
	while (args > 0) {
		value = NULL_VAL;
		
		value = udogGetHostObject(vm, va_arg(argList, UDogValue*));
		
		*fiber->stacktop++ = value;
		args--;
	}
	va_end(argList);

	vm->fiber = fiber;
	
	Value receiver = fiber->stack[0];
	Obj* fn = fiber->frames[0].fn;

	// TODO: How does this handle a runtime error occurring?
	runInterpreter(vm);
	
	Value returnValue = fiber->stack[1];

	// Reset the fiber to get ready for the next call.
	udogResetFiber(fiber, fn);

	// Push the receiver back on the stack.
	*fiber->stacktop++ = receiver;
	
	return udogCreateHostObject(vm, returnValue);
}

UDogValue* udogCall(UDogVM* vm, UDogValue* method, int args, ...) {
	// TODO: Validate that the number of arguments matches what the method
	// expects.

	// Push the arguments.
	va_list argList;
	va_start(argList, args);
	Value val = udogGetHostObject(vm, method);
	if (val == NULL_VAL)
		return NULL;
	ObjFiber* fiber = AS_FIBER(val);
	
	while (args > 0) {
		Value value = NULL_VAL;
		
		value = udogGetHostObject(vm, va_arg(argList, UDogValue*));
		
		*fiber->stacktop++ = value;
		args--;
	}
	va_end(argList);

	vm->fiber = fiber;
	
	Value receiver = fiber->stack[0];
	Obj* fn = fiber->frames[0].fn;

	// TODO: How does this handle a runtime error occurring?
	runInterpreter(vm);
	
	Value returnValue = fiber->stack[1];

	// Reset the fiber to get ready for the next call.
	udogResetFiber(fiber, fn);

	// Push the receiver back on the stack.
	*fiber->stacktop++ = receiver;
	
	return udogCreateHostObject(vm, returnValue);
}

// Flush all host objects
void udogFlushHostObjects(UDogVM* vm) {
	vm->hostObjects.freeNums = udogNewList(vm, 0);
	vm->hostObjects.hostObjects = udogNewTable(vm, 0);
	vm->hostObjects.max = 0;
}

// Will create an object with a certain name
UDogValue* udogCreateObject(UDogVM* vm, const char* module, const char* className, const char* signature, int args, ...) {
	UDogValue* meth = udogGetMethod(vm, module, className, "<instantiate>");
	
	UDogValue* ret = udogCall(vm, meth, 0);
	udogReleaseObject(vm, meth);
	
	meth = udogGetMethod(vm, module, className, signature);
	va_list argList;
	va_start(argList, args);
	UDogValue* actualRet = staticUDogCall(vm, meth, args, ret, argList);
	udogReleaseObject(vm, meth);
	udogReleaseObject(vm, ret);
	
	return actualRet;
}

// Bind the object to the VM as an instance
UDogValue* udogBindObject(UDogVM* vm, const char* module, const char* className, void* obj, size_t size) {
	udogDefineClass(vm, module, className, size, NULL);
	
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}

	int variableSlot = udogSymbolTableFind(&moduleObj->variableNames,
	                                       className, strlen(className));
	
	if (variableSlot < 0)
		return NULL;
	
	ObjClass* classObj = AS_CLASS(moduleObj->variables.data[variableSlot]);
	Value vmObj = udogNewInstance(vm, classObj);
	memcpy(AS_INSTANCE(vmObj)->fields, obj, classObj->numFields*sizeof(Value));
	
	return udogCreateHostObject(vm, vmObj);
}

// Will create a number with value [num]
UDogValue* udogCreateNumber(UDogVM* vm, double num) {
	return udogCreateHostObject(vm, NUM_VAL(num));
}

// Will create a bool with value [val]
UDogValue* udogCreateBool(UDogVM* vm, bool val) {
	return udogCreateHostObject(vm, val ? TRUE_VAL : FALSE_VAL);
}

UDogValue* udogCreateValue(UDogVM* vm) {
	return udogCreateHostObject(vm, NULL_VAL);
}

// Will create a string with value [val]
UDogValue* udogCreateString(UDogVM* vm, const char* text, int length) {
	size_t size = length;
	if (length == -1) size = strlen(text);

	return udogCreateHostObject(vm, udogNewString(vm, text, size));
}

// Creates a new list
UDogValue* udogCreateObjectList(UDogVM* vm) {
	return udogCreateHostObject(vm, OBJ_VAL(udogNewList(vm, 0)));
}

// Adds an element to the list
void udogObjectListAdd(UDogVM* vm, UDogValue* list, UDogValue* variable) {
	Value l = udogGetHostObject(vm, list);
	Value elem = udogGetHostObject(vm, variable);
	udogListAdd(vm, AS_LIST(l), elem);
}

// Creates a new list
UDogValue* udogCreateObjectMap(UDogVM* vm) {
	return udogCreateHostObject(vm, OBJ_VAL(udogNewMap(vm)));
}

// Adds an element to the list
void udogObjectMapSet(UDogVM* vm, UDogValue* list, UDogValue* key, UDogValue* val) {
	Value l = udogGetHostObject(vm, list);
	Value k = udogGetHostObject(vm, key);
	Value v = udogGetHostObject(vm, val);
	udogMapSet(vm, AS_MAP(l), k, v);
}

// Release's a certain object
void udogReleaseObject(UDogVM* vm, UDogValue* val) {
	if (val != NULL)
		udogRemoveHostObject(vm, val);
}

void udogDefineMethod(UDogVM* vm, const char* module, const char* className,
                            const char* signature,
                            udogForeignMethodFn methodFn) {
	defineMethod(vm, module, className, signature, methodFn, false);
}

void udogDefineStaticMethod(UDogVM* vm, const char* module, const char* className,
                            const char* signature,
                            udogForeignMethodFn methodFn) {
	defineMethod(vm, module, className, signature, methodFn, true);
}

// Defines a destructor [destructor] for the given class
// All instances of class [className] will call the destructor when the GC
// decides to destroy the object.
// The exact timing of the destruction can not be known.
// The destructors purpose is to clean up any manual memory from an instance
// of [className]
void udogDefineDestructor(UDogVM* vm, const char* module, const char* className, udogDestructorFn destructor) {
	ASSERT(className != NULL, "Must provide class name.");
	ASSERT(destructor != NULL, "Must provide method function.");
	
	ObjModule* coreModule = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			coreModule = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	// Find or create the class to bind the method to.
	int classSymbol = udogSymbolTableFind(&coreModule->variableNames,
										  className, strlen(className));
	ObjClass* classObj;

	if (classSymbol != -1) {
		classObj = AS_CLASS(coreModule->variables.data[classSymbol]);
	}
	else {
		// The class doesn't already exist, so create it.
		size_t len = strlen(className);
		ObjString* nameString = AS_STRING(udogNewString(vm, className, len));
		
		udogPushRoot(vm, (Obj*)nameString);
		
		classObj = udogNewClass(vm, vm->metatable.objectClass, 0, nameString);
		udogDefineVariable(vm, coreModule, className, len, OBJ_VAL(classObj));
		udogPopRoot(vm);
	}
	
	classObj->destructor = destructor;
}

void udogDefineClass(UDogVM* vm, const char* module, const char* className, size_t size, const char* parent) {
	ASSERT(className != NULL, "Must provide class name.");
	
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}

	// Find or create the class to bind the method to.
	if (udogSymbolTableFind(&moduleObj->variableNames, className, strlen(className)) > 0) return;
										  
	// The class doesn't already exist, so create it.
	size_t length = strlen(className);
	ObjString* nameString = AS_STRING(udogNewString(vm, className, length));
	
	// Set the number of fields correctly
	int numFields = (size / sizeof(Value));
	if (size % sizeof(Value) != 0)
		numFields++;
		
	ObjClass* super = NULL;		
	if (parent == NULL) 
		super = vm->metatable.objectClass;
	else
		super = AS_CLASS(*findVariable(vm, moduleObj, parent));
	
	if (super == NULL) 
		super = vm->metatable.objectClass;

	UDOG_PIN(vm, nameString);

	ObjClass* classObj = udogNewClass(vm, super, numFields, nameString);
	udogDefineVariable(vm, moduleObj, className, length, OBJ_VAL(classObj));

	UDOG_UNPIN(vm);
}

// Get the object as a void*
// This assumes that the object is an instance
void* udogGetInstance(UDogVM* vm, UDogValue* val) {
	Value obj = udogGetHostObject(vm, val);
	return ((Obj*)AS_INSTANCE(obj))+1;
}

// Get the object as a bool
bool udogGetBoolean(UDogVM* vm, UDogValue* val) {
	Value obj = udogGetHostObject(vm, val);
	return AS_BOOL(obj);
}

// Get the object as a number
double udogGetNumber(UDogVM* vm, UDogValue* val) {
	Value obj = udogGetHostObject(vm, val);
	return AS_NUM(obj);
}

// Get the object as a void*
const char* udogGetString(UDogVM* vm, UDogValue* val) {
	Value obj = udogGetHostObject(vm, val);
	return AS_CSTRING(obj);
}


void createModule(UDogVM* vm, const char* name) {
	udogImportModuleVar(vm, udogNewString(vm, name, strlen(name)));
}

void removeModule(UDogVM* vm, const char* name) {
	Value nameValue = udogNewString(vm, name, strlen(name));
	udogPushRoot(vm, AS_OBJ(nameValue));
	
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = udogMapFind(vm->modules, nameValue);
	if (index != UINT32_MAX) {
		udogMapRemoveKey(vm, vm->modules, nameValue);
	}
	
	udogPopRoot(vm);
}

// Removes a variable from the VM
void udogRemoveVariable(UDogVM* vm, const char* module, const char* variable) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	// Find or create the class to bind the method to.
	int symbol = udogSymbolTableFind(&moduleObj->variableNames, variable, strlen(variable));
	if (symbol > 0) {
		moduleObj->variables.data[symbol] = NULL_VAL;
	}
}

// Get a top-level variable from a given module
UDogValue* getModuleVariable(UDogVM* vm, const char* module, const char* variable) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	// Find or create the class to bind the method to.
	int symbol = udogSymbolTableFind(&moduleObj->variableNames, variable, strlen(variable));
	if (symbol > 0) {
		return udogCreateHostObject(vm, moduleObj->variables.data[symbol]);
	}
	return NULL;
}

// Get a method from the VM
void udogRemoveMethod(UDogVM* vm, const char* module, const char* variable, const char* signature) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	// Find or create the class to bind the method to.
	int symbol = udogSymbolTableFind(&moduleObj->variableNames, variable, strlen(variable));
	if (symbol > 0) {
		Value val = moduleObj->variables.data[symbol];
		
		if (!IS_CLASS(val)) return;
		ObjClass* obj = AS_CLASS(val);
		
		int method =  udogSymbolTableFind(&vm->methodNames,
	                                    signature, strlen(signature));
		obj->methods.data[method].type = METHOD_NONE;
	}
}

// Get a method from the VM with an host object
void udogRemoveMethodObject(UDogVM* vm, UDogValue* variable, const char* signature) {
	// Find or create the class to bind the method to.
	Value val = udogGetHostObject(vm, variable);
	
	if (!IS_CLASS(val)) return;
	ObjClass* obj = AS_CLASS(val);
	
	int method =  udogSymbolTableFind(&vm->methodNames,
									signature, strlen(signature));
	obj->methods.data[method].type = METHOD_NONE;
}

///////////////////////////////////////////////////////////////////////////////////
//// THE GETTERS FOR FOREIGN METHODS
///////////////////////////////////////////////////////////////////////////////////

UDogValue* udogGetArgument(UDogVM* vm, int index) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	ASSERT(index >= 0, "index cannot be negative.");
	ASSERT(index < vm->fiber->foreignCallNumArgs, "Not that many arguments.");

	return udogCreateHostObject(vm, *(vm->fiber->foreignCallSlot + index));
}

void udogReturnValue(UDogVM* vm, UDogValue* val) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	
	*vm->fiber->foreignCallSlot = udogGetHostObject(vm, val);
	vm->fiber->foreignCallSlot = NULL;
	udogReleaseObject(vm, val);
}

bool udogGetArgumentBool(UDogVM* vm, int index) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	ASSERT(index >= 0, "index cannot be negative.");
	ASSERT(index < vm->fiber->foreignCallNumArgs, "Not that many arguments.");

	if (!IS_BOOL(*(vm->fiber->foreignCallSlot + index))) return false;

	return AS_BOOL(*(vm->fiber->foreignCallSlot + index));
}

double udogGetArgumentDouble(UDogVM* vm, int index) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	ASSERT(index >= 0, "index cannot be negative.");
	ASSERT(index < vm->fiber->foreignCallNumArgs, "Not that many arguments.");
	
	if (!IS_NUM(*(vm->fiber->foreignCallSlot + index)))
		return 0.0;
	return AS_NUM(*(vm->fiber->foreignCallSlot + index));
}

const char* udogGetArgumentString(UDogVM* vm, int index) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	ASSERT(index >= 0, "index cannot be negative.");
	ASSERT(index < vm->fiber->foreignCallNumArgs, "Not that many arguments.");

	if (!IS_STRING(*(vm->fiber->foreignCallSlot + index)))
		return NULL;
	return AS_CSTRING(*(vm->fiber->foreignCallSlot + index));
}

void udogReturnDouble(UDogVM* vm, double value) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");

	*vm->fiber->foreignCallSlot = NUM_VAL(value);
	vm->fiber->foreignCallSlot = NULL;
}

void udogReturnNull(UDogVM* vm) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");

	*vm->fiber->foreignCallSlot = NULL_VAL;
	vm->fiber->foreignCallSlot = NULL;
}

void udogReturnString(UDogVM* vm, const char* text, int length) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");

	size_t size = length;
	if (length == -1) size = strlen(text);

	*vm->fiber->foreignCallSlot = udogNewString(vm, text, size);
	vm->fiber->foreignCallSlot = NULL;
}

// Provides a boolean return value for a foreign call. This must only be called
// within a function provided to [udogDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void udogReturnBool(UDogVM* vm, bool value) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");

	*vm->fiber->foreignCallSlot = BOOL_VAL(value);
	vm->fiber->foreignCallSlot = NULL;
}

///////////////////////////////////////////////////////////////////////////////////
//// THE METHODS FOR BYTECODE
///////////////////////////////////////////////////////////////////////////////////

// Loads [source], a string of UDog byte code, to an [ObjValue] that will
// execute that code when invoked.
UDogValue* udogLoadByteCode(UDogVM* vm, const char* sourcePath, const char* source) {
	UNUSED(sourcePath);
	Value name = udogNewString(vm, "main", 4);

	ObjModule* module = loadModule(vm, name, NULL_VAL);
	ObjFn* fn = udogCompileFromByteCode(vm, module, AS_CSTRING(name), source);
	if (fn == NULL) return NULL;
	module->func = fn;
	
	// Return the fiber that executes the module.
	ObjFiber* fiber= udogNewFiber(vm, (Obj*)module->func);
	
	if (fiber == NULL)
		return NULL;

	// Link the fiber to the GC
	return udogCreateHostObject(vm, OBJ_VAL((Obj*) fiber));
}

// Saves the entire state of the VM to a string
// The value has to be released by the host application
UDogValue* udogSaveByteCode(UDogVM* vm) {
	const char* module = "main";
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = udogNewString(vm, module, strlen(module));
		uint32_t moduleEntry = udogMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	ObjString* bytecode = udogCompileToByteCode(vm, moduleObj);
	
	// Link the string to the GC
	return udogCreateHostObject(vm, OBJ_VAL((Obj*) bytecode));
}

// Sets the debug mode of the VM [vm] to boolean value [set]
void udogSetDebugMode(UDogVM* vm, bool set) {
	vm->debugMode = set;
}