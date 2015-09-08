#ifndef vm_h
#define vm_h

#include "cardinal.h"

// Creates a new Cardinal VM with the CLI's module loader and other configuration.
CardinalVM* createVM(const char* path);

// Executes the Cardinal script at [path] in a new VM.
//
// Exits if the script failed or could not be loaded.
void runFile(const char* path, const char* debug);

// Runs input on the Repl
void runReplInput(CardinalVM* vm, const char* input);

#endif