#include "cardinal_debugger.h"

#include "cardinal_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

///////////////////////////////////////////////////////////////////////////////////
//// STRUCTS
///////////////////////////////////////////////////////////////////////////////////

// DEBUGGER
#define MAX_LINE_LENGTH 128

// Breakpoint
typedef struct BreakPoint BreakPoint;

/// Represents a breakpoint
struct BreakPoint {
	/// line number
	int line;
};

// Used for breakpoint buffer
DECLARE_BUFFER(BreakPoint, BreakPoint);
DEFINE_BUFFER(BreakPoint, BreakPoint);

/// Data used by the debugger
struct DebugData {
	/// Extra field for data embedded from outside the VM
	void* extra; 
	
	/// Current action
	DebugState action;
	
	/// Buffer of breakpoints
	BreakPointBuffer breakpoints;
};

///////////////////////////////////////////////////////////////////////////////////
//// HELPER FUNCTIONS FOR DEBUGGER
///////////////////////////////////////////////////////////////////////////////////

// Create a new debugger
DebugData* cardinalNewDebugger(CardinalVM* vm) {
	DebugData* ret = (DebugData*) cardinalReallocate(vm, NULL, 0, sizeof(DebugData));
	ret->action = STEP_INTO;
	ret->extra = NULL;
	
	cardinalBreakPointBufferInit(vm, &ret->breakpoints);
	
	return ret;
}

// Free the debugger
void cardinalFreeDebugger(CardinalVM* vm, DebugData* debugger) {
	cardinalBreakPointBufferClear(vm, &debugger->breakpoints);
	cardinalReallocate(vm, debugger, 0, 0);
}

// Add a breakpoint on line [line] in the debugger
void cardinalAddBreakPoint(CardinalVM* vm, DebugData* debugger, int line) {
	BreakPoint brpoint = { line };
	cardinalBreakPointBufferWrite(vm, &debugger->breakpoints, brpoint);
}

// Remove all breakpoints
void cardinalRemoveAllBreakPoints(CardinalVM* vm, DebugData* debugger) {
	cardinalBreakPointBufferClear(vm, &debugger->breakpoints);
}

// Remove a breakpoint on line [line]
void cardinalRemoveBreakPoint(CardinalVM* vm, DebugData* debugger, int line) {
	UNUSED(vm);
	for(int i=0; i<debugger->breakpoints.count; i++) {
		if (debugger->breakpoints.data[i].line == line)
			debugger->breakpoints.data[i].line = -1;
	}
}

// Check if the debugger has to break on line [line]
bool cardinalHasBreakPoint(CardinalVM* vm, DebugData* debugger, int line) {
	UNUSED(vm);
	for(int i=0; i<debugger->breakpoints.count; i++) {
		if (debugger->breakpoints.data[i].line == line)
			return true;
	}
	return false;
}

/// Set the sate of the debug data [debugger] to [state]
void cardinalSetDebugState(DebugData* debugger, DebugState state) {
	debugger->action = state;
}

// Get the state of the debugdata [debugger]
DebugState cardinalGetDebugState(DebugData* debugger) {
	return debugger->action;
}

// Set extra data in the debugger
void cardinalSetExtraDebugData(DebugData* debugger, void* data) {
	debugger->extra = data;
}

// Get extra data from the debugger
void* cardinalGetExtraDebugData(DebugData* debugger) {
	return debugger->extra;
}

///////////////////////////////////////////////////////////////////////////////////
//// HELPER FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////

static int getCurrentLine(CardinalVM* vm);
static Code getCurrentInstruction(CardinalVM* vm);
static ObjFn* getCurrentFunction(CardinalVM* vm);

static bool executeCommand(CardinalVM* vm, const char* line);
static void printHelp();
static void listData(CardinalVM* vm);
static void printLocation(CardinalVM* vm);
static void printStack(CardinalVM* vm);

static bool listDataCommand(CardinalVM* vm, const char* cmd);

static void listBreakPoints(CardinalVM* vm);
static void listLocalVariables(CardinalVM* vm);
static void listGlobalVariables(CardinalVM* vm);
static void listMemberProperties(CardinalVM* vm);
static void listStatistics(CardinalVM* vm);


///////////////////////////////////////////////////////////////////////////////////
//// DEFAULT CALLBACK
///////////////////////////////////////////////////////////////////////////////////

void defaultDebugCallBack(CardinalVM* vm) {
	// Continue to next breakpoint
	if (cardinalGetDebugState(vm->debugger) == CONTINUE) {
		// Check if there is a breakpoint
		if (!cardinalHasBreakPoint(vm, vm->debugger, getCurrentLine(vm))) return;
		else printf("\treached breakpoint on line %d\n", getCurrentLine(vm));
	}

	// Continue to next return statement
	if (cardinalGetDebugState(vm->debugger) == STEP_OUT || cardinalGetDebugState(vm->debugger) == STEP_OVER) {
		// Check we are at a return statement, if not, return
		if (getCurrentInstruction(vm) != CODE_RETURN) return;
	}

	// Now we can continue with the debugging	
	char line[MAX_LINE_LENGTH];
	bool check = false;
	
	while( !check) {
		printf("[dbg]> ");
		
		if (!fgets(line, MAX_LINE_LENGTH, stdin))
			break;
		
		check = executeCommand(vm, line);
	}
}

///////////////////////////////////////////////////////////////////////////////////
//// HELPER FUNCTIONS
///////////////////////////////////////////////////////////////////////////////////

static ObjFn* getCurrentFunction(CardinalVM* vm) {
	CallFrame* frame = &vm->fiber->frames[vm->fiber->numFrames - 1];
	ObjFn* fn;
	if (frame->fn->type == OBJ_FN) {
		fn = (ObjFn*)frame->fn;
	}
	else {
		fn = ((ObjClosure*)frame->fn)->fn;
	}
	return fn;
}

static int getCurrentLine(CardinalVM* vm) {
	CallFrame* frame = &vm->fiber->frames[vm->fiber->numFrames - 1];
	ObjFn* fn;
	if (frame->fn->type == OBJ_FN) {
		fn = (ObjFn*)frame->fn;
	}
	else {
		fn = ((ObjClosure*)frame->fn)->fn;
	}
	
	return fn->debug->sourceLines[frame->pc - fn->bytecode];
}


static Code getCurrentInstruction(CardinalVM* vm) {
	CallFrame* frame = &vm->fiber->frames[vm->fiber->numFrames - 1];
	ObjFn* fn;
	if (frame->fn->type == OBJ_FN) {
		fn = (ObjFn*)frame->fn;
	}
	else {
		fn = ((ObjClosure*)frame->fn)->fn;
	}
	
	return (Code) fn->bytecode[frame->pc - fn->bytecode];
}

static bool executeCommand(CardinalVM* vm, const char* line) {
	if (line[0] == '\n') return false;

	switch (line[0]) {
		case 'c':
			cardinalSetDebugState(vm->debugger, CONTINUE);
			break;

		case 's':
			cardinalSetDebugState(vm->debugger, STEP_INTO);
			break;

		case 'n': {
			// step over
			Code inst = getCurrentInstruction(vm);
			if ((inst >= CODE_CALL_0 && inst <= CODE_CALL_16) || (inst >= CODE_SUPER_0 && inst <= CODE_SUPER_16))
				cardinalSetDebugState(vm->debugger, STEP_OVER);
			else
				cardinalSetDebugState(vm->debugger, STEP_INTO);
			break;
		}
		case 'o':
			// step out
			cardinalSetDebugState(vm->debugger, STEP_OUT);
			break;

		case 'b': {
			// Set break point
			printf("On which line do you want to place a breakpoint: <line> ");
			char breakp[MAX_LINE_LENGTH];
			bool check = false;
			
			while( !check) {
				if (!fgets(breakp, MAX_LINE_LENGTH, stdin))
					break;
				
				char* end;
				int v = strtol(breakp, &end, 10);
					
				if (v != 0) {
					cardinalAddBreakPoint(vm, vm->debugger, v);
					check = true;
				}
			}
			
			printf("\n");
			// take more commands
			return false;
		}
		case 'r': {
			// Remove break point
			printf("Which breakpoint do you want to remove: <all | line number> ");
			char breakp[MAX_LINE_LENGTH];
			bool check = false;
			
			while( !check) {
				if (!fgets(breakp, MAX_LINE_LENGTH, stdin))
					break;
				
				if (strcmp(breakp, "all") == 0) {
					cardinalRemoveAllBreakPoints(vm, vm->debugger);
					check = true;
				}
				else {
					char* end;
					int v = strtol(breakp, &end, 10);
					
					if (v != 0) {
						cardinalRemoveBreakPoint(vm, vm->debugger, v);
						check = true;
					}
				}
			}
			// take more commands
			return false;
		}
		case 'l': {
			// List something
			listData(vm);
			// take more commands
			return false;
		}

		case 'h':
			printHelp();
			// take more commands
			return false;

		case 'w': {
			// Where am I?
			printLocation(vm);
			// take more commands
			return false;
		}
		case 'a':
			vm->fiber = NULL;
			break;

		default:
			printf("Unknown command\n");
			// take more commands
			return false;
	}

	// Continue execution
	return true;
}

static void printLocation(CardinalVM* vm) {
	ObjInstance* old = vm->fiber->error;
	vm->fiber->error = cardinalThrowException(vm, AS_STRING(cardinalNewString(vm, "debugger", 8)));
	cardinalDebugPrintStackTrace(vm, vm->fiber);
	vm->fiber->error = old;
}

static void printHelp() {
	printf("%s%s%s%s%s%s%s%s%s%s","c - Continue\n",
			"s - Step into\n",
			"n - Next step\n",
			"o - Step out\n",
			"b - Set break point\n",
			"l - List various things\n",
			"r - Remove break point\n",
			"w - Where am I?\n",
			"a - Abort execution\n",
			"h - Print this help text\n");
}

static void listData(CardinalVM* vm) {
	char line[MAX_LINE_LENGTH];
	bool check = false;
	
	printf("What do you want to list: \n%s%s%s%s%s%s",
						"b - breakpoints\n",
						"v - local variables\n",
						"m - member properties\n",
						"g - global variables\n",
						"s - statistics\n",
						"f - stack\n");

	while( !check) {
		if (!fgets(line, MAX_LINE_LENGTH, stdin))
			break;
		
		check = listDataCommand(vm, line);
	}
}

static bool listDataCommand(CardinalVM* vm, const char* cmd) {
	if (cmd[0] == 'b') {
		listBreakPoints(vm);
	}
	else if (cmd[0] == 'v') {
		listLocalVariables(vm);
	}
	else if (cmd[0] == 'g') {
		listGlobalVariables(vm);
	}
	else if (cmd[0] == 'm') {
		listMemberProperties(vm);
	}
	else if (cmd[0] == 's') {
		listStatistics(vm);
	}
	else if (cmd[0] == 'f') {
		printStack(vm);
	}
	else {
		printf("%s%s%s%s%s%s%s", "Unknown list option, expected one of:\n",
				"b - breakpoints\n",
				"v - local variables\n",
				"m - member properties\n",
				"g - global variables\n",
				"s - statistics\n",
				"f - stack\n");
		return false;
	}
	return true;
}

static void listBreakPoints(CardinalVM* vm) {
	for(int i=0; i<vm->debugger->breakpoints.count; i++) {
		printf("\tBP: %d\n", vm->debugger->breakpoints.data[i].line);
	}
}

static void listLocalVariables(CardinalVM* vm) {
	ObjFn* fn = getCurrentFunction(vm);
	ObjFiber* fiber = vm->fiber;
	int top = fiber->stacktop - fiber->stack - 2;
	for(int ind = 0; ind < top; ind++) {
		printf("Variable '%.*s': ", fn->debug->locals.data[ind].length, fn->debug->locals.data[ind].buffer);
		cardinalPrintValue(fiber->stack[ind+1]);
		printf("\n");
	}
}

static void listGlobalVariables(CardinalVM* vm) {
	ObjFn* fn = getCurrentFunction(vm);
	
	printf("Listing all global variables: \n");
	for(int i=0; i<fn->module->variables.count; i++) {
		printf("\t%.*s: ", fn->module->variableNames.data[i].length, fn->module->variableNames.data[i].buffer);
		cardinalPrintValue(fn->module->variables.data[i]);
		printf("\n");
	}
}

static void printStack(CardinalVM* vm) {
	cardinalDebugPrintStack(vm, vm->fiber);
}
 
static void listMemberProperties(CardinalVM* vm) {
	UNUSED(vm);
	// find the this member
	ObjFiber* top = vm->fiber;
	
	if (!IS_INSTANCE(top->stack[0])) {
		printf("Cant list members of non-instance \n");
		return;		
	}
		
	// Find al of it's members
	ObjInstance* inst = AS_INSTANCE(top->stack[0]);
	ObjClass* cls = cardinalGetClass(vm, top->stack[0]);
	printf("Instance of class: '%s'\n", cls->name->value);
	for(int i=0; i<cls->numFields; i++) {
		printf("Field '%d' ", i);
		cardinalPrintValue(inst->fields[i]);
		printf("\n");
	}
}

static void listStatistics(CardinalVM* vm) {
	int gcCurrSize, gcTotalDestr, gcTotalDet, gcNewObjects, gcNext, nbHosts;
	cardinalGetGCStatistics(vm, &gcCurrSize, &gcTotalDestr, &gcTotalDet, &gcNewObjects, &gcNext, &nbHosts);

	printf("Garbage collector:\n");
	printf(" current size:          %d\n", gcCurrSize);
	printf(" total destroyed:       %d\n", gcTotalDestr);
	printf(" total detected:        %d\n", gcTotalDet);
	printf(" new objects:           %d\n", gcNewObjects);
	printf(" start new cycle:       %d\n", gcNext);
	printf(" number of host objects:%d\n", nbHosts);
}