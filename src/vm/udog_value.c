#include <stdio.h>
#include <string.h>
#include <math.h>

#ifndef __STDC_LIMIT_MACROS
	#define __STDC_LIMIT_MACROS 1
#endif
#include <stdint.h>

#include "udog.h"
#include "udog_vm.h"

#include <stdarg.h>


DEFINE_BUFFER(Method, Method)
DEFINE_BUFFER(Value, Value)

ObjInstance* udogThrowException(UDogVM* vm, ObjString* str) {
	UDOG_PIN(vm, str);
	ObjClass* prnt = AS_CLASS(udogFindVariable(vm, "Exception"));
	UDOG_PIN(vm, prnt);
	ObjInstance* inst = AS_INSTANCE(udogNewInstance(vm, prnt));
	
	UDOG_UNPIN(vm);
	UDOG_UNPIN(vm);
	inst->fields[0] = OBJ_VAL(str);
	inst->fields[1] = NULL_VAL;
	return inst;
}

ObjInstance* udogInsertStackTrace(ObjInstance* inst, ObjString* str) {
	inst->fields[1] = OBJ_VAL(str);
	return inst;
}

ObjString* udogGetErrorString(UDogVM* vm, ObjFiber* fiber) {
	UNUSED(vm);
	ObjInstance* inst = fiber->error;
	return AS_STRING(inst->fields[0]);
}

bool udogIsObjInstanceOf(UDogVM* vm, Value val, const char* className) {
	if (!udogIsObjType(val, OBJ_INSTANCE)) return false;
	
	ObjClass* cls = udogGetClass(vm, val);
	ObjClass* expected = AS_CLASS(udogFindVariable(vm, className));
	return udogIsSubClass(cls, expected);
}

static void initObj(UDogVM* vm, Obj* obj, ObjType type, ObjClass* classObj) {
	obj->type = type;
	obj->classObj = classObj;
	
	udogAddGCObject(vm, obj);
}

// Creates a new "raw" class. It has no metaclass or superclass whatsoever.
// This is only used for bootstrapping the initial Object and Class classes,
// which are a little special.
ObjClass* udogNewSingleClass(UDogVM* vm, int numFields, ObjString* name) {
	ObjClass* obj = ALLOCATE(vm, ObjClass);	
	initObj(vm, &obj->obj, OBJ_CLASS, NULL);
	obj->name = name;
	obj->superclass = 0;
	obj->superclasses = NULL;
	
	UDOG_PIN(vm, obj);
	udogMethodBufferInit(vm, &obj->methods);
	obj->superclasses = udogNewList(vm, 0);
	UDOG_UNPIN(vm);
	
	obj->numFields = numFields;
	obj->destructor = NULL;

	return obj;
}

bool udogIsSubClass(ObjClass* actual, ObjClass* expected) {
	// Walk the superclass chain looking for the class.
	while (actual != NULL) {
		if (actual == expected) {
			return true;
		}
		for(int i=0; i<actual->superclasses->count; i++) {
			if (udogIsSubClass(AS_CLASS(actual->superclasses->elements[i]), expected))
				return true;
		}
	}
	return false;
}

// Makes [superclass] the superclass of [subclass], and causes subclass to
// inherit its methods. This should be called before any methods are defined
// on subclass.
void udogBindSuperclass(UDogVM* vm, ObjClass* subclass, ObjClass* superclass) {
	if (superclass == NULL) return;
	
	if (subclass->superclasses == NULL) {
		subclass->superclasses = udogNewList(vm, 0);
	}

	udogListAdd(vm, subclass->superclasses, OBJ_VAL(superclass));

	subclass->superclass += superclass->numFields;
	
	// Include the superclass in the total number of fields.
	subclass->numFields += superclass->numFields;

	// Inherit methods from its superclass.
	for (int i = superclass->methods.count-1; i >= 0; i--) {
		if (superclass->methods.data[i].type != METHOD_NONE)
			udogBindMethod(vm, subclass, i, superclass->methods.data[i]);
	}
}

ObjFn* copyMethodBlock(UDogVM* vm, Method method) {
	ObjFn* func = (ObjFn*) method.fn.obj;
	
	uint8_t* bytecode = ALLOCATE_ARRAY(vm, uint8_t, func->bytecodeLength);
	int* sourcelines = ALLOCATE_ARRAY(vm, int, func->bytecodeLength);
	
	for(int i=0; i<func->bytecodeLength; i++) {
		bytecode[i] = func->bytecode[i];
		sourcelines[i] = func->debug->sourceLines[i];
	}
	
	SymbolTable locals;
	SymbolTable lines;
	
	udogSymbolTableInit(vm, &locals);
	udogSymbolTableInit(vm, &lines);
	for(int i=0; i<func->debug->locals.count; i++) {
		udogSymbolTableAdd(vm, &locals, func->debug->locals.data[i].buffer, func->debug->locals.data[i].length);
	}
	for(int i=0; i<func->debug->lines.count; i++) {
		udogSymbolTableAdd(vm, &lines, func->debug->lines.data[i].buffer, func->debug->lines.data[i].length);
	}
	
	FnDebug* debug = udogNewDebug(vm, func->debug->sourcePath, func->debug->name, strlen(func->debug->name), sourcelines, locals, lines);
	
	return udogNewFunction(vm, func->module,
                       func->constants, func->numConstants,
                       func->numUpvalues, func->numParams,
                       bytecode, func->bytecodeLength,
                       debug);
}


static ObjClass* bindSuperClassesFirst(UDogVM* vm, ObjClass* superclass, int adjustment, int num) {
	//if (adjustment == 0 || superclass->numFields == 0) return superclass;
	
	// Create a new class with no superclass and correct amount of fields
	ObjClass* newClass = udogNewClass(vm, NULL, superclass->numFields - superclass->superclass, superclass->name);
	UDOG_PIN(vm, newClass);
	// Set the superclass fields for the new class
	newClass->superclass = adjustment;

	// Bind all super classes from the old superclass
	for(int i=0; i< superclass->superclasses->count; i++) {
		/// \todo is this call correct?
		udogAddSuperclass(vm, i, newClass, AS_CLASS(superclass->superclasses->elements[i]));
	}
	// Copy methods from the original class
	for (int i = superclass->methods.count-1; i >= 0; i--) {
		if (superclass->methods.data[i].type != METHOD_NONE) {
			if (superclass->methods.data[i].type != METHOD_BLOCK) {
				udogBindMethod(vm, newClass, i, superclass->methods.data[i]);
			}
			else {
				Value methodValue = OBJ_VAL(copyMethodBlock(vm, superclass->methods.data[i]));
				
				ObjFn* methodFn = AS_FN(methodValue);
				UDOG_PIN(vm, methodFn);
				// Methods are always bound against the class, and not the metaclass, even
				// for static methods, so that constructors (which are static) get bound like
				// instance methods.
				udogBindMethodSuperCode(vm, num, methodFn);
				
				Method method;
				method.type = METHOD_BLOCK;
				method.fn.obj = AS_OBJ(methodValue);

				udogBindMethod(vm, newClass, i, method);
				UDOG_UNPIN(vm);
			}
		}
	}
	UDOG_UNPIN(vm);
	return newClass;
}

// Makes [superclass] the superclass of [subclass], and causes subclass to
// inherit its methods. This should be called before any methods are defined
// on subclass.
void udogAddFirstSuper(UDogVM* vm, ObjClass* subclass, ObjClass* superclass) {
	ASSERT(superclass != NULL, "Superclass must exist");

	ObjClass* sprclass = bindSuperClassesFirst(vm, superclass, subclass->superclass, 0);
	UDOG_PIN(vm, sprclass);
	udogBindSuperclass(vm, subclass, sprclass);
	UDOG_UNPIN(vm);
}

static ObjClass* bindSuperClasses(UDogVM* vm, ObjClass* superclass, int adjustment, int num) {
	//if (adjustment == 0 || superclass->numFields == 0) return superclass;
	
	// Create a new class with no superclass and correct amount of fields
	ObjClass* newClass = udogNewClass(vm, NULL, superclass->numFields - superclass->superclass, superclass->name);
	UDOG_PIN(vm, newClass);
	// Set the superclass fields for the new class
	newClass->superclass = adjustment;
	
	if (superclass->superclasses == NULL) {
		superclass->superclasses = udogNewList(vm, 0);
	}

	// Bind all super classes from the old superclass
	for(int i=0; i< superclass->superclasses->count; i++) {
		udogAddSuperclass(vm, i, newClass, AS_CLASS(superclass->superclasses->elements[i]));
	}
	// Copy methods from the original class
	for (int i = superclass->methods.count-1; i >= 0; i--) {
		if (superclass->methods.data[i].type != METHOD_NONE) {
			if (superclass->methods.data[i].type != METHOD_BLOCK) {
				udogBindMethod(vm, newClass, i, superclass->methods.data[i]);
			}
			else {
				Value methodValue = OBJ_VAL(copyMethodBlock(vm, superclass->methods.data[i]));
				
				ObjFn* methodFn = AS_FN(methodValue);
				
				// Methods are always bound against the class, and not the metaclass, even
				// for static methods, so that constructors (which are static) get bound like
				// instance methods.
				udogBindMethodCode(vm, num, newClass, methodFn);
				
				Method method;
				method.type = METHOD_BLOCK;
				method.fn.obj = AS_OBJ(methodValue);

				udogBindMethod(vm, newClass, i, method);
			}
		}
	}
	UDOG_UNPIN(vm);
	return newClass;
}

// Makes [superclass] the superclass of [subclass], and causes subclass to
// inherit its methods. This should be called before any methods are defined
// on subclass.
void udogAddSuperclass(UDogVM* vm, int num, ObjClass* subclass, ObjClass* superclass) {
	ASSERT(superclass != NULL, "Superclass must exist");

	ObjClass* sprclass = bindSuperClasses(vm, superclass, subclass->superclass, num);
	udogBindSuperclass(vm, subclass, sprclass);
}

// Creates a new class object as well 
// as its associated metaclass.
ObjClass* udogNewClass(UDogVM* vm, ObjClass* superclass, int numFields, ObjString* name) {
	UDOG_PIN(vm, name);
	// Create the metaclass.
	ObjString* metaclassName = udogStringConcat(vm, name->value, name->length,
                                              " metaclass", -1);
	UDOG_PIN(vm, metaclassName);

	ObjClass* metaclass = udogNewSingleClass(vm, 0, metaclassName);
	metaclass->obj.classObj = vm->metatable.classClass;

	UDOG_UNPIN(vm);

	// Make sure the metaclass isn't collected when we allocate the class.
	UDOG_PIN(vm, metaclass);

	// Metaclasses always inherit Class and do not parallel the non-metaclass
	// hierarchy.
	udogBindSuperclass(vm, metaclass, vm->metatable.classClass);

	ObjClass* classObj = udogNewSingleClass(vm, numFields, name);

	// Make sure the class isn't collected while the inherited methods are being
	// bound.
	UDOG_PIN(vm, classObj);
	classObj->obj.classObj = metaclass;

	if (superclass != NULL)
		udogBindSuperclass(vm, classObj, superclass);

	UDOG_UNPIN(vm);
	UDOG_UNPIN(vm);
	UDOG_UNPIN(vm);

	return classObj;
}

// Bind a method to the VM
void udogBindMethod(UDogVM* vm, ObjClass* classObj, int symbol, Method method) {
	// Make sure the buffer is big enough to reach the symbol's index.
	Method noMethod;
	noMethod.type = METHOD_NONE;
	while (symbol >= classObj->methods.count) {
		udogMethodBufferWrite(vm, &classObj->methods, noMethod);
	}

	classObj->methods.data[symbol] = method;
}

ObjMethod* udogNewMethod(UDogVM* vm) {
	ObjMethod* method = ALLOCATE(vm, ObjMethod);
	initObj(vm, &method->obj, OBJ_METHOD, vm->metatable.methodClass);
	
	//find the correct symbol
	method->symbol = -1;
	method->name = NULL;
	method->caller = NULL_VAL;
	
	return method;
}

bool methodIsReady(UDogVM* vm, ObjMethod* method) {
	UNUSED(vm);
	return method->symbol >= 0 && method->name != NULL && method->caller != NULL_VAL;
}

void udogLoadMethod(UDogVM* vm, ObjMethod* method, ObjString* name) {
	//find the correct symbol
	method->symbol = udogSymbolTableFind(&vm->methodNames, name->value, name->length);
	method->name = name;	
}

// Creates a new closure object that invokes [fn]. Allocates room for its
// upvalues, but assumes outside code will populate it.
ObjClosure* udogNewClosure(UDogVM* vm, ObjFn* fn) {
	ObjClosure* closure = ALLOCATE_FLEX(vm, ObjClosure,
                                      Upvalue*, fn->numUpvalues);
	initObj(vm, &closure->obj, OBJ_CLOSURE, vm->metatable.fnClass);

	closure->fn = fn;

	// Clear the upvalue array. We need to do this in case a GC is triggered
	// after the closure is created but before the upvalue array is populated.
	for (int i = 0; i < fn->numUpvalues; i++) closure->upvalues[i] = NULL;

	return closure;
}

// Creates a new fiber object that will invoke [fn], which can be a function or
// closure.
ObjFiber* udogNewFiber(UDogVM* vm, Obj* fn) {
	ObjFiber* fiber = ALLOCATE(vm, ObjFiber);
	initObj(vm, &fiber->obj, OBJ_FIBER, vm->metatable.fiberClass);
	
	fiber->stack = NULL;
	fiber->frames = NULL;
	UDOG_PIN(vm, fiber);
	udogResetFiber(fiber, fn);
	// Initialise stack and callframe
	fiber->stacksize = STACKSIZE;
	fiber->stack = ALLOCATE_ARRAY(vm, Value, fiber->stacksize);
	
	fiber->framesize = CALLFRAMESIZE;
	fiber->frames = ALLOCATE_ARRAY(vm, CallFrame, fiber->framesize);

	udogResetFiber(fiber, fn);
	UDOG_UNPIN(vm);

	return fiber;
}

void udogResetFiber(ObjFiber* fiber, Obj* fn) {
	// Push the stack frame for the function.
	fiber->stacktop = fiber->stack;
	fiber->numFrames = 1;
	fiber->openUpvalues = NULL;
	fiber->caller = NULL;
	fiber->error = NULL;
	fiber->callerIsTrying = false;
	fiber->yielded = false;
	
	if (fiber->frames != NULL) {
		CallFrame* frame = &fiber->frames[0];
		frame->fn = fn;
		frame->top = fiber->stack;
		if (fn->type == OBJ_FN) {
			frame->pc = ((ObjFn*)fn)->bytecode;
		}
		else {
			frame->pc = ((ObjClosure*)fn)->fn->bytecode;
		}
	
	}
}

FnDebug* udogNewDebug(UDogVM* vm, ObjString* debugSourcePath, const char* debugName, int debugNameLength, 
						int* sourceLines, SymbolTable locals, SymbolTable lines) {
	FnDebug* debug = ALLOCATE(vm, FnDebug);

	debug->sourcePath = debugSourcePath;

	// Copy the function's name.
	debug->name = ALLOCATE_ARRAY(vm, char, debugNameLength + 1);
	strncpy(debug->name, debugName, debugNameLength);
	debug->name[debugNameLength] = '\0';
	debug->sourceLines = sourceLines;
	debug->locals = locals;
	debug->lines = lines;
	return debug;
}
					   
// Creates a new function object with the given code and constants. The new
// function will take over ownership of [bytecode] and [sourceLines]. It will
// copy [constants] into its own array.
ObjFn* udogNewFunction(UDogVM* vm, ObjModule* module,
                       Value* constants, int numConstants,
                       int numUpvalues, int arity,
                       uint8_t* bytecode, int bytecodeLength,
                       FnDebug* debug) {
	// Allocate these before the function in case they trigger a GC which would
	// free the function.
	Value* copiedConstants = NULL;
	if (numConstants > 0) {
		copiedConstants = ALLOCATE_ARRAY(vm, Value, numConstants);
		for (int i = 0; i < numConstants; i++) {
			copiedConstants[i] = constants[i];
		}
	}
	
	ObjFn* fn = ALLOCATE(vm, ObjFn);
	initObj(vm, &fn->obj, OBJ_FN, vm->metatable.fnClass);

	// When the compiler grows the bytecode list, it's capacity will often 
	// exceed the actual used size. Copying to an exact-sized buffer 
	// will save a bit of memory. I tried doing this, 
	// but it made the "for" benchmark ~15% slower for some unknown reason.
	fn->bytecode = bytecode;
	fn->constants = copiedConstants;
	fn->module = module;
	fn->numUpvalues = numUpvalues;
	fn->numConstants = numConstants;
	fn->numParams = arity;
	fn->bytecodeLength = bytecodeLength;
	fn->debug = debug;

	return fn;
}

// Creates a new instance of the given [classObj].
Value udogNewInstance(UDogVM* vm, ObjClass* classObj) {
	ObjInstance* instance = ALLOCATE_FLEX(vm, ObjInstance,
                                        Value, classObj->numFields); 
	initObj(vm, &instance->obj, OBJ_INSTANCE, classObj);

	// Initialize fields to null.
	for (int i = 0; i < classObj->numFields; i++) {
		instance->fields[i] = NULL_VAL;
	}
	
	return OBJ_VAL(instance);
}

// Generates a hash code for [num].
static uint32_t hashNumber(double num) {
	// Hash the raw bits of the value.
	DoubleBits data;
	data.num = num;
	return data.bits32[0] ^ data.bits32[1];
}

// Generates a hash code for [object].
static uint32_t hashObject(Obj* object) {
	switch (object->type) {
		case OBJ_CLASS:
			// Classes just use their name.
			return hashObject((Obj*)((ObjClass*)object)->name);

		case OBJ_RANGE: {
			ObjRange* range = (ObjRange*)object;
			return hashNumber(range->from) ^ hashNumber(range->to);
		}

		case OBJ_STRING:
			return ((ObjString*)object)->hash;

		default:
			ASSERT(false, "Only immutable objects can be hashed.");
			return 0;
	}
}

// Generates a hash code for [value], which must be one of the built-in
// immutable types: null, bool, class, num, range, or string.
static uint32_t hashValue(Value value) {
#if UDOG_NAN_TAGGING
	if (IS_OBJ(value)) return hashObject(AS_OBJ(value));

	// Hash the raw bits of the unboxed value.
	DoubleBits bits;

	bits.bits64 = value;
	return bits.bits32[0] ^ bits.bits32[1];
#else
	switch (value.type) {
		case VAL_FALSE: return 0;
		case VAL_NULL: return 1;
		case VAL_NUM: return hashNumber(AS_NUM(value));
		case VAL_TRUE: return 2;
		case VAL_OBJ: return hashObject(AS_OBJ(value));
		default:
			UNREACHABLE("hash");
			return 0;
	}
#endif
}
// Creates a new list with [numElements] elements (which are left
// uninitialized.)
ObjList* udogNewList(UDogVM* vm, int numElements) {
	// Allocate this before the list object in case it triggers a GC which would
	// free the list.
	Value* elements = NULL;
	if (numElements > 0) {
		elements = ALLOCATE_ARRAY(vm, Value, numElements);
	}

	ObjList* list = ALLOCATE(vm, ObjList);
	initObj(vm, &list->obj, OBJ_LIST, vm->metatable.listClass);
	list->capacity = numElements;
	list->count = numElements;
	list->elements = elements;
	return list;
}

// Grows [list] if needed to ensure it can hold [count] elements.
static void ensureListCapacity(UDogVM* vm, ObjList* list, int count) {
	if (list->capacity >= count) return;

	int capacity = list->capacity * LIST_GROW_FACTOR;
	if (capacity < LIST_MIN_CAPACITY) capacity = LIST_MIN_CAPACITY;

	list->elements = (Value*) udogReallocate(vm, list->elements, list->capacity * sizeof(Value), capacity * sizeof(Value));

	list->capacity = capacity;
}

// Adds [value] to [list], reallocating and growing its storage if needed.
void udogListAdd(UDogVM* vm, ObjList* list, Value value) {
	if (IS_OBJ(value)) UDOG_PIN(vm, AS_OBJ(value));

	ensureListCapacity(vm, list, list->count + 1);

	if (IS_OBJ(value)) UDOG_UNPIN(vm);

	list->elements[list->count++] = value;
}

// Inserts [value] in [list] at [index], shifting down the other elements.
void udogListInsert(UDogVM* vm, ObjList* list, Value value, int index) {
	if (IS_OBJ(value)) UDOG_PIN(vm, AS_OBJ(value));

	ensureListCapacity(vm, list, list->count + 1);

	if (IS_OBJ(value)) UDOG_UNPIN(vm);

	// Shift items down.
	for (int i = list->count; i > index; i--) {
		list->elements[i] = list->elements[i - 1];
	}

	list->elements[index] = value;
	list->count++;
}

// Removes and returns the item at [index] from [list].
Value udogListRemoveAt(UDogVM* vm, ObjList* list, int index) {
	Value removed = list->elements[index];

	if (IS_OBJ(removed)) UDOG_PIN(vm, AS_OBJ(removed));

	// Shift items up.
	for (int i = index; i < list->count - 1; i++) {
		list->elements[i] = list->elements[i + 1];
	}

	// If we have too much excess capacity, shrink it.
	if (list->capacity / LIST_GROW_FACTOR >= list->count) {
		list->elements = (Value*) udogReallocate(vm, list->elements,
										sizeof(Value) * list->capacity,
										sizeof(Value) * (list->capacity / LIST_GROW_FACTOR));
		list->capacity /= LIST_GROW_FACTOR;
	}

	if (IS_OBJ(removed)) UDOG_UNPIN(vm);

	list->count--;
	return removed;
}

void udogListRemoveLast(UDogVM* vm, ObjList* list) {
	// If we have too much excess capacity, shrink it.
	if (list->capacity / LIST_GROW_FACTOR >= list->count) {
		list->elements = (Value*) udogReallocate(vm, list->elements,
										sizeof(Value) * list->capacity,
										sizeof(Value) * (list->capacity / LIST_GROW_FACTOR));
		list->capacity /= LIST_GROW_FACTOR;
	}
	
	list->count--;
}

ObjMap* udogNewMap(UDogVM* vm) {
	ObjMap* map = ALLOCATE(vm, ObjMap);
	initObj(vm, &map->obj, OBJ_MAP, vm->metatable.mapClass);
	map->capacity = 0;
	map->count = 0;
	map->entries = NULL;
	return map;
}

// Inserts [key] and [value] in the array of [entries] with the given
// [capacity].
//
// Returns `true` if this is the first time [key] was added to the map.
static bool addEntry(MapEntry* entries, uint32_t capacity,
                     Value key, Value value) {
	// Figure out where to insert it in the table. Use open addressing and
	// basic linear probing.
	uint32_t index = hashValue(key) % capacity;

	// We don't worry about an infinite loop here because resizeMap() ensures
	// there are open slots in the array.
	while (true) {
		MapEntry* entry = &entries[index];

		// If we found an open slot, the key is not in the table.
		if (IS_UNDEFINED(entry->key)) {
			// Don't stop at a tombstone, though, because the key may be found after
			// it.
			if (IS_FALSE(entry->value)) {
				entry->key = key;
				entry->value = value;
				return true;
			}
		}
		else if (udogValuesEqual(entry->key, key)) {
			// If the key already exists, just replace the value.
			entry->value = value;
			return false;
		}

		// Try the next slot.
		index = (index + 1) % capacity;
	}
}

// Updates [map]'s entry array to [capacity].
static void resizeMap(UDogVM* vm, ObjMap* map, uint32_t capacity) {
	
	// Create the new empty hash table.
	MapEntry* entries = ALLOCATE_ARRAY(vm, MapEntry, capacity);
	for (uint32_t i = 0; i < capacity; i++) {
		entries[i].key = UNDEFINED_VAL;
		entries[i].value = FALSE_VAL;
	}

	// Re-add the existing entries.
	if (map->capacity > 0) {
		for (uint32_t i = 0; i < map->capacity; i++) {
			MapEntry* entry = &map->entries[i];
			if (IS_UNDEFINED(entry->key)) continue;

			addEntry(entries, capacity, entry->key, entry->value);
		}
	}

	// Replace the array.
	udogReallocate(vm, map->entries, 0, 0);
	map->entries = entries;
	map->capacity = capacity;
}

uint32_t udogMapFind(ObjMap* map, Value key) {
	// If there is no entry array (an empty map), we definitely won't find it.
	if (map->capacity == 0) return UINT32_MAX;

	// Figure out where to insert it in the table. Use open addressing and
	// basic linear probing.
	uint32_t index = hashValue(key) % map->capacity;

	// We don't worry about an infinite loop here because ensureMapCapacity()
	// ensures there are empty (i.e. UNDEFINED) spaces in the table.
	while (true) {
		MapEntry* entry = &map->entries[index];

		if (IS_UNDEFINED(entry->key)) {
			// If we found an empty slot, the key is not in the table. If we found a
			// slot that contains a deleted key, we have to keep looking.
			if (IS_FALSE(entry->value)) return UINT32_MAX;
		}
		else if (udogValuesEqual(entry->key, key)) {
			// If the key matches, we found it.
			return index;
		}

		// Try the next slot.
		index = (index + 1) % map->capacity;
	}
}

static MapEntry* findEntry(ObjMap* map, Value key) {
	// If there is no entry array (an empty map), we definitely won't find it.
	if (map->capacity == 0) return NULL;

	// Figure out where to insert it in the table. Use open addressing and
	// basic linear probing.
	uint32_t index = hashValue(key) % map->capacity;

	// We don't worry about an infinite loop here because ensureMapCapacity()
	// ensures there are empty (i.e. UNDEFINED) spaces in the table.
	while (true) {
		MapEntry* entry = &map->entries[index];

		if (IS_UNDEFINED(entry->key)) {
			// If we found an empty slot, the key is not in the table. If we found a
			// slot that contains a deleted key, we have to keep looking.
			if (IS_FALSE(entry->value)) return NULL;
		}
		else if (udogValuesEqual(entry->key, key)) {
			// If the key matches, we found it.
			return entry;
		}

		// Try the next slot.
		index = (index + 1) % map->capacity;
	}
}

Value udogMapGet(ObjMap* map, Value key) {
	MapEntry *entry = findEntry(map, key);
	if (entry != NULL) return entry->value;
  
	return UNDEFINED_VAL;
}

Value udogMapGetInd(ObjMap* map, uint32_t ind) {
	return map->entries[ind].value;
}

void udogMapSet(UDogVM* vm, ObjMap* map, Value key, Value value) {
	// If the map is getting too full, make room first.
	if (map->count + 5 > map->capacity * MAP_LOAD_PERCENT / 100) {
		// Figure out the new hash table size.
		uint32_t capacity = map->capacity * TABLE_GROW_FACTOR;
		if (capacity < TABLE_MIN_CAPACITY) capacity = TABLE_MIN_CAPACITY;

		resizeMap(vm, map, capacity);
	}
	if (addEntry(map->entries, map->capacity, key, value)) {
		// A new key was added.
		map->count++;
	}
}

void udogMapClear(UDogVM* vm, ObjMap* map) {
	udogReallocate(vm, map->entries, 0, 0);
	map->entries = NULL;
	map->capacity = 0;
	map->count = 0;
}

Value udogMapRemoveKey(UDogVM* vm, ObjMap* map, Value key) {
	MapEntry *entry = findEntry(map, key);
	if (entry == NULL) return NULL_VAL;

	// Remove the entry from the map. Set this value to true, which marks it as a
	// deleted slot. When searching for a key, we will stop on empty slots, but
	// continue past deleted slots.
	Value value = entry->value;
	entry->key = UNDEFINED_VAL;
	entry->value = TRUE_VAL;

	if (IS_OBJ(value)) udogPushRoot(vm, AS_OBJ(value));

	map->count--;

	if (map->count == 0) {
		// Removed the last item, so free the array.
		udogMapClear(vm, map);
	}
	else if (map->capacity > TABLE_MIN_CAPACITY &&
	         map->count < map->capacity / TABLE_GROW_FACTOR * MAP_LOAD_PERCENT / 100) {
		uint32_t capacity = map->capacity / TABLE_GROW_FACTOR;
		if (capacity < TABLE_MIN_CAPACITY) capacity = TABLE_MIN_CAPACITY;

		// The map is getting empty, so shrink the entry array back down.
		resizeMap(vm, map, capacity);
	}

	if (IS_OBJ(value)) udogPopRoot(vm);
	return value;
}

ObjModule* udogNewModule(UDogVM* vm) {
	ObjModule* module = ALLOCATE(vm, ObjModule);

	// Modules are never used as first-class objects, so don't need a class.
	initObj(vm, (Obj*)module, OBJ_MODULE, vm->metatable.moduleClass);

	UDOG_PIN(vm, (Obj*)module);

	udogSymbolTableInit(vm, &module->variableNames);
	udogValueBufferInit(vm, &module->variables);
	module->func = NULL;
	module->count = 0;
	module->source = NULL;
	module->name = NULL;
	module->name = NULL;
	module->name = AS_STRING(udogNewString(vm, "module", 6));

	UDOG_UNPIN(vm);
	return module;
}

// Find and return a value from this module
Value udogModuleFind(UDogVM* vm, ObjModule* module, ObjString* key) {
	UNUSED(vm);
	int index = udogSymbolTableFind(&module->variableNames, key->value, key->length);

	if (index < 0)
		return NULL_VAL;
	
	Value val = module->variables.data[index];

	return val;
}

// Set a value onto a variable
Value udogModuleSet(UDogVM* vm, ObjModule* module, ObjString* key, Value val) {
	int index = udogSymbolTableFind(&module->variableNames, key->value, key->length);

	if (index < 0) {
		index = udogSymbolTableAdd(vm, &module->variableNames, key->value, key->length);
		udogValueBufferWrite(vm, &module->variables, val);
		module->count++;
	}
	
	module->variables.data[index] = val;

	return val;
}

Value udogNewRange(UDogVM* vm, double from, double to, bool isInclusive) {
	ObjRange* range = ALLOCATE(vm, ObjRange);
	initObj(vm, &range->obj, OBJ_RANGE, vm->metatable.rangeClass);
	range->from = from;
	range->to = to;
	range->isInclusive = isInclusive;

	return OBJ_VAL(range);
}

// Creates a new string object with a null-terminated buffer large enough to
// hold a string of [length] but does not fill in the bytes.
//
// The caller is expected to fill in the buffer and then calculate the string's
// hash.
static ObjString* allocateString(UDogVM* vm, size_t length) {
  ObjString* string = ALLOCATE_FLEX(vm, ObjString, char, length + 1);
  initObj(vm, &string->obj, OBJ_STRING, vm->metatable.stringClass);
  string->length = (int)length;
  string->value[length] = '\0';

  return string;
}

// Calculates and stores the hash code for [string].
static void hashString(ObjString* string) {
	// FNV-1a hash. See: http://www.isthe.com/chongo/tech/comp/fnv/
	uint32_t hash = 2166136261u;

	// This is O(n) on the length of the string, but we only call this when a new
	// string is created. Since the creation is also O(n) (to copy/initialize all
	// the bytes), we allow this here.
	for (uint32_t i = 0; i < (uint32_t) string->length; i++) {
		hash ^= string->value[i];
		hash *= 16777619;
	}

	string->hash = hash;
}
// Creates a new string object of [length] and copies [text] into it.
//
// [text] may be NULL if [length] is zero.
Value udogNewString(UDogVM* vm, const char* text, size_t length) {
	// Allow NULL if the string is empty since byte buffers don't allocate any
	// characters for a zero-length string.
	ASSERT(length == 0 || text != NULL, "Unexpected NULL string.");

	ObjString* string = allocateString(vm, length); //AS_STRING(udogNewUninitializedString(vm, length));

	// Copy the string (if given one).
	if (length > 0) strncpy(string->value, text, length);

	string->value[length] = '\0';
	
	hashString(string);

	return OBJ_VAL(string);
}

// Creates a new string object with a buffer large enough to hold a string of
// [length] but does no initialization of the buffer.
//
// The caller is expected to fully initialize the buffer after calling.
Value udogNewUninitializedString(UDogVM* vm, size_t length) {
	// Allocate before the string object in case this triggers a GC which would
	// free the string object.
	ObjString* string = ALLOCATE_FLEX(vm, ObjString, char, length + 1);
	initObj(vm, &string->obj, OBJ_STRING, vm->metatable.stringClass);
	string->length = (int)length;
	string->value[length] = '\0';

	return OBJ_VAL(string);
}

// Creates a new string that is the concatenation of [left] and [right].
ObjString* udogStringConcat(UDogVM* vm, const char* left, int leftLength,
                            const char* right, int rightLength) {
	if (leftLength == -1) leftLength = (int)strlen(left);
	if (rightLength == -1) rightLength = (int)strlen(right);

	Value value = udogNewUninitializedString(vm, leftLength + rightLength);
	ObjString* string = AS_STRING(value);
	memcpy(string->value, left, leftLength);
	memcpy(string->value + leftLength, right, rightLength);
	string->value[leftLength + rightLength] = '\0';

	return string;
}

Value udogStringFromCodePoint(UDogVM* vm, int value) {
  int length = udogUtf8NumBytes(value);
  ASSERT(length != 0, "Value out of range.");

  ObjString* string = allocateString(vm, length);

  udogUtf8Encode(value, (uint8_t*)string->value);
  hashString(string);

  return OBJ_VAL(string);
}

// Format a string [str] into an udog String
ObjString* udogStringFormat(UDogVM* vm, const char* str, ...) {
	va_list arglist;	
    va_start(arglist, str);
	
	char a[1024];
	int len = vsnprintf (a, 1023, str, arglist);
	va_end(arglist);

	Value value = udogNewString(vm, a, len+1);
	ObjString* string = AS_STRING(value);
	
	hashString(string);
    
    return string;
/*	
	va_list argList;
	
	// Calculate the length of the result string. Do this up front so we can
	// create the final string with a single allocation.
	va_start(argList, format);
	size_t totalLength = 0;
	for (const char* c = format; *c != '\0'; c++) {
		switch (*c) {
			case '$':
				totalLength += strlen(va_arg(argList, const char*));
				break;
	
			case '@':
				totalLength += AS_STRING(va_arg(argList, Value))->length;
				break;
	
			default:
				// Any other character is interpreted literally.
				totalLength++;
		}
	}
	va_end(argList);
	
	// Concatenate the string.
	ObjString* result = allocateString(vm, totalLength);
	
	va_start(argList, format);
	char* start = result->value;
	for (const char* c = format; *c != '\0'; c++) {
		switch (*c) {
			case '$': {
				const char* string = va_arg(argList, const char*);
				size_t length = strlen(string);
				memcpy(start, string, length);
				start += length;
				break;
			}
	
			case '@': {
				ObjString* string = AS_STRING(va_arg(argList, Value));
				memcpy(start, string->value, string->length);
				start += string->length;
				break;
			}
	
			default:
				// Any other character is interpreted literally.
				*start++ = *c;
		}
	}
	va_end(argList);
	
	hashString(result);
	
	return OBJ_VAL(result);
*/
}

Value udogStringCodePointAt(UDogVM* vm, ObjString* string, int index) {
  ASSERT(index < string->length, "Index out of bounds.");

  char first = string->value[index];

  // The first byte's high bits tell us how many bytes are in the UTF-8
  // sequence. If the byte starts with 10xxxxx, it's the middle of a UTF-8
  // sequence, so return an empty string.
  int numBytes;
  if      ((first & 0xc0) == 0x80) numBytes = 0;
  else if ((first & 0xf8) == 0xf0) numBytes = 4;
  else if ((first & 0xf0) == 0xe0) numBytes = 3;
  else if ((first & 0xe0) == 0xc0) numBytes = 2;
  else numBytes = 1;

  return udogNewString(vm, string->value + index, numBytes);
}

// Uses the Boyer-Moore-Horspool string matching algorithm.
uint32_t udogStringFind(UDogVM* vm, ObjString* haystack, ObjString* needle) {
	UNUSED(vm);
	// Corner case, an empty needle is always found.
	if (needle->length == 0) return 0;

	// If the needle is longer than the haystack it won't be found.
	if (needle->length > haystack->length) return UINT32_MAX;

	// Pre-calculate the shift table. For each character (8-bit value), we
	// determine how far the search window can be advanced if that character is
	// the last character in the haystack where we are searching for the needle
	// and the needle doesn't match there.
	uint32_t shift[UINT8_MAX];
	uint32_t needleEnd = needle->length - 1;

	// By default, we assume the character is not the needle at all. In that case
	// case, if a match fails on that character, we can advance one whole needle
	// width since.
	for (uint32_t index = 0; index < UINT8_MAX; index++) {
		shift[index] = needle->length;
	}

	// Then, for every character in the needle, determine how far it is from the
	// end. If a match fails on that character, we can advance the window such
	// that it the last character in it lines up with the last place we could
	// find it in the needle.
	for (uint32_t index = 0; index < needleEnd; index++) {
		char c = needle->value[index];
		shift[(uint8_t)c] = needleEnd - index;
	}

	// Slide the needle across the haystack, looking for the first match or
	// stopping if the needle goes off the end.
	char lastChar = needle->value[needleEnd];
	uint32_t range = haystack->length - needle->length;

	for (uint32_t index = 0; index <= range; ) {
		// Compare the last character in the haystack's window to the last character
		// in the needle. If it matches, see if the whole needle matches.
		char c = haystack->value[index + needleEnd];
		if (lastChar == c &&
		        memcmp(haystack->value + index, needle->value, needleEnd) == 0) {
			// Found a match.
			return index;
		}

		// Otherwise, slide the needle forward.
		index += shift[(uint8_t)c];
	}

	// Not found.
	return UINT32_MAX;
}

// Creates a new open upvalue pointing to [value] on the stack.
Upvalue* udogNewUpvalue(UDogVM* vm, Value* value) {
	Upvalue* upvalue = ALLOCATE(vm, Upvalue);

	// Upvalues are never used as first-class objects, so don't need a class.
	initObj(vm, &upvalue->obj, OBJ_UPVALUE, NULL);

	upvalue->value = value;
	upvalue->closed = NULL_VAL;
	upvalue->next = NULL;
	return upvalue;
}

// Sets the mark flag on [obj]. Returns true if it was already set so that we
// can avoid recursing into already-processed objects. That ensures we don't
// crash on an object cycle.
static bool setMarkedFlag(UDogVM* vm, Obj* obj) {
	UNUSED(vm);
	if (obj->gcflag & FLAG_MARKED) return true;
	obj->gcflag = (GCFlag) (obj->gcflag | FLAG_MARKED);
	return false;
}

static void markClass(UDogVM* vm, ObjClass* classObj);
static void markFn(UDogVM* vm, ObjFn* fn);
static void markList(UDogVM* vm, ObjList* list);
static void markString(UDogVM* vm, ObjString* string) ;
static void markClosure(UDogVM* vm, ObjClosure* closure);
static void markFiber(UDogVM* vm, ObjFiber* fiber);
static void markInstance(UDogVM* vm, ObjInstance* instance);
static void markUpvalue(UDogVM* vm, Upvalue* upvalue);

void markTable(UDogVM* vm, ObjTable* value);
void markTableElement(UDogVM* vm, HashValue* value);

// Mark [value] as reachable and still in use. This should only be called
// during the sweep phase of a garbage collection.
void markTable(UDogVM* vm, ObjTable* list) {
	if (setMarkedFlag(vm, &list->obj)) return;
	
	// Mark the elements.
	HashValue** elements = list->hashmap;
	HashValue* ptr = NULL;
	for (int i = 0; i < list->capacity; i++) {
		if (elements[i] != NULL) {
			ptr = elements[i];
			
			while (ptr != NULL) {
				markTableElement(vm, ptr);
				ptr = ptr->next;
			}
		}
	}
}

// Mark [value] as reachable and still in use. This should only be called
// during the sweep phase of a garbage collection.
void markTableElement(UDogVM* vm, HashValue* value) {
	if (setMarkedFlag(vm, &value->obj)) return;
	
	udogMarkValue(vm, value->key);
	udogMarkValue(vm, value->val);
}

static void markClass(UDogVM* vm, ObjClass* classObj) {
	if (setMarkedFlag(vm, &classObj->obj)) return;

	// The metaclass.
	if (classObj->obj.classObj != NULL) markClass(vm, classObj->obj.classObj);

	// The superclass.
	if (classObj->superclasses != NULL) markList(vm, classObj->superclasses);

	// Method function objects.
	for (int i = 0; i < classObj->methods.count; i++) {
		if (classObj->methods.data[i].type == METHOD_BLOCK) {
			udogMarkObj(vm, classObj->methods.data[i].fn.obj);
		}
	}


	if (classObj->name != NULL) markString(vm, classObj->name);
	
	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjClass);
	vm->garbageCollector.bytesAllocated += classObj->methods.capacity * sizeof(Method);
}

static void markFn(UDogVM* vm, ObjFn* fn) {
	if (setMarkedFlag(vm, &fn->obj)) return;

	// Mark the constants.
	for (int i = 0; i < fn->numConstants; i++) {
		udogMarkValue(vm, fn->constants[i]);
	}

	udogMarkObj(vm, (Obj*)fn->debug->sourcePath);

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjFn);
	vm->garbageCollector.bytesAllocated += sizeof(uint8_t) * fn->bytecodeLength;
	vm->garbageCollector.bytesAllocated += sizeof(Value) * fn->numConstants;

	// The debug line number buffer.
	vm->garbageCollector.bytesAllocated += sizeof(int) * fn->bytecodeLength;

	// The function name
	vm->garbageCollector.bytesAllocated += strlen(fn->debug->name);
}

static void markList(UDogVM* vm, ObjList* list) {
	if (setMarkedFlag(vm, &list->obj)) return;

	// Mark the elements.
	Value* elements = list->elements;
	for (int i = 0; i < list->count; i++) {
		udogMarkValue(vm, elements[i]);
	}

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjList);
	if (list->elements != NULL) {
		vm->garbageCollector.bytesAllocated += sizeof(Value) * list->capacity;
	}
}

static void markString(UDogVM* vm, ObjString* string) {
	if (setMarkedFlag(vm, &string->obj)) return;

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjString);

	vm->garbageCollector.bytesAllocated += string->length;
}

static void markClosure(UDogVM* vm, ObjClosure* closure) {
	if (setMarkedFlag(vm, &closure->obj)) return;

	// Mark the function.
	markFn(vm, closure->fn);

	// Mark the upvalues.
	for (int i = 0; i < closure->fn->numUpvalues; i++) {
		Upvalue* upvalue = closure->upvalues[i];
		markUpvalue(vm, upvalue);
	}

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjClosure);
	vm->garbageCollector.bytesAllocated += sizeof(Upvalue*) * closure->fn->numUpvalues;
}

static void markFiber(UDogVM* vm, ObjFiber* fiber) {
	if (setMarkedFlag(vm, &fiber->obj)) return;

	// Stack functions.
	if (fiber->frames != NULL)
		for (int i = 0; i < fiber->numFrames; i++) {
			udogMarkObj(vm, fiber->frames[i].fn);
		}
	
	if (fiber->stack != NULL)
		// Stack variables.
		for (Value* slot = fiber->stack; slot < fiber->stacktop; slot++) {
			udogMarkValue(vm, *slot);
		}

	// Open upvalues.
	Upvalue* upvalue = fiber->openUpvalues;
	while (upvalue != NULL) {
		markUpvalue(vm, upvalue);
		upvalue = upvalue->next;
	}

	// The caller.
	if (fiber->caller != NULL) markFiber(vm, fiber->caller);

	if (fiber->error != NULL) markInstance(vm, fiber->error);

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjFiber);
}

static void markInstance(UDogVM* vm, ObjInstance* instance) {
	if (setMarkedFlag(vm, &instance->obj)) return;

	markClass(vm, instance->obj.classObj);

	// Mark the fields.
	for (int i = 0; i < instance->obj.classObj->numFields; i++) {
		udogMarkValue(vm, instance->fields[i]);
	}

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjInstance);
	vm->garbageCollector.bytesAllocated += sizeof(Value) * instance->obj.classObj->numFields;
}

static void markUpvalue(UDogVM* vm, Upvalue* upvalue) {
	// This can happen if a GC is triggered in the middle of initializing the
	// closure.
	if (upvalue == NULL) return;

	if (setMarkedFlag(vm, &upvalue->obj)) return;

	// Mark the closed-over object (in case it is closed).
	udogMarkValue(vm, upvalue->closed);

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(Upvalue);
}

static void markMethod(UDogVM* vm, ObjMethod* method) {
	if (setMarkedFlag(vm, &method->obj)) return;
	
	udogMarkValue(vm, method->caller);
	
	if (method->name != NULL) markString(vm, method->name);
}

static void markMap(UDogVM* vm, ObjMap* map) {
	if (setMarkedFlag(vm, &map->obj)) return;

	// Mark the entries.
	for (int i = 0; i < (int) map->capacity; i++) {
		MapEntry* entry = &map->entries[i];
		if (IS_UNDEFINED(entry->key)) continue;

		udogMarkValue(vm, entry->key);
		udogMarkValue(vm, entry->value);
	}

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjMap);
	vm->garbageCollector.bytesAllocated += sizeof(MapEntry) * map->capacity;
}
static void markModule(UDogVM* vm, ObjModule* module) {
	if (setMarkedFlag(vm, &module->obj)) return;

	// Top-level variables.
	for (int i = 0; i < module->variables.count; i++) {
		udogMarkValue(vm, module->variables.data[i]);
	}
	
	if (module->func != NULL) markFn(vm, module->func);
		
	if (module->name != NULL) markString(vm, module->name);
	
	if (module->source != NULL) markString(vm, module->source);

	// Keep track of how much memory is still in use.
	vm->garbageCollector.bytesAllocated += sizeof(ObjModule);
}

// Mark [value] as reachable and still in use. This should only be called
// during the sweep phase of a garbage collection.
void udogMarkValue(UDogVM* vm, Value value) {
	if (!IS_OBJ(value)) return;
	udogMarkObj(vm, AS_OBJ(value));
}

// Mark [obj] as reachable and still in use. This should only be called
// during the sweep phase of a garbage collection.
void udogMarkObj(UDogVM* vm, Obj* obj) {
#if UDOG_DEBUG_TRACE_MEMORY
	static int indent = 0;
	indent++;
	for (int i = 0; i < indent; i++) printf("  ");
	printf("mark ");
	udogPrintValue(OBJ_VAL(obj));
	printf(" @ %p\n", obj);
#endif
	
	switch (obj->type) {
		case OBJ_CLASS: markClass(vm, (ObjClass*) obj); break;
		case OBJ_FN: markFn(vm, (ObjFn*) obj); break;
		case OBJ_LIST: markList(vm, (ObjList*) obj); break;
		case OBJ_STRING: markString(vm, (ObjString*) obj); break;
		case OBJ_CLOSURE: markClosure(vm, (ObjClosure*) obj); break;
		case OBJ_FIBER: markFiber(vm, (ObjFiber*) obj); break;
		case OBJ_INSTANCE: markInstance(vm, (ObjInstance*) obj); break;
		case OBJ_UPVALUE: markUpvalue(vm, (Upvalue*) obj); break;
		case OBJ_RANGE: setMarkedFlag(vm, obj); break;
		case OBJ_TABLE: markTable(vm, (ObjTable*) obj); break;
		case OBJ_TABLE_ELEM: markTableElement(vm, (HashValue*) obj); break;
		case OBJ_MAP: markMap(vm, (ObjMap*) obj); break;
		case OBJ_MODULE: markModule(vm, (ObjModule*) obj); break;
		case OBJ_METHOD: markMethod(vm, (ObjMethod*) obj); break;
		default: break;
	}	

#if UDOG_DEBUG_TRACE_MEMORY
	indent--;
#endif
}

// Releases all memory owned by [obj], including [obj] itself.
void udogFreeObj(UDogVM* vm, Obj* obj) {
#if UDOG_DEBUG_TRACE_MEMORY || UDOG_DEBUG_TRACE_FREE
	printf("free ");
	udogPrintValue(OBJ_VAL(obj));
	printf(" @ %p\n", obj);
#endif

	switch (obj->type) {
		case OBJ_CLASS:
			udogMethodBufferClear(vm, &((ObjClass*)obj)->methods);
			break;

		case OBJ_FN: 
		{
			ObjFn* fn = (ObjFn*)obj;
			udogReallocate(vm, fn->constants, 0, 0);
			udogReallocate(vm, fn->bytecode, 0, 0);
			udogReallocate(vm, fn->debug->name, 0, 0);
			udogReallocate(vm, fn->debug->sourceLines, 0, 0);
			
			udogSymbolTableClear(vm, &(fn->debug->locals));
			udogSymbolTableClear(vm, &(fn->debug->lines));
			
			udogReallocate(vm, fn->debug, 0, 0);
			break;
		}

		case OBJ_LIST:
			udogReallocate(vm, ((ObjList*)obj)->elements, 0, 0);
			break;
			
		case OBJ_MAP:
			udogReallocate(vm, ((ObjMap*)obj)->entries, 0, 0);
			break;

		case OBJ_TABLE:
			udogReallocate(vm, ((ObjTable*)obj)->hashmap, 0, 0);
			break;
		case OBJ_FIBER:
			udogReallocate(vm, ((ObjFiber*)obj)->stack, 0, 0);
			udogReallocate(vm, ((ObjFiber*)obj)->frames, 0, 0);
			break;
		case OBJ_MODULE:
			udogSymbolTableClear(vm, &((ObjModule*)obj)->variableNames);
			udogValueBufferClear(vm, &((ObjModule*)obj)->variables);
			break;
		case OBJ_INSTANCE: {
			// call the foreign destructor
			ObjClass* cls = udogGetClass(vm, OBJ_VAL(obj));
			if (cls->destructor != NULL) {
				// call the destructor
				cls->destructor(obj+1);
			}
			break;
		}
		case OBJ_STRING:
		case OBJ_TABLE_ELEM:
		case OBJ_CLOSURE:
		case OBJ_RANGE:
		case OBJ_UPVALUE:
		case OBJ_METHOD:
		default:
		  break;
	}

	udogReallocate(vm, obj, 0, 0);
}

// Returns the class of [value].
//
// Unlike udogGetClassInline in udog_vm.h, this is not inlined. Inlining helps
// performance (significantly) in some cases, but degrades it in others. The
// ones used by the implementation were chosen to give the best results in the
// benchmarks.
ObjClass* udogGetClass(UDogVM* vm, Value value) {
	return udogGetClassInline(vm, value);
}

static void printList(ObjList* list) {
	printf("[");
	for (int i = 0; i < list->count; i++) {
		if (i > 0) printf(", ");
		udogPrintValue(list->elements[i]);
	}
	printf("]");
}

static void printObject(Obj* obj) {
	if (obj->gcflag != GC_GRAY) return;
	
	switch (obj->type) {
		case OBJ_CLASS: printf("[class %p]", obj); break;
		case OBJ_CLOSURE: printf("[closure %p]", obj); break;
		case OBJ_FIBER: printf("[fiber %p]", obj); break;
		case OBJ_FN: printf("[fn %p]", obj); break;
		case OBJ_INSTANCE: printf("[instance %p]", obj); break;
		case OBJ_LIST: printList((ObjList*)obj); break;
		case OBJ_STRING: printf("%s", ((ObjString*)obj)->value); break;
		case OBJ_UPVALUE: printf("[upvalue %p]", obj); break;
		case OBJ_TABLE: printf("[table %p]", obj); break;
		case OBJ_TABLE_ELEM: printf("[table element %p]", obj); break;
		case OBJ_MAP: printf("[map %p]", obj); break;
		case OBJ_MODULE: printf("[module %p]", obj); break;
		case OBJ_RANGE: printf("[fn %p]", obj); break;
		case OBJ_METHOD: printf("[method %p]", obj); break;
		default: printf("[unknown object]"); break;
	}
}

void udogPrintValue(Value value) {
#if UDOG_NAN_TAGGING
	if (IS_NUM(value)) {
		printf("%.14g", AS_NUM(value));
	}
	else if (IS_OBJ(value)) {
		printObject(AS_OBJ(value));
	}
	else {
		switch (GET_TAG(value)) {
			case TAG_FALSE: printf("false"); break;
			case TAG_NAN: printf("NaN"); break;
			case TAG_NULL: printf("null"); break;
			case TAG_TRUE: printf("true"); break;
			case TAG_UNDEFINED: UNREACHABLE("print");
			default: UNREACHABLE("print");
		}
	}
#else
	switch (value.type) {
		case VAL_FALSE: printf("false"); break;
		case VAL_NULL: printf("null"); break;
		case VAL_NUM: printf("%.14g", AS_NUM(value)); break;
		case VAL_TRUE: printf("true"); break;
		case VAL_OBJ: printObject(AS_OBJ(value));
		default: ;
	}
	printf("\n");
#endif
}

bool compareFloat(udog_number a, udog_number b) {
	return fabs(a - b) < EPSILON;
}

bool udogValuesEqual(Value a, Value b) {
	if (udogValuesSame(a, b)) return true;

	// If we get here, it's only possible for two heap-allocated immutable objects
	// to be equal.
	if (!IS_OBJ(a) || !IS_OBJ(b)) return false;

	Obj* aObj = AS_OBJ(a);
	Obj* bObj = AS_OBJ(b);

	// Must be the same type.
	if (aObj->type != bObj->type) return false;

	switch (aObj->type) {
		case OBJ_RANGE: {
			ObjRange* aRange = (ObjRange*)aObj;
			ObjRange* bRange = (ObjRange*)bObj;
			return aRange->from == bRange->from &&
			       aRange->to == bRange->to &&
			       aRange->isInclusive == bRange->isInclusive;
		}

		case OBJ_STRING: {
			ObjString* aString = (ObjString*)aObj;
			ObjString* bString = (ObjString*)bObj;
			return aString->length == bString->length &&
					aString->hash == bString->hash &&
			       memcmp(aString->value, bString->value, aString->length) == 0;
		}

		default:
			// All other types are only equal if they are same, which they aren't if
			// we get here.
			return false;
	}
}

Value udogGetHostObject(UDogVM* vm, UDogValue* key) {
	//int ind = udogMapFind(vm->hostObjects.hostObjects, NUM_VAL(key->value));
	//return vm->hostObjects.hostObjects->entries[ind].value;
	return udogTableFind(vm, vm->hostObjects.hostObjects, NUM_VAL(key->value));
}

void udogSetHostObject(UDogVM* vm, Value val, UDogValue* key) {
	udogTableAdd(vm, vm->hostObjects.hostObjects, NUM_VAL(key->value), val);
}

UDogValue* udogCreateHostObject(UDogVM* vm, Value val) {
	if (IS_OBJ(val)) UDOG_PIN(vm, AS_OBJ(val));
	UDogValue* ret = ALLOCATE(vm, UDogValue);

	if (vm->hostObjects.freeNums->count > 0)
		ret->value = udogListRemoveAt(vm, vm->hostObjects.freeNums, vm->hostObjects.freeNums->count-1);
	else {
		ret->value = vm->hostObjects.max;
		vm->hostObjects.max++;
	}	

	udogTableAdd(vm, vm->hostObjects.hostObjects, NUM_VAL(ret->value), val);
	if (IS_OBJ(val)) UDOG_UNPIN(vm);
	return ret;
}

void udogRemoveHostObject(UDogVM* vm, UDogValue* key) {
	Value v = NUM_VAL(key->value);
	if (IS_OBJ(v)) UDOG_PIN(vm, AS_OBJ(v));
	udogTableRemove(vm, vm->hostObjects.hostObjects, v);
	udogListAdd(vm, vm->hostObjects.freeNums, NUM_VAL(key->value));
	if (IS_OBJ(v)) UDOG_UNPIN(vm);
	
	udogReallocate(vm, key, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTIONS: TABLE	
///////////////////////////////////////////////////////////////////////////////////

// Creates a new list with [numElements] elements (which are left
// uninitialized.)
ObjTable* udogNewTable(UDogVM* vm, int numElements) {
	if (numElements < TABLE_MIN_CAPACITY)
		numElements = TABLE_MIN_CAPACITY;

	// Allocate this before the list object in case it triggers a GC which would
	// free the list.
	HashValue** elements = NULL;
	if (numElements > 0) {
		elements = ALLOCATE_ARRAY(vm, HashValue*, numElements);
		
		for(int i=0; i<numElements; i++)
			elements[i] = NULL;
	}

	ObjTable* list = ALLOCATE(vm, ObjTable);
	initObj(vm, &list->obj, OBJ_TABLE, vm->metatable.tableClass);
	list->capacity = numElements;
	
	list->count = 0;
	list->hashmap = elements;
	
	return list;
}

static void checkNullTable(UDogVM* vm, ObjTable* list) {
	if (list->hashmap != NULL) return;
	
	int numElements = TABLE_MIN_CAPACITY;
	HashValue** elements = NULL;
	if (numElements > 0) {
		elements = ALLOCATE_ARRAY(vm, HashValue*, numElements);
		
		for(int i=0; i<numElements; i++)
			elements[i] = NULL;
	}

	list->capacity = numElements;
	list->count = 0;
	list->hashmap = elements;
}

void udogTablePrint(UDogVM* vm, ObjTable* list) {
	checkNullTable(vm, list);
	printf("Table: \n");
	for(int i=0; i<list->capacity; i++) {
		// check if element exists first
		HashValue* ptr = list->hashmap[i];
		while (ptr != NULL) {
			printf("Key: ");
			udogPrintValue(ptr->key);
			printf(" Value: ");
			udogPrintValue(ptr->val);
			printf(" at hash: %d\n", i);
			
			ptr = ptr->next;
		}
	}
}

// Grows [list] if needed to ensure it can hold [count] elements.
static bool ensureTableCapacity(UDogVM* vm, ObjTable* list) {
	HashValue** elements = NULL;
	int newSize = 0;
	if (list->count > list->capacity) {
		newSize = list->capacity * TABLE_GROW_FACTOR;				
	}
	else if (list->capacity > TABLE_MIN_CAPACITY && list->count < list->capacity / 2 - 1) {
		newSize = list->capacity / TABLE_GROW_FACTOR;		
	}
	else
		return false;
	
	elements = ALLOCATE_ARRAY(vm, HashValue*, newSize);
	for(int i=0; i<newSize; i++)
		elements[i] = NULL;
		
	for(int i=0; i<list->capacity; i++) {
		// check if element exists first
		HashValue* ptr = list->hashmap[i];
		HashValue* temp;
		while (ptr != NULL) {
			udog_uinteger hash = hashValue(ptr->key);
			hash = hash % newSize;
			
			temp = ptr->next;
			
			// set the element in the linked list
			ptr->next = elements[hash];
			elements[hash] = ptr;
			ptr = temp;
		}
	}
	
	udogReallocate(vm, list->hashmap, 0, 0);
	list->hashmap = elements;
	list->capacity = newSize;
	return true;
}

// Adds [value] to [list], reallocating and growing its storage if needed.
void udogTableAdd(UDogVM* vm, ObjTable* list, Value key, Value value) {
	if (IS_OBJ(value)) UDOG_PIN(vm, AS_OBJ(value));
	if (IS_OBJ(key)) UDOG_PIN(vm, AS_OBJ(key));
	
	checkNullTable(vm, list);
	
	udog_uinteger hash = hashValue(key);
	hash = hash % list->capacity;
	
	// check if element exists first
	HashValue* ptr = list->hashmap[hash];
	while (ptr != NULL && !udogValuesEqual(ptr->key, key)) ptr = ptr->next;
	
	if (ptr == NULL) {
		if (ensureTableCapacity(vm, list)) {
			hash = hashValue(key);
			hash = hash % list->capacity;
		}
		
		// create a new element
		HashValue* element = ALLOCATE(vm, HashValue);
		initObj(vm, &element->obj, OBJ_TABLE_ELEM, NULL);
		
		// set the values of the element
		element->key = key;
		element->val = value;
		
		// set the element in the linked list
		element->next = list->hashmap[hash];
		list->hashmap[hash] = element;
		list->count++;
	}
	else {
		ptr->val = value;
	}
	
	if (IS_OBJ(key)) UDOG_UNPIN(vm);
	if (IS_OBJ(value)) UDOG_UNPIN(vm);
}

// Find [key] in [list], shifting down the other elements.
Value udogTableFind(UDogVM* vm, ObjTable* list, Value key) {
	if (IS_OBJ(key)) UDOG_PIN(vm, AS_OBJ(key));
	checkNullTable(vm, list);
	if (IS_OBJ(key)) UDOG_UNPIN(vm);
	
	udog_uinteger hash = hashValue(key);
	hash = hash % list->capacity;

	HashValue* ptr = list->hashmap[hash];
	
	while (ptr != NULL && !udogValuesEqual(ptr->key, key)) ptr = ptr->next;
	
	if (ptr == NULL)
		return NULL_VAL;
	else
		return ptr->val;
}

// Removes and returns the item at [index] from [list].
Value udogTableRemove(UDogVM* vm, ObjTable* list, Value key) {
	if (IS_OBJ(key)) UDOG_PIN(vm, AS_OBJ(key));
	checkNullTable(vm, list);
	if (IS_OBJ(key)) UDOG_UNPIN(vm);
	
	udog_uinteger hash = hashValue(key);
	hash = hash % list->capacity;
	
	HashValue* ptr = list->hashmap[hash];
	HashValue* prev = NULL;
	
	while (ptr != NULL && !udogValuesEqual(ptr->key, key)) {
		prev = ptr;
		ptr = ptr->next;
	}
	
	if (ptr == NULL)
		return NULL_VAL;
	else {
		Value ret = ptr->val;
		
		if (prev)
			prev->next = ptr->next;
		else
			list->hashmap[hash] = ptr->next;
		
		return ret;
	}
}

HashValue* udogGetTableIndex(ObjTable* list, int ind) {
	HashValue* ptr;
	int i = 0;
	int len = 0;
	while (len < list->capacity) {
		ptr = list->hashmap[len];
		
		if (ptr == NULL) {
			len++;
			continue;
		}
		
		while (ptr != NULL && i != ind) {
			ptr = ptr->next;
			i++;
		}
		
		if (i == ind && ptr != NULL)
			return ptr;
		len++;
	}
	return NULL;
	
	/*
	HashValue* ptr;
	int i = 0;
	int len = 0;
	while (len < list->capacity) {
		ptr = list->hashmap[len];
		
		if (ptr == NULL) {
			len++;
			continue;
		}
		
		while (ptr != NULL && i != ind) {
			ptr = ptr->next;
			i++;
		}
		
		if (i == ind)
			return ptr;
		len++;
	}
	return NULL;*/
}