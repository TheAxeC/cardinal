#ifndef cardinal_debugger_h
#define cardinal_debugger_h

#include "cardinal.h"
#include "cardinal_value.h"

// DebugState
typedef enum DebugState {
	CONTINUE,  // continue until next break point
	STEP_INTO, // stop at next instruction
	STEP_OVER, // stop at next instruction, skipping called functions
	STEP_OUT,   // run until returning from current function
	DEFAULT
} DebugState;

// Debug data
typedef struct DebugData DebugData;

// Default callback function for the debugger
void defaultDebugCallBack(CardinalVM* vm);

///////////////////////////////////////////////////////////////////////////////////
//// HELPER FUNCTIONS FOR DEBUGGER
///////////////////////////////////////////////////////////////////////////////////

// Create a new debugger
DebugData* cardinalNewDebugger(CardinalVM* vm);

// Free the debugger
void cardinalFreeDebugger(CardinalVM* vm, DebugData* debugger);

// Add a breakpoint on line [line] in the debugger
void cardinalAddBreakPoint(CardinalVM* vm, DebugData* debugger, int line);

// Remove all breakpoints
void cardinalRemoveAllBreakPoints(CardinalVM* vm, DebugData* debugger);

// Remove a breakpoint on line [line]
void cardinalRemoveBreakPoint(CardinalVM* vm, DebugData* debugger, int line);

// Check if the debugger has to break on line [line]
bool cardinalHasBreakPoint(CardinalVM* vm, DebugData* debugger, int line);

/// Set the sate of the debug data [debugger] to [state]
void cardinalSetDebugState(DebugData* debugger, DebugState state);

// Get the state of the debugdata [debugger]
DebugState cardinalGetDebugState(DebugData* debugger);

// Set extra data in the debugger
void cardinalSetExtraDebugData(DebugData* debugger, void* data);

// Get extra data from the debugger
void* cardinalGetExtraDebugData(DebugData* debugger);



#endif
