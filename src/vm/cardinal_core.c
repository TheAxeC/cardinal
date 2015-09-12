#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cardinal_config.h"
#include "cardinal_core.h"
#include "cardinal_value.h"
#include "cardinal_debug.h"

#if CARDINAL_USE_MEMORY
	#include "cardinal_datacenter.h"
#endif

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

static bool validateFn(CardinalVM* vm, Value* args, int index, const char* argName);
static bool validateNum(CardinalVM* vm, Value* args, int index, const char* argName);
static bool validateIntValue(CardinalVM* vm, Value* args, double value, const char* argName);
static bool validateInt(CardinalVM* vm, Value* args, int index, const char* argName);
static int validateIndexValue(CardinalVM* vm, Value* args, int count, double value, const char* argName);
static int validateIndex(CardinalVM* vm, Value* args, int count, int argIndex, const char* argName);
static bool validateString(CardinalVM* vm, Value* args, int index, const char* argName);
static bool validateException(CardinalVM* vm, Value* args, int index, const char* argName);

static ObjClass* defineSingleClass(CardinalVM* vm, const char* name);


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

///////////////////////////////////////////////////////////////////////////////////
//// SRC CODE
///////////////////////////////////////////////////////////////////////////////////

static const char* libSource =
"class System {}\n"
"class Fiber {}\n"
"class Num {}\n"
"class Null {}\n"
"class Fn {}\n"
"class Bool {}\n"
"class Method {}\n"
"class Module {}\n"
"\n"
"class Exception {\n"
"	fields {\n"
"		_message\n"
"		_stack\n"
"	}\n"
"	\n"
"	construct new(mes) {\n"
"		_message = mes\n"
"	}\n"
"	\n"
"	setMessage(a) {\n"
"		_message = a\n"
"	}\n"
"	\n"
"	getMessage { _message }\n"
"	\n"
"	toString { _message }\n"
"	\n"
"	getStackTrace { _stack }\n"
"}\n"
"\n"
"class Sequence {\n"
"	all(f) {\n"
"		var result = true\n"
"		for (element in this) {\n"
"			result = f.call(element)\n"
"			if (!result) return result\n"
"		}\n"
"		return result\n"
"	}\n"
"\n"
"	any(f) {\n"
"		var result = false\n"
"		for (element in this) {\n"
"			result = f.call(element)\n"
"			if (result) return result\n"
"		}\n"
"		return result\n"
"	}\n"
"	\n"
"	contains(element) {\n"
"		for (item in this) {\n"
"			if (element == item) return true\n"
"		}\n"
"		return false\n"
"	}\n"
"\n"
"	count {\n"
"		var result = 0\n"
"		for (element in this) {\n"
"			result = result + 1\n"
"		}\n"
"		return result\n"
"	}\n"
"\n"
"	count(f) {\n"
"		var result = 0\n"
"		for (element in this) {\n"
"			if (f.call(element)) result = result + 1\n"
"		}\n"
"		return result\n"
"	}\n"
"\n"
"	each(f) {\n"
"		for (element in this) {\n"
"			f.call(element)\n"
"		}\n"
"	}\n"
"	\n"
"	map(f) {\n"
"		var result = List.new\n"
"		for (element in this) {\n"
"			result.add(f.call(element))\n"
"		}\n"
"		return result\n"
"	}\n"
"\n"
"	where(f) {\n"
"		var result = List.new\n"
"		for (element in this) {\n"
"			if (f.call(element)) result.add(element)\n"
"		}\n"
"		return result\n"
"	}\n"
"\n"
"	//map(transformation) { MapSequence.new(this, transformation) }\n"
"\n"
"	//where(predicate) { WhereSequence.new(this, predicate) }\n"
"  \n"
"	reduce(acc, f) {\n"
"		for (element in this) {\n"
"			acc = f.call(acc, element)\n"
"		}\n"
"		return acc\n"
"	}\n"
"\n"
"	reduce(f) {\n"
"		var iter = iterate(null)\n"
"		if (!iter) Fiber.abort(\"Can't reduce an empty sequence.\")\n"
"\n"
"		// Seed with the first element.\n"
"		var result = iteratorValue(iter)\n"
"		while (iter = iterate(iter)) {\n"
"			result = f.call(result, iteratorValue(iter))\n"
"		}\n"
"\n"
"		return result\n"
"	}\n"
"\n"
"	join { join(\"\") }\n"
"\n"
"	join(sep) {\n"
"		var first = true\n"
"		var result = \"\"\n"
"\n"
"		for (element in this) {\n"
"			if (!first) result = result + sep\n"
"			first = false\n"
"			result = result + element.toString\n"
"		}\n"
"\n"
"		return result\n"
"	}\n"
"  \n"
"	toList {\n"
"		var result = List.new\n"
"		for (element in this) {\n"
"			result.add(element)\n"
"		}\n"
"		return result\n"
"	}\n"
"}\n"
"\n"
"\n"
"class MapSequence is Sequence {\n"
"	fields {\n"
"		_sequence\n"
"		_fn\n"
"	}\n"
"	construct new(sequence, fn) {\n"
"		_sequence = sequence\n"
"		_fn = fn\n"
"	}\n"
"\n"
"	iterate(iterator) { _sequence.iterate(iterator) }\n"
"	iteratorValue(iterator) { _fn.call(_sequence.iteratorValue(iterator)) }\n"
"}\n"
"\n"
"class WhereSequence is Sequence {\n"
"	fields {\n"
"		_sequence\n"
"		_fn\n"
"	}\n"
"	\n"
"	construct new(sequence, fn) {\n"
"		_sequence = sequence\n"
"		_fn = fn\n"
"	}\n"
"\n"
"	iterate(iterator) {\n"
"		while (iterator = _sequence.iterate(iterator)) {\n"
"		  if (_fn.call(_sequence.iteratorValue(iterator))) break\n"
"		}\n"
"		return iterator\n"
"	}\n"
"\n"
"	iteratorValue(iterator) { _sequence.iteratorValue(iterator) }\n"
"}\n"
"\n"
"class String is Sequence {  \n"
"	bytes { StringByteSequence.new(this) }\n"
"}\n"
"\n"
"class StringByteSequence is Sequence {\n"
"	fields {\n"
"		_string\n"
"	}\n"
"	construct new(string) {\n"
"		_string = string\n"
"	}\n"
"\n"
"	[index] { _string.byteAt(index) }\n"
"	iterate(iterator) { _string.iterateByte_(iterator) }\n"
"	iteratorValue(iterator) { _string.byteAt(iterator) }\n"
"}\n"
"\n"
"class List is Sequence {\n"
"	addAll(other) {\n"
"		for (element in other) {\n"
"			add(element)\n"
"		}\n"
"		return other\n"
"	}\n"
"	\n"
"	toString { \"[\" + join(\", \") + \"]\" }\n"
"	\n"
"	+(other) {\n"
"		var result = this[0..-1]\n"
"		for (element in other) {\n"
"			result.add(element)\n"
"		}\n"
"		return result\n"
"	}\n"
"	\n"
"	contains(element) {\n"
"		for (item in this) {\n"
"			if (element == item) {\n"
"				return true\n"
"			}\n"
"		}\n"
"			   return false\n"
"	}\n"
"}\n"
"\n"
"class Map {\n"
"	keys { MapKeySequence.new(this) }\n"
"	values { MapValueSequence.new(this) }\n"
"\n"
"	toString {\n"
"		var first = true\n"
"		var result = \"{\"\n"
"\n"
"		for (key in keys) {\n"
"			if (!first) result = result + \", \"\n"
"			first = false\n"
"			result = result + key.toString + \": \" + this[key].toString\n"
"		}\n"
"\n"
"		return result + \"}\"\n"
"	}\n"
"}\n"
"\n"
"class MapKeySequence is Sequence {\n"
"	fields {\n"
"		_map\n"
"	}\n"
"\n"
"	construct new(map) {\n"
"		_map = map\n"
"	}\n"
"\n"
"	iterate(n) { _map.iterate_(n) }\n"
"	iteratorValue(iterator) { _map.keyIteratorValue_(iterator) }\n"
"}\n"
"\n"
"class MapValueSequence is Sequence {\n"
"	fields {\n"
"		_map\n"
"	}\n"
"\n"
"	construct new(map) {\n"
"		_map = map\n"
"	}\n"
"\n"
"	iterate(n) { _map.iterate_(n) }\n"
"	iteratorValue(iterator) { _map.valueIteratorValue_(iterator) }\n"
"}\n"
"\n"
"\n"
"class Range is Sequence {}\n"
"\n"
"class Table {\n"
"	keys { TableKeySequence.new(this) }\n"
"	values { TableValueSequence.new(this) }\n"
"\n"
"	toString {\n"
"		var first = true\n"
"		var result = \"{\"\n"
"\n"
"		for (key in keys) {\n"
"			if (!first) result = result + \", \"\n"
"			first = false\n"
"			result = result + key.toString + \": \" + this[key].toString\n"
"		}\n"
"\n"
"		return result + \"}\"\n"
"	}\n"
"}\n"
"\n"
"class TableKeySequence is Sequence {\n"
"	fields {\n"
"		_map\n"
"	}\n"
"\n"
"	construct new(map) {\n"
"		_map = map\n"
"	}\n"
"\n"
"	iterate(n) { _map.iterate_(n) }\n"
"	iteratorValue(iterator) { _map.keyIteratorValue_(iterator) }\n"
"}\n"
"\n"
"class TableValueSequence is Sequence {\n"
"	fields {\n"
"		_map\n"
"	}\n"
"\n"
"	construct new(map) {\n"
"		_map = map\n"
"	}\n"
"\n"
"	iterate(n) { _map.iterate_(n) }\n"
"	iteratorValue(iterator) { _map.valueIteratorValue_(iterator) }\n"
"}\n";

///////////////////////////////////////////////////////////////////////////////////
//// VALIDATE
///////////////////////////////////////////////////////////////////////////////////

// Validates that the given argument in [args] is a function. Returns true if
// it is. If not, reports an error and returns false.
static bool validateFn(CardinalVM* vm, Value* args, int index, const char* argName) {
	if (IS_FN(args[index]) || IS_CLOSURE(args[index])) return true;

	args[0] = OBJ_VAL(cardinalStringConcat(vm, argName, -1,
                                     " must be a function.", -1));
	return false;
}

// Validates that the given argument in [args] is a Num. Returns true if it is.
// If not, reports an error and returns false.
static bool validateNum(CardinalVM* vm, Value* args, int index, const char* argName) {
	if (IS_NUM(args[index])) return true;

	args[0] = OBJ_VAL(cardinalStringConcat(vm, argName, -1,
                                     " must be a number.", -1));
	return false;
}

// Validates that [value] is an integer. Returns true if it is. If not, reports
// an error and returns false.
static bool validateIntValue(CardinalVM* vm, Value* args, double value, const char* argName) {
	if (compareFloat(trunc(value),value)) return true;

	args[0] = OBJ_VAL(cardinalStringConcat(vm, argName, -1,
                                     " must be an integer.", -1));
	return false;
}

// Validates that the given argument in [args] is an integer. Returns true if
// it is. If not, reports an error and returns false.
static bool validateInt(CardinalVM* vm, Value* args, int index, const char* argName) {
	// Make sure it's a number first.
	if (!validateNum(vm, args, index, argName)) return false;

	return validateIntValue(vm, args, AS_NUM(args[index]), argName);
}

// Validates that [value] is an integer within `[0, count)`. Also allows
// negative indices which map backwards from the end. Returns the valid positive
// index value. If invalid, reports an error and returns -1.
static int validateIndexValue(CardinalVM* vm, Value* args, int count, double value, const char* argName) {
	if (!validateIntValue(vm, args, value, argName)) return -1;

	int index = (int)value;

	// Negative indices count from the end.
	if (index < 0) index = count + index;

	// Check bounds.
	if (index >= 0 && index < count) return index;

	args[0] = OBJ_VAL(cardinalStringConcat(vm, argName, -1, " out of bounds.", -1));
	return -1;
}

// Validates that [key] is a valid object for use as a map key. Returns true if
// it is. If not, reports an error and returns false.
static bool validateKey(CardinalVM* vm, Value* args, int index) {
	Value arg = args[index];
	if (IS_BOOL(arg) || IS_CLASS(arg) || IS_NULL(arg) ||
	        IS_NUM(arg) || IS_RANGE(arg) || IS_STRING(arg)) {
		return true;
	}

	args[0] = cardinalNewString(vm, "Key must be a value type.", 25);
	return false;
}


// Validates that the argument at [argIndex] is an integer within `[0, count)`.
// Also allows negative indices which map backwards from the end. Returns the
// valid positive index value. If invalid, reports an error and returns -1.
static int validateIndex(CardinalVM* vm, Value* args, int count, int argIndex, const char* argName) {
	if (!validateNum(vm, args, argIndex, argName)) return -1;

	return validateIndexValue(vm, args, count, AS_NUM(args[argIndex]), argName);
}

// Validates that the given argument in [args] is a String. Returns true if it
// is. If not, reports an error and returns false.
static bool validateString(CardinalVM* vm, Value* args, int index, const char* argName) {
	if (IS_STRING(args[index])) return true;

	args[0] = OBJ_VAL(cardinalStringConcat(vm, argName, -1,
                                     " must be a string.", -1));
	return false;
}

// Validates that the given argument in [args] is a String. Returns true if it
// is. If not, reports an error and returns false.
static bool validateException(CardinalVM* vm, Value* args, int index, const char* argName) {
	if (cardinalIsObjInstanceOf(vm, args[index], "Exception")) return true;

	args[0] = OBJ_VAL(cardinalStringConcat(vm, argName, -1,
                                     " must be a string.", -1));
	return false;
}


// Prepare a function call to execute the function that was given as parameter
static PrimitiveResult callFunction(CardinalVM* vm, Value* args, int numArgs) {
	ObjFn* fn;
	if (IS_CLOSURE(args[0])) {
		fn = AS_CLOSURE(args[0])->fn;
	}
	else {
		fn = AS_FN(args[0]);
	}

	if (numArgs < fn->numParams) RETURN_ERROR("Function expects more arguments.");

	return PRIM_CALL;
}

static void pushParamCore(CardinalVM* vm, Value val) {
	*vm->fiber->stacktop++ = val;
}


// Given a [range] and the [length] of the object being operated on, determines
// the series of elements that should be chosen from the underlying object.
// Handles ranges that count backwards from the end as well as negative ranges.
//
// Returns the index from which the range should start or -1 if the range is
// invalid. After calling, [length] will be updated with the number of elements
// in the resulting sequence. [step] will be direction that the range is going:
// `1` if the range is increasing from the start index or `-1` if the range is
// decreasing.
static int calculateRange(CardinalVM* vm, Value* args, ObjRange* range,
                          int* length, int* step) {
	// Corner case: an empty range at zero is allowed on an empty sequence.
	// This way, list[0..-1] and list[0...list.count] can be used to copy a list
	// even when empty.
	if (*length == 0 && range->from == 0 &&
	        range->to == (range->isInclusive ? -1 : 0)) {
		*step = 0;
		return 0;
	}

	int from = validateIndexValue(vm, args, *length, range->from,
	                              "Range start");
	if (from == -1) return -1;

	int to;

	if (range->isInclusive) {
		to = validateIndexValue(vm, args, *length, range->to, "Range end");
		if (to == -1) return -1;

		*length = abs(from - to) + 1;
	}
	else {
		if (!validateIntValue(vm, args, range->to, "Range end")) return -1;

		// Bounds check it manually here since the excusive range can hang over
		// the edge.
		to = (int)range->to;
		if (to < 0) to = *length + to;

		if (to < -1 || to > *length) {
			args[0] = cardinalNewString(vm, "Range end out of bounds.", 24);
			return -1;
		}

		*length = abs(from - to);
	}

	*step = from < to ? 1 : -1;
	return from;
}

///////////////////////////////////////////////////////////////////////////////////
//// MODULE
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(moduleS_import)
	ObjModule* result;
	ObjString* toLoad = AS_STRING(args[1]);
	result = cardinalImportModuleVar(vm, OBJ_VAL(toLoad));
	
	RETURN_VAL(OBJ_VAL(result));
END_NATIVE

DEF_NATIVE(moduleS_save)
	ObjModule* module = AS_MODULE(args[1]);
	ObjString* name = AS_STRING(args[2]);

	cardinalSaveModule(vm, module, name);
	
	RETURN_NUM(args[1]);
END_NATIVE

DEF_NATIVE(module_import)
	ObjModule* module = AS_MODULE(args[0]);
	args[0] = OBJ_VAL(module->func);
	*numargs = *numargs - 1; 
	
	return PRIM_CALL;
END_NATIVE

DEF_NATIVE(module_subscript)
	ObjModule* module = AS_MODULE(args[0]);
	RETURN_VAL(cardinalModuleFind(vm, module, AS_STRING(args[1])));
END_NATIVE

DEF_NATIVE(module_subscriptSetter)
	cardinalModuleSet(vm, AS_MODULE(args[0]), AS_STRING(args[1]), args[2]);
	RETURN_VAL(args[2]);
END_NATIVE

DEF_NATIVE(module_toString)
	ObjString* name = AS_MODULE(args[0])->name;
	RETURN_OBJ(name);
END_NATIVE

DEF_NATIVE(module_count)
	RETURN_NUM(AS_MODULE(args[0])->count);
END_NATIVE

DEF_NATIVE(module_current)
	Value obj = OBJ_VAL(fiber->frames[fiber->numFrames - 1].fn);
	
	ObjFn* fn;
	if (IS_CLOSURE(obj)) {
		fn = AS_CLOSURE(obj)->fn;
	}
	else {
		fn = AS_FN(obj);
	}
	
	RETURN_OBJ(fn->module);
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// METHOD
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(method_new)
	ObjMethod* meth = cardinalNewMethod(vm);
	RETURN_OBJ(meth);
END_NATIVE

DEF_NATIVE(method_new1)
	ObjMethod* meth = cardinalNewMethod(vm);
	ObjString* name = AS_STRING(args[1]);
	cardinalLoadMethod(vm, meth, name);
	RETURN_OBJ(meth);
END_NATIVE

DEF_NATIVE(method_new2)
	ObjMethod* meth = cardinalNewMethod(vm);
	ObjString* name = AS_STRING(args[1]);
	cardinalLoadMethod(vm, meth, name);
	
	meth->caller = args[2];
	
	RETURN_OBJ(meth);
END_NATIVE

DEF_NATIVE(method_toString)
	ObjString* name = AS_METHOD(args[0])->name;
	RETURN_OBJ(name);
END_NATIVE

DEF_NATIVE(method_load)
	ObjMethod* meth = AS_METHOD(args[0]);
	ObjString* name = AS_STRING(args[1]);
	
	cardinalLoadMethod(vm, meth, name);
	
	RETURN_OBJ(meth);
END_NATIVE

DEF_NATIVE(method_arity)
	ObjMethod* meth = AS_METHOD(args[0]);
	
	if (meth->symbol < 0)
		RETURN_NUM(-1);
	
	// Count the number parameters the method expects.
	const char* signature = vm->methodNames.data[meth->symbol].buffer;
	int numParams = 0;
	for (const char* s = signature; *s != '\0'; s++) {
		if (*s == '_') numParams++;
	}
	RETURN_NUM(numParams);
END_NATIVE

DEF_NATIVE(method_loadCaller)
	ObjMethod* meth = AS_METHOD(args[0]);
	
	meth->caller = args[1];
	
	RETURN_OBJ(meth);
END_NATIVE

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

static PrimitiveResult callMethodCore(CardinalVM* vm, ObjFiber* fiber, Value* args, int* numargs) {
	ObjMethod* meth = AS_METHOD(args[0]);
	
	if (!methodIsReady(vm, meth))
		RETURN_ERROR("Methodcall is invalid. ");
	
	Value val = meth->caller;
	ObjClass* classObj = cardinalGetClassInline(vm, val);
	
	Method* method = &classObj->methods.data[meth->symbol];
	
	//set the stack correctly
	
	if (method->type == METHOD_FOREIGN) {
		args[0] = val;
		callForeign(vm, fiber, method->fn.foreign, *numargs);
		return PRIM_VALUE;
	}
	else if (method->type == METHOD_BLOCK) {
		// Set the first argument correct
		args[0] = OBJ_VAL(method->fn.obj);

		// Extract the params
		int nbParam = (*numargs);

		// Push the param on the stack
		for(int i = nbParam; i > 0; i--) {
			args[i] = args[i-1];
		}
		args[1] = val;
		
		// Add one for the implicit receiver argument.
		*numargs = nbParam; // + 1; 
		fiber->stacktop++;
		
		return PRIM_CALL; // PRIM_METH
	}
	else if (method->type == METHOD_PRIMITIVE) {
		args[0] = val;
		// After calling this, the result will be in the first arg slot.
		return method->fn.primitive(vm, fiber, args, numargs);
	}
	else { //if (method->type == METHOD_NONE) {
		RETURN_ERROR("Methodcall is invalid. ");
	}
}

DEF_NATIVE(method_call0)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call1)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call2)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call3)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call4)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call5)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call6)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call7)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call8)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call9)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call10)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call11)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call12)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call13)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call14)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call15)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

DEF_NATIVE(method_call16)
	return callMethodCore(vm, fiber, args, numargs);
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// BOOL
///////////////////////////////////////////////////////////////////////////////////

// Defines the not operator on bool types
DEF_NATIVE(bool_not)
	RETURN_BOOL(!AS_BOOL(args[0]));
END_NATIVE

// Defines a tostring function on bool types
DEF_NATIVE(bool_toString)
	if (AS_BOOL(args[0])) {
		RETURN_VAL(cardinalNewString(vm, "true", 4));
	}
	else {
		RETURN_VAL(cardinalNewString(vm, "false", 5));
	}
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// CLASS
///////////////////////////////////////////////////////////////////////////////////

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

static PrimitiveResult bindMethodNative(CardinalVM* vm, ObjFiber* fiber, Value* args, int* numargs, int type) {
	UNUSED(vm);
	UNUSED(fiber);
	UNUSED(args);
	UNUSED(numargs);
	ObjClass* classObj = AS_CLASS(args[0]);
	ObjString* name = AS_STRING(args[2]);
	
	int symbol = cardinalSymbolTableFind(&vm->methodNames, name->value, name->length);
	
	if (symbol < 0) {
		symbol = cardinalSymbolTableAdd(vm, &vm->methodNames, name->value, name->length);
	}
	
	// Binds the code of methodValue to the classObj (not the metaclass)
	// if the type is a static method: the classObj will become the metaclass
	// afterwords, bind the method to the vm
	bindMethod(vm, type, symbol, classObj, args[1]);
	RETURN_OBJ(classObj);
}


// Create a new instance of a given class
DEF_NATIVE(class_instantiate)
	ObjClass* classObj = AS_CLASS(args[0]);
	RETURN_VAL(cardinalNewInstance(vm, classObj));
END_NATIVE

// Get the name of an given class
DEF_NATIVE(class_name)
	ObjClass* classObj = AS_CLASS(args[0]);
	RETURN_OBJ(classObj->name);
END_NATIVE

DEF_NATIVE(class_bindMethod)
	return bindMethodNative(vm, fiber, args, numargs, (int) CODE_METHOD_INSTANCE);
END_NATIVE

DEF_NATIVE(class_bindMethodStatic)
	return bindMethodNative(vm, fiber, args, numargs, (int) CODE_METHOD_STATIC);
END_NATIVE

DEF_NATIVE(class_tradeMethod)
	ObjClass* classObj = AS_CLASS(args[0]);
	ObjClass* other = AS_CLASS(args[1]);
	ObjString* name = AS_STRING(args[2]);
	
	int symbol = cardinalSymbolTableFind(&vm->methodNames, name->value, name->length);
	
	if (symbol < 0) RETURN_NULL;
	
	Method method = other->methods.data[symbol];
	cardinalBindMethod(vm, classObj, symbol, method);
	
	RETURN_OBJ(classObj);
END_NATIVE

DEF_NATIVE(class_tradeStaticMethod)
	ObjClass* classObj = AS_CLASS(args[0]);
	ObjClass* other = cardinalGetClass(vm, args[1]);
	ObjString* name = AS_STRING(args[2]);
	
	int symbol = cardinalSymbolTableFind(&vm->methodNames, name->value, name->length);
	
	if (symbol < 0) RETURN_NULL;
	
	Method method = other->methods.data[symbol];
	cardinalBindMethod(vm, cardinalGetClass(vm, args[0]), symbol, method);
	
	RETURN_OBJ(classObj);
END_NATIVE

// Create a new class
DEF_NATIVE(class_newClass)
	ObjString* name = AS_STRING(args[1]);
	RETURN_OBJ(cardinalNewClass(vm, vm->metatable.objectClass, 0, name));
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// OBJECT
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(object_not)
	RETURN_VAL(FALSE_VAL);
END_NATIVE

DEF_NATIVE(object_eqeq)
	RETURN_BOOL(cardinalValuesEqual(args[0], args[1]));
END_NATIVE

DEF_NATIVE(object_bangeq)
	RETURN_BOOL(!cardinalValuesEqual(args[0], args[1]));
END_NATIVE

DEF_NATIVE(object_new)
	// This is the default argument-less constructor that all objects inherit.
	// It just returns "this".
	if (IS_CLASS(args[0]))
		args[0] = OBJ_VAL(cardinalNewInstance(vm, AS_CLASS(args[0])));
	RETURN_VAL(args[0]);
END_NATIVE

DEF_NATIVE(object_toString)
	if (IS_CLASS(args[0])) {
		RETURN_OBJ(AS_CLASS(args[0])->name);
	}
	else if (IS_INSTANCE(args[0])) {
		ObjInstance* instance = AS_INSTANCE(args[0]);
		ObjString* name = instance->obj.classObj->name;
		RETURN_OBJ(cardinalStringConcat(vm, "instance of ", -1,
							name->value, name->length));
	}

	RETURN_VAL(cardinalNewString(vm, "<object>", 8));
END_NATIVE

DEF_NATIVE(object_type)
	RETURN_OBJ(cardinalGetClass(vm, args[0]));
END_NATIVE

DEF_NATIVE(object_getMethod)
	ObjMethod* meth = cardinalNewMethod(vm);
	ObjString* name = AS_STRING(args[1]);
	cardinalLoadMethod(vm, meth, name);
	
	meth->caller = args[0];
	
	RETURN_OBJ(meth);
END_NATIVE

DEF_NATIVE(object_getAllMethods)
	ObjList* list = cardinalNewList(vm, 0);
	
	ObjClass* type = cardinalGetClassInline(vm, args[0]);
	
	for(int i=0; i<type->methods.count; i++) {
		if (type->methods.data[i].type == METHOD_NONE)
			continue;
		ObjMethod* meth = cardinalNewMethod(vm);
		Value name = cardinalNewString(vm, vm->methodNames.data[i].buffer, vm->methodNames.data[i].length);
		cardinalLoadMethod(vm, meth, AS_STRING(name));
		
		meth->caller = args[0];
		cardinalListAdd(vm, list, OBJ_VAL(meth));
	}
	
	
	RETURN_OBJ(list);
END_NATIVE

DEF_NATIVE(object_instantiate)
	RETURN_ERROR("Must provide a class to 'new' to construct.");
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// STRING
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(string_contains)
	if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

	ObjString* string = AS_STRING(args[0]);
	ObjString* search = AS_STRING(args[1]);

	RETURN_BOOL(cardinalStringFind(vm, string, search) != UINT32_MAX);
END_NATIVE

DEF_NATIVE(string_count)
	double count = AS_STRING(args[0])->length;
	RETURN_NUM(count);
END_NATIVE

DEF_NATIVE(string_endsWith)
	if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

	ObjString* string = AS_STRING(args[0]);
	ObjString* search = AS_STRING(args[1]);

	// Corner case, if the search string is longer than return false right away.
	if (search->length > string->length) RETURN_FALSE;

	int result = memcmp(string->value + string->length - search->length,
	                    search->value, search->length);

	RETURN_BOOL(result == 0);
END_NATIVE

DEF_NATIVE(string_indexOf)
	if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

	ObjString* string = AS_STRING(args[0]);
	ObjString* search = AS_STRING(args[1]);

	uint32_t index = cardinalStringFind(vm, string, search);

	RETURN_NUM(index == UINT32_MAX ? -1 : (int)index);
END_NATIVE

DEF_NATIVE(string_iterate)
	ObjString* string = AS_STRING(args[0]);

	// If we're starting the iteration, return the first index.
	if (IS_NULL(args[1])) {
		if (string->length == 0) RETURN_FALSE;
		RETURN_NUM(0);
	}

	if (!validateInt(vm, args, 1, "Iterator")) return PRIM_ERROR;

	int index = (int)AS_NUM(args[1]);
	if (index < 0) RETURN_FALSE;

	// Advance to the beginning of the next UTF-8 sequence.
	do {
		index++;
		if (index >= string->length) RETURN_FALSE;
	}
	while ((string->value[index] & 0xc0) == 0x80);

	RETURN_NUM(index);
END_NATIVE

DEF_NATIVE(string_iteratorValue)
	ObjString* string = AS_STRING(args[0]);
	int index = validateIndex(vm, args, string->length, 1, "Iterator");
	if (index == -1) return PRIM_ERROR;

	RETURN_VAL(cardinalStringCodePointAt(vm, string, index));
END_NATIVE

DEF_NATIVE(string_startsWith)
	if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

	ObjString* string = AS_STRING(args[0]);
	ObjString* search = AS_STRING(args[1]);

	// Corner case, if the search string is longer than return false right away.
	if (search->length > string->length) RETURN_FALSE;

	RETURN_BOOL(memcmp(string->value, search->value, search->length) == 0);
END_NATIVE

DEF_NATIVE(string_toString)
	RETURN_VAL(args[0]);
END_NATIVE

DEF_NATIVE(string_plus)
	if (!validateString(vm, args, 1, "Right operand")) return PRIM_ERROR;
	ObjString* left = AS_STRING(args[0]);
	ObjString* right = AS_STRING(args[1]);
	RETURN_OBJ(cardinalStringConcat(vm, left->value, left->length,
							  right->value, right->length));
END_NATIVE

DEF_NATIVE(string_subscript)
	ObjString* string = AS_STRING(args[0]);

	if (IS_NUM(args[1])) {
		int index = validateIndex(vm, args, string->length, 1, "Subscript");
		if (index == -1) return PRIM_ERROR;

		RETURN_VAL(cardinalStringCodePointAt(vm, string, index));
	}

	if (!IS_RANGE(args[1])) {
		RETURN_ERROR("Subscript must be a number or a range.");
	}

	int step;
	int count = string->length;
	int start = calculateRange(vm, args, AS_RANGE(args[1]), &count, &step);
	if (start == -1) return PRIM_ERROR;

	ObjString* result = AS_STRING(cardinalNewUninitializedString(vm, count));
	for (int i = 0; i < count; i++) {
		result->value[i] = string->value[start + (i * step)];
	}
	result->value[count] = '\0';

	RETURN_OBJ(result);
END_NATIVE

DEF_NATIVE(string_fromCodePoint)
	if (!validateInt(vm, args, 1, "Code point")) return PRIM_ERROR;

	int codePoint = (int)AS_NUM(args[1]);
	if (codePoint < 0) {
		RETURN_ERROR("Code point cannot be negative.");
	}
	else if (codePoint > 0x10ffff) {
		RETURN_ERROR("Code point cannot be greater than 0x10ffff.");
	}

	RETURN_VAL(cardinalStringFromCodePoint(vm, codePoint));
END_NATIVE

DEF_NATIVE(string_byteAt)
	ObjString* string = AS_STRING(args[0]);

	uint32_t index = validateIndex(vm, args, string->length, 1, "Index");
	if (index == UINT32_MAX) return PRIM_ERROR;

	RETURN_NUM((uint8_t)string->value[index]);
END_NATIVE

DEF_NATIVE(string_codePointAt)
	ObjString* string = AS_STRING(args[0]);

	uint32_t index = validateIndex(vm, args, string->length, 1, "Index");
	if (index == UINT32_MAX) return PRIM_ERROR;

	// If we are in the middle of a UTF-8 sequence, indicate that.
	const uint8_t* bytes = (uint8_t*)string->value;
	if ((bytes[index] & 0xc0) == 0x80) RETURN_NUM(-1);

	// Decode the UTF-8 sequence.
	RETURN_NUM(cardinalUtf8Decode((uint8_t*)string->value + index,
							string->length - index));
END_NATIVE


DEF_NATIVE(string_iterateByte)
	ObjString* string = AS_STRING(args[0]);

	// If we're starting the iteration, return the first index.
	if (IS_NULL(args[1])) {
		if (string->length == 0) RETURN_FALSE;
		RETURN_NUM(0);
	}

	if (!validateInt(vm, args, 1, "Iterator")) return PRIM_ERROR;

	if (AS_NUM(args[1]) < 0) RETURN_FALSE;
	uint32_t index = (uint32_t)AS_NUM(args[1]);

	// Advance to the next byte.
	index++;
	if (index >= (size_t) string->length) RETURN_FALSE;

	RETURN_NUM(index);
END_NATIVE


///////////////////////////////////////////////////////////////////////////////////
//// FIBER
///////////////////////////////////////////////////////////////////////////////////


DEF_NATIVE(fiber_instantiate)
	// Return the Fiber class itself. When we then call "new" on it, it will
	// create the fiber.
	RETURN_VAL(args[0]);
END_NATIVE

DEF_NATIVE(fiber_new)
	if (!validateFn(vm, args, 1, "Argument")) return PRIM_ERROR;

	ObjFiber* newFiber = cardinalNewFiber(vm, AS_OBJ(args[1]));

	// The compiler expects the first slot of a function to hold the receiver.
	// Since a fiber's stack is invoked directly, it doesn't have one, so put it
	newFiber->stack[0] = NULL_VAL;
	newFiber->stacktop++;

	RETURN_OBJ(newFiber);
END_NATIVE

// Defines a tostring function on fn type
DEF_NATIVE(fiber_toString)
	RETURN_VAL(cardinalNewString(vm, "<fiber>", 7));
END_NATIVE

DEF_NATIVE(fiber_abort)
	if (!validateString(vm, args, 1, "Error message")) return PRIM_ERROR;

	// Move the error message to the return position.
	args[0] = args[1];
	return PRIM_ERROR;
END_NATIVE

DEF_NATIVE(fiber_throw)
	if (!validateException(vm, args, 1, "Error message")) return PRIM_ERROR;

	// Move the error message to the return position.
	args[0] = args[1];
	return PRIM_ERROR;
END_NATIVE

DEF_NATIVE(fiber_call)
	ObjFiber* runFiber = AS_FIBER(args[0]);

	if (runFiber->numFrames == 0) RETURN_ERROR("Cannot call a finished fiber.");
	if (runFiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

	// Remember who ran it.
	runFiber->caller = fiber;
	runFiber->yielded = false;

	// If the fiber was yielded, make the yield call return null.
	if (runFiber->stacktop > runFiber->stack) {
		*(runFiber->stacktop - 1) = NULL_VAL;
	}

	return PRIM_RUN_FIBER;
END_NATIVE

static PrimitiveResult callFiber(CardinalVM* vm, ObjFiber* fiber, Value* args, int numargs) {
	ObjFiber* runFiber = AS_FIBER(args[0]);

	if (runFiber->numFrames == 0) RETURN_ERROR("Cannot call a finished fiber.");
	if (runFiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

	// Remember who ran it.
	runFiber->caller = fiber;

	// If the fiber was yielded, make the yield call return the value passed to
	// run.
	if (runFiber->yielded) {
		if (numargs > 1)
			RETURN_ERROR("Fiber only accepts 1 parameter after it has been yielded.");
		*(runFiber->stacktop - 1) = args[1];
		runFiber->yielded = false;
	}
	else {
		int i = 1;
		while (i <= numargs) {
			*(runFiber->stacktop + i - 1) = args[i];
			i++;
		}
		runFiber->stacktop += numargs;
	}

	// When the calling fiber resumes, we'll store the result of the run call
	// in its stack. Since fiber.run(value) has two arguments (the fiber and the
	// value) and we only need one slot for the result, discard the other slot
	// now.
	fiber->stacktop -= numargs;

	return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_call1)
	return callFiber(vm, fiber, args, 1);
END_NATIVE

DEF_NATIVE(fiber_call2)
	return callFiber(vm, fiber, args, 2);
END_NATIVE

DEF_NATIVE(fiber_call3)
	return callFiber(vm, fiber, args, 3);
END_NATIVE

DEF_NATIVE(fiber_call4)
	return callFiber(vm, fiber, args, 4);
END_NATIVE

DEF_NATIVE(fiber_call5)
	return callFiber(vm, fiber, args, 5);
END_NATIVE

DEF_NATIVE(fiber_call6)
	return callFiber(vm, fiber, args, 6);
END_NATIVE

DEF_NATIVE(fiber_call7)
	return callFiber(vm, fiber, args, 7);
END_NATIVE

DEF_NATIVE(fiber_call8)
	return callFiber(vm, fiber, args, 8);
END_NATIVE

DEF_NATIVE(fiber_call9)
	return callFiber(vm, fiber, args, 9);
END_NATIVE

DEF_NATIVE(fiber_call10)
	return callFiber(vm, fiber, args, 10);
END_NATIVE

DEF_NATIVE(fiber_call11)
	return callFiber(vm, fiber, args, 11);
END_NATIVE

DEF_NATIVE(fiber_call12)
	return callFiber(vm, fiber, args, 12);
END_NATIVE

DEF_NATIVE(fiber_call13)
	return callFiber(vm, fiber, args, 13);
END_NATIVE

DEF_NATIVE(fiber_call14)
	return callFiber(vm, fiber, args, 14);
END_NATIVE

DEF_NATIVE(fiber_call15)
	return callFiber(vm, fiber, args, 15);
END_NATIVE

DEF_NATIVE(fiber_call16)
	return callFiber(vm, fiber, args, 16);
END_NATIVE

DEF_NATIVE(fiber_error)
	ObjFiber* runFiber = AS_FIBER(args[0]);
	if (runFiber->error == NULL) RETURN_NULL;
	RETURN_OBJ(runFiber->error);
END_NATIVE

DEF_NATIVE(fiber_isDone)
	ObjFiber* runFiber = AS_FIBER(args[0]);
	RETURN_BOOL(runFiber->numFrames == 0 || runFiber->error != NULL);
END_NATIVE

static PrimitiveResult runFiber(CardinalVM* vm, ObjFiber* fiber, Value* args, int numargs) {
	ObjFiber* runFiber = AS_FIBER(args[0]);

	if (runFiber->numFrames == 0) RETURN_ERROR("Cannot call a finished fiber.");

	// If the fiber was yielded, make the yield call return the value passed to
	// run.
	if (runFiber->yielded) {
		if (numargs > 1)
			RETURN_ERROR("Fiber only accepts 1 parameter after it has been yielded.");
		*(runFiber->stacktop - 1) = args[1];
		runFiber->yielded = false;
	}
	else {
		int i = 1;
		while (i <= numargs) {
			*(runFiber->stacktop + i - 1) = args[i];
			i++;
		}
		runFiber->stacktop += numargs;
	}

	// When the calling fiber resumes, we'll store the result of the run call
	// in its stack. Since fiber.run(value) has two arguments (the fiber and the
	// value) and we only need one slot for the result, discard the other slot
	// now.
	fiber->stacktop -= numargs;
	
	// Unlike run, this does not remember the calling fiber. Instead, it
	// remember's *that* fiber's caller. You can think of it like tail call
	// elimination. The switched-from fiber is discarded and when the switched
	// to fiber completes or yields, control passes to the switched-from fiber's
	// caller.
	runFiber->caller = fiber->caller;

	return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_run)
	ObjFiber* runFiber = AS_FIBER(args[0]);

	if (runFiber->numFrames == 0) RETURN_ERROR("Cannot run a finished fiber.");

	// If the fiber was yielded, make the yield call return null.
	if (runFiber->caller == NULL && runFiber->stacktop > runFiber->stack) {
		*(runFiber->stacktop - 1) = NULL_VAL;
	}

	// Unlike run, this does not remember the calling fiber. Instead, it
	// remember's *that* fiber's caller. You can think of it like tail call
	// elimination. The switched-from fiber is discarded and when the switched
	// to fiber completes or yields, control passes to the switched-from fiber's
	// caller.
	runFiber->caller = fiber->caller;
	runFiber->yielded = false;

	return PRIM_RUN_FIBER;
END_NATIVE

DEF_NATIVE(fiber_run1)
	return runFiber(vm, fiber, args, 1);
END_NATIVE

DEF_NATIVE(fiber_run2)
	return runFiber(vm, fiber, args, 2);
END_NATIVE

DEF_NATIVE(fiber_run3)
	return runFiber(vm, fiber, args, 3);
END_NATIVE

DEF_NATIVE(fiber_run4)
	return runFiber(vm, fiber, args, 4);
END_NATIVE

DEF_NATIVE(fiber_run5)
	return runFiber(vm, fiber, args, 5);
END_NATIVE

DEF_NATIVE(fiber_run6)
	return runFiber(vm, fiber, args, 6);
END_NATIVE

DEF_NATIVE(fiber_run7)
	return runFiber(vm, fiber, args, 7);
END_NATIVE

DEF_NATIVE(fiber_run8)
	return runFiber(vm, fiber, args, 8);
END_NATIVE

DEF_NATIVE(fiber_run9)
	return runFiber(vm, fiber, args, 9);
END_NATIVE

DEF_NATIVE(fiber_run10)
	return runFiber(vm, fiber, args, 10);
END_NATIVE

DEF_NATIVE(fiber_run11)
	return runFiber(vm, fiber, args, 11);
END_NATIVE

DEF_NATIVE(fiber_run12)
	return runFiber(vm, fiber, args, 12);
END_NATIVE

DEF_NATIVE(fiber_run13)
	return runFiber(vm, fiber, args, 13);
END_NATIVE

DEF_NATIVE(fiber_run14)
	return runFiber(vm, fiber, args, 14);
END_NATIVE

DEF_NATIVE(fiber_run15)
	return runFiber(vm, fiber, args, 15);
END_NATIVE

DEF_NATIVE(fiber_run16)
	return runFiber(vm, fiber, args, 16);
END_NATIVE

DEF_NATIVE(fiber_try)
	ObjFiber* runFiber = AS_FIBER(args[0]);

	if (runFiber->numFrames == 0) RETURN_ERROR("Cannot try a finished fiber.");
	if (runFiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

	// Remember who ran it.
	runFiber->caller = fiber;
	runFiber->callerIsTrying = true;

	// If the fiber was yielded, make the yield call return null.
	if (runFiber->stacktop > runFiber->stack) {
		*(runFiber->stacktop - 1) = NULL_VAL;
	}
	
	runFiber->yielded = false;

	return PRIM_RUN_FIBER;
END_NATIVE

static PrimitiveResult tryFiber(CardinalVM* vm, ObjFiber* fiber, Value* args, int numargs) {
	ObjFiber* runFiber = AS_FIBER(args[0]);

	if (runFiber->numFrames == 0) RETURN_ERROR("Cannot call a finished fiber.");
	if (runFiber->caller != NULL) RETURN_ERROR("Fiber has already been called.");

	// If the fiber was yielded, make the yield call return the value passed to
	// run.
	if (runFiber->yielded) {
		if (numargs > 1)
			RETURN_ERROR("Fiber only accepts 1 parameter after it has been yielded.");
		*(runFiber->stacktop - 1) = args[1];
		runFiber->yielded = false;
	}
	else {
		int i = 1;
		while (i <= numargs) {
			*(runFiber->stacktop + i - 1) = args[i];
			i++;
		}
		runFiber->stacktop += numargs;
	}

	// When the calling fiber resumes, we'll store the result of the run call
	// in its stack. Since fiber.run(value) has two arguments (the fiber and the
	// value) and we only need one slot for the result, discard the other slot
	// now.
	fiber->stacktop -= numargs;
	
	// Remember who ran it.
	runFiber->caller = fiber;
	runFiber->callerIsTrying = true;

	return PRIM_RUN_FIBER;
}

DEF_NATIVE(fiber_try1)
	return tryFiber(vm, fiber, args, 1);
END_NATIVE

DEF_NATIVE(fiber_try2)
	return tryFiber(vm, fiber, args, 2);
END_NATIVE

DEF_NATIVE(fiber_try3)
	return tryFiber(vm, fiber, args, 3);
END_NATIVE

DEF_NATIVE(fiber_try4)
	return tryFiber(vm, fiber, args, 4);
END_NATIVE

DEF_NATIVE(fiber_try5)
	return tryFiber(vm, fiber, args, 5);
END_NATIVE

DEF_NATIVE(fiber_try6)
	return tryFiber(vm, fiber, args, 6);
END_NATIVE

DEF_NATIVE(fiber_try7)
	return tryFiber(vm, fiber, args, 7);
END_NATIVE

DEF_NATIVE(fiber_try8)
	return tryFiber(vm, fiber, args, 8);
END_NATIVE

DEF_NATIVE(fiber_try9)
	return tryFiber(vm, fiber, args, 9);
END_NATIVE

DEF_NATIVE(fiber_try10)
	return tryFiber(vm, fiber, args, 10);
END_NATIVE

DEF_NATIVE(fiber_try11)
	return tryFiber(vm, fiber, args, 11);
END_NATIVE

DEF_NATIVE(fiber_try12)
	return tryFiber(vm, fiber, args, 12);
END_NATIVE

DEF_NATIVE(fiber_try13)
	return tryFiber(vm, fiber, args, 13);
END_NATIVE

DEF_NATIVE(fiber_try14)
	return tryFiber(vm, fiber, args, 14);
END_NATIVE

DEF_NATIVE(fiber_try15)
	return tryFiber(vm, fiber, args, 15);
END_NATIVE

DEF_NATIVE(fiber_try16)
	return tryFiber(vm, fiber, args, 16);
END_NATIVE

DEF_NATIVE(fiber_yield)
	ObjFiber* caller = fiber->caller;
	fiber->caller = NULL;
	fiber->callerIsTrying = false;
	fiber->yielded = true;

	// If we don't have any other pending fibers, jump all the way out of the
	// interpreter.
	if (caller == NULL) {
		args[0] = NULL_VAL;
	}
	else {
		// Make the caller's run method return null.
		*(caller->stacktop - 1) = NULL_VAL;

		// Return the fiber to resume.
		args[0] = OBJ_VAL(caller);
	}

	return PRIM_RUN_FIBER;
END_NATIVE

DEF_NATIVE(fiber_yield1)
	ObjFiber* caller = fiber->caller;
	fiber->caller = NULL;
	fiber->callerIsTrying = false;
	fiber->yielded = true;

	// If we don't have any other pending fibers, jump all the way out of the
	// interpreter.
	if (caller == NULL) {
		args[0] = NULL_VAL;
	}
	else {
		// Make the caller's run method return the argument passed to yield.
		*(caller->stacktop - 1) = args[1];

		// When the yielding fiber resumes, we'll store the result of the yield call
		// in its stack. Since Fiber.yield(value) has two arguments (the Fiber class
		// and the value) and we only need one slot for the result, discard the other
		// slot now.
		fiber->stacktop--;

		// Return the fiber to resume.
		args[0] = OBJ_VAL(caller);
	}

	return PRIM_RUN_FIBER;
END_NATIVE

DEF_NATIVE(fiber_current)
	RETURN_OBJ(fiber);
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// LIST
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(list_instantiate)
	RETURN_OBJ(cardinalNewList(vm, 0));
END_NATIVE

DEF_NATIVE(list_add)
	ObjList* list = AS_LIST(args[0]);
	cardinalListAdd(vm, list, args[1]);
	RETURN_VAL(args[1]);
END_NATIVE

DEF_NATIVE(list_clear)
	ObjList* list = AS_LIST(args[0]);
	DEALLOCATE(vm, list->elements);
	list->elements = NULL;
	list->capacity = 0;
	list->count = 0;
	RETURN_NULL;
END_NATIVE

DEF_NATIVE(list_count)
	ObjList* list = AS_LIST(args[0]);
	RETURN_NUM((double) list->count);
END_NATIVE

DEF_NATIVE(list_head)
	ObjList* list = AS_LIST(args[0]);
	if (list->count == 0) return PRIM_ERROR;

	RETURN_VAL(list->elements[0]);
END_NATIVE

DEF_NATIVE(list_tail)
	ObjList* list = AS_LIST(args[0]);

	ObjList* newlist = cardinalNewList(vm, 0);
	for(int i=1; i < list->count; i++) {
		cardinalListAdd(vm, newlist, list->elements[i]);
	}
	RETURN_OBJ(newlist);
END_NATIVE

DEF_NATIVE(list_init)
	ObjList* list = AS_LIST(args[0]);

	ObjList* newlist = cardinalNewList(vm, 0);
	for(int i=0; i < list->count - 1; i++) {
		cardinalListAdd(vm, newlist, list->elements[i]);
	}
	RETURN_OBJ(newlist);
END_NATIVE

DEF_NATIVE(list_last)
	ObjList* list = AS_LIST(args[0]);
	if (list->count == 0) return PRIM_ERROR;

	RETURN_VAL(list->elements[list->count - 1]);
END_NATIVE

DEF_NATIVE(list_conc)
	ObjList* list = AS_LIST(args[0]);
	if (list->count == 0) return PRIM_ERROR;

	cardinalListInsert(vm, list, args[1], 0);
	RETURN_VAL(args[1]);
END_NATIVE

DEF_NATIVE(list_call)
	ObjList* list = AS_LIST(args[0]);
	if (list->count == 0) return PRIM_ERROR;
		
	// Set the first argument correct
	args[0] = list->elements[0];

	// Extract the params
	int nbParam = (list->count - 1);
	
	// Extract the function from the list
	ObjFn* fn;
	if (IS_CLOSURE(args[0])) {
		fn = AS_CLOSURE(args[0])->fn;
	}
	else if (IS_FN(args[0])) {
		fn = AS_FN(args[0]);
	}
	else {
		RETURN_ERROR("List first element should be a function.");
	}
	
	// Check the amount of parameters
	if (nbParam < fn->numParams) RETURN_ERROR("Function expects more arguments.");
	if (nbParam > fn->numParams) nbParam = fn->numParams;
	
	// Push the param on the stack
	for(int i=1; i < nbParam + 1; i++) {
		pushParamCore(vm, list->elements[i]);
	}
	
	// Add one for the implicit receiver argument.
	*numargs = nbParam + 1; 

	return PRIM_CALL;
	
END_NATIVE

DEF_NATIVE(list_insert)
	ObjList* list = AS_LIST(args[0]);

	// count + 1 here so you can "insert" at the very end.
	int index = validateIndex(vm, args, list->count + 1, 2, "Index");
	if (index == -1) return PRIM_ERROR;

	cardinalListInsert(vm, list, args[1], index);
	RETURN_VAL(args[1]);
END_NATIVE

DEF_NATIVE(list_iterate)
	ObjList* list = AS_LIST(args[0]);

	// If we're starting the iteration, return the first index.
	if (IS_NULL(args[1])) {
		if (list->count == 0) RETURN_FALSE;
		RETURN_NUM(0);
	}

	if (!validateInt(vm, args, 1, "Iterator")) return PRIM_ERROR;

	int index = (int)AS_NUM(args[1]);

	// Stop if we're out of bounds.
	if (index < 0 || index >= list->count - 1) RETURN_FALSE;

	// Otherwise, move to the next index.
	RETURN_NUM((double) index + 1);
END_NATIVE

DEF_NATIVE(list_iteratorValue)
	ObjList* list = AS_LIST(args[0]);
	int index = validateIndex(vm, args, list->count, 1, "Iterator");
	if (index == -1) return PRIM_ERROR;

	RETURN_VAL(list->elements[index]);
END_NATIVE

DEF_NATIVE(list_removeAt)
	ObjList* list = AS_LIST(args[0]);
	int index = validateIndex(vm, args, list->count, 1, "Index");
	if (index == -1) return PRIM_ERROR;

	RETURN_VAL(cardinalListRemoveAt(vm, list, index));
END_NATIVE

DEF_NATIVE(list_subscript)
	ObjList* list = AS_LIST(args[0]);

	if (IS_NUM(args[1])) {
		int index = validateIndex(vm, args, list->count, 1, "Subscript");
		if (index == -1) return PRIM_ERROR;

		RETURN_VAL(list->elements[index]);
	}

	if (!IS_RANGE(args[1])) {
		RETURN_ERROR("Subscript must be a number or a range.");
	}

	int step;
	int count = list->count;
	int start = calculateRange(vm, args, AS_RANGE(args[1]), &count, &step);
	if (start == -1) return PRIM_ERROR;

	ObjList* result = cardinalNewList(vm, count);
	for (int i = 0; i < count; i++) {
		result->elements[i] = list->elements[start + (i * step)];
	}

	RETURN_OBJ(result);
END_NATIVE

DEF_NATIVE(list_subscriptSetter)
	ObjList* list = AS_LIST(args[0]);
	int index = validateIndex(vm, args, list->count, 1, "Subscript");
	if (index == -1) return PRIM_ERROR;

	list->elements[index] = args[2];
	RETURN_VAL(args[2]);
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// MAP
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(map_instantiate)
	RETURN_OBJ(cardinalNewMap(vm));
END_NATIVE

DEF_NATIVE(map_subscript)
	if (!validateKey(vm, args, 1)) return PRIM_ERROR;

	ObjMap* map = AS_MAP(args[0]);
	uint32_t index = cardinalMapFind(map, args[1]);
	if (index == UINT32_MAX) RETURN_NULL;

	RETURN_VAL(map->entries[index].value);
END_NATIVE

DEF_NATIVE(map_subscriptSetter)
	if (!validateKey(vm, args, 1)) return PRIM_ERROR;

	cardinalMapSet(vm, AS_MAP(args[0]), args[1], args[2]);
	RETURN_VAL(args[2]);
END_NATIVE

DEF_NATIVE(map_clear)
	cardinalMapClear(vm, AS_MAP(args[0]));
	RETURN_NULL;
END_NATIVE

DEF_NATIVE(map_containsKey)
	if (!validateKey(vm, args, 1)) return PRIM_ERROR;

	RETURN_BOOL(cardinalMapFind(AS_MAP(args[0]), args[1]) != UINT32_MAX);
END_NATIVE

DEF_NATIVE(map_count)
	RETURN_NUM(AS_MAP(args[0])->count);
END_NATIVE

DEF_NATIVE(map_iterate)
	ObjMap* map = AS_MAP(args[0]);

	if (map->count == 0) RETURN_FALSE;

	// If we're starting the iteration, start at the first used entry.
	uint32_t index = 0;

	// Otherwise, start one past the last entry we stopped at.
	if (!IS_NULL(args[1])) {
		if (!validateInt(vm, args, 1, "Iterator")) return PRIM_ERROR;

		if (AS_NUM(args[1]) < 0) RETURN_FALSE;
		index = (uint32_t)AS_NUM(args[1]);

		if (index >= map->capacity) RETURN_FALSE;

		// Advance the iterator.
		index++;
	}

	// Find a used entry, if any.
	for (; index < map->capacity; index++) {
		if (!IS_UNDEFINED(map->entries[index].key)) RETURN_NUM(index);
	}

	// If we get here, walked all of the entries.
	RETURN_FALSE;
END_NATIVE

DEF_NATIVE(map_remove)
	if (!validateKey(vm, args, 1)) return PRIM_ERROR;

	RETURN_VAL(cardinalMapRemoveKey(vm, AS_MAP(args[0]), args[1]));
END_NATIVE

DEF_NATIVE(map_keyIteratorValue)
	ObjMap* map = AS_MAP(args[0]);
	int index = validateIndex(vm, args, map->capacity, 1, "Iterator");
	if (index == -1) return PRIM_ERROR;

	MapEntry* entry = &map->entries[index];
	if (IS_UNDEFINED(entry->key)) {
		RETURN_ERROR("Invalid map iterator value.");
	}

	RETURN_VAL(entry->key);
END_NATIVE

DEF_NATIVE(map_valueIteratorValue)
	ObjMap* map = AS_MAP(args[0]);
	int index = validateIndex(vm, args, map->capacity, 1, "Iterator");
	if (index == -1) return PRIM_ERROR;

	MapEntry* entry = &map->entries[index];
	if (IS_UNDEFINED(entry->key)) {
		RETURN_ERROR("Invalid map iterator value.");
	}

	RETURN_VAL(entry->value);
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// RANGE
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(range_toString)
	char buffer[51];
	ObjRange* range = AS_RANGE(args[0]);
	sprintf(buffer, "%.14g%s%.14g", range->from,
	        range->isInclusive ? ".." : "...", range->to);
	RETURN_VAL(cardinalNewString(vm, buffer, strlen(buffer)));
END_NATIVE

DEF_NATIVE(range_from)
	ObjRange* range = AS_RANGE(args[0]);
	RETURN_NUM(range->from);
END_NATIVE

DEF_NATIVE(range_to)
	ObjRange* range = AS_RANGE(args[0]);
	RETURN_NUM(range->to);
END_NATIVE

DEF_NATIVE(range_min)
	ObjRange* range = AS_RANGE(args[0]);
	RETURN_NUM(fmin(range->from, range->to));
END_NATIVE

DEF_NATIVE(range_max)
	ObjRange* range = AS_RANGE(args[0]);
	RETURN_NUM(fmax(range->from, range->to));
END_NATIVE

DEF_NATIVE(range_isInclusive)
	ObjRange* range = AS_RANGE(args[0]);
	RETURN_BOOL(range->isInclusive);
END_NATIVE

DEF_NATIVE(range_iterate)
	ObjRange* range = AS_RANGE(args[0]);

	// Special case: empty range.
	if (range->from == range->to && !range->isInclusive) RETURN_FALSE;

	// Start the iteration.
	if (IS_NULL(args[1])) RETURN_NUM(range->from);

	if (!validateNum(vm, args, 1, "Iterator")) return PRIM_ERROR;

	double iterator = AS_NUM(args[1]);

	// Iterate towards [to] from [from].
	if (range->from < range->to) {
		iterator++;
		if (iterator > range->to) RETURN_FALSE;
	}
	else {
		iterator--;
		if (iterator < range->to) RETURN_FALSE;
	}

	if (!range->isInclusive && iterator == range->to) RETURN_FALSE;

	RETURN_NUM(iterator);
END_NATIVE

DEF_NATIVE(range_iteratorValue)
	// Assume the iterator is a number so that is the value of the range.
	RETURN_VAL(args[1]);
END_NATIVE


///////////////////////////////////////////////////////////////////////////////////
//// NUM
///////////////////////////////////////////////////////////////////////////////////


// Defines a primitive method on Num that returns the result of [fn].
#define DEF_NUM_FN(name, fn) \
	DEF_NATIVE(num_##name) \
		RETURN_NUM(fn(AS_NUM(args[0]))); \
	END_NATIVE

DEF_NUM_FN(acos,    acos)
DEF_NUM_FN(asin,    asin)
DEF_NUM_FN(atan,    atan)
DEF_NUM_FN(tan,     tan)

DEF_NATIVE(num_pi)
	RETURN_NUM(3.14159265358979323846);
END_NATIVE

DEF_NATIVE(num_atan2)
	RETURN_NUM(atan2(AS_NUM(args[0]), AS_NUM(args[1])));
END_NATIVE

DEF_NATIVE(num_abs)
	RETURN_NUM(fabs(AS_NUM(args[0])));
END_NATIVE

DEF_NATIVE(num_ceil)
	RETURN_NUM(ceil(AS_NUM(args[0])));
END_NATIVE

DEF_NATIVE(num_cos) 
	RETURN_NUM(cos(AS_NUM(args[0])));
END_NATIVE

DEF_NATIVE(num_floor)
	RETURN_NUM(floor(AS_NUM(args[0])));
END_NATIVE

DEF_NATIVE(num_isNan)
	RETURN_BOOL(isnan(AS_NUM(args[0])));
END_NATIVE

DEF_NATIVE(num_sin) 
	RETURN_NUM(sin(AS_NUM(args[0])));
END_NATIVE

DEF_NATIVE(num_sqrt)
	RETURN_NUM(sqrt(AS_NUM(args[0])));
END_NATIVE

DEF_NATIVE(num_toString)
	double value = AS_NUM(args[0]);

	// Corner case: If the value is NaN, different versions of libc produce
	// different outputs (some will format it signed and some won't). To get
	// reliable output, handle that ourselves.
	if (value != value) RETURN_VAL(cardinalNewString(vm, "nan", 3));

	// This is large enough to hold any double converted to a string using
	// "%.14g". Example:
	//
	//     -1.12345678901234e-1022
	//
	// So we have:
	//
	// + 1 char for sign
	// + 1 char for digit
	// + 1 char for "."
	// + 14 chars for decimal digits
	// + 1 char for "e"
	// + 1 char for "-" or "+"
	// + 4 chars for exponent
	// + 1 char for "\0"
	// = 24
	char buffer[24];
	int length = sprintf(buffer, "%.14g", value);
	RETURN_VAL(cardinalNewString(vm, buffer, length));
END_NATIVE

DEF_NATIVE(num_fromString)
	if (!validateString(vm, args, 1, "Argument")) return PRIM_ERROR;

	ObjString* string = AS_STRING(args[1]);

	// Corner case: Can't parse an empty string.
	if (string->length == 0) RETURN_NULL;

	//errno = 0;
	char* end;
	double number = strtod(string->value, &end);

	// Skip past any trailing whitespace.
	while (*end != '\0' && isspace(*end)) end++;

	//if (errno == ERANGE) {
	//	args[0] = cardinalNewString(vm, "Number literal is too large.", 28);
	//	return PRIM_ERROR;
	//}

	// We must have consumed the entire string. Otherwise, it contains non-number
	// characters and we can't parse it.
	if (end < string->value + string->length) RETURN_NULL;

	RETURN_NUM(number);
END_NATIVE

DEF_NATIVE(num_truncate)
	double integer;
	modf(AS_NUM(args[0]) , &integer);
	RETURN_NUM(integer);
END_NATIVE

DEF_NATIVE(num_rad)
	RETURN_NUM(floor(AS_NUM(args[0]) / 57.2957795130823208768));
END_NATIVE

DEF_NATIVE(num_sign)
	double value = AS_NUM(args[0]);
	if (value > 0) {
		RETURN_NUM(1);
	}
	else if (value < 0) {
		RETURN_NUM(-1);
	}
	else {
		RETURN_NUM(0);
	}
END_NATIVE

DEF_NATIVE(num_deg)
	RETURN_NUM(floor(AS_NUM(args[0]) * 57.2957795130823208768));
END_NATIVE
 
DEF_NATIVE(num_fraction)
	double dummy;
	RETURN_NUM(modf(AS_NUM(args[0]) , &dummy));
END_NATIVE

DEF_NATIVE(num_negate)
	RETURN_NUM(-AS_NUM(args[0]));
END_NATIVE

DEF_NATIVE(num_minus)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_NUM(AS_NUM(args[0]) - AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_plus)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_NUM(AS_NUM(args[0]) + AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_multiply)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_NUM(AS_NUM(args[0]) * AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_divide)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_NUM(AS_NUM(args[0]) / AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_mod)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_NUM(fmod(AS_NUM(args[0]), AS_NUM(args[1])));
END_NATIVE

DEF_NATIVE(num_lt)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_BOOL(AS_NUM(args[0]) < AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_gt)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_BOOL(AS_NUM(args[0]) > AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_lte)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_BOOL(AS_NUM(args[0]) <= AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_gte)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;
	RETURN_BOOL(AS_NUM(args[0]) >= AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_eqeq)
	if (!IS_NUM(args[1])) RETURN_FALSE;
	RETURN_BOOL(AS_NUM(args[0]) == AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_bangeq)
	if (!IS_NUM(args[1])) RETURN_TRUE;
	RETURN_BOOL(AS_NUM(args[0]) != AS_NUM(args[1]));
END_NATIVE

DEF_NATIVE(num_bitwiseNot)
	// Bitwise operators always work on 32-bit unsigned ints.
	cardinal_uinteger value = (cardinal_uinteger)AS_NUM(args[0]);
	RETURN_NUM((double) ~value);
END_NATIVE

DEF_NATIVE(num_bitwiseAnd)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;

	// Bitwise operators always work on 32-bit unsigned ints.
	cardinal_uinteger left = (cardinal_uinteger)AS_NUM(args[0]);
	cardinal_uinteger right = (cardinal_uinteger)AS_NUM(args[1]);
	RETURN_NUM((double) (left & right));
END_NATIVE

DEF_NATIVE(num_bitwiseOr)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;

	// Bitwise operators always work on 32-bit unsigned ints.
	cardinal_uinteger left = (cardinal_uinteger)AS_NUM(args[0]);
	cardinal_uinteger right = (cardinal_uinteger)AS_NUM(args[1]);
	RETURN_NUM((double) (left | right));
END_NATIVE

DEF_NATIVE(num_bitwiseXor)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;

	// Bitwise operators always work on 32-bit unsigned ints.
	cardinal_uinteger left = (cardinal_uinteger)AS_NUM(args[0]);
	cardinal_uinteger right = (cardinal_uinteger)AS_NUM(args[1]);
	RETURN_NUM(left ^ right);
END_NATIVE

DEF_NATIVE(num_bitwiseLeftShift)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;

	// Bitwise operators always work on 32-bit unsigned ints.
	cardinal_uinteger left = (cardinal_uinteger)AS_NUM(args[0]);
	cardinal_uinteger right = (cardinal_uinteger)AS_NUM(args[1]);
	RETURN_NUM(left << right);
END_NATIVE

DEF_NATIVE(num_bitwiseRightShift)
	if (!validateNum(vm, args, 1, "Right operand")) return PRIM_ERROR;

	// Bitwise operators always work on 32-bit unsigned ints.
	cardinal_uinteger left = (cardinal_uinteger)AS_NUM(args[0]);
	cardinal_uinteger right = (cardinal_uinteger)AS_NUM(args[1]);
	RETURN_NUM(left >> right);
END_NATIVE

DEF_NATIVE(num_dotDot)
	if (!validateNum(vm, args, 1, "Right hand side of range")) return PRIM_ERROR;

	double from = AS_NUM(args[0]);
	double to = AS_NUM(args[1]);

	RETURN_VAL(cardinalNewRange(vm, from, to, true));
END_NATIVE

DEF_NATIVE(num_dotDotDot)
	if (!validateNum(vm, args, 1, "Right hand side of range")) return PRIM_ERROR;

	double from = AS_NUM(args[0]);
	double to = AS_NUM(args[1]);

	RETURN_VAL(cardinalNewRange(vm, from, to, false));
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// FN
///////////////////////////////////////////////////////////////////////////////////

// Instantiate an fn object
DEF_NATIVE(fn_instantiate)
	// Return the Fn class itself. When we then call "new" on it, it will
	// return the block.
	RETURN_VAL(args[0]);
END_NATIVE

// Create a new fn object
DEF_NATIVE(fn_new)
	if (!validateFn(vm, args, 1, "Argument")) return PRIM_ERROR;

	// The block argument is already a function, so just return it.
	RETURN_VAL(args[1]);
END_NATIVE

// Perform a function call with no arguments to 16 arguments
DEF_NATIVE(fn_call0)
	return callFunction(vm, args, 0);
END_NATIVE

DEF_NATIVE(fn_call1)
	return callFunction(vm, args, 1);
END_NATIVE

DEF_NATIVE(fn_call2)
	return callFunction(vm, args, 2);
END_NATIVE

DEF_NATIVE(fn_call3)
	return callFunction(vm, args, 3);
END_NATIVE

DEF_NATIVE(fn_call4)
	return callFunction(vm, args, 4);
END_NATIVE

DEF_NATIVE(fn_call5)
	return callFunction(vm, args, 5);
END_NATIVE

DEF_NATIVE(fn_call6)
	return callFunction(vm, args, 6);
END_NATIVE

DEF_NATIVE(fn_call7)
	return callFunction(vm, args, 7);
END_NATIVE

DEF_NATIVE(fn_call8)
	return callFunction(vm, args, 8);
END_NATIVE

DEF_NATIVE(fn_call9)
	return callFunction(vm, args, 9);
END_NATIVE

DEF_NATIVE(fn_call10)
	return callFunction(vm, args, 10);
END_NATIVE

DEF_NATIVE(fn_call11)
	return callFunction(vm, args, 11);
END_NATIVE

DEF_NATIVE(fn_call12)
	return callFunction(vm, args, 12);
END_NATIVE

DEF_NATIVE(fn_call13)
	return callFunction(vm, args, 13);
END_NATIVE

DEF_NATIVE(fn_call14)
	return callFunction(vm, args, 14);
END_NATIVE

DEF_NATIVE(fn_call15)
	return callFunction(vm, args, 15);
END_NATIVE

DEF_NATIVE(fn_call16)
	return callFunction(vm, args, 16);
END_NATIVE

// Defines a tostring function on fn type
DEF_NATIVE(fn_toString)
	RETURN_VAL(cardinalNewString(vm, AS_FN(args[0])->debug->name, strlen(AS_FN(args[0])->debug->name)));
END_NATIVE

DEF_NATIVE(fn_arity)
	ObjFn* fn;
	if (IS_CLOSURE(args[0])) {
		fn = AS_CLOSURE(args[0])->fn;
	}
	else {
		fn = AS_FN(args[0]);
	}

	double arity = fn->numParams;

	RETURN_NUM(arity);
END_NATIVE


///////////////////////////////////////////////////////////////////////////////////
//// NULL
///////////////////////////////////////////////////////////////////////////////////

// Defines a tostring function on null type
DEF_NATIVE(null_toString)
	RETURN_VAL(cardinalNewString(vm, "null", 4));
END_NATIVE


DEF_NATIVE(null_not)
	RETURN_VAL(TRUE_VAL);
END_NATIVE


///////////////////////////////////////////////////////////////////////////////////
//// HASHMAP
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(table_new)
	ObjTable* list = cardinalNewTable(vm, 0);
	RETURN_OBJ(list);
END_NATIVE

DEF_NATIVE(table_instantiate)
	RETURN_OBJ(cardinalNewTable(vm, 0));
END_NATIVE

DEF_NATIVE(table_newSize)
	if (!validateNum(vm, args, 1, "New operator")) return PRIM_ERROR;
	
	ObjTable* list = cardinalNewTable(vm, AS_NUM(args[1]));
	RETURN_OBJ(list);
END_NATIVE

DEF_NATIVE(table_containsKey)
	if (!validateKey(vm, args, 1)) return PRIM_ERROR;

	RETURN_BOOL(cardinalTableFind(vm, AS_TABLE(args[0]), args[1]) != NULL_VAL);
END_NATIVE

DEF_NATIVE(table_add)
	ObjTable* list = AS_TABLE(args[0]);
	cardinalTableAdd(vm, list, args[1], args[2]);
	RETURN_VAL(args[1]);
END_NATIVE

DEF_NATIVE(table_toString)
	RETURN_VAL(cardinalNewString(vm, "Instance of Table", 17));
END_NATIVE

DEF_NATIVE(table_clear)
	ObjTable* list = AS_TABLE(args[0]);
	cardinalReallocate(vm, list->hashmap, 0, 0);
	list->hashmap = NULL;
	list->capacity = 0;
	list->count = 0;
	RETURN_NULL;
END_NATIVE

DEF_NATIVE(table_count)
	ObjTable* list = AS_TABLE(args[0]);
	RETURN_NUM((double) list->count);
END_NATIVE

DEF_NATIVE(table_remove)
	ObjTable* list = AS_TABLE(args[0]);
	RETURN_VAL(cardinalTableRemove(vm, list, args[1]));
END_NATIVE

DEF_NATIVE(table_subscript)
	ObjTable* list = AS_TABLE(args[0]);
	RETURN_VAL(cardinalTableFind(vm, list, args[1]));
END_NATIVE

DEF_NATIVE(table_subscriptSetter)
	ObjTable* list = AS_TABLE(args[0]);
	cardinalTableAdd(vm, list, args[1], args[2]);
	RETURN_VAL(args[2]);
END_NATIVE

DEF_NATIVE(table_printAll)
	ObjTable* list = AS_TABLE(args[0]);
	cardinalTablePrint(vm, list);
	RETURN_TRUE;
END_NATIVE

DEF_NATIVE(table_iterate)
	ObjTable* map = AS_TABLE(args[0]);

	if (map->count == 0) RETURN_FALSE;

	// If we're starting the iteration, start at the first used entry.
	uint32_t index = 0;

	// Otherwise, start one past the last entry we stopped at.
	if (!IS_NULL(args[1])) {
		if (!validateInt(vm, args, 1, "Iterator")) return PRIM_ERROR;

		if (AS_NUM(args[1]) < 0) RETURN_FALSE;
		index = (uint32_t)AS_NUM(args[1]);

		if (index >= (size_t) map->capacity) RETURN_FALSE;

		// Advance the iterator.
		index++;
	}

	// Find a used entry, if any.
	HashValue* val = cardinalGetTableIndex(map, index);
	if (val == NULL) RETURN_FALSE;
	RETURN_NUM(index);
END_NATIVE

DEF_NATIVE(table_keyIteratorValue)
	ObjTable* map = AS_TABLE(args[0]);
	int index = validateIndex(vm, args, map->capacity, 1, "Iterator");
	if (index == -1) return PRIM_ERROR;

	HashValue* entry = cardinalGetTableIndex(map, index);
	if (entry == NULL || IS_UNDEFINED(entry->key)) {
		RETURN_ERROR("Invalid map iterator value.");
	}

	RETURN_VAL(entry->key);
END_NATIVE

DEF_NATIVE(table_get)
	ObjTable* map = AS_TABLE(args[0]);
	int index = validateIndex(vm, args, map->capacity, 1, "Iterator");
	if (index == -1) return PRIM_ERROR;
	
	// Index is the x-th element we want to acces
	HashValue* entry = cardinalGetTableIndex(map, index);
	if (entry == NULL || IS_UNDEFINED(entry->key)) {
		RETURN_ERROR("Invalid map iterator value.");
	}

	RETURN_VAL(entry->val);
END_NATIVE

DEF_NATIVE(table_valueIteratorValue)
	ObjTable* map = AS_TABLE(args[0]);
	int index = validateIndex(vm, args, map->capacity, 1, "Iterator");
	if (index == -1) return PRIM_ERROR;

	// Index is the x-th element we want to acces
	HashValue* entry = cardinalGetTableIndex(map, index);
	if (entry == NULL || IS_UNDEFINED(entry->key)) {
		RETURN_ERROR("Invalid map iterator value.");
	}

	RETURN_VAL(entry->val);
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// SYSTEM
///////////////////////////////////////////////////////////////////////////////////

static void deassembleFunction(CardinalVM* vm) {
	CardinalValue* val = cardinalGetArgument(vm, 1);
	Value obj = cardinalGetHostObject(vm, val);
	
	ObjFn* fn;
	if (IS_CLOSURE(obj)) {
		fn = AS_CLOSURE(obj)->fn;
	}
	else {
		fn = AS_FN(obj);
	}

	cardinalDebugPrintCode(vm, fn);
	cardinalRemoveHostObject(vm, val);
}

static void runCode(CardinalVM* vm) {
	const char* source = cardinalGetArgumentString(vm, 1);
	ObjString* str = cardinalStringConcat(vm, "return new Fiber {\n", -1, source, -1);
	ObjString* res = cardinalStringConcat(vm, str->value, str->length, "\n}\n", -1);

	ObjFiber* fiber = loadModuleFiber(vm, cardinalNewString(vm, "<runtime>", 9), OBJ_VAL(res));
	
	if (fiber == NULL) {
		cardinalReturnNull(vm);
	}
	else {
		ObjFiber* old = vm->fiber;
		vm->fiber = fiber;
		runInterpreter(vm);
		vm->fiber = old;
		
		Value returnValue = fiber->stack[1];
	
		CardinalValue* ret = cardinalCreateHostObject(vm, returnValue);
		cardinalReturnValue(vm, ret);
	}
}

static void runCodeParam(CardinalVM* vm) {
	const char* param = cardinalGetArgumentString(vm, 1);
	const char* source = cardinalGetArgumentString(vm, 2);
	ObjString* str = cardinalStringConcat(vm, "return new Fiber { |", -1, param, -1);
	ObjString* str2 = cardinalStringConcat(vm, str->value, str->length, "|\n", -1);
	ObjString* str3 = cardinalStringConcat(vm, str2->value, str2->length, source, -1);
	ObjString* res = cardinalStringConcat(vm, str3->value, str3->length, "\n}\n", -1);
	
	ObjFiber* fiber = loadModuleFiber(vm, cardinalNewString(vm, "<runtime>", 9), OBJ_VAL(res));
	
	if (fiber == NULL) {
		cardinalReturnNull(vm);
	}
	else {
		ObjFiber* old = vm->fiber;
		vm->fiber = fiber;
		runInterpreter(vm);
		vm->fiber = old;
		
		Value returnValue = fiber->stack[1];
	
		CardinalValue* ret = cardinalCreateHostObject(vm, returnValue);
		cardinalReturnValue(vm, ret);
	}
}

static void getHostObject(CardinalVM* vm) {
	double ind = cardinalGetArgumentDouble(vm, 1);
	
	CardinalValue* val = cardinalCreateValue(vm);
	val->value = NUM_VAL(ind);
	
	cardinalReturnValue(vm, val);
}

static void setHostObject(CardinalVM* vm) {
	CardinalValue* obj = cardinalGetArgument(vm, 2);
	double ind = cardinalGetArgumentDouble(vm, 1);
	
	CardinalValue val;
	val.value = ind;
	cardinalSetHostObject(vm, cardinalGetHostObject(vm, obj), &val);
	cardinalRemoveHostObject(vm, obj);
}

static void collect(CardinalVM* vm) {
	cardinalCollectGarbage(vm);
}

static void setGC(CardinalVM* vm) {
	cardinalEnableGC(vm, cardinalGetArgumentBool(vm, 1));
}

static void listStatistics(CardinalVM* vm) {
	int gcCurrSize, gcTotalDestr, gcTotalDet, gcNewObjects, gcNext, nbHosts;
	cardinalGetGCStatistics(vm, &gcCurrSize, &gcTotalDestr, &gcTotalDet, &gcNewObjects, &gcNext, &nbHosts);

	vm->printFunction("Garbage collector:\n");
	vm->printFunction(" current size:          %d\n", gcCurrSize);
	vm->printFunction(" total destroyed:       %d\n", gcTotalDestr);
	vm->printFunction(" total detected:        %d\n", gcTotalDet);
	vm->printFunction(" new objects:           %d\n", gcNewObjects);
	vm->printFunction(" start new cycle:       %d\n", gcNext);
	vm->printFunction(" number of host objects:%d\n", nbHosts);
}

///////////////////////////////////////////////////////////////////////////////////
//// CORE
///////////////////////////////////////////////////////////////////////////////////

static ObjClass* defineSingleClass(CardinalVM* vm, const char* name) {
	size_t length = strlen(name);
	ObjString* nameString = AS_STRING(cardinalNewString(vm, name, length));
	CARDINAL_PIN(vm, nameString);

	ObjClass* classObj = cardinalNewSingleClass(vm, 0, nameString);
	cardinalDefineVariable(vm, NULL, name, length, OBJ_VAL(classObj));

	CARDINAL_UNPIN(vm);
	return classObj;
}

void cardinalInitializeCore(CardinalVM* vm) {
	// Define the root Object class. This has to be done a little specially
	// because it has no superclass and an unusual metaclass (Class).
	vm->metatable.objectClass = defineSingleClass(vm, "Object");
	NATIVE(vm->metatable.objectClass, "!", object_not);
	NATIVE(vm->metatable.objectClass, "==(_)", object_eqeq);
	NATIVE(vm->metatable.objectClass, "!=(_)", object_bangeq);
	NATIVE(vm->metatable.objectClass, "new", object_new);
	NATIVE(vm->metatable.objectClass, "new()", object_new);
	NATIVE(vm->metatable.objectClass, "toString", object_toString);
	NATIVE(vm->metatable.objectClass, "type", object_type);
	NATIVE(vm->metatable.objectClass, "getMethod(_)", object_getMethod);
	NATIVE(vm->metatable.objectClass, "getAllMethods()", object_getAllMethods);
	NATIVE(vm->metatable.objectClass, "<instantiate>", object_instantiate);

#if CARDINAL_USE_MEMORY
	cardinalInitialiseManualMemoryManagement(vm);
#endif
	
	// Now we can define Class, which is a subclass of Object, but Object's
	// metaclass.
	vm->metatable.classClass = defineSingleClass(vm, "Class");

	// Now that Object and Class are defined, we can wire them up to each other.
	cardinalBindSuperclass(vm, vm->metatable.classClass, vm->metatable.objectClass);
	vm->metatable.objectClass->obj.classObj = vm->metatable.classClass;
	vm->metatable.classClass->obj.classObj = vm->metatable.classClass;

	// Define the methods specific to Class after wiring up its superclass to
	// prevent the inherited ones from overwriting them.
	NATIVE(vm->metatable.classClass, "<instantiate>", class_instantiate);
	NATIVE(vm->metatable.classClass, "name", class_name);
	NATIVE(vm->metatable.classClass, "bindMethod(_,_)", class_bindMethod);
	NATIVE(vm->metatable.classClass, "bindMethodStatic(_,_)", class_bindMethodStatic);
	NATIVE(vm->metatable.classClass, "tradeStaticMethod(_,_)", class_tradeStaticMethod);
	NATIVE(vm->metatable.classClass, "tradeMethod(_,_)", class_tradeMethod);
	NATIVE(vm->metatable.classClass->obj.classObj, "create(_)", class_newClass);
	
	// The core class diagram ends up looking like this, where single lines point
	// to a class's superclass, and double lines point to its metaclass:
	//
	//           .------------.    .========.
	//           |            |    ||      ||
	//           v            |    v       ||
	//     .---------.   .--------------.  ||
	//     | Object  |==>|    Class     |==='
	//     '---------'   '--------------'
	//          ^               ^
	//          |               |
	//     .---------.   .--------------.   -.
	//     |  Base   |==>|  Base.type   |    |
	//     '---------'   '--------------'    |
	//          ^               ^            | Hypothetical example classes
	//          |               |            |
	//     .---------.   .--------------.    |
	//     | Derived |==>| Derived.type |    |
	//     '---------'   '--------------'   -'
	
	// CORE
	cardinalInterpret(vm, "", libSource);


	// The rest of the classes can not be defined normally.
	
	// MODULES
	vm->metatable.moduleClass = AS_CLASS(cardinalFindVariable(vm, "Module"));
	NATIVE(vm->metatable.moduleClass, "importModule", module_import);
	NATIVE(vm->metatable.moduleClass, "[_]", module_subscript);
	NATIVE(vm->metatable.moduleClass, "[_]=(_)", module_subscriptSetter);
	NATIVE(vm->metatable.moduleClass, "toString", module_toString);
	NATIVE(vm->metatable.moduleClass, "count", module_count);
	NATIVE(vm->metatable.moduleClass->obj.classObj, "importModule(_)", moduleS_import);
	NATIVE(vm->metatable.moduleClass->obj.classObj, "saveModule(_,_)", moduleS_save);
	NATIVE(vm->metatable.moduleClass->obj.classObj, "current", module_current);
	
	// METHODS
	vm->metatable.methodClass = AS_CLASS(cardinalFindVariable(vm, "Method"));
	NATIVE(vm->metatable.methodClass->obj.classObj, "new()", method_new);
	NATIVE(vm->metatable.methodClass->obj.classObj, "new(_)", method_new1);
	NATIVE(vm->metatable.methodClass->obj.classObj, "new(_,_)", method_new2);
	NATIVE(vm->metatable.methodClass, "loadCaller(_)", method_loadCaller);
	NATIVE(vm->metatable.methodClass, "loadMethod(_)", method_load);
	NATIVE(vm->metatable.methodClass, "toString", method_toString);
	NATIVE(vm->metatable.methodClass, "arity", method_arity);
	NATIVE(vm->metatable.methodClass, "call()", method_call0);
	NATIVE(vm->metatable.methodClass, "call(_)", method_call1);
	NATIVE(vm->metatable.methodClass, "call(_,_)", method_call2);
	NATIVE(vm->metatable.methodClass, "call(_,_,_)", method_call3);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_)", method_call4);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_)", method_call5);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_)", method_call6);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_)", method_call7);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_)", method_call8);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_,_)", method_call9);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_,_,_)", method_call10);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_,_,_,_)", method_call11);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_,_,_,_,_)", method_call12);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_)", method_call13);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_)", method_call14);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", method_call15);
	NATIVE(vm->metatable.methodClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", method_call16);
	
	// BOOLEANS
	vm->metatable.boolClass = AS_CLASS(cardinalFindVariable(vm, "Bool"));
	NATIVE(vm->metatable.boolClass, "toString", bool_toString);
	NATIVE(vm->metatable.boolClass, "!", bool_not);

	// FUNCTIONS	
	vm->metatable.fnClass = AS_CLASS(cardinalFindVariable(vm, "Fn"));
	NATIVE(vm->metatable.fnClass->obj.classObj, "<instantiate>", fn_instantiate);
	NATIVE(vm->metatable.fnClass->obj.classObj, "new(_)", fn_new);

	NATIVE(vm->metatable.fnClass, "call()", fn_call0);
	NATIVE(vm->metatable.fnClass, "call(_)", fn_call1);
	NATIVE(vm->metatable.fnClass, "call(_,_)", fn_call2);
	NATIVE(vm->metatable.fnClass, "call(_,_,_)", fn_call3);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_)", fn_call4);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_)", fn_call5);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_)", fn_call6);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_)", fn_call7);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_)", fn_call8);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_,_)", fn_call9);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_,_,_)", fn_call10);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_,_,_,_)", fn_call11);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_)", fn_call12);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_)", fn_call13);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fn_call14);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fn_call15);
	NATIVE(vm->metatable.fnClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fn_call16);
	NATIVE(vm->metatable.fnClass, "toString", fn_toString);
	NATIVE(vm->metatable.fnClass, "arity", fn_arity);

	// NULL
	vm->metatable.nullClass = AS_CLASS(cardinalFindVariable(vm, "Null"));
	NATIVE(vm->metatable.nullClass, "!", null_not);
	NATIVE(vm->metatable.nullClass, "toString", null_toString);
	
	// NUMBER
	vm->metatable.numClass = AS_CLASS(cardinalFindVariable(vm, "Num"));
	NATIVE(vm->metatable.numClass->obj.classObj, "fromString(_)", num_fromString);
	NATIVE(vm->metatable.numClass->obj.classObj, "pi", num_pi);
	NATIVE(vm->metatable.numClass, "abs", num_abs);
	NATIVE(vm->metatable.numClass, "ceil", num_ceil);
	NATIVE(vm->metatable.numClass, "cos", num_cos);
	NATIVE(vm->metatable.numClass, "floor", num_floor);
	NATIVE(vm->metatable.numClass, "isNan", num_isNan);
	NATIVE(vm->metatable.numClass, "sin", num_sin);
	NATIVE(vm->metatable.numClass, "sqrt", num_sqrt);
	NATIVE(vm->metatable.numClass, "toString", num_toString);
	NATIVE(vm->metatable.numClass, "deg", num_deg);
	NATIVE(vm->metatable.numClass, "fraction", num_fraction);
	NATIVE(vm->metatable.numClass, "rad", num_rad);
	NATIVE(vm->metatable.numClass, "sign", num_sign);
	NATIVE(vm->metatable.numClass, "truncate", num_truncate);
	NATIVE(vm->metatable.numClass, "-", num_negate);
	NATIVE(vm->metatable.numClass, "-(_)", num_minus);
	NATIVE(vm->metatable.numClass, "+(_)", num_plus);
	NATIVE(vm->metatable.numClass, "*(_)", num_multiply);
	NATIVE(vm->metatable.numClass, "/(_)", num_divide);
	NATIVE(vm->metatable.numClass, "%(_)", num_mod);
	NATIVE(vm->metatable.numClass, "<(_)", num_lt);
	NATIVE(vm->metatable.numClass, ">(_)", num_gt);
	NATIVE(vm->metatable.numClass, "<=(_)", num_lte);
	NATIVE(vm->metatable.numClass, ">=(_)", num_gte);
	NATIVE(vm->metatable.numClass, "~", num_bitwiseNot);
	NATIVE(vm->metatable.numClass, "&(_)", num_bitwiseAnd);
	NATIVE(vm->metatable.numClass, "|(_)", num_bitwiseOr);
	NATIVE(vm->metatable.numClass, "^(_)", num_bitwiseXor);
	NATIVE(vm->metatable.numClass, "<<(_)", num_bitwiseLeftShift);
	NATIVE(vm->metatable.numClass, ">>(_)", num_bitwiseRightShift);
	NATIVE(vm->metatable.numClass, "..(_)", num_dotDot);
	NATIVE(vm->metatable.numClass, "...(_)", num_dotDotDot);
	NATIVE(vm->metatable.numClass, "acos", num_acos);
	NATIVE(vm->metatable.numClass, "asin", num_asin);
	NATIVE(vm->metatable.numClass, "atan", num_atan);
	NATIVE(vm->metatable.numClass, "tan", num_tan);
	NATIVE(vm->metatable.numClass, "atan(_)", num_atan2);
	
	// These are defined just so that 0 and -0 are equal, which is specified by
	// IEEE 754 even though they have different bit representations.
	NATIVE(vm->metatable.numClass, "==(_)", num_eqeq);
	NATIVE(vm->metatable.numClass, "!=(_)", num_bangeq);
	
	// FIBER
	vm->metatable.fiberClass = AS_CLASS(cardinalFindVariable(vm, "Fiber"));
	NATIVE(vm->metatable.fiberClass->obj.classObj, "<instantiate>", fiber_instantiate);
	NATIVE(vm->metatable.fiberClass->obj.classObj, "new(_)", fiber_new);
	NATIVE(vm->metatable.fiberClass->obj.classObj, "init new(_)", fiber_new);
	NATIVE(vm->metatable.fiberClass->obj.classObj, "abort(_)", fiber_abort);
	NATIVE(vm->metatable.fiberClass->obj.classObj, "throw(_)", fiber_throw);
	NATIVE(vm->metatable.fiberClass->obj.classObj, "yield()", fiber_yield);
	NATIVE(vm->metatable.fiberClass->obj.classObj, "yield(_)", fiber_yield1);
	NATIVE(vm->metatable.fiberClass->obj.classObj, "current", fiber_current);
	NATIVE(vm->metatable.fiberClass, "toString", fiber_toString);
	NATIVE(vm->metatable.fiberClass, "error", fiber_error);
	NATIVE(vm->metatable.fiberClass, "isDone", fiber_isDone);
	
	NATIVE(vm->metatable.fiberClass, "call()", fiber_call);
	NATIVE(vm->metatable.fiberClass, "call(_)", fiber_call1);
	NATIVE(vm->metatable.fiberClass, "call(_,_)", fiber_call2);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_)", fiber_call3);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_)", fiber_call4);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_)", fiber_call5);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_)", fiber_call6);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_)", fiber_call7);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_)", fiber_call8);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_,_)", fiber_call9);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_,_,_)", fiber_call10);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_,_,_,_)", fiber_call11);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_,_,_,_,_)", fiber_call12);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_call13);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_call14);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_call15);
	NATIVE(vm->metatable.fiberClass, "call(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_call16);

	NATIVE(vm->metatable.fiberClass, "run()", fiber_run);
	NATIVE(vm->metatable.fiberClass, "run(_)", fiber_run1);
	NATIVE(vm->metatable.fiberClass, "run(_,_)", fiber_run2);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_)", fiber_run3);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_)", fiber_run4);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_)", fiber_run5);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_)", fiber_run6);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_)", fiber_run7);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_)", fiber_run8);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_,_)", fiber_run9);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_,_,_)", fiber_run10);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_,_,_,_)", fiber_run11);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_,_,_,_,_)", fiber_run12);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_run13);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_run14);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_run15);
	NATIVE(vm->metatable.fiberClass, "run(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_run16);
	
	NATIVE(vm->metatable.fiberClass, "try()", fiber_try);
	NATIVE(vm->metatable.fiberClass, "try(_)", fiber_try1);
	NATIVE(vm->metatable.fiberClass, "try(_,_)", fiber_try2);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_)", fiber_try3);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_)", fiber_try4);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_)", fiber_try5);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_)", fiber_try6);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_)", fiber_try7);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_)", fiber_try8);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_,_)", fiber_try9);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_,_,_)", fiber_try10);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_,_,_,_)", fiber_try11);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_,_,_,_,_)", fiber_try12);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_try13);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_try14);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_try15);
	NATIVE(vm->metatable.fiberClass, "try(_,_,_,_,_,_,_,_,_,_,_,_,_,_,_,_)", fiber_try16);
	
	// STRING
	vm->metatable.stringClass = AS_CLASS(cardinalFindVariable(vm, "String"));
	NATIVE(vm->metatable.stringClass, "+(_)", string_plus);
	NATIVE(vm->metatable.stringClass, "[_]", string_subscript);
	NATIVE(vm->metatable.stringClass, "contains(_)", string_contains);
	NATIVE(vm->metatable.stringClass, "count", string_count);
	NATIVE(vm->metatable.stringClass, "endsWith(_)", string_endsWith);
	NATIVE(vm->metatable.stringClass, "indexOf(_)", string_indexOf);
	NATIVE(vm->metatable.stringClass, "iterate(_)", string_iterate);
	NATIVE(vm->metatable.stringClass, "iteratorValue(_)", string_iteratorValue);
	NATIVE(vm->metatable.stringClass, "startsWith(_)", string_startsWith);
	NATIVE(vm->metatable.stringClass, "toString", string_toString);
	
	NATIVE(vm->metatable.stringClass->obj.classObj, "fromCodePoint(_)", string_fromCodePoint);
	NATIVE(vm->metatable.stringClass, "byteAt(_)", string_byteAt);
	NATIVE(vm->metatable.stringClass, "codePointAt(_)", string_codePointAt);
	NATIVE(vm->metatable.stringClass, "iterateByte_(_)", string_iterateByte);
	
	// LIST
	vm->metatable.listClass = AS_CLASS(cardinalFindVariable(vm, "List")); 
	NATIVE(vm->metatable.listClass->obj.classObj, "<instantiate>", list_instantiate);
	NATIVE(vm->metatable.listClass->obj.classObj, "new()", list_instantiate);
	NATIVE(vm->metatable.listClass, "add(_)", list_add);
	NATIVE(vm->metatable.listClass, "head", list_head);
	NATIVE(vm->metatable.listClass, "tail", list_tail);
	NATIVE(vm->metatable.listClass, "last", list_last);
	NATIVE(vm->metatable.listClass, "init", list_init);
	NATIVE(vm->metatable.listClass, "conc(_)", list_conc);
	NATIVE(vm->metatable.listClass, "call()", list_call);
	NATIVE(vm->metatable.listClass, "clear()", list_clear);
	NATIVE(vm->metatable.listClass, "count", list_count);
	NATIVE(vm->metatable.listClass, "insert(_,_)", list_insert);
	NATIVE(vm->metatable.listClass, "iterate(_)", list_iterate);
	NATIVE(vm->metatable.listClass, "iteratorValue(_)", list_iteratorValue);
	NATIVE(vm->metatable.listClass, "removeAt(_)", list_removeAt);
	NATIVE(vm->metatable.listClass, "[_]", list_subscript);
	NATIVE(vm->metatable.listClass, "[_]=", list_subscriptSetter);

	// MAP
	vm->metatable.mapClass = AS_CLASS(cardinalFindVariable(vm, "Map"));
	NATIVE(vm->metatable.mapClass->obj.classObj, "<instantiate>", map_instantiate);
	NATIVE(vm->metatable.mapClass->obj.classObj, "new()", map_instantiate);
	NATIVE(vm->metatable.mapClass, "[_]", map_subscript);
	NATIVE(vm->metatable.mapClass, "[_]=(_)", map_subscriptSetter);
	NATIVE(vm->metatable.mapClass, "clear()", map_clear);
	NATIVE(vm->metatable.mapClass, "containsKey(_)", map_containsKey);
	NATIVE(vm->metatable.mapClass, "count", map_count);
	NATIVE(vm->metatable.mapClass, "remove(_)", map_remove);
	NATIVE(vm->metatable.mapClass, "iterate_(_)", map_iterate);
	NATIVE(vm->metatable.mapClass, "keyIteratorValue_(_)", map_keyIteratorValue);
	NATIVE(vm->metatable.mapClass, "valueIteratorValue_(_)", map_valueIteratorValue);

	// TABLE
	vm->metatable.tableClass = AS_CLASS(cardinalFindVariable(vm, "Table"));
	NATIVE(vm->metatable.tableClass->obj.classObj, "<instantiate>", table_instantiate);
	NATIVE(vm->metatable.tableClass->obj.classObj, "new()", table_new);
	NATIVE(vm->metatable.tableClass->obj.classObj, "new(_)", table_newSize);
	NATIVE(vm->metatable.tableClass, "toString", table_toString);
	NATIVE(vm->metatable.tableClass, "add(_,_)", table_add);
	NATIVE(vm->metatable.tableClass, "clear", table_clear);
	NATIVE(vm->metatable.tableClass, "count", table_count);
	NATIVE(vm->metatable.tableClass, "remove(_)", table_remove);	
	NATIVE(vm->metatable.tableClass, "[_]", table_subscript);
	NATIVE(vm->metatable.tableClass, "[_]=(_)", table_subscriptSetter);
	NATIVE(vm->metatable.tableClass, "printAll", table_printAll);
	NATIVE(vm->metatable.tableClass, "containsKey(_)", table_containsKey);	
	
	NATIVE(vm->metatable.tableClass, "iterate_(_)", table_iterate);
	NATIVE(vm->metatable.tableClass, "keyIteratorValue_(_)", table_keyIteratorValue);
	NATIVE(vm->metatable.tableClass, "valueIteratorValue_(_)", table_valueIteratorValue);
	NATIVE(vm->metatable.tableClass, "get(_)", table_get);
	
	// RANGE
	vm->metatable.rangeClass = AS_CLASS(cardinalFindVariable(vm, "Range"));
	NATIVE(vm->metatable.rangeClass, "from", range_from);
	NATIVE(vm->metatable.rangeClass, "to", range_to);
	NATIVE(vm->metatable.rangeClass, "min", range_min);
	NATIVE(vm->metatable.rangeClass, "max", range_max);
	NATIVE(vm->metatable.rangeClass, "isInclusive", range_isInclusive);
	NATIVE(vm->metatable.rangeClass, "iterate(_)", range_iterate);
	NATIVE(vm->metatable.rangeClass, "iteratorValue(_)", range_iteratorValue);
	NATIVE(vm->metatable.rangeClass, "toString", range_toString);
	
	// System
	cardinalDefineStaticMethod(vm, NULL, "System", "deassemble(_)", deassembleFunction);
	cardinalDefineStaticMethod(vm, NULL, "System", "run(_)", runCode);
	cardinalDefineStaticMethod(vm, NULL, "System", "run(_,_)", runCodeParam);
	cardinalDefineStaticMethod(vm, NULL, "System", "getHostObject(_)", getHostObject);
	cardinalDefineStaticMethod(vm, NULL, "System", "setHostObject(_,_)", setHostObject);
	cardinalDefineStaticMethod(vm, NULL, "System", "printGC()", listStatistics);
	cardinalDefineStaticMethod(vm, NULL, "System", "setGC(_)", setGC);
	cardinalDefineStaticMethod(vm, NULL, "System", "collect()", collect);

	// While bootstrapping the core types and running the core library, a number
	// string objects have been created, many of which were instantiated before
	// stringClass was stored in the VM. Some of them *must* be created first:
	// the ObjClass for string itself has a reference to the ObjString for its
	// name.
	//
	// These all currently a NULL classObj pointer, so go back and assign them
	// now that the string class is known.
	for (Obj* obj = vm->garbageCollector.first; obj != NULL; obj = obj->next) {
		if (obj->type == OBJ_STRING) obj->classObj = vm->metatable.stringClass;
	}
}
