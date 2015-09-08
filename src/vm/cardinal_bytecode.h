#ifndef cardinal_bytecode_h
#define cardinal_bytecode_h

#include "cardinal.h"
#include "cardinal_value.h"

// This module defines the bytecode compiler for Cardinal. It takes a string of bytecode
// and parses, and compiles it. Cardinal uses a single-pass compiler for both regular source code
// as bytecode.
//
// Loading from bytecode is considerably faster than loading regular source code
// There is almost a one-on one mapping between the bytecode string and the actual
// running program
//
// There are some restrictions.
// All debug information is lost in the bytecode file
// This makes debugging a bytecode string impossible
// The bytecode string is also version dependent
// Since different versions from Cardinal may have different formations for the
// bytecode. 
//
// To test for this the bytecode string is constructed as follows:
//		- Starts with identifier telling the VM this is a bytecode string
//		- Next is the VM version
//		- Next is the string size
//		- And then finally the actual bytecode follows

// Compiles [source], a string of Cardinal byte code located in [module], to an
// [ObjFn] that will execute that code when invoked.
ObjFn* cardinalCompileFromByteCode(CardinalVM* vm, ObjModule* module,
                   const char* sourcePath, const char* source);
				
// Creates a string of bytecode [ObjString] located in [module], from
// the VM [vm]
ObjString* cardinalCompileToByteCode(CardinalVM* vm, ObjModule* module);

#endif
