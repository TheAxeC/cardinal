#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#ifndef __STDC_LIMIT_MACROS
	#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h>

#include "cardinal.h"
#include "cardinal_config.h"
#include "cardinal_compiler.h"
#include "cardinal_core.h"
#include "cardinal_debug.h"
#include "cardinal_vm.h"

#if CARDINAL_USE_MEMORY
	#include "cardinal_datacenter.h"
#endif

#if CARDINAL_USE_LIB_IO
  #include "cardinal_io.h"
#endif

#if CARDINAL_DEBUG_TRACE_MEMORY || CARDINAL_DEBUG_TRACE_GC
  #include <time.h>
#endif

#if CARDINAL_USE_DEFAULT_FILE_LOADER
	#include "cardinal_file.h"
#endif

#if CARDINAL_BYTECODE
	#include "cardinal_bytecode.h"
#endif

#if CARDINAL_DEBUGGER
	#include "cardinal_debugger.h"
#endif

#if CARDINAL_USE_REGEX
	#include "cardinal_regex.h"
#endif

///////////////////////////////////////////////////////////////////////////////////
//// STATIC
///////////////////////////////////////////////////////////////////////////////////

static void* defaultReallocate(void* memory, size_t oldSize, size_t newSize);
static void initGarbageCollector(CardinalVM* vm, CardinalConfiguration* configuration);

static Upvalue* captureUpvalue(CardinalVM* vm, ObjFiber* fiber, Value* local);
static void closeUpvalue(ObjFiber* fiber);

static void bindMethod(CardinalVM* vm, int methodType, int symbol, ObjClass* classObj, Value methodValue);
static void callForeign(CardinalVM* vm, ObjFiber* fiber, cardinalForeignMethodFn foreign, int numArgs);
static ObjFiber* runtimeError(CardinalVM* vm, ObjFiber* fiber, ObjString* error);
static ObjFiber* runtimeThrow(CardinalVM* vm, ObjFiber* fiber, Value error);
static void callFunction(ObjFiber* fiber, Obj* function, int numArgs);

static void defineMethod(CardinalVM* vm, const char* module, const char* className,
                         const char* signature,
                         cardinalForeignMethodFn methodFn, bool isStatic);

static void collectGarbage(CardinalVM* vm);

///////////////////////////////////////////////////////////////////////////////////
//// VIRTUAL MACHINE INITIALISATION
///////////////////////////////////////////////////////////////////////////////////

static void cardinalLoadLibraries(CardinalVM* vm) {
#if CARDINAL_USE_MEMORY
	cardinalInitializeDataCenter(vm);
#endif
#if CARDINAL_USE_LIB_IO
	cardinalLoadIOLibrary(vm);
#endif
#if CARDINAL_USE_DEFAULT_FILE_LOADER
	cardinalLoadFileLibrary(vm);
#endif
#if CARDINAL_USE_REGEX
	cardinalLoadRegexLibrary(vm);
#endif
}

static void loadCallBacks(CardinalConfiguration* configuration, CardinalVM* vm) {
	
	cardinalPrintCallBack print = printf;
	cardinalLoadModuleFn moduleLoader = NULL;
	cardinalCallBack callback = NULL;
	
#if CARDINAL_USE_DEFAULT_FILE_LOADER
	moduleLoader = defaultModuleLoader;
#endif
#if CARDINAL_DEBUGGER
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

CardinalVM* cardinalNewVM(CardinalConfiguration* configuration) {
	// Load the memory allocation and the VM
	cardinalReallocateFn reallocate = defaultReallocate;
	if (configuration->reallocateFn != NULL) {
		reallocate = configuration->reallocateFn;
	}
	CardinalVM* vm = (CardinalVM*) reallocate(NULL, 0, sizeof(CardinalVM));
	vm->reallocate = reallocate;
	
	// Set some callbacks
	loadCallBacks(configuration, vm);

	// Initialise the GC
	initGarbageCollector(vm, configuration);
	
	// Initiate the method table
	cardinalSymbolTableInit(vm, &vm->methodNames);
	
	// Create a new debugger
	vm->debugger = cardinalNewDebugger(vm);
	vm->debugMode = false;
	
	// Set the root directory
	cardinalSetRootDirectory(vm, configuration->rootDirectory);
	
	// Implicitly create a "main" module for the REPL or entry script.
	ObjModule* mainModule = cardinalNewModule(vm);
	cardinalPushRoot(vm, (Obj*)mainModule);
	vm->modules = cardinalNewMap(vm);
	cardinalMapSet(vm, vm->modules, NULL_VAL, OBJ_VAL(mainModule));
	cardinalPopRoot(vm);
	
	// Create the script specific libraries
	cardinalInitializeCore(vm);
	cardinalLoadLibraries(vm);
	
	cardinalFlushHostObjects(vm);
	
	return vm;
}



void cardinalFreeVM(CardinalVM* vm) {
	if( vm == NULL || vm->methodNames.count == 0 ) return;
	
	// Free all of the GC objects.
	Obj* obj = vm->garbageCollector.first;
	while (obj != NULL) {
		Obj* next = obj->next;
		cardinalFreeObj(vm, obj);
		obj = next;
	}
	
	cardinalSymbolTableClear(vm, &vm->methodNames);
	cardinalFreeDebugger(vm, vm->debugger);
	
#if CARDINAL_DEBUG_TRACE_MEMORY || CARDINAL_DEBUG_TRACE_GC
	vm->printFunction("Memory in use: %ld\n", vm->garbageCollector.bytesAllocated);
	vm->printFunction("Nb of allocations: %ld\n", vm->garbageCollector.nbAllocations);
	vm->printFunction("Nb of frees: %ld\n", vm->garbageCollector.nbFrees);
#endif
	
	DEALLOCATE(vm, vm);
}

// Set the root directory
void cardinalSetRootDirectory(CardinalVM* vm, const char* path) {
	vm->rootDirectory = NULL;
	if (path == NULL) return;
	
	// Use the directory where the file is as the root to resolve imports
	// relative to.
	const char* lastSlash = strrchr(path, '/');
	if (lastSlash != NULL) {
		vm->rootDirectory = AS_STRING(cardinalNewString(vm, path, lastSlash - path + 1));
	}
}

// Set the root directory
static ObjString* cardinalGetRootDirectory(CardinalVM* vm, const char* path) {
	if (path == NULL) return NULL;
	
	// Use the directory where the file is as the root to resolve imports
	// relative to.
	const char* lastSlash = strrchr(path, '/');
	if (lastSlash != NULL) {
		return AS_STRING(cardinalNewString(vm, path, lastSlash - path + 1));
	}
	return NULL;
}


// The built-in reallocation function used when one is not provided by the
// configuration.
static void* defaultReallocate(void* buffer , size_t oldSize, size_t newSize) {
	UNUSED(oldSize);
	if (newSize == 0) { free(buffer); return NULL; }
	if (buffer == NULL) return malloc(newSize);
	return realloc(buffer, newSize);
}


static void initMetaClasses(CardinalVM* vm) {
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

static void initGarbageCollector(CardinalVM* vm, CardinalConfiguration* configuration) {
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
	vm->garbageCollector.isCoupled = true;
}

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS FOR THE VM
///////////////////////////////////////////////////////////////////////////////////

// Captures the local variable [local] into an [Upvalue]. If that local is
// already in an upvalue, the existing one will be used. (This is important to
// ensure that multiple closures closing over the same variable actually see
// the same variable.) Otherwise, it will create a new open upvalue and add it
// the fiber's list of upvalues.
static Upvalue* captureUpvalue(CardinalVM* vm, ObjFiber* fiber, Value* local) {
	// If there are no open upvalues at all, we must need a new one.
	if (fiber->openUpvalues == NULL) {
		fiber->openUpvalues = cardinalNewUpvalue(vm, local);
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
	Upvalue* createdUpvalue = cardinalNewUpvalue(vm, local);
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

static void bindMethod(CardinalVM* vm, int methodType, int symbol, ObjClass* classObj, Value methodValue) {
	ObjFn* methodFn = IS_FN(methodValue) ? AS_FN(methodValue) : AS_CLOSURE(methodValue)->fn;

	// Methods are always bound against the class, and not the metaclass, even
	// for static methods, so that constructors (which are static) get bound like
	// instance methods.
	cardinalBindMethodCode(vm, -1, classObj, methodFn);

	Method method;
	method.type = METHOD_BLOCK;
	method.fn.obj = AS_OBJ(methodValue);

	if (methodType == CODE_METHOD_STATIC) {
		classObj = classObj->obj.classObj;
	}

	cardinalBindMethod(vm, classObj, symbol, method);
}

static void callForeign(CardinalVM* vm, ObjFiber* fiber, cardinalForeignMethodFn foreign, int numArgs) {
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
static ObjFiber* runtimeError(CardinalVM* vm, ObjFiber* fiber, ObjString* error) {
	ASSERT(fiber->error == NULL, "Can only fail once.");

	// Store the error in the fiber so it can be accessed later.
	fiber->error = cardinalThrowException(vm, error);
	cardinalInsertStackTrace(fiber->error, cardinalDebugGetStackTrace(vm, fiber));

	// If the caller ran this fiber using "try", give it the error.
	if (fiber->callerIsTrying) {
		ObjFiber* caller = fiber->caller;

		// Make the caller's try method return the error message.
		*(caller->stacktop - 1) = OBJ_VAL(fiber->error);
		return caller;
	}

	// If we got here, nothing caught the error, so show the stack trace.
	cardinalDebugPrintStackTrace(vm, fiber);
	return NULL;
}

static ObjFiber* runtimeThrow(CardinalVM* vm, ObjFiber* fiber, Value error) {
	if (IS_STRING(error)) return runtimeError(vm, fiber, AS_STRING(error));
	
	ASSERT(fiber->error == NULL, "Can only fail once.");

	// Store the error in the fiber so it can be accessed later.
	fiber->error = AS_INSTANCE(error);
	cardinalInsertStackTrace(fiber->error, cardinalDebugGetStackTrace(vm, fiber));

	// If the caller ran this fiber using "try", give it the error.
	if (fiber->callerIsTrying) {
		ObjFiber* caller = fiber->caller;

		// Make the caller's try method return the error message.
		*(caller->stacktop - 1) = OBJ_VAL(fiber->error);
		return caller;
	}

	// If we got here, nothing caught the error, so show the stack trace.
	cardinalDebugPrintStackTrace(vm, fiber);
	return NULL;
}

// Generates an error at runtime and stops execution at once
static void runtimeCrash(CardinalVM* vm, ObjFiber* fiber, const char* error) {
	ASSERT(fiber->error == NULL, "Can only fail once.");

	// Store the error in the fiber so it can be accessed later.
	fiber->error =  cardinalThrowException(vm, AS_STRING(cardinalNewString(vm, error, strlen(error))));

	// If we got here, nothing caught the error, so show the stack trace.
	cardinalDebugPrintStackTrace(vm, fiber);
}

// Creates a string containing an appropriate method not found error for a
// method with [symbol] on [classObj].
static ObjString* methodNotFound(CardinalVM* vm, ObjClass* classObj, int symbol) {
	char message[MAX_VARIABLE_NAME + MAX_METHOD_NAME + 24];
	sprintf(message, "%s does not implement '%s'.",
		classObj->name->value,
		vm->methodNames.data[symbol].buffer);

	return AS_STRING(cardinalNewString(vm, message, strlen(message)));
}

// Verifies that [superclass] is a valid object to inherit from. That means it
// must be a class and cannot be the class of any built-in type.
//
// If successful, returns NULL. Otherwise, returns a string for the runtime
// error message.
static ObjString* validateSuperclass(CardinalVM* vm, ObjString* name,
                                     Value superclassValue) {
	// Make sure the superclass is a class.
	if (!IS_CLASS(superclassValue)) {
		return AS_STRING(cardinalNewString(vm, "Must inherit from a class.", 26));
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
		return AS_STRING(cardinalNewString(vm, message, strlen(message)));
	}

	return NULL;
}

///////////////////////////////////////////////////////////////////////////////////
//// MODULES
///////////////////////////////////////////////////////////////////////////////////

// Checks whether a module with the given name exists, and if so
// Replaces it with the given module
// Otherwise the module is added to the module list
void cardinalSaveModule(CardinalVM* vm, ObjModule* module, ObjString* name) {
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = cardinalMapFind(vm->modules, OBJ_VAL(name));
	if (index != UINT32_MAX) {
		vm->modules->entries[index].value = OBJ_VAL(module);
		return;
	}
	
	// Store it in the VM's module registry so we don't load the same module
	// multiple times.
	module->name = name;
	cardinalMapSet(vm, vm->modules, OBJ_VAL(name), OBJ_VAL(module));
}

// Looks up the core module in the module map.
ObjModule* getCoreModule(CardinalVM* vm) {
	uint32_t entry = cardinalMapFind(vm->modules, NULL_VAL);
	ASSERT(entry != UINT32_MAX, "Could not find core module.");
	return AS_MODULE(vm->modules->entries[entry].value);
}

// Ready a new module
ObjModule* cardinalReadyNewModule(CardinalVM* vm) {
	ObjModule* module = cardinalNewModule(vm);
	
	CARDINAL_PIN(vm, module);
	// Implicitly import the core module.
	ObjModule* coreModule = getCoreModule(vm);
	for (int i = 0; i < coreModule->variables.count; i++) {
		cardinalDefineVariable(vm, module,
						   coreModule->variableNames.data[i].buffer,
						   coreModule->variableNames.data[i].length,
						   coreModule->variables.data[i]);
		module->count--;
	}
	CARDINAL_UNPIN(vm);
	return module;
}

static ObjModule* loadModule(CardinalVM* vm, Value name, Value source) {
	ObjModule* module = NULL;

	// See if the module has already been loaded.
	uint32_t index = cardinalMapFind(vm->modules, name);
	if (index == UINT32_MAX) {
		module = cardinalReadyNewModule(vm);
		module->name = AS_STRING(name);
		// Store it in the VM's module registry so we don't load the same module
		// multiple times.
		CARDINAL_PIN(vm, module);
		cardinalMapSet(vm, vm->modules, name, OBJ_VAL(module));
		CARDINAL_UNPIN(vm);
		if (IS_NULL(source)) return module;
	}
	else {
		// Execute the new code in the context of the existing module.
		module = AS_MODULE(vm->modules->entries[index].value);
		
		if (IS_NULL(source)) return module;
	}
	
	CARDINAL_PIN(vm, module);
	ObjFn* fn = cardinalCompile(vm, module, AS_CSTRING(name), AS_CSTRING(source));
	CARDINAL_UNPIN(vm);
	if (fn == NULL) return NULL;

	module->func = fn;
	module->source = AS_STRING(source);
	return module;
}

ObjFiber* loadModuleFiber(CardinalVM* vm, Value name, Value source) {
	ObjModule* module = loadModule(vm, name, source);
	CARDINAL_PIN(vm, module);
	ObjFiber* moduleFiber = cardinalNewFiber(vm, (Obj*)module->func);
	CARDINAL_UNPIN(vm);

	// Return the fiber that executes the module.
	return moduleFiber;
}

ObjModule* cardinalImportModuleVar(CardinalVM* vm, Value nameValue) {
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = cardinalMapFind(vm->modules, nameValue);
	if (index != UINT32_MAX) {
		return AS_MODULE(vm->modules->entries[index].value);
	}

	// Load the module's source code from the embedder.
	CardinalValue* source = vm->loadModule(vm, AS_CSTRING(nameValue));
	if (source == NULL) {
		// Couldn't load the module, create a new Module.
		ObjModule* module = cardinalReadyNewModule(vm);
		CARDINAL_PIN(vm, module);
		// Store it in the VM's module registry so we don't load the same module
		// multiple times.
		cardinalMapSet(vm, vm->modules, nameValue, OBJ_VAL(module));
		CARDINAL_UNPIN(vm);
		return module;
	}

	ObjModule* module = loadModule(vm, nameValue, cardinalGetHostObject(vm, source));
	CARDINAL_PIN(vm, module);
	cardinalReleaseObject(vm, source);
	CARDINAL_UNPIN(vm);
	// Return the module.
	return module;
}

ObjModule* cardinalGetModule(CardinalVM* vm, Value nameValue) {
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = cardinalMapFind(vm->modules, nameValue);
	if (index != UINT32_MAX) {
		return AS_MODULE(vm->modules->entries[index].value);
	}

	// Couldn't load the module, create a new Module.
	ObjModule* module = cardinalReadyNewModule(vm);
	CARDINAL_PIN(vm, module);
	// Store it in the VM's module registry so we don't load the same module
	// multiple times.
	cardinalMapSet(vm, vm->modules, nameValue, OBJ_VAL(module));
	CARDINAL_UNPIN(vm);
	return module;
}

static ObjFiber* loadModuleNoMemory(CardinalVM* vm, Value name, const char* source) {
	ObjModule* module = loadModule(vm, name, NULL_VAL);
	ObjFn* fn = cardinalCompile(vm, module, AS_CSTRING(name), source);
	if (fn == NULL) return NULL;
	module->func = fn;
	
	// Return the fiber that executes the module.
	return cardinalNewFiber(vm, (Obj*)module->func);
}

static Value importModule(CardinalVM* vm, Value name) {
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = cardinalMapFind(vm->modules, name);
	if (index != UINT32_MAX) return NULL_VAL;

	// Load the module's source code from the embedder.
	CardinalValue* source = vm->loadModule(vm, AS_CSTRING(name));
	if (source == NULL) {
		// Couldn't load the module.
		Value error = cardinalNewUninitializedString(vm, 25 + AS_STRING(name)->length);
		sprintf(AS_STRING(error)->value, "Could not find module '%s'.",
		        AS_CSTRING(name));
		return error;
	}

	ObjFiber* moduleFiber = loadModuleFiber(vm, name, cardinalGetHostObject(vm, source));
	moduleFiber->rootDirectory = cardinalGetRootDirectory(vm, AS_CSTRING(name));
	CARDINAL_PIN(vm, moduleFiber);
	cardinalReleaseObject(vm, source);
	CARDINAL_UNPIN(vm);
	// Return the fiber that executes the module.
	return OBJ_VAL(moduleFiber);
}

static bool importVariable(CardinalVM* vm, Value moduleName, Value variableName,
                           Value* result) {
	UNUSED(variableName);
	uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
	ASSERT(moduleEntry != UINT32_MAX, "Should only look up loaded modules.");

	ObjModule* module = AS_MODULE(vm->modules->entries[moduleEntry].value);
	
	*result = OBJ_VAL(module);
	return true;
}

///////////////////////////////////////////////////////////////////////////////////
//// CHECKING STACK AND CALLFRAME
///////////////////////////////////////////////////////////////////////////////////

bool cardinalFiberStack(CardinalVM* vm, ObjFiber* fiber, Value** stackstart) {
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
	
	fiber->stack = (Value*) cardinalReallocate(vm, fiber->stack, fiber->stacksize * sizeof(Value), newSize * sizeof(Value));
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
bool cardinalFiberCallFrame(CardinalVM* vm, ObjFiber* fiber, CallFrame** frame) {
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

	fiber->frames = (CallFrame*) cardinalReallocate(vm, fiber->frames, fiber->framesize * sizeof(CallFrame), newSize * sizeof(CallFrame));
	fiber->framesize = newSize;
	
	// reset frame variable
	*frame = &(fiber->frames[fiber->numFrames-1]);
	
	return false;
}

bool checkMethodManual(CardinalVM* vm, ObjClass*& classObj, stackTop& stacktop, Value*& args, cardinal_integer& symbol, int& numArgs, int& adj, Method*& method) {

	if (classObj == vm->metatable.pointerClass) {
		void* ptr = AS_POINTER(args[0]);
		args[0] = (stacktop-1)[0];
		stacktop--;
		numArgs--;
		
		if (!IS_CLASS(args[0])) return false;
		classObj = AS_CLASS(args[0]);
		
		// find the correct method symbol so we can allocate manually
		String name = vm->methodNames.data[symbol];
		char str[256]; 
		str[0] = 'i';
		str[1] = 'n';
		str[2] = 'i';
		str[3] = 't';
		str[4] = ' ';
		int i;
		for(i=0; i<(int) name.length; i++) {
			str[5+i] = name.buffer[i];
		}
		i = i+5;
		if (numArgs >= 1) {
			// we need to remove a ,_
			str[i-3] = ')';
			str[i-2] = '\0';
			i = i-2;
		} else {
			// we only need to remove a _
			str[i-2] = ')';
			str[i-1] = '\0';
			i = i-1;
		}
		symbol = cardinalSymbolTableFind(&vm->methodNames, str, i);
		
		if (symbol >= classObj->methods.count ) return false;
		
		method = cardinalGetMethod(vm, classObj, symbol, adj);
		if (method == NULL || method->type == METHOD_NONE) return false;
		
		if (AS_CLASS(args[0]) == vm->metatable.classClass ||
	        AS_CLASS(args[0]) == vm->metatable.fiberClass ||
	        AS_CLASS(args[0]) == vm->metatable.fnClass || // Includes OBJ_CLOSURE.
	        AS_CLASS(args[0]) == vm->metatable.listClass ||
	        AS_CLASS(args[0]) == vm->metatable.mapClass ||
	        AS_CLASS(args[0]) == vm->metatable.rangeClass ||
	        AS_CLASS(args[0]) == vm->metatable.stringClass) {
				return false;
			}
		args[0] = cardinalNewInstance(vm, AS_CLASS(args[0]), ptr);
		
		return true;
	}
	else return false;
}

///////////////////////////////////////////////////////////////////////////////////
//// INTERPRETER
///////////////////////////////////////////////////////////////////////////////////

bool runInterpreter(CardinalVM* vm) {
	//Load the DispatchTable
#ifdef COMPUTED_GOTO
	// Note that the order of instructions here must exacly match the Code enum
	// in cardinal_vm.h or horrendously bad things happen.
	static void* dispatchTable[] = {
		#define OPCODE(name) &&code_##name,
		#include "cardinal_opcodes.h"
		#undef OPCODE
	};

#endif

#ifdef COMPUTED_GOTO
	#define INTERPRET_LOOP DISPATCH();

	#if CARDINAL_DEBUG_TRACE_INSTRUCTIONS
		// Prints the stack and instruction before each instruction is executed.
		#define DISPATCH() \
			{ \
			  cardinalDebugPrintStack(vm, fiber); \
			  cardinalDebugPrintInstruction(vm, fn, (int)(ip - fn->bytecode)); \
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

#define CHECK_STACK() if (cardinalFiberStack(vm, fiber, &stackStart)) { \
							runtimeCrash(vm, fiber, "Stack size limit reached"); \
							return false; \
						}

#define CHECK_CALLFRAME() if (cardinalFiberCallFrame(vm, fiber, &frame)) { \
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
			cardinal_integer field = READ_FIELD();
			Value receiver = stackStart[0];
			ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
			ObjInstance* instance = AS_INSTANCE(receiver);
			ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
			PUSH(instance->fields[field + cardinalStackPeek(vm, &instance->stack)]);
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
			cardinal_integer symbol = READ_METHOD();

			Value* args = fiber->stacktop - numArgs;
			ObjClass* classObj = cardinalGetClassInline(vm, args[0]);
			
			bool checkManual = false;
			
			// If the class's method table doesn't include the symbol, bail.
			int adj = 0;
			Method* method;
			if (symbol >= classObj->methods.count) {
				checkManual = true;
			} else {
				adj = 0;
				method = cardinalGetMethod(vm, classObj, symbol, adj);
			
				if (method == NULL || method->type == METHOD_NONE) {
					checkManual = true;
				}
			}
			
			if (checkManual) {
#if CARDINAL_USE_MEMORY
				// Maybe we are dealing with manual memory allocation
				if (! checkMethodManual(vm, classObj, fiber->stacktop, args, symbol, numArgs, adj, method) )
#endif
				RUNTIME_ERROR(methodNotFound(vm, classObj, (int) symbol));
			}

			if (IS_INSTANCE(args[0])) {
				cardinalStackPush(vm, &AS_INSTANCE(args[0])->stack, adj);
			}
			
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
			cardinal_integer symbol = READ_METHOD();

			Value* args = fiber->stacktop - numArgs;
			ObjClass* receive = cardinalGetClassInline(vm, args[0]);
			
			ObjInstance* instance = NULL;
			if (IS_INSTANCE(args[0])) {
				instance = AS_INSTANCE(args[0]);
			}

			// Ignore methods defined on the receiver's immediate class.
			//int super = READ_CONSTANT();
			//ObjClass* classObj = AS_CLASS(receive->superclasses->elements[super]);
			int adj = 0;
			ObjClass* classObj = receive;
			ObjList* list = AS_LIST(fn->constants[READ_CONSTANT()]);
			adj = classObj->superclass;
			for(int i=0; i<list->count; i++) {
				uint32_t super = AS_NUM(list->elements[i]);
				for(uint32_t a=0; a<super; a++) {
					adj += AS_CLASS(classObj->superclasses->elements[a])->superclass;
				}
				classObj = AS_CLASS(classObj->superclasses->elements[super]);
			}
			
			if (instance != NULL) {
				cardinalStackPush(vm, &instance->stack, adj);
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
			cardinal_integer field = READ_FIELD();
			Value receiver = stackStart[0];
			ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
			ObjInstance* instance = AS_INSTANCE(receiver);
			ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
			instance->fields[field + cardinalStackPeek(vm, &instance->stack)] = PEEK();
			DISPATCH();
		}
		
		// Load a field
		CASECODE(LOAD_FIELD):
		{
			cardinal_integer field = READ_FIELD();
			Value receiver = POP();
			ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
			ObjInstance* instance = AS_INSTANCE(receiver);
			ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
			PUSH(instance->fields[field + cardinalStackPeek(vm, &instance->stack)]);
			CHECK_STACK();
			DISPATCH();
		}
		
		// Store a field into an instance
		CASECODE(STORE_FIELD):
		{
			cardinal_integer field = READ_FIELD();
			Value receiver = POP();
			ASSERT(IS_INSTANCE(receiver), "Receiver should be instance.");
			ObjInstance* instance = AS_INSTANCE(receiver);
			ASSERT(field < instance->obj.classObj->numFields, "Out of bounds field.");
			instance->fields[field + cardinalStackPeek(vm, &instance->stack)] = PEEK();
			DISPATCH();
		}
		
		// Jump around in the bytecode
		CASECODE(JUMP):
		{
			cardinal_integer offset = READ_OFFSET();
			ip += offset;
			DISPATCH();
		}
		
		// Jump back up to the top of the loop
		CASECODE(LOOP):
		{
			// Jump back to the top of the loop.
			cardinal_integer offset = READ_OFFSET();
			ip -= offset;
			DISPATCH();
		}
		
		// Jump if the top of the stack is [false] or [null], and pop the top
		CASECODE(JUMP_IF):
		{
			cardinal_integer offset = READ_OFFSET();
			Value condition = POP();

			if (IS_FALSE(condition) || IS_NULL(condition)) ip += offset;
			DISPATCH();
		}

		// Jump if top of the stack is [false] or [null], else pop the top of the stack
		CASECODE(AND):
		{
			cardinal_integer offset = READ_OFFSET();
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
			cardinal_integer offset = READ_OFFSET();
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
				RUNTIME_ERROR(AS_STRING(cardinalNewString(vm, message, strlen(message))));
			}
			
			ObjClass* actual = cardinalGetClass(vm, POP());
			bool isInstance = cardinalIsSubClass(actual, AS_CLASS(expected));
			
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
			
			if (IS_INSTANCE(stackStart[0])) {
				cardinalStackPop(vm, &AS_INSTANCE(stackStart[0])->stack);
			}

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
			ObjClosure* closure = cardinalNewClosure(vm, prototype);
			PUSH(OBJ_VAL(closure));

			// Capture upvalues.
			for (int i = 0; i < prototype->numUpvalues; i++) {
				bool isLocal = READ_BOOL();
				cardinal_integer index = READ_LOCAL();
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
		
		CASECODE(CONSTRUCT):
		{
			ASSERT(IS_CLASS(stackStart[0]), "'this' should be a class.");
			stackStart[0] = cardinalNewInstance(vm, AS_CLASS(stackStart[0]));
			DISPATCH();
		}
		// Create a new class and push it onto the stack
		CASECODE(CLASS):
		{
			// Stack
			// Possible classes
			// NULL or first superclass
			// Name
			
			bool exists = AS_BOOL(POP());
			cardinal_integer numFields = READ_FIELD();
			if (!exists || numFields > 0) {
				ObjString* name = AS_STRING(POP());
				CARDINAL_PIN(vm, name);
				ObjClass* superclass = vm->metatable.objectClass;
	
				// Use implicit Object superclass if none given.
				if (!IS_NULL(PEEK())) {
					ObjString* error = validateSuperclass(vm, name, PEEK());
					if (error != NULL) RUNTIME_ERROR(error);
					superclass = AS_CLASS(PEEK());
				}
				DROP();
				
				cardinal_integer numSuperClasses = READ_CONSTANT() - 1;
	
				ObjClass* classObj = cardinalNewClass(vm, superclass, numFields, name);
				
				CARDINAL_UNPIN(vm);
				CARDINAL_PIN(vm, classObj);
				int i = 1;
				while (numSuperClasses > 0) {
					if (!IS_NULL(PEEK())) {
						ObjString* error = validateSuperclass(vm, name, PEEK());
						if (error != NULL) RUNTIME_ERROR(error);
						superclass = AS_CLASS(PEEK());
					
						cardinalBindSuperclass(vm, classObj, superclass);
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
	
					RUNTIME_ERROR(AS_STRING(cardinalNewString(vm, message, strlen(message))));
				}
				CARDINAL_UNPIN(vm);
				PUSH(OBJ_VAL(classObj));
			} else {
				cardinal_integer numSuperClasses = READ_CONSTANT();
				int i = 1;
				ObjClass* classObj = AS_CLASS(POP());
				ObjString* name = AS_STRING(POP());
				while (numSuperClasses > 0) {
					if (!IS_NULL(PEEK())) {
						ObjString* error = validateSuperclass(vm, name, PEEK());
						if (error != NULL) RUNTIME_ERROR(error);
						ObjClass* superclass = AS_CLASS(PEEK());
						
						cardinalBindSuperclass(vm, classObj, superclass);
					}
					DROP();
					i++;
					numSuperClasses--;
				}
				PUSH(OBJ_VAL(classObj));
			}
			CHECK_STACK();
			DISPATCH();
		}
		
		// Create a new method and push it onto the stack
		CASECODE(METHOD_INSTANCE):
		CASECODE(METHOD_STATIC):
		{
			cardinal_integer symbol = READ_METHOD();
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
void cardinalSetCompiler(CardinalVM* vm, CardinalCompiler* compiler) {
	vm->compiler = compiler;
}

// Execute [source] in the context of the core module.
static CardinalLangResult loadIntoCore(CardinalVM* vm, const char* source) {
	ObjModule* coreModule = getCoreModule(vm);

	ObjFn* fn = cardinalCompile(vm, coreModule, "", source);
	if (fn == NULL) return CARDINAL_COMPILE_ERROR;

	cardinalPushRoot(vm, (Obj*)fn);
	vm->fiber = cardinalNewFiber(vm, (Obj*)fn);
	cardinalPopRoot(vm); // fn.

	return runInterpreter(vm) ? CARDINAL_SUCCESS : CARDINAL_RUNTIME_ERROR;
}

CardinalLangResult cardinalInterpret(CardinalVM* vm, const char* sourcePath, const char* source) {
	return cardinalInterpretModule(vm, sourcePath, source, "main");
}

Value cardinalFindVariable(CardinalVM* vm, const char* name) {
	ObjModule* coreModule = getCoreModule(vm);
	int symbol = cardinalSymbolTableFind(&coreModule->variableNames,
	                                 name, strlen(name));
	return coreModule->variables.data[symbol];
}

int cardinalFindVariableSymbol(CardinalVM* vm, ObjModule* module, const char* name, int length) {
	if (module == NULL) module = getCoreModule(vm);

	return cardinalSymbolTableFind(&module->variableNames,
	                                 name, length);
}

int cardinalDeclareVariable(CardinalVM* vm, ObjModule* module, const char* name,
                        size_t length) {
	if (module == NULL) module = getCoreModule(vm);
	if (module->variables.count == MAX_GLOBALS) return -2;

	module->count++;
	cardinalValueBufferWrite(vm, &module->variables, UNDEFINED_VAL);
	return cardinalSymbolTableAdd(vm, &module->variableNames, name, length);
}

int cardinalDefineVariable(CardinalVM* vm, ObjModule* module, const char* name,
                       size_t length, Value value) {
	if (module == NULL) module = getCoreModule(vm);
	if (module->variables.count == MAX_GLOBALS) return -2;

	if (IS_OBJ(value)) cardinalPushRoot(vm, AS_OBJ(value));

	// See if the variable is already explicitly or implicitly declared.
	int symbol = cardinalSymbolTableFind(&module->variableNames, name, length);

	if (symbol == -1) {
		// Brand new variable.
		symbol = cardinalSymbolTableAdd(vm, &module->variableNames, name, length);
		cardinalValueBufferWrite(vm, &module->variables, value);
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

	if (IS_OBJ(value)) cardinalPopRoot(vm);

	return symbol;
}

///////////////////////////////////////////////////////////////////////////////////
//// GARBAGE COLLECTOR
///////////////////////////////////////////////////////////////////////////////////

void cardinalPushRoot(CardinalVM* vm, Obj* obj) {
	ASSERT(vm->garbageCollector.numTempRoots < CARDINAL_MAX_TEMP_ROOTS, "Too many temporary roots.");
	vm->garbageCollector.tempRoots[vm->garbageCollector.numTempRoots++] = obj;
}

void cardinalPopRoot(CardinalVM* vm) {
	ASSERT(vm->garbageCollector.numTempRoots > 0, "No temporary roots to release.");
	vm->garbageCollector.numTempRoots--;
}

void cardinalAddGCObject(CardinalVM* vm, Obj* obj) {
	// Check if the garbage collector is in use
	if (obj->type == OBJ_TABLE_ELEM || obj->type == OBJ_UPVALUE || vm->garbageCollector.isCoupled) {
		obj->gcflag = (GCFlag) 0;
		
		obj->next = vm->garbageCollector.first;
		obj->prev = NULL;
		if (vm->garbageCollector.first != NULL)
			vm->garbageCollector.first->prev = obj;
		vm->garbageCollector.first = obj;
	}
}

/// Removes an object from the GC
void cardinalRemoveGCObject(CardinalVM* vm, Obj* obj) {
	if (obj->next != NULL) obj->next->prev = obj->prev;
	if (obj->prev != NULL) obj->prev->next = obj->next;
	else vm->garbageCollector.first = obj->next;
}

/// Used to get statistics from the Garbage collector
void cardinalGetGCStatistics(CardinalVM* vm, int* size, int* destroyed, int* detected, int* newObj, int* nextCycle, int* nbHosts) {
	*size = vm->garbageCollector.bytesAllocated;
	*destroyed = vm->garbageCollector.destroyed;
	*detected = vm->garbageCollector.destroyed;
	*newObj = vm->garbageCollector.active;
	*nextCycle = vm->garbageCollector.nextGC;
	*nbHosts = vm->hostObjects.hostObjects->count;
}

static void collectGarbage(CardinalVM* vm) {
	if (vm->garbageCollector.isWorking) return;
#if CARDINAL_DEBUG_TRACE_MEMORY || CARDINAL_DEBUG_TRACE_GC
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
	
	if (vm->rootDirectory != NULL) cardinalMarkObj(vm, (Obj*)vm->rootDirectory);
	
	if (vm->modules != NULL) cardinalMarkObj(vm, (Obj*)vm->modules);

	// Temporary roots.
	for (int i = 0; i < vm->garbageCollector.numTempRoots; i++) {
		cardinalMarkObj(vm, vm->garbageCollector.tempRoots[i]);
	}
	
	if (vm->hostObjects.freeNums != NULL)
		cardinalMarkObj(vm, (Obj*) vm->hostObjects.freeNums);
	if (vm->hostObjects.hostObjects != NULL)
		cardinalMarkObj(vm, (Obj*) vm->hostObjects.hostObjects);
	
	// The current fiber.
	if (vm->fiber != NULL) cardinalMarkObj(vm, (Obj*)vm->fiber);

	// Any object the compiler is using (if there is one).
	if (vm->compiler != NULL) cardinalMarkCompiler(vm, vm->compiler);
	
	// Collect any unmarked objects.
	vm->garbageCollector.active = 0;
	Obj** obj = &vm->garbageCollector.first;
	while (*obj != NULL) {
		if (!((*obj)->gcflag & FLAG_MARKED)) {
			// This object wasn't reached, so remove it from the list and free it.
			Obj* unreached = *obj;
			if (unreached->next != NULL) unreached->next->prev = unreached->prev;
			if (unreached->prev != NULL) unreached->prev->next = unreached->next;
			*obj = unreached->next;
			cardinalFreeObj(vm, unreached);
			vm->garbageCollector.destroyed++;
		}
		else {
			// This object was reached, so unmark it (for the next GC) and move on to
			// the next.
			(*obj)->gcflag = (GCFlag) ( (*obj)->gcflag & ~FLAG_MARKED );
			obj = &(*obj)->next;
			vm->garbageCollector.active++;
		}
	}
	
	vm->garbageCollector.nextGC = vm->garbageCollector.bytesAllocated * vm->garbageCollector.heapScalePercent / 100;
	if (vm->garbageCollector.nextGC < vm->garbageCollector.minNextGC) vm->garbageCollector.nextGC = vm->garbageCollector.minNextGC;
	
	vm->garbageCollector.isWorking = false;
	
#if CARDINAL_DEBUG_TRACE_MEMORY || CARDINAL_DEBUG_TRACE_GC
	double elapsed = ((double)clock() / CLOCKS_PER_SEC) - startTime;
	vm->printFunction("GC %ld before, %ld after (%ld collected), next at %ld. Took %.3fs.\n",
         before, vm->garbageCollector.bytesAllocated, before - vm->garbageCollector.bytesAllocated, vm->garbageCollector.nextGC,
         elapsed);
#endif
}


///////////////////////////////////////////////////////////////////////////////////
//// MEMORY ALLOCATOR
///////////////////////////////////////////////////////////////////////////////////

void* cardinalReallocate(CardinalVM* vm, void* buffer, size_t oldSize, size_t newSize) {
#if CARDINAL_DEBUG_TRACE_MEMORY
	vm->printFunction("reallocate %p %ld -> %ld\n", buffer, oldSize, newSize);
	
#endif
#if CARDINAL_DEBUG_TRACE_MEMORY || CARDINAL_DEBUG_TRACE_GC
	if (newSize != 0)
		vm->garbageCollector.nbAllocations++;
	
	if (buffer != NULL)
		vm->garbageCollector.nbFrees++;
#endif
	
	// If new bytes are being allocated, add them to the total count. If objects
	// are being completely deallocated, we don't track that (since we don't
	// track the original size). Instead, that will be handled while marking
	// during the next GC.
	vm->garbageCollector.bytesAllocated += newSize - oldSize;
	
#if CARDINAL_DEBUG_GC_STRESS
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

void cardinalCollectGarbage(CardinalVM* vm) {
	collectGarbage(vm);
}

// Set the garbage collector enabled or disabled
void cardinalEnableGC(CardinalVM* vm, bool enable) {
	vm->garbageCollector.isWorking = enable;
}

static Value* findVariable(CardinalVM* vm, ObjModule* module, const char* name) {
	UNUSED(vm);
	int symbol = cardinalSymbolTableFind(&module->variableNames, name, strlen(name));
	if (symbol != -1) return &module->variables.data[symbol];
	return NULL;
}

static void defineMethod(CardinalVM* vm, const char* module, const char* className,
                         const char* signature,
                         cardinalForeignMethodFn methodFn, bool isStatic) {
	ASSERT(className != NULL, "Must provide class name.");

	int length = (int)strlen(signature);
	ASSERT(signature != NULL, "Must provide signature.");
	ASSERT(strlen(signature) < MAX_METHOD_SIGNATURE, "Signature too long.");

	ASSERT(methodFn != NULL, "Must provide method function.");
	
	ObjModule* coreModule = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			coreModule = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	//ObjModule* coreModule = getCoreModule(vm);
	// Find or create the class to bind the method to.
	int classSymbol = cardinalSymbolTableFind(&coreModule->variableNames,
										  className, strlen(className));
	ObjClass* classObj;

	if (classSymbol != -1) {
		classObj = AS_CLASS(coreModule->variables.data[classSymbol]);
	}
	else {
		// The class doesn't already exist, so create it.
		size_t len = strlen(className);
		ObjString* nameString = AS_STRING(cardinalNewString(vm, className, len));
		
		cardinalPushRoot(vm, (Obj*)nameString);
		
		classObj = cardinalNewClass(vm, vm->metatable.objectClass, 0, nameString);
		cardinalDefineVariable(vm, coreModule, className, len, OBJ_VAL(classObj));
		cardinalPopRoot(vm);
	}
	// Bind the method.
	int methodSymbol = cardinalSymbolTableEnsure(vm, &vm->methodNames,
					   signature, length);

	Method method;
	method.type = METHOD_FOREIGN;
	method.fn.foreign = methodFn;

	if (isStatic) classObj = classObj->obj.classObj;

	cardinalBindMethod(vm, classObj, methodSymbol, method);
}

// Compiles [source], a string of Cardinal source code, to an [ObjFn] that will
// execute that code when invoked.
CardinalValue* cardinalCompileScript(CardinalVM* vm, const char* sourcePath, const char* source) {
	return cardinalCompileScriptModule(vm, sourcePath, source, "main");
}

// Runs [source], a string of Cardinal source code in a new fiber in [vm].
CardinalLangResult cardinalInterpretModule(CardinalVM* vm, const char* sourcePath, const char* source, const char* module) {
	if (strlen(sourcePath) == 0) return loadIntoCore(vm, source);
	
	Value name = cardinalNewString(vm, module, strlen(module));
	cardinalPushRoot(vm, AS_OBJ(name));
	
	ObjFiber* fiber = loadModuleNoMemory(vm, name, source);
	
	if (fiber == NULL) {
		cardinalPopRoot(vm);
		return CARDINAL_COMPILE_ERROR;
	}
	
	fiber->rootDirectory = vm->rootDirectory;
	vm->fiber = fiber;
	
	bool succeeded = runInterpreter(vm);
	
	cardinalPopRoot(vm); // name
	
	return succeeded ? CARDINAL_SUCCESS : CARDINAL_RUNTIME_ERROR;
}

// Compiles [source], a string of Cardinal source code, to an [ObjFn] that will
// execute that code when invoked.
CardinalValue* cardinalCompileScriptModule(CardinalVM* vm, const char* sourcePath, const char* source, const char* module) {
	UNUSED(sourcePath);
	Value name = cardinalNewString(vm, module, strlen(module));
	ObjFiber* fiber = loadModuleNoMemory(vm, name, source);
	
	if (fiber == NULL)
		return NULL;

	// Link the fiber to the GC
	return cardinalCreateHostObject(vm, OBJ_VAL((Obj*) fiber));
}

// Runs a fiber loaded with a function [key] in the virtual machine [vm].
CardinalLangResult cardinalRunFunction(CardinalVM* vm, CardinalValue* key) {
	Value val = cardinalGetHostObject(vm, key);
	if (!IS_FIBER(val)) return CARDINAL_COMPILE_ERROR;
	
	vm->fiber = AS_FIBER(val);
	if (runInterpreter(vm)) {
		return CARDINAL_SUCCESS;
	}
	else {
		return CARDINAL_RUNTIME_ERROR;
	}
}

// Creates an [ObjFn] that invokes a method with [signature] when called.
static ObjFn* makeCallStub(CardinalVM* vm, ObjModule* module, const char* signature) {
	int signatureLength = (int)strlen(signature);

	// Count the number parameters the method expects.
	int numParams = 0;
	for (const char* s = signature; *s != '\0'; s++) {
		if (*s == '_') numParams++;
	}

	int method =  cardinalSymbolTableEnsure(vm, &vm->methodNames,
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
	cardinalSymbolTableInit(vm, &locals);
	cardinalSymbolTableInit(vm, &lines);
	FnDebug* debug = cardinalNewDebug(vm, NULL, signature, signatureLength, debugLines, locals, lines);
	return cardinalNewFunction(vm, module, NULL, 0, 0, 0, bytecode, end+2, debug);
}

static CardinalValue* getMethod(CardinalVM* vm, ObjModule* moduleObj, Value variable,
                          const char* signature) {
	ObjFn* fn = makeCallStub(vm, moduleObj, signature);
	cardinalPushRoot(vm, (Obj*)fn);

	// Create a single fiber that we can reuse each time the method is invoked.
	ObjFiber* fiber = cardinalNewFiber(vm, (Obj*)fn);
	cardinalPushRoot(vm, (Obj*)fiber);

	// Create a handle that keeps track of the function that calls the method.
	CardinalValue* ret = cardinalCreateHostObject(vm, OBJ_VAL(fiber));

	// Store the receiver in the fiber's stack so we can use it later in the call.
	*fiber->stacktop++ = variable;

	cardinalPopRoot(vm); // fiber.
	cardinalPopRoot(vm); // fn.

	return ret;
}

CardinalValue* cardinalGetMethod(CardinalVM* vm, const char* module, const char* variable,
                          const char* signature) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}

	int variableSlot = cardinalSymbolTableFind(&moduleObj->variableNames,
	                                       variable, strlen(variable));
	
	if (variableSlot < 0)
		return NULL;
	
	return getMethod(vm, moduleObj, moduleObj->variables.data[variableSlot], signature);
}

CardinalValue* cardinalGetMethodObject(CardinalVM* vm, const char* module, CardinalValue* variable,
                          const char* signature) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}

	Value val = cardinalGetHostObject(vm, variable);
	
	return getMethod(vm, moduleObj, val, signature);
}

static CardinalValue* staticCardinalCall(CardinalVM* vm, CardinalValue* method, int args, CardinalValue* arg, va_list argList) {
	// TODO: Validate that the number of arguments matches what the method
	// expects.

	// Push the arguments.
	ObjFiber* fiber = AS_FIBER(cardinalGetHostObject(vm, method));
	
	Value value = NULL_VAL;
	value = cardinalGetHostObject(vm, arg);
	*fiber->stacktop++ = value;
		
	while (args > 0) {
		value = NULL_VAL;
		
		value = cardinalGetHostObject(vm, va_arg(argList, CardinalValue*));
		
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
	cardinalResetFiber(fiber, fn);

	// Push the receiver back on the stack.
	*fiber->stacktop++ = receiver;
	
	return cardinalCreateHostObject(vm, returnValue);
}

CardinalValue* cardinalCall(CardinalVM* vm, CardinalValue* method, int args, ...) {
	// TODO: Validate that the number of arguments matches what the method
	// expects.

	// Push the arguments.
	va_list argList;
	va_start(argList, args);
	Value val = cardinalGetHostObject(vm, method);
	if (IS_NULL(val))
		return NULL;
	ObjFiber* fiber = AS_FIBER(val);
	
	while (args > 0) {
		Value value = NULL_VAL;
		
		value = cardinalGetHostObject(vm, va_arg(argList, CardinalValue*));
		
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
	cardinalResetFiber(fiber, fn);

	// Push the receiver back on the stack.
	*fiber->stacktop++ = receiver;
	
	return cardinalCreateHostObject(vm, returnValue);
}

// Flush all host objects
void cardinalFlushHostObjects(CardinalVM* vm) {
	vm->hostObjects.freeNums = cardinalNewList(vm, 0);
	vm->hostObjects.hostObjects = cardinalNewTable(vm, 0);
	vm->hostObjects.max = 0;
}

// Will create an object with a certain name
CardinalValue* cardinalCreateObject(CardinalVM* vm, const char* module, const char* className, const char* signature, int args, ...) {
	CardinalValue* meth = cardinalGetMethod(vm, module, className, "<instantiate>");
	
	CardinalValue* ret = cardinalCall(vm, meth, 0);
	cardinalReleaseObject(vm, meth);
	
	meth = cardinalGetMethod(vm, module, className, signature);
	va_list argList;
	va_start(argList, args);
	CardinalValue* actualRet = staticCardinalCall(vm, meth, args, ret, argList);
	cardinalReleaseObject(vm, meth);
	cardinalReleaseObject(vm, ret);
	
	return actualRet;
}

// Bind the object to the VM as an instance
CardinalValue* cardinalBindObject(CardinalVM* vm, const char* module, const char* className, void* obj, size_t size) {
	cardinalDefineClass(vm, module, className, size, NULL);
	
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}

	int variableSlot = cardinalSymbolTableFind(&moduleObj->variableNames,
	                                       className, strlen(className));
	
	if (variableSlot < 0)
		return NULL;
	
	ObjClass* classObj = AS_CLASS(moduleObj->variables.data[variableSlot]);
	Value vmObj = cardinalNewInstance(vm, classObj);
	memcpy(AS_INSTANCE(vmObj)->fields, obj, classObj->numFields*sizeof(Value));
	
	return cardinalCreateHostObject(vm, vmObj);
}

// Will create a number with value [num]
CardinalValue* cardinalCreateNumber(CardinalVM* vm, double num) {
	return cardinalCreateHostObject(vm, NUM_VAL(num));
}

// Will create a bool with value [val]
CardinalValue* cardinalCreateBool(CardinalVM* vm, bool val) {
	return cardinalCreateHostObject(vm, val ? TRUE_VAL : FALSE_VAL);
}

CardinalValue* cardinalCreateValue(CardinalVM* vm) {
	return cardinalCreateHostObject(vm, NULL_VAL);
}

// Will create a string with value [val]
CardinalValue* cardinalCreateString(CardinalVM* vm, const char* text, int length) {
	size_t size = length;
	if (length == -1) size = strlen(text);

	return cardinalCreateHostObject(vm, cardinalNewString(vm, text, size));
}

// Creates a new list
CardinalValue* cardinalCreateObjectList(CardinalVM* vm) {
	return cardinalCreateHostObject(vm, OBJ_VAL(cardinalNewList(vm, 0)));
}

// Adds an element to the list
void cardinalObjectListAdd(CardinalVM* vm, CardinalValue* list, CardinalValue* variable) {
	Value l = cardinalGetHostObject(vm, list);
	Value elem = cardinalGetHostObject(vm, variable);
	cardinalListAdd(vm, AS_LIST(l), elem);
}

// Creates a new list
CardinalValue* cardinalCreateObjectMap(CardinalVM* vm) {
	return cardinalCreateHostObject(vm, OBJ_VAL(cardinalNewMap(vm)));
}

// Adds an element to the list
void cardinalObjectMapSet(CardinalVM* vm, CardinalValue* list, CardinalValue* key, CardinalValue* val) {
	Value l = cardinalGetHostObject(vm, list);
	Value k = cardinalGetHostObject(vm, key);
	Value v = cardinalGetHostObject(vm, val);
	cardinalMapSet(vm, AS_MAP(l), k, v);
}

// Release's a certain object
void cardinalReleaseObject(CardinalVM* vm, CardinalValue* val) {
	if (val != NULL)
		cardinalRemoveHostObject(vm, val);
}

void cardinalDefineMethod(CardinalVM* vm, const char* module, const char* className,
                            const char* signature,
                            cardinalForeignMethodFn methodFn) {
	defineMethod(vm, module, className, signature, methodFn, false);
}

void cardinalDefineStaticMethod(CardinalVM* vm, const char* module, const char* className,
                            const char* signature,
                            cardinalForeignMethodFn methodFn) {
	defineMethod(vm, module, className, signature, methodFn, true);
}

// Defines a destructor [destructor] for the given class
// All instances of class [className] will call the destructor when the GC
// decides to destroy the object.
// The exact timing of the destruction can not be known.
// The destructors purpose is to clean up any manual memory from an instance
// of [className]
void cardinalDefineDestructor(CardinalVM* vm, const char* module, const char* className, cardinalDestructorFn destructor) {
	ASSERT(className != NULL, "Must provide class name.");
	ASSERT(destructor != NULL, "Must provide method function.");
	
	ObjModule* coreModule = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			coreModule = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	// Find or create the class to bind the method to.
	int classSymbol = cardinalSymbolTableFind(&coreModule->variableNames,
										  className, strlen(className));
	ObjClass* classObj;

	if (classSymbol != -1) {
		classObj = AS_CLASS(coreModule->variables.data[classSymbol]);
	}
	else {
		// The class doesn't already exist, so create it.
		size_t len = strlen(className);
		ObjString* nameString = AS_STRING(cardinalNewString(vm, className, len));
		
		cardinalPushRoot(vm, (Obj*)nameString);
		
		classObj = cardinalNewClass(vm, vm->metatable.objectClass, 0, nameString);
		cardinalDefineVariable(vm, coreModule, className, len, OBJ_VAL(classObj));
		cardinalPopRoot(vm);
	}
	
	classObj->destructor = destructor;
}

void cardinalDefineClass(CardinalVM* vm, const char* module, const char* className, size_t size, const char* parent) {
	ASSERT(className != NULL, "Must provide class name.");
	
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}

	// Find or create the class to bind the method to.
	if (cardinalSymbolTableFind(&moduleObj->variableNames, className, strlen(className)) > 0) return;
										  
	// The class doesn't already exist, so create it.
	size_t length = strlen(className);
	ObjString* nameString = AS_STRING(cardinalNewString(vm, className, length));
	
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

	CARDINAL_PIN(vm, nameString);

	ObjClass* classObj = cardinalNewClass(vm, super, numFields, nameString);
	cardinalDefineVariable(vm, moduleObj, className, length, OBJ_VAL(classObj));

	CARDINAL_UNPIN(vm);
}

// Get the object as a void*
// This assumes that the object is an instance
void* cardinalGetInstance(CardinalVM* vm, CardinalValue* val) {
	Value obj = cardinalGetHostObject(vm, val);
	return ((Obj*)AS_INSTANCE(obj))+1;
}

// Get the object as a bool
bool cardinalGetBoolean(CardinalVM* vm, CardinalValue* val) {
	Value obj = cardinalGetHostObject(vm, val);
	return AS_BOOL(obj);
}

// Get the object as a number
double cardinalGetNumber(CardinalVM* vm, CardinalValue* val) {
	Value obj = cardinalGetHostObject(vm, val);
	return AS_NUM(obj);
}

// Get the object as a void*
const char* cardinalGetString(CardinalVM* vm, CardinalValue* val) {
	Value obj = cardinalGetHostObject(vm, val);
	return AS_CSTRING(obj);
}


void createModule(CardinalVM* vm, const char* name) {
	cardinalImportModuleVar(vm, cardinalNewString(vm, name, strlen(name)));
}

void removeModule(CardinalVM* vm, const char* name) {
	Value nameValue = cardinalNewString(vm, name, strlen(name));
	cardinalPushRoot(vm, AS_OBJ(nameValue));
	
	// If the module is already loaded, we don't need to do anything.
	uint32_t index = cardinalMapFind(vm->modules, nameValue);
	if (index != UINT32_MAX) {
		cardinalMapRemoveKey(vm, vm->modules, nameValue);
	}
	
	cardinalPopRoot(vm);
}

// Removes a variable from the VM
void cardinalRemoveVariable(CardinalVM* vm, const char* module, const char* variable) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	// Find or create the class to bind the method to.
	int symbol = cardinalSymbolTableFind(&moduleObj->variableNames, variable, strlen(variable));
	if (symbol > 0) {
		moduleObj->variables.data[symbol] = NULL_VAL;
	}
}

// Get a top-level variable from a given module
CardinalValue* getModuleVariable(CardinalVM* vm, const char* module, const char* variable) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	// Find or create the class to bind the method to.
	int symbol = cardinalSymbolTableFind(&moduleObj->variableNames, variable, strlen(variable));
	if (symbol > 0) {
		return cardinalCreateHostObject(vm, moduleObj->variables.data[symbol]);
	}
	return NULL;
}

// Get a method from the VM
void cardinalRemoveMethod(CardinalVM* vm, const char* module, const char* variable, const char* signature) {
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	// Find or create the class to bind the method to.
	int symbol = cardinalSymbolTableFind(&moduleObj->variableNames, variable, strlen(variable));
	if (symbol > 0) {
		Value val = moduleObj->variables.data[symbol];
		
		if (!IS_CLASS(val)) return;
		ObjClass* obj = AS_CLASS(val);
		
		int method =  cardinalSymbolTableFind(&vm->methodNames,
	                                    signature, strlen(signature));
		obj->methods.data[method].type = METHOD_NONE;
	}
}

// Get a method from the VM with an host object
void cardinalRemoveMethodObject(CardinalVM* vm, CardinalValue* variable, const char* signature) {
	// Find or create the class to bind the method to.
	Value val = cardinalGetHostObject(vm, variable);
	
	if (!IS_CLASS(val)) return;
	ObjClass* obj = AS_CLASS(val);
	
	int method =  cardinalSymbolTableFind(&vm->methodNames,
									signature, strlen(signature));
	obj->methods.data[method].type = METHOD_NONE;
}

///////////////////////////////////////////////////////////////////////////////////
//// THE GETTERS FOR FOREIGN METHODS
///////////////////////////////////////////////////////////////////////////////////

CardinalValue* cardinalGetArgument(CardinalVM* vm, int index) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	ASSERT(index >= 0, "index cannot be negative.");
	ASSERT(index < vm->fiber->foreignCallNumArgs, "Not that many arguments.");

	return cardinalCreateHostObject(vm, *(vm->fiber->foreignCallSlot + index));
}

void cardinalReturnValue(CardinalVM* vm, CardinalValue* val) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	
	*vm->fiber->foreignCallSlot = cardinalGetHostObject(vm, val);
	vm->fiber->foreignCallSlot = NULL;
	cardinalReleaseObject(vm, val);
}

bool cardinalGetArgumentBool(CardinalVM* vm, int index) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	ASSERT(index >= 0, "index cannot be negative.");
	ASSERT(index < vm->fiber->foreignCallNumArgs, "Not that many arguments.");

	if (!IS_BOOL(*(vm->fiber->foreignCallSlot + index))) return false;

	return AS_BOOL(*(vm->fiber->foreignCallSlot + index));
}

double cardinalGetArgumentDouble(CardinalVM* vm, int index) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	ASSERT(index >= 0, "index cannot be negative.");
	ASSERT(index < vm->fiber->foreignCallNumArgs, "Not that many arguments.");
	
	if (!IS_NUM(*(vm->fiber->foreignCallSlot + index)))
		return 0.0;
	return AS_NUM(*(vm->fiber->foreignCallSlot + index));
}

const char* cardinalGetArgumentString(CardinalVM* vm, int index) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");
	ASSERT(index >= 0, "index cannot be negative.");
	ASSERT(index < vm->fiber->foreignCallNumArgs, "Not that many arguments.");

	if (!IS_STRING(*(vm->fiber->foreignCallSlot + index)))
		return NULL;
	return AS_CSTRING(*(vm->fiber->foreignCallSlot + index));
}

void cardinalReturnDouble(CardinalVM* vm, double value) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");

	*vm->fiber->foreignCallSlot = NUM_VAL(value);
	vm->fiber->foreignCallSlot = NULL;
}

void cardinalReturnNull(CardinalVM* vm) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");

	*vm->fiber->foreignCallSlot = NULL_VAL;
	vm->fiber->foreignCallSlot = NULL;
}

void cardinalReturnString(CardinalVM* vm, const char* text, int length) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");

	size_t size = length;
	if (length == -1) size = strlen(text);

	*vm->fiber->foreignCallSlot = cardinalNewString(vm, text, size);
	vm->fiber->foreignCallSlot = NULL;
}

// Provides a boolean return value for a foreign call. This must only be called
// within a function provided to [cardinalDefineMethod]. Once this is called, the
// foreign call is done, and no more arguments can be read or return calls made.
void cardinalReturnBool(CardinalVM* vm, bool value) {
	ASSERT(vm->fiber->foreignCallSlot != NULL, "Must be in foreign call.");

	*vm->fiber->foreignCallSlot = BOOL_VAL(value);
	vm->fiber->foreignCallSlot = NULL;
}

///////////////////////////////////////////////////////////////////////////////////
//// THE METHODS FOR BYTECODE
///////////////////////////////////////////////////////////////////////////////////

// Loads [source], a string of Cardinal byte code, to an [ObjValue] that will
// execute that code when invoked.
CardinalValue* cardinalLoadByteCode(CardinalVM* vm, const char* sourcePath, const char* source) {
	UNUSED(sourcePath);
	Value name = cardinalNewString(vm, "main", 4);

	ObjModule* module = loadModule(vm, name, NULL_VAL);
	ObjFn* fn = cardinalCompileFromByteCode(vm, module, AS_CSTRING(name), source);
	if (fn == NULL) return NULL;
	module->func = fn;
	
	// Return the fiber that executes the module.
	ObjFiber* fiber= cardinalNewFiber(vm, (Obj*)module->func);
	
	if (fiber == NULL)
		return NULL;

	// Link the fiber to the GC
	return cardinalCreateHostObject(vm, OBJ_VAL((Obj*) fiber));
}

// Saves the entire state of the VM to a string
// The value has to be released by the host application
CardinalValue* cardinalSaveByteCode(CardinalVM* vm) {
	const char* module = "main";
	ObjModule* moduleObj = getCoreModule(vm);
	if (module != NULL) {
		Value moduleName = cardinalNewString(vm, module, strlen(module));
		uint32_t moduleEntry = cardinalMapFind(vm->modules, moduleName);
		
		if (moduleEntry != UINT32_MAX) {
			moduleObj = AS_MODULE(vm->modules->entries[moduleEntry].value);
		}
	}
	
	ObjString* bytecode = cardinalCompileToByteCode(vm, moduleObj);
	
	// Link the string to the GC
	return cardinalCreateHostObject(vm, OBJ_VAL((Obj*) bytecode));
}

// Sets the debug mode of the VM [vm] to boolean value [set]
void cardinalSetDebugMode(CardinalVM* vm, bool set) {
	vm->debugMode = set;
}