#ifndef cardinal_utils_h
#define cardinal_utils_h

#include "cardinal.h"
#include "cardinal_config.h"

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
    void cardinal##name##BufferInit(CardinalVM* vm, name##Buffer* buffer); \
    void cardinal##name##BufferClear(CardinalVM* vm, name##Buffer* buffer); \
    void cardinal##name##BufferWrite(CardinalVM* vm, name##Buffer* buffer, type data); \
	void cardinal##name##BufferSetCap(CardinalVM* vm, name##Buffer* buffer, size_t size)
	
#define DEFINE_BUFFER(name, type) \
    void cardinal##name##BufferInit(CardinalVM* vm, name##Buffer* buffer) { \
		UNUSED(vm); \
		buffer->data = NULL; \
		buffer->capacity = 0; \
		buffer->count = 0; \
    } \
	void cardinal##name##BufferSetCap(CardinalVM* vm, name##Buffer* buffer, size_t size) { \
		if (size < (size_t) buffer->count) return; \
		buffer->data = (type*) cardinalReallocate(vm, buffer->data,buffer->capacity * sizeof(type), (size+2) * sizeof(type)); \
		buffer->capacity = size+2; \
		buffer->count = size+1; \
	} \
    \
    void cardinal##name##BufferClear(CardinalVM* vm, name##Buffer* buffer) { \
		cardinalReallocate(vm, buffer->data, 0, 0); \
		cardinal##name##BufferInit(vm, buffer); \
    } \
    void cardinal##name##BufferWrite(CardinalVM* vm, name##Buffer* buffer, type data) { \
		if (buffer->capacity < buffer->count + 1) { \
			int capacity = buffer->capacity == 0 ? 8 : buffer->capacity * 2; \
			buffer->data = (type*) cardinalReallocate(vm, buffer->data, \
				buffer->capacity * sizeof(type), capacity * sizeof(type)); \
			buffer->capacity = capacity; \
		} \
		buffer->data[buffer->count] = data; \
		buffer->count++; \
    } 
	
DECLARE_BUFFER(Byte, uint8_t);
DECLARE_BUFFER(ByteCode, uint8_t*);
DECLARE_BUFFER(Int, int);
DECLARE_BUFFER(String, String);
DECLARE_BUFFER(Char, char);


// Stack
typedef struct {
	size_t* data;
	size_t count;
	size_t capacity;
} CardinalStack;

// Initialise the cardinal stack
void cardinalStackInit(CardinalVM* vm, CardinalStack* buffer);

// Pop an element from the cardinal stack
void cardinalStackPop(CardinalVM* vm, CardinalStack* buffer);

// Peek at the top element on the cardinal stack
size_t cardinalStackPeek(CardinalVM* vm, CardinalStack* buffer);

// Clear the cardinal stack
void cardinalStackClear(CardinalVM* vm, CardinalStack* buffer);

// Push an integer onto the cardinal stack
void cardinalStackPush(CardinalVM* vm, CardinalStack* buffer, int elem);

// The symboltable maps to a stringbuffer 
//
typedef StringBuffer SymbolTable;

// Initializes the symbol table.
// The symbol table is growable
void cardinalSymbolTableInit(CardinalVM* vm, SymbolTable* symbols);

// Frees all dynamically allocated memory used by the symbol table, but not the
// SymbolTable itself.
void cardinalSymbolTableClear(CardinalVM* vm, SymbolTable* symbols);

// Adds name to the symbol table. Returns the index of it in the table. Returns
// -1 if the symbol is already present.
int cardinalSymbolTableAdd(CardinalVM* vm, SymbolTable* symbols, const char* name, size_t length);

// Adds name to the symbol table. Returns the index of it in the table. Will
// use an existing symbol if already present.
int cardinalSymbolTableEnsure(CardinalVM* vm, SymbolTable* symbols, const char* name, size_t length);

// Looks up name in the symbol table. 
// Returns its index if found or -1 if not.
int cardinalSymbolTableFind(SymbolTable* symbols, const char* name, size_t length);

// Returns the number of bytes needed to encode [value] in UTF-8.
//
// Returns 0 if [value] is too large to encode.
int cardinalUtf8NumBytes(int value);

// Encodes value as a series of bytes in [bytes], which is assumed to be large
// enough to hold the encoded result.
void cardinalUtf8Encode(int value, uint8_t* bytes);

// Decodes the UTF-8 sequence in [bytes] (which has max [length]), returning
// the code point.
int cardinalUtf8Decode(const uint8_t* bytes, uint32_t length);

#endif