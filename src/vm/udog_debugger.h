#ifndef udog_debugger_h
#define udog_debugger_h

#include "udog.h"
#include "udog_value.h"

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
void defaultDebugCallBack(UDogVM* vm);

///////////////////////////////////////////////////////////////////////////////////
//// HELPER FUNCTIONS FOR DEBUGGER
///////////////////////////////////////////////////////////////////////////////////

// Create a new debugger
DebugData* udogNewDebugger(UDogVM* vm);

// Free the debugger
void udogFreeDebugger(UDogVM* vm, DebugData* debugger);

// Add a breakpoint on line [line] in the debugger
void udogAddBreakPoint(UDogVM* vm, DebugData* debugger, int line);

// Remove all breakpoints
void udogRemoveAllBreakPoints(UDogVM* vm, DebugData* debugger);

// Remove a breakpoint on line [line]
void udogRemoveBreakPoint(UDogVM* vm, DebugData* debugger, int line);

// Check if the debugger has to break on line [line]
bool udogHasBreakPoint(UDogVM* vm, DebugData* debugger, int line);

/// Set the sate of the debug data [debugger] to [state]
void udogSetDebugState(DebugData* debugger, DebugState state);

// Get the state of the debugdata [debugger]
DebugState udogGetDebugState(DebugData* debugger);

// Set extra data in the debugger
void udogSetExtraDebugData(DebugData* debugger, void* data);

// Get extra data from the debugger
void* udogGetExtraDebugData(DebugData* debugger);



#endif
