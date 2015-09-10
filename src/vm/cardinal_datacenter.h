#ifndef cardinal_datacenter_h
#define cardinal_datacenter_h

#include "cardinal_vm.h"

// Unplug the VM and GC (try to minimize the size of the VM itself)
// So we can create VM's and run them as a heaveweight thread
// Allow manual memory management
// Allow the creation of bytecode on the fly in scripts
// With functions like: createVar, makeLoop, etc (every opcode is a class or something)

struct CardinalDataCenter {
		
};

// Used when we allocate an object
struct CardinalManualObject {
	Obj* object;
}

// Used to allocate own memory
// Has to be explicitly released by the user of the script
struct CardinalManualMemory {
	void* memory;
	size_t size;
}

// The method binds the DataCenter to the VM 
// The Data Center can be used to store elements
void cardinalInitializeDataCenter(CardinalVM* vm);

#endif
