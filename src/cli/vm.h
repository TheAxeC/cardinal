#ifndef vm_h
#define vm_h

#include "udog.h"

// Creates a new UDog VM with the CLI's module loader and other configuration.
UDogVM* createVM(const char* path);

// Executes the UDog script at [path] in a new VM.
//
// Exits if the script failed or could not be loaded.
void runFile(const char* path, const char* debug);

// Runs input on the Repl
void runReplInput(UDogVM* vm, const char* input);

#endif