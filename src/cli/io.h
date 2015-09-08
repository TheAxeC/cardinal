#ifndef io_h
#define io_h

#include "udog.h"

// Simple IO functions.

// Reads the contents of the file at [path] and returns it as a heap allocated
// string.
//
// Returns `NULL` if the path could not be found. Exits if it was found but
// could not be read.
char* readFile(const char* path);

// Prints the intro for the Repl
void printReplIntro();

// Read a multiline input from the terminal
bool readInput(char* input, int maxSize);

#endif