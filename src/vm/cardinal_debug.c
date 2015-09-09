#include <stdio.h>

#include "cardinal_debug.h"
#include "cardinal_value.h"

static int debugPrintInstruction(CardinalVM* vm, ObjFn* fn, int i, int* lastLine);

void cardinalDebugPrintStackTrace(CardinalVM* vm, ObjFiber* fiber) {
	UNUSED(vm);
	fprintf(stderr, "%s\n", cardinalGetErrorString(vm, fiber)->value);

	for (int i = fiber->numFrames - 1; i >= 0; i--) {
		CallFrame* frame = &fiber->frames[i];
		ObjFn* fn;
		if (frame->fn->type == OBJ_FN) {
			fn = (ObjFn*)frame->fn;
		}
		else {
			fn = ((ObjClosure*)frame->fn)->fn;
		}
		
		// Built-in libraries have no source path and are explicitly omitted from
		// stack traces since we don't want to highlight to a user the
		// implementation detail of what part of a core library is implemented in
		// C and what is in Cardinal.
		if (fn->debug->sourcePath == NULL ||
		        fn->debug->sourcePath->length == 0) {
			continue;
		}

		// - 1 because IP has advanced past the instruction that it just executed.
		int line = fn->debug->sourceLines[frame->pc - fn->bytecode - 1];
		fprintf(stderr, "[%s line %d] in %s\n", fn->debug->sourcePath->value, line, fn->debug->name);
	}
}

ObjString* cardinalDebugGetStackTrace(CardinalVM* vm, ObjFiber* fiber) {
	UNUSED(vm);
	ObjString* str = cardinalStringFormat(vm, "%s\n", cardinalGetErrorString(vm, fiber)->value);
	CARDINAL_PIN(vm, str);
	for (int i = fiber->numFrames - 1; i >= 0; i--) {
		CallFrame* frame = &fiber->frames[i];
		ObjFn* fn;
		if (frame->fn->type == OBJ_FN) {
			fn = (ObjFn*)frame->fn;
		}
		else {
			fn = ((ObjClosure*)frame->fn)->fn;
		}
		
		// Built-in libraries have no source path and are explicitly omitted from
		// stack traces since we don't want to highlight to a user the
		// implementation detail of what part of a core library is implemented in
		// C and what is in Cardinal.
		if (fn->debug->sourcePath == NULL ||
		        fn->debug->sourcePath->length == 0) {
			continue;
		}

		// - 1 because IP has advanced past the instruction that it just executed.
		int line = fn->debug->sourceLines[frame->pc - fn->bytecode - 1];
		ObjString* insert = cardinalStringFormat(vm, "[%s line %d] in %s\n", fn->debug->sourcePath->value, line, fn->debug->name);
		CARDINAL_PIN(vm, str);
		ObjString* newstr = cardinalStringConcat(vm, str->value, -1, insert->value, -1);
		CARDINAL_UNPIN(vm);
		CARDINAL_UNPIN(vm);
		CARDINAL_PIN(vm, newstr);
		str = newstr;
	}
	
	CARDINAL_UNPIN(vm);
	return str;
}

int cardinalDebugPrintInstruction(CardinalVM* vm, ObjFn* fn, int i) {
	return debugPrintInstruction(vm, fn, i, NULL);
}

void cardinalDebugPrintCode(CardinalVM* vm, ObjFn* fn) {
	printf("%s: %s\n", fn->debug->sourcePath->value, fn->debug->name);

	int i = 0;
	int lastLine = -1;
	for (;;) {
		int offset = debugPrintInstruction(vm, fn, i, &lastLine);
		if (offset == -1) break;
		i += offset;
	}

	printf("\n");
}

void cardinalDebugPrintStack(CardinalVM* vm, ObjFiber* fiber) {
	UNUSED(vm);
	printf("(fiber %p) ", fiber);
	for (Value* slot = fiber->stack; slot < fiber->stacktop; slot++) {
		cardinalPrintValue(*slot);
		printf(" | ");
	}
	printf("\n");
}

static int debugPrintInstruction(CardinalVM* vm, ObjFn* fn, int i, int* lastLine) {
	int start = i;
	uint8_t* bytecode = fn->bytecode;
	Code code = (Code) bytecode[i];

	int line = fn->debug->sourceLines[i];
	if (lastLine == NULL || *lastLine != line) {
		printf("%4d:", line);
		if (lastLine != NULL) *lastLine = line;
	}
	else {
		printf("     ");
	}

	printf(" %04d  ", i++);

#define READ_BYTE() (bytecode[i++])
#define READ_SHORT() (i += 2, (bytecode[i - 2] << 8) | bytecode[i - 1])
#define READ_INT()  (i += 4, ((int) bytecode[i - 4] << 24) | ((int) bytecode[i - 3] << 16) | (bytecode[i - 2] << 8) | bytecode[i - 1])
#define READ_LONG() (i += 8, ((int) bytecode[i - 8] << 56) | ((int) bytecode[i - 7] << 48) | ((int) bytecode[i - 6] << 40) | ((int) bytecode[i - 5] << 32) | ((int) bytecode[i - 4] << 24) | ((int) bytecode[i - 3] << 16) | (bytecode[i - 2] << 8) | bytecode[i - 1])

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
#if METHOD_BYTE == 8
	#define READ_METHOD() READ_LONG()
#elif METHOD_BYTE == 4
	#define READ_METHOD() READ_INT()
#elif METHOD_BYTE == 2
	#define READ_METHOD() READ_SHORT()
#else
	#error Method byte instruction size not set
#endif


#define LOCAL_INSTRUCTION(name) \
	printf("%-16s %5d\n", name, READ_LOCAL()); \
	break; \
	
#define UPVALUE_INSTRUCTION(name) \
	printf("%-16s %5d\n", name, READ_UPVALUE()); \
	break; \
	
#define FIELD_INSTRUCTION(name) \
	printf("%-16s %5d\n", name, READ_FIELD()); \
	break; \
	 
	switch (code) {
		case CODE_CONSTANT: {
			int constant = READ_CONSTANT();
			printf("%-16s %5d '", "CONSTANT", constant);
			cardinalPrintValue(fn->constants[constant]);
			printf("'\n");
			break;
		}
		case CODE_EMPTY:  printf("EMPTY\n"); break;
		case CODE_NULL:  printf("NULL\n"); break;
		case CODE_FALSE: printf("FALSE\n"); break;
		case CODE_TRUE:  printf("TRUE\n"); break;

		case CODE_LOAD_LOCAL_0: printf("LOAD_LOCAL_0\n"); break;
		case CODE_LOAD_LOCAL_1: printf("LOAD_LOCAL_1\n"); break;
		case CODE_LOAD_LOCAL_2: printf("LOAD_LOCAL_2\n"); break;
		case CODE_LOAD_LOCAL_3: printf("LOAD_LOCAL_3\n"); break;
		case CODE_LOAD_LOCAL_4: printf("LOAD_LOCAL_4\n"); break;
		case CODE_LOAD_LOCAL_5: printf("LOAD_LOCAL_5\n"); break;
		case CODE_LOAD_LOCAL_6: printf("LOAD_LOCAL_6\n"); break;
		case CODE_LOAD_LOCAL_7: printf("LOAD_LOCAL_7\n"); break;
		case CODE_LOAD_LOCAL_8: printf("LOAD_LOCAL_8\n"); break;

		case CODE_LOAD_LOCAL: LOCAL_INSTRUCTION("LOAD_LOCAL");
		case CODE_STORE_LOCAL: LOCAL_INSTRUCTION("STORE_LOCAL");
		case CODE_LOAD_UPVALUE: UPVALUE_INSTRUCTION("LOAD_UPVALUE");
		case CODE_STORE_UPVALUE: UPVALUE_INSTRUCTION("STORE_UPVALUE");

		case CODE_LOAD_MODULE_VAR: {
			int slot = READ_GLOBAL();
			printf("%-16s %5d '%s'\n", "LOAD_MODULE_VAR", slot,
			       fn->module->variableNames.data[slot].buffer);
			break;
		}
		
		case CODE_STORE_MODULE_VAR: {
			int slot = READ_GLOBAL();
			printf("%-16s %5d '%s'\n", "STORE_MODULE_VAR", slot,
			       fn->module->variableNames.data[slot].buffer);
			break;
		}

		case CODE_LOAD_FIELD_THIS: FIELD_INSTRUCTION("LOAD_FIELD_THIS");
		case CODE_STORE_FIELD_THIS: FIELD_INSTRUCTION("STORE_FIELD_THIS");
		case CODE_LOAD_FIELD: FIELD_INSTRUCTION("LOAD_FIELD");
		case CODE_STORE_FIELD: FIELD_INSTRUCTION("STORE_FIELD");

		case CODE_POP: printf("POP\n"); break;
		case CODE_DUP: printf("DUP\n"); break;

		case CODE_CALL_0:
		case CODE_CALL_1:
		case CODE_CALL_2:
		case CODE_CALL_3:
		case CODE_CALL_4:
		case CODE_CALL_5:
		case CODE_CALL_6:
		case CODE_CALL_7:
		case CODE_CALL_8:
		case CODE_CALL_9:
		case CODE_CALL_10:
		case CODE_CALL_11:
		case CODE_CALL_12:
		case CODE_CALL_13:
		case CODE_CALL_14:
		case CODE_CALL_15:
		case CODE_CALL_16: {
			int numArgs = bytecode[i - 1] - CODE_CALL_0;
			int symbol = READ_METHOD();
			printf("CALL_%-11d %5d '%s'\n", numArgs, symbol,
			       vm->methodNames.data[symbol].buffer);
			break;
		}

		case CODE_SUPER_0:
		case CODE_SUPER_1:
		case CODE_SUPER_2:
		case CODE_SUPER_3:
		case CODE_SUPER_4:
		case CODE_SUPER_5:
		case CODE_SUPER_6:
		case CODE_SUPER_7:
		case CODE_SUPER_8:
		case CODE_SUPER_9:
		case CODE_SUPER_10:
		case CODE_SUPER_11:
		case CODE_SUPER_12:
		case CODE_SUPER_13:
		case CODE_SUPER_14:
		case CODE_SUPER_15:
		case CODE_SUPER_16: {
			int numArgs = bytecode[i - 1] - CODE_SUPER_0;
			int symbol = READ_METHOD();
			int constant = READ_CONSTANT();
			printf("SUPER_%-10d %5d '%d.%s'\n", numArgs, symbol,constant,
			       vm->methodNames.data[symbol].buffer);
			break;
		}
		case CODE_JUMP: {
			int offset = READ_OFFSET();
			printf("%-16s %5d to %d\n", "JUMP", offset, i + offset);
			break;
		}

		case CODE_LOOP: {
			int offset = READ_OFFSET();
			printf("%-16s %5d to %d\n", "LOOP", offset, i - offset);
			break;
		}

		case CODE_JUMP_IF: {
			int offset = READ_OFFSET();
			printf("%-16s %5d to %d\n", "JUMP_IF", offset, i + offset);
			break;
		}

		case CODE_AND: {
			int offset = READ_OFFSET();
			printf("%-16s %5d to %d\n", "AND", offset, i + offset);
			break;
		}

		case CODE_OR: {
			int offset = READ_OFFSET();
			printf("%-16s %5d to %d\n", "OR", offset, i + offset);
			break;
		}

		case CODE_IS:            printf("CODE_IS\n"); break;
		case CODE_CLOSE_UPVALUE: printf("CLOSE_UPVALUE\n"); break;
		case CODE_RETURN:        printf("CODE_RETURN\n"); break;

		case CODE_CLOSURE: {
			int constant = READ_CONSTANT();
			printf("%-16s %5d ", "CLOSURE", constant);
			cardinalPrintValue(fn->constants[constant]);
			printf(" ");
			ObjFn* loadedFn = AS_FN(fn->constants[constant]);
			for (int j = 0; j < loadedFn->numUpvalues; j++) {
				int isLocal = READ_BOOL();
				int index = READ_LOCAL();
				if (j > 0) printf(", ");
				printf("%s %d", isLocal ? "local" : "upvalue", index);
			}
			printf("\n");
			break;
		}
		
		case CODE_CONSTRUCT:	printf("CODE_CONSTRUCT\n"); break;

		case CODE_CLASS: {
			int numFields = READ_FIELD();
			int constants = READ_CONSTANT();
			printf("%-16s %5d fields and %d superclasses\n", "CLASS", numFields, constants);
			break;
		}

		case CODE_METHOD_INSTANCE: {
			int symbol = READ_METHOD();
			printf("%-16s %5d '%s'\n", "METHOD_INSTANCE", symbol,
			       vm->methodNames.data[symbol].buffer);
			break;
		}

		case CODE_METHOD_STATIC: {
			int symbol = READ_METHOD();
			printf("%-16s %5d '%s'\n", "METHOD_STATIC", symbol,
			       vm->methodNames.data[symbol].buffer);
			break;
		}
		
		case CODE_LOAD_MODULE: {
			int constant = READ_CONSTANT();
			printf("%-16s %5d '", "LOAD_MODULE", constant);
			cardinalPrintValue(fn->constants[constant]);
			printf("'\n");
			break;
		}
		
		case CODE_IMPORT_VARIABLE: {
			int module = READ_CONSTANT();
			int variable = READ_CONSTANT();
			printf("%-16s %5d '", "IMPORT_VARIABLE", module);
			cardinalPrintValue(fn->constants[module]);
			printf("' '");
			cardinalPrintValue(fn->constants[variable]);
			printf("'\n");
			break;
		}
		
		case CODE_MODULE: {
			printf("MODULE\n");
			break;
		}
		
		case CODE_END:
			printf("CODE_END\n");
			break;
		case CODE_BREAK:
			printf("CODE_BREAK\n");
			break;
		default:
			printf("UKNOWN! [%d]\n", bytecode[i - 1]);
			break;
	}

	// Return how many bytes this instruction takes, or -1 if it's an END.
	if (code == CODE_END) return -1;
	return i - start;
}


void checkDebugger(CardinalVM* vm) {
	CallFrame* frame = &vm->fiber->frames[vm->fiber->numFrames - 1];
	ObjFn* fn;
	if (frame->fn->type == OBJ_FN) {
		fn = (ObjFn*)frame->fn;
	}
	else {
		fn = ((ObjClosure*)frame->fn)->fn;
	}

	// Built-in libraries have no source path and are explicitly omitted from
	// stack traces since we don't want to highlight to a user the
	// implementation detail of what part of a core library is implemented in
	// C and what is in Cardinal.
	if (fn->debug->sourcePath == NULL ||
			fn->debug->sourcePath->length == 0) {
		return;
	}
	
	Code instruction = (Code) fn->bytecode[frame->pc - fn->bytecode];
	switch (instruction) {
		case CODE_EMPTY:
		case CODE_NULL:
			break;
		case CODE_CONSTANT:
		case CODE_FALSE:
		case CODE_TRUE:
			// Check the value of the local variable
			vm->callBackFunction(vm);
			break;	
		case CODE_LOAD_LOCAL_0:
		case CODE_LOAD_LOCAL_1:
		case CODE_LOAD_LOCAL_2:
		case CODE_LOAD_LOCAL_3:
		case CODE_LOAD_LOCAL_4:
		case CODE_LOAD_LOCAL_5:
		case CODE_LOAD_LOCAL_6:
		case CODE_LOAD_LOCAL_7:
		case CODE_LOAD_LOCAL_8:
		case CODE_LOAD_UPVALUE:
		case CODE_LOAD_LOCAL:
			break;
		case CODE_STORE_LOCAL: 
			// Check the value of the local variable
			vm->callBackFunction(vm);
			break;
		case CODE_POP:
		case CODE_DUP:
		case CODE_LOAD_MODULE_VAR:
			break;
		case CODE_STORE_UPVALUE:
		case CODE_STORE_MODULE_VAR: {
			// Store global variable
			vm->callBackFunction(vm);
			break;
		}

		case CODE_LOAD_FIELD_THIS:
			break;
		case CODE_STORE_FIELD_THIS: {
			// Storing a field
			vm->callBackFunction(vm);
			break;
		}
		case CODE_LOAD_FIELD:
			break;
		case CODE_STORE_FIELD: {
			// Storing a field
			vm->callBackFunction(vm);
			break;
		}
		case CODE_CALL_0:
		case CODE_CALL_1:
		case CODE_CALL_2:
		case CODE_CALL_3:
		case CODE_CALL_4:
		case CODE_CALL_5:
		case CODE_CALL_6:
		case CODE_CALL_7:
		case CODE_CALL_8:
		case CODE_CALL_9:
		case CODE_CALL_10:
		case CODE_CALL_11:
		case CODE_CALL_12:
		case CODE_CALL_13:
		case CODE_CALL_14:
		case CODE_CALL_15:
		case CODE_CALL_16:
		case CODE_SUPER_0:
		case CODE_SUPER_1:
		case CODE_SUPER_2:
		case CODE_SUPER_3:
		case CODE_SUPER_4:
		case CODE_SUPER_5:
		case CODE_SUPER_6:
		case CODE_SUPER_7:
		case CODE_SUPER_8:
		case CODE_SUPER_9:
		case CODE_SUPER_10:
		case CODE_SUPER_11:
		case CODE_SUPER_12:
		case CODE_SUPER_13:
		case CODE_SUPER_14:
		case CODE_SUPER_15:
		case CODE_SUPER_16: {
			// Perhaps print which function you are calling
			vm->callBackFunction(vm);
			break;
		}
		case CODE_LOOP: {
			// We're at a loop
			vm->callBackFunction(vm);
			break;
		}

		case CODE_JUMP_IF: {
			// We're at an if statement
			vm->callBackFunction(vm);
			break;
		}
		case CODE_JUMP:
		case CODE_AND:
		case CODE_OR:
		case CODE_IS:
		case CODE_CLOSE_UPVALUE:
		case CODE_CLOSURE:
			break;
		case CODE_RETURN: { 
			// Returning from a function
			vm->callBackFunction(vm);
			break;
		}


		case CODE_CLASS: {
			// Creation of a class
			vm->callBackFunction(vm);
			break;
		}
		case CODE_MODULE: {
			// We're at an import statement
			vm->callBackFunction(vm);
			break;
		}
		case CODE_METHOD_INSTANCE:
		case CODE_METHOD_STATIC:
			// Binding of a method
			
			vm->callBackFunction(vm);
			break;
		case CODE_LOAD_MODULE:
		case CODE_IMPORT_VARIABLE:
		case CODE_END:
			break;
		case CODE_BREAK:
			break;
		default:
			UNREACHABLE("debugger");
			break;
	}
}