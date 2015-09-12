#ifndef cardinal_datacenter_h
#define cardinal_datacenter_h

#include "cardinal_vm.h"

struct CardinalDataCenter {
	
};

// This methods allows the decoupling and recoupling of objects
// to the Garbage collector
void cardinalInitialiseManualMemoryManagement(CardinalVM* vm);

// The method binds the DataCenter to the VM 
// The Data Center can be used to store elements
void cardinalInitializeDataCenter(CardinalVM* vm);

#endif
