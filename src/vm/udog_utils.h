#ifndef udog_utils_h
#define udog_utils_h

#include "udog.h"
#include "udog_config.h"

// Reusable data structures and other utility functions.

/// A simple structure to keep trace of the string length as long as its data
/// (including the null-terminator)
typedef struct {
	/// Buffer for the string characters
	char* buffer;
	/// Length of the string
	uint32_t length;
} String;

// We need buffers of a few different types. To avoid lots of casting between
// void* and back, we'll use the preprocessor as a poor man's generics and let
// it generate a few type-specific ones.
#define DECLARE_BUFFER(name, type) \
    typedef struct { \
      type* data; \
      int count; \
      int capacity; \
    } name##Buffer; \
    void udog##name##BufferInit(UDogVM* vm, name##Buffer* buffer); \
    void udog##name##BufferClear(UDogVM* vm, name##Buffer* buffer); \
    void udog##name##BufferWrite(UDogVM* vm, name##Buffer* buffer, type data); \
	void udog##name##BufferSetCap(UDogVM* vm, name##Buffer* buffer, size_t size)
	
#define DEFINE_BUFFER(name, type) \
    void udog##name##BufferInit(UDogVM* vm, name##Buffer* buffer) { \
		UNUSED(vm); \
		buffer->data = NULL; \
		buffer->capacity = 0; \
		buffer->count = 0; \
    } \
	void udog##name##BufferSetCap(UDogVM* vm, name##Buffer* buffer, size_t size) { \
		if (size < (size_t) buffer->count) return; \
		buffer->data = (type*) udogReallocate(vm, buffer->data,buffer->capacity * sizeof(type), (size+2) * sizeof(type)); \
		buffer->capacity = size+2; \
		buffer->count = size+1; \
	} \
    \
    void udog##name##BufferClear(UDogVM* vm, name##Buffer* buffer) { \
		udogReallocate(vm, buffer->data, 0, 0); \
		udog##name##BufferInit(vm, buffer); \
    } \
    void udog##name##BufferWrite(UDogVM* vm, name##Buffer* buffer, type data) { \
		if (buffer->capacity < buffer->count + 1) { \
			int capacity = buffer->capacity == 0 ? 8 : buffer->capacity * 2; \
			buffer->data = (type*) udogReallocate(vm, buffer->data, \
				buffer->capacity * sizeof(type), capacity * sizeof(type)); \
			buffer->capacity = capacity; \
		} \
		buffer->data[buffer->count] = data; \
		buffer->count++; \
    } 
	
DECLARE_BUFFER(Byte, uint8_t);
/*
typedef struct {
	UDogCompiler* compiler;
	int pos;
	uint8_t* bytecode;
}BytePosChanger
*/
DECLARE_BUFFER(ByteCode, uint8_t*);
DECLARE_BUFFER(Int, int);
DECLARE_BUFFER(String, String);
DECLARE_BUFFER(Char, char);

// The symboltable maps to a stringbuffer 
//
typedef StringBuffer SymbolTable;

// Initializes the symbol table.
// The symbol table is growable
void udogSymbolTableInit(UDogVM* vm, SymbolTable* symbols);

// Frees all dynamically allocated memory used by the symbol table, but not the
// SymbolTable itself.
void udogSymbolTableClear(UDogVM* vm, SymbolTable* symbols);

// Adds name to the symbol table. Returns the index of it in the table. Returns
// -1 if the symbol is already present.
int udogSymbolTableAdd(UDogVM* vm, SymbolTable* symbols, const char* name, size_t length);

// Adds name to the symbol table. Returns the index of it in the table. Will
// use an existing symbol if already present.
int udogSymbolTableEnsure(UDogVM* vm, SymbolTable* symbols, const char* name, size_t length);

// Looks up name in the symbol table. 
// Returns its index if found or -1 if not.
int udogSymbolTableFind(SymbolTable* symbols, const char* name, size_t length);

// Returns the number of bytes needed to encode [value] in UTF-8.
//
// Returns 0 if [value] is too large to encode.
int udogUtf8NumBytes(int value);

// Encodes value as a series of bytes in [bytes], which is assumed to be large
// enough to hold the encoded result.
void udogUtf8Encode(int value, uint8_t* bytes);

// Decodes the UTF-8 sequence in [bytes] (which has max [length]), returning
// the code point.
int udogUtf8Decode(const uint8_t* bytes, uint32_t length);

#endif
