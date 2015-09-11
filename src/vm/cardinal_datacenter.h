#ifndef cardinal_datacenter_h
#define cardinal_datacenter_h

#include "cardinal_vm.h"

// Unplug the VM and GC (try to minimize the size of the VM itself)
// So we can create VM's and run them as a heaveweight thread

// Allow the creation of bytecode on the fly in scripts
// With functions like: createVar, makeLoop, etc (every opcode is a class or something)

// create pointer
// allow create and createInplace

// make language expression based

struct CardinalDataCenter {
	
};

// This methods allows the decoupling and recoupling of objects
// to the Garbage collector
void cardinalInitialiseManualMemoryManagement(CardinalVM* vm);

// The method binds the DataCenter to the VM 
// The Data Center can be used to store elements
void cardinalInitializeDataCenter(CardinalVM* vm);

#endif
