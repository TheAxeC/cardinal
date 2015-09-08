#include "udog_bytecode.h"

// This module defines the bytecode compiler for UDog. It takes a string of bytecode
// and parses, and compiles it. UDog uses a single-pass compiler for both regular source code
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
// Since different versions from UDog may have different formations for the
// bytecode. 
//
// To test for this the bytecode string is constructed as follows:
//		- Starts with identifier telling the VM this is a bytecode string
//		- Next is the VM version
//		- Next is the string size
//		- And then finally the actual bytecode follows

// Compiles [source], a string of UDog byte code located in [module], to an
// [ObjFn] that will execute that code when invoked.
ObjFn* udogCompileFromByteCode(UDogVM* vm, ObjModule* module,
                   const char* sourcePath, const char* source) {
					   UNUSED(vm);
					   UNUSED(module);
					   UNUSED(source);
					   UNUSED(sourcePath);
					   return NULL;
				   }

// Write 1 character to the buffer
static void writeChar(UDogVM* vm, CharBuffer* buffer, char a) {
	udogCharBufferWrite(vm, buffer, a);
}

// Write a String to the buffer
static void writeString(UDogVM* vm, CharBuffer* buffer, const char* a, int len) {
	for(int i=0; i<len; i++)
		udogCharBufferWrite(vm, buffer, a[i]);
}

static void writeByte(UDogVM* vm, CharBuffer* buffer, udog_integer arg) {
	writeChar(vm, buffer, (char) arg);
}

static void writeShort(UDogVM* vm, CharBuffer* buffer, udog_integer arg) {
	writeChar(vm, buffer, (char) ((arg >> 8) & 0xff));
	writeChar(vm, buffer, (char) arg);
}

static void writeInt(UDogVM* vm, CharBuffer* buffer, udog_integer arg) {
	writeChar(vm, buffer, (char) ((arg >> 24) & 0xff));
	writeChar(vm, buffer, (char) ((arg >> 16) & 0xff));
	writeChar(vm, buffer, (char) ((arg >> 8) & 0xff));
	writeChar(vm, buffer, (char) (arg & 0xff));
}

static void writeLong(UDogVM* vm, CharBuffer* buffer, udog_integer arg) {
	udogLong num = arg;
	writeChar(vm, buffer, (char) ((num >> 56) & 0xff));
	writeChar(vm, buffer, (char) ((num >> 48) & 0xff));
	writeChar(vm, buffer, (char) ((num >> 40) & 0xff));
	writeChar(vm, buffer, (char) ((num >> 32) & 0xff));
	writeChar(vm, buffer, (char) ((num >> 24) & 0xff));
	writeChar(vm, buffer, (char) ((num >> 16) & 0xff));
	writeChar(vm, buffer, (char) ((num >> 8) & 0xff));
	writeChar(vm, buffer, (char) (num & 0xff));
}

static void setByteCodeBuffer(UDogVM* vm, CharBuffer* buffer, udog_integer arg, int bytes) {
	if (bytes == 1)
		writeByte(vm, buffer, arg);
	else if (bytes == 2)
		writeShort(vm, buffer, arg);
	else if (bytes == 4)
		writeInt(vm, buffer, arg);
	else if (bytes == 8)
		writeLong(vm, buffer, arg);
}



static void writeFunction(UDogVM* vm, CharBuffer* buffer, ObjFn* fn) {
	UNUSED(vm);
	UNUSED(buffer);
	UNUSED(fn);
}

// Creates a string of bytecode [ObjString] located in [module], from
// the VM [vm]
ObjString* udogCompileToByteCode(UDogVM* vm, ObjModule* module) {
	CharBuffer buffer;
	udogCharBufferInit(vm, &buffer);
	
	UNUSED(module);
	writeFunction(vm, &buffer, NULL);
	setByteCodeBuffer(vm, &buffer, 0, 0);
	writeString(vm, &buffer, "#UDOGBC", 7);
	
	return NULL;
}
