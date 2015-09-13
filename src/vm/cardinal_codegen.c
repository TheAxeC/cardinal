#include "cardinal_codegen.h"

#if CARDINAL_USE_CODEGEN

#include <string.h>
#include <stdio.h>

#include "cardinal_config.h"
#include "cardinal_value.h"
#include "cardinal_debug.h"

#include "cardinal_native.h"

///////////////////////////////////////////////////////////////////////////////////
//// FUNCTION
///////////////////////////////////////////////////////////////////////////////////

DEF_NATIVE(fn_create)
	ObjModule* module = AS_MODULE(args[1]);
	ObjList* constants = AS_LIST(args[2]);
	int numUpvalues = (int) AS_NUM(args[3]);
	int arity = (int) AS_NUM(args[4]);
	uint8_t* bytecode = (uint8_t*) AS_POINTER(args[5]);
	int size = (int) AS_NUM(args[6]);
	ObjString* sourcePath = AS_STRING(args[7]);
	ObjString* name = AS_STRING(args[8]);
	ObjList* lineData = AS_LIST(args[9]);
	ObjList* localList = AS_LIST(args[10]);
	ObjList* linesList = AS_LIST(args[11]);
	
	int* linesArray = new int[size];
	SymbolTable locals;
	SymbolTable lines;
	cardinalSymbolTableInit(vm, &locals);
	cardinalSymbolTableInit(vm, &lines);
	
	for(int i=0; i<size; i++) {
		linesArray[i] = AS_NUM(lineData->elements[i]);
		cardinalSymbolTableAdd(vm, &locals,
				AS_STRING(localList->elements[i])->value, 
				AS_STRING(localList->elements[i])->length);
		cardinalSymbolTableAdd(vm, &lines, 
				AS_STRING(linesList->elements[i])->value, 
				AS_STRING(linesList->elements[i])->length);
	}
	
	FnDebug* debug = cardinalNewDebug(vm, sourcePath, name->value, name->length, linesArray, locals, lines);
	ObjFn* fn = cardinalNewFunction(vm, module, constants->elements, constants->count, numUpvalues,
				arity, bytecode, size, debug);
	
	RETURN_OBJ(fn);
END_NATIVE

///////////////////////////////////////////////////////////////////////////////////
//// CODE GENERATION
///////////////////////////////////////////////////////////////////////////////////

// The method binds the Code generator to the VM 
// This can be used to generate code during the running of a program
// It will be used to compile and generate bytecode
void cardinalInitializeCodeGenerator(CardinalVM* vm) {
	NATIVE(vm->metatable.fnClass->obj.classObj, "create(_,_,_,_,_,_,_,_,_,_,_)", fn_create);
}

#endif