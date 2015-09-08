#include <string.h>

#include "udog_utils.h"
#include "udog_vm.h"

DEFINE_BUFFER(String, String)
DEFINE_BUFFER(Byte, uint8_t)
DEFINE_BUFFER(ByteCode, uint8_t*);
DEFINE_BUFFER(Int, int)
DEFINE_BUFFER(Char, char)

// Initializes the symbol table.
void udogSymbolTableInit(UDogVM* vm, SymbolTable* symbols) {
	udogStringBufferInit(vm, symbols);
}

// Frees all dynamically allocated memory used by the symbol table, but not the
// SymbolTable itself.
void udogSymbolTableClear(UDogVM* vm, SymbolTable* symbols) {
	for(int i=symbols->count-1;i>=0;i--) {
		DEALLOCATE(vm, symbols->data[i].buffer);
	}
	udogStringBufferClear(vm, symbols);
}

// Adds name to the symbol table. Returns the index of it in the table. Returns
// -1 if the symbol is already present.
int udogSymbolTableAdd(UDogVM* vm, SymbolTable* symbols, const char* name, size_t length) {
	String symbol;
	symbol.buffer = (char*)udogReallocate(vm, NULL, 0,
							sizeof(char) * (length + 1));
	memcpy(symbol.buffer, name, length);
	symbol.buffer[length] = '\0';
	symbol.length = (int)length;

	udogStringBufferWrite(vm, symbols, symbol);
	return symbols->count - 1;
}

// Adds name to the symbol table. Returns the index of it in the table. Will
// use an existing symbol if already present.
int udogSymbolTableEnsure(UDogVM* vm, SymbolTable* symbols, const char* name, size_t length) {
	int index = udogSymbolTableFind(symbols, name, length);
	if (index != -1) {
		return index;
	}
	return udogSymbolTableAdd(vm, symbols, name, length);
}

// Looks up name in the symbol table. Returns its index if found or -1 if not.
int udogSymbolTableFind(SymbolTable* symbols, const char* name, size_t length) {
	// See if the symbol is already defined.
	for (int i = 0; i < symbols->count; i++) {
		if (symbols->data[i].length == length &&
			memcmp(symbols->data[i].buffer, name, length) == 0) return i;
	}
	return -1;
}


int udogUtf8NumBytes(int value) {
	ASSERT(value >= 0, "Cannot encode a negative value.");

	if (value <= 0x7f) return 1;
	if (value <= 0x7ff) return 2;
	if (value <= 0xffff) return 3;
	if (value <= 0x10ffff) return 4;
	return 0;
}

void udogUtf8Encode(int value, uint8_t* bytes) {
	if (value <= 0x7f) {
		// Single byte (i.e. fits in ASCII).
		*bytes = value & 0x7f;
	}
	else if (value <= 0x7ff) {
		// Two byte sequence: 110xxxxx 10xxxxxx.
		*bytes = 0xc0 | ((value & 0x7c0) >> 6);
		bytes++;
		*bytes = 0x80 | (value & 0x3f);
	}
	else if (value <= 0xffff) {
		// Three byte sequence: 1110xxxx 10xxxxxx 10xxxxxx.
		*bytes = 0xe0 | ((value & 0xf000) >> 12);
		bytes++;
		*bytes = 0x80 | ((value & 0xfc0) >> 6);
		bytes++;
		*bytes = 0x80 | (value & 0x3f);
	}
	else if (value <= 0x10ffff) {
		// Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx.
		*bytes = 0xf0 | ((value & 0x1c0000) >> 18);
		bytes++;
		*bytes = 0x80 | ((value & 0x3f000) >> 12);
		bytes++;
		*bytes = 0x80 | ((value & 0xfc0) >> 6);
		bytes++;
		*bytes = 0x80 | (value & 0x3f);
	}
	else {
		// Invalid Unicode value. See: http://tools.ietf.org/html/rfc3629
		ASSERT(false, "Invalid UTF-8 value.");
	}
}

int udogUtf8Decode(const uint8_t* bytes, uint32_t length) {
	// Single byte (i.e. fits in ASCII).
	if (*bytes <= 0x7f) return *bytes;

	int value;
	uint32_t remainingBytes;
	if ((*bytes & 0xe0) == 0xc0) {
		// Two byte sequence: 110xxxxx 10xxxxxx.
		value = *bytes & 0x1f;
		remainingBytes = 1;
	}
	else if ((*bytes & 0xf0) == 0xe0) {
		// Three byte sequence: 1110xxxx	 10xxxxxx 10xxxxxx.
		value = *bytes & 0x0f;
		remainingBytes = 2;
	}
	else if ((*bytes & 0xf8) == 0xf0) {
		// Four byte sequence: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx.
		value = *bytes & 0x07;
		remainingBytes = 3;
	}
	else {
		// Invalid UTF-8 sequence.
		return -1;
	}

	// Don't read past the end of the buffer on truncated UTF-8.
	// TODO: Test this.
	if (remainingBytes > length - 1) return -1;

	while (remainingBytes > 0) {
		bytes++;
		remainingBytes--;

		// Remaining bytes must be of form 10xxxxxx.
		if ((*bytes & 0xc0) != 0x80) return -1;

		value = value << 6 | (*bytes & 0x3f);
	}

	return value;
}
